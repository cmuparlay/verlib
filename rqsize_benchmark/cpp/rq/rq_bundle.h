// Jacob Nelson
//
// This file contains the implementation of the bundle range query provider. It
// follows a similar principle to the RQProviders implemented by Arbel-Raviv and
// Brown. Updates are responsible for calling the required APIs to prepare and
// finalize the bundles.

#pragma once
#ifndef BUNDLE_RQ_BUNDLE_H
#define BUNDLE_RQ_BUNDLE_H

#ifdef BUNDLE_CLEANUP_BACKGROUND
#ifndef BUNDLE_CLEANUP_SLEEP
#error BUNDLE_CLEANUP_SLEEP NOT DEFINED
#endif
#endif

#if defined BUNDLE_CIRCULAR_BUNDLE
#include "circular_bundle.h"
#error Not implemented
#elif defined BUNDLE_LINKED_BUNDLE
#define BUNDLE_TYPE_DECL LinkedBundle
#include "linked_bundle.h"
#elif defined BUNDLE_UNSAFE_BUNDLE
#define BUNDLE_UNSAFE
#define BUNDLE_TYPE_DECL LinkedBundle
#include "linked_bundle.h"
#else
#error NO BUNDLE TYPE DEFINED
#endif

#include "common_bundle.h"

static thread_local int backoff_amt = 0;

#define __THREAD_DATA_SIZE 1024
// Used to announce an active range query and its linearization point.
union __rq_thread_data {
  struct {
    volatile timestamp_t rq_lin_time;
    std::atomic<bool> rq_flag;
#ifdef BUNDLE_TIMESTAMP_RELAXATION
    volatile char pad1[PREFETCH_SIZE_BYTES];
    volatile long local_timestamp;
#endif
  } data;
  volatile char bytes[__THREAD_DATA_SIZE];
} __attribute__((aligned(__THREAD_DATA_SIZE)));

// NOTES ON IMPLEMENTATION DETAILS.
// --------------------------------
// The active RQ array is the total number of processes to accomodate any
// number of range query threads. Snapshots are still taken by iterating over
// the list.

// Ensures consistent view of data structure for range queries by augmenting
// updates to keep track of their linearization points and observe any active
// range queries.
template <typename K, typename V, typename NodeType, typename DataStructure,
          typename RecordManager, bool logicalDeletion,
          bool canRetireNodesLogicallyDeletedByOtherProcesses>
class RQProvider {
 private:
  // Number of processes concurrently operating on the data structure.
  const int num_processes_;
  volatile char pad0[PREFETCH_SIZE_BYTES];
  // Timestamp used by range queries to linearize accesses.
  std::atomic<timestamp_t> curr_timestamp_;
  volatile char pad1[PREFETCH_SIZE_BYTES];

  // Array of RQ announcements. One per thread.
  __rq_thread_data *rq_thread_data_;
  volatile char pad2[PREFETCH_SIZE_BYTES];

  DataStructure *ds_;
  RecordManager *const recmgr_;

  int init_[MAX_TID_POW2] = {
      0,
  };

// Metadata for cleaning up with an independent thread.
#ifdef BUNDLE_CLEANUP_BACKGROUND
  struct cleanup_args {
    std::atomic<bool> *const stop;
    DataStructure *const ds;
    int tid;
  };

  pthread_t cleanup_thread_;
  struct cleanup_args *cleanup_args_;
  std::atomic<bool> stop_cleanup_;
#endif

 public:
  RQProvider(const int num_processes, DataStructure *ds, RecordManager *recmgr)
      : num_processes_(num_processes), ds_(ds), recmgr_(recmgr) {
    if (num_processes > MAX_TID_POW2) {
      cerr << "num_processes (" << num_processes << ") > maxthreads_pow2 ("
           << MAX_TID_POW2 << "): Please increase maxthreads_pow2 in config.mk";
      exit(1);
    }
    rq_thread_data_ = new __rq_thread_data[num_processes];
    for (int i = 0; i < num_processes; ++i) {
      rq_thread_data_[i].data.rq_lin_time = BUNDLE_NULL_TIMESTAMP;
      rq_thread_data_[i].data.rq_flag = false;
    }
    curr_timestamp_ = BUNDLE_MIN_TIMESTAMP;

// Launches a background thread to handle bundle entry cleanup.
#ifdef BUNDLE_CLEANUP_BACKGROUND
    cleanup_args_ = new cleanup_args{&stop_cleanup_, ds_, num_processes_ - 1};
    if (pthread_create(&cleanup_thread_, nullptr, cleanup_run,
                       (void *)cleanup_args_)) {
      cerr << "ERROR: could not create thread" << endl;
      exit(-1);
    }
    std::stringstream ss;
    ss << "Cleanup started: 0x" << std::hex << cleanup_thread_ << std::endl;
    std::cout << ss.str() << std::flush;
#endif
  }

  ~RQProvider() {
#ifdef BUNDLE_CLEANUP_BACKGROUND
    std::cout << "Stopping cleanup..." << std::endl << std::flush;
    stop_cleanup_ = true;
    if (pthread_join(cleanup_thread_, nullptr)) {
      cerr << "ERROR: could not join thread" << endl;
      exit(-1);
    }
    delete cleanup_args_;
#endif
    delete[] rq_thread_data_;
  }

  void initThread(const int tid) {
    if (init_[tid])
      return;
    else
      init_[tid] = !init_[tid];
  }

  void deinitThread(const int tid) {
    if (!init_[tid])
      return;
    else
      init_[tid] = !init_[tid];
  }

  inline void backoff(int amount) {
    if (amount == 0) return;
    volatile long long sum = 0;
    int limit = amount;
    for (int i = 0; i < limit; i++) sum += i;
  }

  inline long long getNextTS(const int tid) {
    // return __sync_fetch_and_add(&timestamp, 1);
    timestamp_t ts = curr_timestamp_.load(std::memory_order_seq_cst);
    backoff(backoff_amt);
    if (ts == curr_timestamp_.load(std::memory_order_seq_cst)) {
      if (curr_timestamp_.fetch_add(1, std::memory_order_release) == ts)
        backoff_amt /= 2;
      else
        backoff_amt *= 2;
    }
    if (backoff_amt < 1) backoff_amt = 1;
    if (backoff_amt > 512) backoff_amt = 512;
    return ts + 1;
  }

  inline void init_node(int tid, NodeType *const node) {}

  // for each address addr that is modified by rq_linearize_update_at_write
  // or rq_linearize_update_at_cas, you must replace any initialization of addr
  // with invocations of rq_write_addr
  template <typename T>
  inline void write_addr(const int tid, T volatile *const addr, const T val) {
    *addr = val;
  }

  // for each address addr that is modified by rq_linearize_update_at_write
  // or rq_linearize_update_at_cas, you must replace any reads of addr with
  // invocations of rq_read_addr
  template <typename T>
  inline T read_addr(const int tid, T volatile *const addr) {
    return *addr;
  }

#define BUNDLE_INIT_CLEANUP(provider) \
  const timestamp_t ts = provider->get_oldest_active_rq();
#define BUNDLE_CLEAN_BUNDLE(bundle) bundle.reclaimEntries(ts)

  // Creates a snapshot of the current state of active RQs.
  inline timestamp_t get_oldest_active_rq() {
    timestamp_t oldest_active = curr_timestamp_.load(std::memory_order_acquire);
    timestamp_t curr_rq;
    for (int i = 0; i < num_processes_; ++i) {
      while (rq_thread_data_[i].data.rq_flag == true)
        ;  // Wait until RQ linearizes itself.
      curr_rq = rq_thread_data_[i].data.rq_lin_time;
      if (curr_rq != BUNDLE_NULL_TIMESTAMP && curr_rq < oldest_active) {
        oldest_active = curr_rq;  // Update oldest.
      }
    }
    return oldest_active;
  }

#ifdef BUNDLE_CLEANUP_BACKGROUND
  static void *cleanup_run(void *args) {
    std::cout << "Starting cleanup" << std::endl << std::flush;
    struct cleanup_args *c = (struct cleanup_args *)args;
    long i = 0;
    while (!(*(c->stop))) {
      usleep(BUNDLE_CLEANUP_SLEEP);
      c->ds->cleanup(c->tid);
    }
    pthread_exit(nullptr);
  }
#endif

  // Atomically increments the global timestamp and returns the new value to the
  // caller.
  inline timestamp_t get_update_lin_time(int tid) {
#ifdef BUNDLE_RQTS
    return curr_timestamp_.load(std::memory_order_acquire);
#elif defined(BUNDLE_UNSAFE_BUNDLE)
#ifdef BUNDLE_TIMESTAMP_RELAXATION
    if (((rq_thread_data_[tid].data.local_timestamp + 1) %
         BUNDLE_TIMESTAMP_RELAXATION) == 0) {
      ++rq_thread_data_[tid].data.local_timestamp;
      return curr_timestamp_.fetch_add(1) + 1;
    } else {
      ++rq_thread_data_[tid].data.local_timestamp;
      return curr_timestamp_;
    }
// #elif defined(BUNDLE_UPDATE_USES_CAS)
#else
    return BUNDLE_MIN_TIMESTAMP;
#endif
#else
    // timestamp_t ts = curr_timestamp_;
    // if (curr_timestamp_.compare_exchange_strong(ts, ts + 1)) {
    //   return ts + 1;
    // } else {
    //   return ts;
    // }
    // return curr_timestamp_.fetch_add(1, std::memory_order_relaxed) + 1;
    timestamp_t ts = getNextTS(tid);
    return ts;
#endif
#endif
  }

  inline timestamp_t get_curr_timestamp(int tid) {
    return curr_timestamp_.load(std::memory_order_seq_cst);
  }

  // Write the range query linearization time so updates do not recycle any
  // edges needed by this range query.
  inline timestamp_t start_traversal(int tid) {
#if defined(BUNDLE_RQTS)
// Reads drive timestamp.
#if defined(BUNDLE_UPDATE_USES_CAS)
    timestamp_t ts = curr_timestamp_;
    curr_timestamp_.compare_exchange_strong(ts, ts + 1);
    return ts;
#else
    rq_thread_data_[tid].data.rq_flag.store(true, std::memory_order_acquire);
    rq_thread_data_[tid].data.rq_lin_time = getNextTS(tid) - 1;
    rq_thread_data_[tid].data.rq_flag.store(false, std::memory_order_release);
    return rq_thread_data_[tid].data.rq_lin_time;
    // return getNextTS(tid) - 1;
#endif
#elif defined(BUNDLE_UNSAFE_BUNDLE)
// Bundle is updated periodically or
#if defined(BUNDLE_TIMESTAMP_RELAXATION)
  ++rq_thread_data_[tid].data.local_timestamp;
  if (((rq_thread_data_[tid].data.local_timestamp + 1) %
       BUNDLE_TIMESTAMP_RELAXATION) == 0) {
    rq_thread_data_[tid].data.local_timestamp = curr_timestamp_;
    return rq_thread_data_[tid].data.local_timestamp;
  } else {
    return rq_thread_data_[tid].data.local_timestamp;
  }
  // #elif defined(BUNDLE_UPDATE_USES_CAS)
#else
  return BUNDLE_MIN_TIMESTAMP;
#endif
#else
  rq_thread_data_[tid].data.rq_flag.store(true, std::memory_order_acquire);
  rq_thread_data_[tid].data.rq_lin_time = curr_timestamp_;
  rq_thread_data_[tid].data.rq_flag.store(false, std::memory_order_release);
  return rq_thread_data_[tid].data.rq_lin_time;
#endif
  }

  // invoke each time a traversal visits a node with a key in the desired range:
  // if the node belongs in the range query, it will be placed in
  // rqResult[index]
  inline void traversal_try_add(const int tid, NodeType *const node,
                                K *const rqResultKeys, V *const rqResultValues,
                                int *const startIndex, const K &lo,
                                const K &hi) {
    int start = (*startIndex);
    int keysInNode =
        ds_->getKeys(tid, node, rqResultKeys + start, rqResultValues + start);
    assert(keysInNode < RQ_DEBUGGING_MAX_KEYS_PER_NODE);
    if (keysInNode == 0) return;
    int location = start;
    for (int i = start; i < keysInNode + start; ++i) {
      if (ds_->isInRange(rqResultKeys[i], lo, hi)) {
        rqResultKeys[location] = rqResultKeys[i];
        rqResultValues[location] = rqResultValues[i];
        ++location;
      }
    }
    *startIndex = location;
#if defined MICROBENCH
    assert(*startIndex <= RQSIZE);
#endif
  }

  // Reset the range query linearization time so that updates may recycle an
  // edge we needed.
  inline void end_traversal(int tid) {
#ifndef BUNDLE_UNSAFE_BUNDLE
    rq_thread_data_[tid].data.rq_lin_time = BUNDLE_NULL_TIMESTAMP;
#endif
  }

  // Prepares bundles by calling prepare on each provided bundle-pointer pair.
  inline void prepare_bundles(BUNDLE_TYPE_DECL<NodeType> *bundles[],
                              NodeType *const *const ptrs) {
    // PENDING_TIMESTAMP blocks all RQs that might see the update, ensuring that
    // the update is visible (i.e., get and RQ have the same linearization
    // point).
    SOFTWARE_BARRIER;
    int i = 0;
    BUNDLE_TYPE_DECL<NodeType> *curr_bundle = bundles[0];
    NodeType *curr_ptr = ptrs[0];
    while (curr_bundle != nullptr) {
      curr_bundle->prepare(curr_ptr);
#ifdef BUNDLE_CLEANUP_UPDATE
      curr_bundle->reclaimEntries(get_oldest_active_rq());
#endif
      ++i;
      curr_bundle = bundles[i];
      curr_ptr = ptrs[i];
    }
  }

  inline void finalize_bundles(BUNDLE_TYPE_DECL<NodeType> **bundles,
                               timestamp_t ts) {
    int i = 0;
    BUNDLE_TYPE_DECL<NodeType> *curr_bundle = bundles[0];
    while (curr_bundle != nullptr) {
      curr_bundle->finalize(ts);
      ++i;
      curr_bundle = bundles[i];
    }
    SOFTWARE_BARRIER;
  }

  // Find and update the newest reference in the predecesor's bundle. If this
  // operation is an insert, then the new nodes bundle must also be
  // initialized. Any node whose bundle is passed here must be locked.
  template <typename T>
  inline timestamp_t linearize_update_at_write(const int tid,
                                               T volatile *const lin_addr,
                                               const T &lin_newval) {
    // Get update linearization timestamp.
    SOFTWARE_BARRIER;
    timestamp_t lin_time = get_update_lin_time(tid);
    *lin_addr = lin_newval;  // Original linearization point.
    SOFTWARE_BARRIER;
    return lin_time;
  }

  // IF DATA STRUCTURE PERFORMS LOGICAL DELETION
  // run some time BEFORE the physical deletion of a node
  // whose key has ALREADY been logically deleted.
  inline void announce_physical_deletion(const int tid,
                                         NodeType *const *const deletedNodes) {}

  // IF DATA STRUCTURE PERFORMS LOGICAL DELETION
  // run AFTER performing announce_physical_deletion,
  // if the cas that was trying to physically delete node failed.
  inline void physical_deletion_failed(const int tid,
                                       NodeType *const *const deletedNodes) {}

  // IF DATA STRUCTURE PERFORMS LOGICAL DELETION
  // run AFTER performing announce_physical_deletion,
  // if the cas that was trying to physically delete node succeeded.
  inline void physical_deletion_succeeded(const int tid,
                                          NodeType *const *const deletedNodes) {
    int i;
    for (i = 0; deletedNodes[i]; ++i) {
      recmgr_->retire(tid, deletedNodes[i]);
    }
  }

  // replace the linearization point of an update that inserts or deletes nodes
  // with an invocation of this function if the linearization point is a CAS
  template <typename T>
  inline T linearize_update_at_cas(const int tid, T volatile *const lin_addr,
                                   const T &lin_oldval, const T &lin_newval,
                                   NodeType *const *const insertedNodes,
                                   NodeType *const *const deletedNodes) {
    if (!logicalDeletion) {
      // physical deletion will happen at the same time as logical deletion
      announce_physical_deletion(tid, deletedNodes);
    }

    T res = __sync_val_compare_and_swap(
        lin_addr, lin_oldval, lin_newval);  // original linearization point

    if (res == lin_oldval) {
      if (!logicalDeletion) {
        // physical deletion will happen at the same time as logical deletion
        physical_deletion_succeeded(tid, deletedNodes);
      }
#if defined USE_RQ_DEBUGGING
      DEBUG_RECORD_UPDATE_CHECKSUM<K, V>(tid, ts, insertedNodes, deletedNodes,
                                         ds);
#endif
    } else {
      if (!logicalDeletion) {
        // physical deletion will happen at the same time as logical deletion
        physical_deletion_failed(tid, deletedNodes);
      }
    }

    return res;
  }
};