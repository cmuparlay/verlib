// Jacob Nelson
//
// This file implements a bundle as a linked list of bundle entries. A bundle is
// prepared by CASing the head of the bundle to a pending entry.

#ifndef BUNDLE_LINKED_BUNDLE_H
#define BUNDLE_LINKED_BUNDLE_H

#include <pthread.h>
#include <sys/types.h>

#include <atomic>
#include <mutex>

#include "common_bundle.h"
#include "plaf.h"
#include "rq_debugging.h"

#define CPU_RELAX asm volatile("pause\n" ::: "memory")
#define likely(x) __builtin_expect((x), 1)
#define unlikely(x) __builtin_expect((x), 0)

#define DEBUG_PRINT_INIT() unsigned long i = 0
#define DEBUG_PRINT(str)                         \
  if ((i + 1) % 10000 == 0) {                    \
    std::cout << str << std::endl << std::flush; \
  }                                              \
  ++i;

enum op { NOP, INSERT, REMOVE };

template <typename NodeType>
class BundleEntry {
 public:
  std::atomic<timestamp_t> ts_;
  NodeType *ptr_;
  std::atomic<BundleEntry *> next_;
  volatile timestamp_t deleted_ts_;

  BundleEntry() = delete;
  BundleEntry(timestamp_t ts, NodeType *ptr, BundleEntry *next)
      : ts_(ts), next_(next) {
    this->ptr_ = ptr;
    deleted_ts_ = BUNDLE_NULL_TIMESTAMP;
  }
  ~BundleEntry() {}

  void set_ts(const timestamp_t ts) { ts_ = ts; }
  void set_ptr(NodeType *const ptr) { this->ptr_ = ptr; }
  void set_next(BundleEntry *const next) { next_ = next; }
  void mark(timestamp_t ts) { deleted_ts_ = ts; }
  timestamp_t marked() { return deleted_ts_; }

  inline void validate() {
    if (ts_ < next_->ts_) {
      std::cout << "Invalid bundle" << std::endl;
      exit(1);
    }
  }
};

template <typename NodeType>
class LinkedBundle {
 private:
  std::atomic<BundleEntry<NodeType> *> head_;
  // BundleEntry<NodeType> *volatile tail_;

#ifdef BUNDLE_DEBUG
  volatile int updates = 0;
  BundleEntry<NodeType> *volatile last_recycled = nullptr;
  volatile int oldest_edge = 0;
#endif

 public:
  ~LinkedBundle() {
    BundleEntry<NodeType> *curr = head_.load();
    BundleEntry<NodeType> *next;
    while (curr != nullptr) {
      assert(curr != nullptr);
      next = curr->next_;
      delete curr;
      curr = next;
    }
  }

  void init() { head_ = nullptr; }

  // Inserts a new rq_bundle_node at the head of the bundle.
  inline void prepare(NodeType *const ptr) {
    BundleEntry<NodeType> *new_entry =
        new BundleEntry<NodeType>(BUNDLE_PENDING_TIMESTAMP, ptr, nullptr);

#ifdef BUNDLE_LOCKFREE
    while (true) {
      BundleEntry<NodeType> *expected = head_;
      if (expected->ts_ != BUNDLE_PENDING_TIMESTAMP &&
          head_.compare_exchange_weak(expected, new_entry)) {
        new_entry->next_ = expected;
#ifdef BUNDLE_DEBUG
        ++updates;
#endif
        return;
      }
      // long i = 0;
      while (expected->ts_ == BUNDLE_PENDING_TIMESTAMP) {
        // DEBUG_PRINT("insertAtHead");
        CPU_RELAX;
      }
    }
#else
    // Since we have a lock on this node presently, we are able to use a less
    // stringent memory order
    new_entry->next_.store(head_, std::memory_order_relaxed);
    head_.store(new_entry, std::memory_order_relaxed);
#ifdef BUNDLE_DEBUG
    ++updates;
#endif
    return;
#endif
  }

  inline void abort() {
    assert(head_->ts_ == BUNDLE_PENDING_TIMESTAMP);
    NodeType *entry = head_;
    head_ = entry->ptr_;
    delete entry;
  }

  // Labels the pending entry to make it visible to range queries.
  inline void finalize(timestamp_t ts) {
    assert(ts != BUNDLE_PENDING_TIMESTAMP);
    assert(head_.load()->ts_ == BUNDLE_PENDING_TIMESTAMP);
    head_.load()->ts_ = ts;
  }

  inline bool getPtr(int tid, NodeType **next) {
    BundleEntry<NodeType> *curr = head_;
    timestamp_t curr_ts = curr->ts_;
    if (curr_ts != BUNDLE_PENDING_TIMESTAMP) {
#ifdef __HANDLE_STATS
      GSTATS_ADD(tid, bundle_first, 1);
#endif
      *next = curr->ptr_;
      return true;
    }

    while (curr->ts_ == BUNDLE_PENDING_TIMESTAMP) {
      CPU_RELAX;
    }
    *next = curr->ptr_;
    return true;
  }

  // Returns a reference to the node that immediately followed at timestamp ts.
  inline bool getPtrByTimestamp(int tid, timestamp_t ts, NodeType **next) {
    // Check if the first entry satisfies the timestamp.
    BundleEntry<NodeType> *curr = head_;
    assert(head_ != nullptr);  // An inserted node should always have an entry.
    if (curr != nullptr) {
      timestamp_t curr_ts = curr->ts_;
      if (curr_ts <= ts) {
#ifdef __HANDLE_STATS
        GSTATS_ADD(tid, bundle_first, 1);
#endif
        *next = curr->ptr_;
        return true;
      }
    }

    // Optimization. Check the second entry in hopes that it satisfies
    bool skip_first = false;
    BundleEntry<NodeType> *second = curr->next_;
    if (second != nullptr) {
      timestamp_t second_ts = second->ts_;
      if (second_ts == ts) {
// Success!
#ifdef __HANDLE_STATS
        GSTATS_ADD(tid, bundle_second, 1);
#endif
        *next = second->ptr_;
        return true;
      } else if (second_ts > ts) {
// Ignore the pending entry.
#ifdef __HANDLE_STATS
        GSTATS_ADD(tid, bundle_skip_first, 1);
#endif
        curr = second;
        skip_first = true;
      }
    }

    if (!skip_first) {
      long long retries = 0;
      while (curr->ts_ == BUNDLE_PENDING_TIMESTAMP) {
        CPU_RELAX;
        ++retries;
      }

#ifdef __HANDLE_STATS
      GSTATS_APPEND(tid, bundle_retries, retries);
#endif
    }

    long long traversals = 0;
    while (curr != nullptr && curr->ts_ > ts) {
      assert(curr->ts_ != BUNDLE_NULL_TIMESTAMP);
      curr = curr->next_;
      ++traversals;
    }
#ifdef __HANDLE_STATS
    GSTATS_APPEND(tid, bundle_traversals, traversals);
#endif
#ifdef BUNDLE_DEBUG
    if (curr->marked()) {
      std::cout << dump(0) << std::flush;
      exit(1);
    }
#endif
    if (curr != nullptr) {
      *next = curr->ptr_;
      return true;
    } else {
      return false;
    }
  }

  // Reclaims any edges that are older than ts. At the moment this should be
  // ordered before adding a new entry to the bundle.
  inline void reclaimEntries(timestamp_t ts) {
    // Obtain a reference to the pred non-reclaimable entry and first
    // reclaimable one. Ignore the first entry if it is pending or return if
    // there is nothing to reclaim.
    BundleEntry<NodeType> *pred = head_;
    if (pred == nullptr) return;
    if (pred->ts_ == BUNDLE_PENDING_TIMESTAMP) {
      pred = pred->next_;
      if (pred == nullptr) return;
    }
    BundleEntry<NodeType> *curr = pred->next_;
    if (curr == nullptr) return;

    // Traverse the list of entries until we find the first entry whose
    // timestamp is less than or equal to the timestamp of the oldest range
    // query. If none is found, then return.
    while (curr != nullptr && pred->ts_ > ts) {
      pred = curr;
      curr = curr->next_;
    }
    if (curr == nullptr) return;  // No reclaimable entry found.

    // At this point, pred points to the oldest node required by the given
    // timestamp. Therefore, we know that the chain starting at curr is
    // reclaimable and can be unlinked.
    pred->next_ = nullptr;

#ifdef BUNDLE_DEBUG
    last_recycled = curr;
    oldest_edge = pred->ts_;
#endif

    // Reclaim old entries by traversing the chain starting from curr.
    assert(curr != head_ && pred->next_ == nullptr);
    while (curr != nullptr) {
      pred = curr;
      curr = curr->next_;
      pred->mark(ts);
#ifndef BUNDLE_CLEANUP_NO_FREE
      delete pred;
#endif
    }
#ifdef BUNDLE_DEBUG
    if (curr != nullptr) {
      std::cout << curr << std::endl;
      std::cout << dump(ts) << std::flush;
      exit(1);
    }
#endif
  }

  // [UNSAFE] Returns the number of bundle entries.
  int size() {
    int size = 0;
    BundleEntry<NodeType> *curr = head_;
    while (curr != nullptr) {
#ifdef BUNDLE_DEBUG
      if (curr->marked()) {
        std::cout << dump(0) << std::flush;
        exit(1);
      }
#endif
      ++size;
      curr = curr->next_;
#ifdef BUNDLE_DEBUG
      if (curr == nullptr) {
        std::cout << dump(0) << std::flush;
        exit(1);
      }
#endif
    }
    return size;
  }

  inline NodeType *first(timestamp_t &ts) {
    BundleEntry<NodeType> *entry = head_;
    ts = entry->ts_;
    return entry->ptr_;
  }

  std::pair<NodeType *, timestamp_t> *get(int &length) {
    // Find the number of entries in the list.
    BundleEntry<NodeType> *curr_entry = head_;
    int size = 0;
    while (curr_entry != nullptr) {
      ++size;
      curr_entry = curr_entry->next_;
    }

    // Build the return array.
    std::pair<NodeType *, timestamp_t> *retarr =
        new std::pair<NodeType *, timestamp_t>[size];
    int pos = 0;
    NodeType *ptr;
    timestamp_t ts;
    curr_entry = head_;
    while (curr_entry != nullptr) {
      ptr = curr_entry->ptr_;
      ts = curr_entry->ts_;
      retarr[pos++] = std::pair<NodeType *, timestamp_t>(ptr, ts);
      curr_entry = curr_entry->next_;
    }
    length = size;
    return retarr;
  }

  string __attribute__((noinline)) dump(timestamp_t ts) {
    BundleEntry<NodeType> *curr = head_;
    std::stringstream ss;
    ss << "(ts=" << ts << ") : ";
    long i = 0;
    while (curr != nullptr && curr != nullptr) {
      ss << "<" << curr->ts_ << "," << curr->ptr_ << "," << curr->next_ << ">"
         << "-->";
      curr = curr->next_;
    }
    if (curr == nullptr) {
      ss << "(tail)<" << curr->ts_ << "," << curr->ptr_ << ","
         << reinterpret_cast<long>(curr->next_.load(std::memory_order_relaxed))
         << ">";
    } else {
      ss << "(unexpected end)";
    }
#ifdef BUNDLE_DEBUG
    ss << " [updates=" << updates << ", last_recycled=" << last_recycled
       << ", oldest_edge=" << oldest_edge << "]" << std::endl;
#else
    ss << std::endl;
#endif
    return ss.str();
  }
};

#endif  // BUNDLE_LINKED_BUNDLE_H