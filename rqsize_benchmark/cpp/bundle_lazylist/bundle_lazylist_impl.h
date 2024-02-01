// Jacob Nelson
//
// This is the implementation of a bundled lazylist, building off of the
// implementation provided by Arbel-Raviv and Brown (see the 'lazylist'
// directory for more information on it).

#ifndef LAZYLIST_IMPL_H
#define LAZYLIST_IMPL_H

#include <cassert>
#include <csignal>

#include "bundle_lazylist.h"
#include "locks_impl.h"

#ifndef casword_t
#define casword_t uintptr_t
#endif

template <typename K, typename V>
class node_t {
 public:
  K key;
  volatile V val;
  node_t *volatile next;
  volatile int lock;
  volatile long long
      marked;  // is stored as a long long simply so it is large enough to be
               // used with the lock-free RQProvider (which requires all fields
               // that are modified at linearization points of operations to be
               // at least as large as a machine word)
  BUNDLE_TYPE_DECL<node_t<K, V>> rqbundle;

  node_t() {}

  ~node_t() {
    // delete rqbundle;
  }

  template <typename RQProvider>
  bool isMarked(const int tid, RQProvider *const prov) {
    return marked;
  }

  bool validate() {
    timestamp_t ts;
    return next == rqbundle.first(ts);
  }
};

template <typename K, typename V, class RecManager>
bundle_lazylist<K, V, RecManager>::bundle_lazylist(const int numProcesses,
                                                   const K _KEY_MIN,
                                                   const K _KEY_MAX,
                                                   const V _NO_VALUE)
    // Plus 1 for the background thread.
    : recordmgr(new RecManager(numProcesses, SIGQUIT)),
      rqProvider(
          new RQProvider<K, V, node_t<K, V>, bundle_lazylist<K, V, RecManager>,
                         RecManager, true, false>(numProcesses, this,
                                                  recordmgr))
#ifdef USE_DEBUGCOUNTERS
      ,
      counters(new debugCounters(numProcesses))
#endif
      ,
      KEY_MIN(_KEY_MIN),
      KEY_MAX(_KEY_MAX),
      NO_VALUE(_NO_VALUE) {
  const int tid = 0;
  initThread(tid);
  nodeptr max = new_node(tid, KEY_MAX, 0, NULL);
  head = new_node(tid, KEY_MIN, 0, NULL);

  // Perform linearization of max to ensure bundles correctly added.
  BUNDLE_TYPE_DECL<node_t<K, V>> *bundles[] = {&head->rqbundle, nullptr};
  nodeptr ptrs[] = {max, nullptr};
  rqProvider->prepare_bundles(bundles, ptrs);
  timestamp_t lin_time =
      rqProvider->linearize_update_at_write(tid, &head->next, max);
  rqProvider->finalize_bundles(bundles, lin_time);
}

template <typename K, typename V, class RecManager>
bundle_lazylist<K, V, RecManager>::~bundle_lazylist() {
  const int dummyTid = 0;
  nodeptr curr = head;
  while (curr->key < KEY_MAX) {
    nodeptr next = curr->next;
    recordmgr->deallocate(dummyTid, curr);
    curr = next;
  }
  recordmgr->deallocate(dummyTid, curr);
  delete rqProvider;
  recordmgr->printStatus();
  delete recordmgr;
#ifdef USE_DEBUGCOUNTERS
  delete counters;
#endif
}

template <typename K, typename V, class RecManager>
void bundle_lazylist<K, V, RecManager>::initThread(const int tid) {
  if (init[tid])
    return;
  else
    init[tid] = !init[tid];

  recordmgr->initThread(tid);
  rqProvider->initThread(tid);
}

template <typename K, typename V, class RecManager>
void bundle_lazylist<K, V, RecManager>::deinitThread(const int tid) {
  if (!init[tid])
    return;
  else
    init[tid] = !init[tid];

  recordmgr->deinitThread(tid);
  rqProvider->deinitThread(tid);
}

template <typename K, typename V, class RecManager>
nodeptr bundle_lazylist<K, V, RecManager>::new_node(const int tid, const K &key,
                                                    const V &val,
                                                    nodeptr next) {
  nodeptr nnode = recordmgr->template allocate<node_t<K, V>>(tid);
  if (nnode == NULL) {
    cout << "out of memory" << endl;
    exit(1);
  }
  nnode->key = key;
  nnode->val = val;
  nnode->next = next;
  nnode->marked = 0LL;
  nnode->lock = false;
  nnode->rqbundle.init();
#ifdef __HANDLE_STATS
  GSTATS_APPEND(tid, node_allocated_addresses, ((long long)nnode) % (1 << 12));
#endif
  return nnode;
}

template <typename K, typename V, class RecManager>
inline int bundle_lazylist<K, V, RecManager>::validateLinks(const int tid,
                                                            nodeptr pred,
                                                            nodeptr curr) {
  return (!pred->marked && !curr->marked && (pred->next == curr));
}

template <typename K, typename V, class RecManager>
bool bundle_lazylist<K, V, RecManager>::contains(const int tid, const K &key) {
  bool ok;
  while (true) {
    recordmgr->leaveQuiescentState(tid, true);

    nodeptr curr = head;
    nodeptr pred = curr;

    while (curr->key < key) {
      pred = curr;
      curr = curr->next;
    }

    ok = pred->rqbundle.getPtr(tid, &curr);
    assert(ok);

    while (curr->key < key) {
      ok = curr->rqbundle.getPtr(tid, &curr);
      assert(ok);
    }

    V res = NO_VALUE;
    if ((curr->key == key) && !curr->marked) {
      res = curr->val;
    }
    recordmgr->enterQuiescentState(tid);
    return (res != NO_VALUE);
  }
  return false;
}

template <typename K, typename V, class RecManager>
V bundle_lazylist<K, V, RecManager>::doInsert(const int tid, const K &key,
                                              const V &val, bool onlyIfAbsent) {
  nodeptr curr;
  nodeptr pred;
  nodeptr newnode;
  V result;
  while (true) {
    recordmgr->leaveQuiescentState(tid);
    pred = head;
    curr = pred->next;
    while (curr->key < key) {
      pred = curr;
      curr = curr->next;
    }
    acquireLock(&(pred->lock));
    if (validateLinks(tid, pred, curr)) {
      if (curr->key == key) {
        if (curr->marked) {  // this is an optimization
          releaseLock(&(pred->lock));
          recordmgr->enterQuiescentState(tid);
          continue;
        }
        // node containing key is not marked
        if (onlyIfAbsent) {
          V result = curr->val;
          releaseLock(&(pred->lock));
          recordmgr->enterQuiescentState(tid);
          return result;
        }
        cout << "ERROR: insert-replace functionality not implemented for "
                "bundle_lazylist_impl at this time."
             << endl;
        exit(-1);
      }
      // key is not in list
      assert(curr->key != key);
      result = NO_VALUE;
      newnode = new_node(tid, key, val, curr);
      acquireLock(&(newnode->lock));

      // Prepare bundles.
      BUNDLE_TYPE_DECL<node_t<K, V>> *bundles[] = {&newnode->rqbundle,
                                                   &pred->rqbundle, nullptr};
      nodeptr ptrs[] = {curr, newnode, nullptr};
      rqProvider->prepare_bundles(bundles, ptrs);
      SOFTWARE_BARRIER;

      // Perform original linearization.
      timestamp_t lin_time =
          rqProvider->linearize_update_at_write(tid, &pred->next, newnode);

      // Finalize bundles.
      rqProvider->finalize_bundles(bundles, lin_time);

      // Release locks and return.
      releaseLock(&(newnode->lock));
      releaseLock(&(pred->lock));
      recordmgr->enterQuiescentState(tid);
      return result;
    }
    releaseLock(&(pred->lock));
    recordmgr->enterQuiescentState(tid);
  }
}

/*
 * Logically remove an element by setting a mark bit to 1
 * before removing it physically.
 */
template <typename K, typename V, class RecManager>
V bundle_lazylist<K, V, RecManager>::erase(const int tid, const K &key) {
  nodeptr pred;
  nodeptr curr;
  V result;
  while (true) {
    recordmgr->leaveQuiescentState(tid);
    pred = head;
    curr = pred->next;
    while (curr->key < key) {
      pred = curr;
      curr = curr->next;
    }

    if (curr->key != key) {
      result = NO_VALUE;
      recordmgr->enterQuiescentState(tid);
      return result;
    }
    acquireLock(&(curr->lock));
    acquireLock(&(pred->lock));
    if (validateLinks(tid, pred, curr)) {
      // TODO: maybe implement version with atomic removal of consecutive marked
      // nodes
      assert(curr->key == key);
      result = curr->val;
      nodeptr c_nxt = curr->next;

      // Prepare bundles.
      BUNDLE_TYPE_DECL<node_t<K, V>> *bundles[] = {&pred->rqbundle, nullptr};
      nodeptr ptrs[] = {c_nxt, nullptr};
      rqProvider->prepare_bundles(bundles, ptrs);

      // Perform original linearization point.
      timestamp_t lin_time =
          rqProvider->linearize_update_at_write(tid, &curr->marked, 1LL);

      // Finalize bundles.
      rqProvider->finalize_bundles(bundles, lin_time);

      pred->next = c_nxt;
      nodeptr deletedNodes[] = {curr, nullptr};
      rqProvider->physical_deletion_succeeded(tid, deletedNodes);

      releaseLock(&(curr->lock));
      releaseLock(&(pred->lock));
      recordmgr->enterQuiescentState(tid);
      return result;
    }
    releaseLock(&(curr->lock));
    releaseLock(&(pred->lock));
    recordmgr->enterQuiescentState(tid);
  }
}

template <typename K, typename V, class RecManager>
inline bool bundle_lazylist<K, V, RecManager>::enterSnapshot(const int tid,
                                                             nodeptr pred,
                                                             timestamp_t ts,
                                                             nodeptr *next) {
  return pred->rqbundle.getPtrByTimestamp(tid, ts, next);
}

template <typename K, typename V, class RecManager>
int bundle_lazylist<K, V, RecManager>::rangeQuery(const int tid, const K &lo,
                                                  const K &hi,
                                                  K *const resultKeys,
                                                  V *const resultValues) {
  timestamp_t ts;
  int cnt = 0;
  bool ok;
  for (;;) {
    recordmgr->leaveQuiescentState(tid, true);

    // Phase 1. Traverse to node immediately preceding range.
    nodeptr curr = head;
    nodeptr pred = curr;
    while (curr != nullptr && curr->key < lo) {
      pred = curr;
      curr = curr->next;
    }
    assert(curr != nullptr);

    // Phase 2. Enter range using bundles.
    ts = rqProvider->start_traversal(tid);
    if (!enterSnapshot(tid, pred, ts, &curr)) {
      // If entering the range fails, then restart.
      rqProvider->end_traversal(tid);
      recordmgr->enterQuiescentState(tid);
      continue;
    }

    // Phase 3. Range collect.
    while (curr != nullptr && curr->key <= hi) {
      if (curr->key >= lo) {
        // Phase 3. Collect snapshot while in the range.
        cnt += getKeys(tid, curr, resultKeys + cnt, resultValues + cnt);
      }
      ok = curr->rqbundle.getPtrByTimestamp(tid, ts, &curr);
      assert(
          ok);  // At this point we should always find a bundle entry to follow
    }

    // Clears entry in active range query array.
    rqProvider->end_traversal(tid);
    recordmgr->enterQuiescentState(tid);

    // Traversal was completed successfully.
    if (curr != nullptr) {
      return cnt;
    }
  }
}

template <typename K, typename V, class RecManager>
void bundle_lazylist<K, V, RecManager>::cleanup(int tid) {
  // Walk the list using the newest edge and reclaim bundle entries.
  recordmgr->leaveQuiescentState(tid);
  BUNDLE_INIT_CLEANUP(rqProvider);
  while (head == nullptr)
    ;
  BUNDLE_CLEAN_BUNDLE(head->rqbundle);
  for (nodeptr curr = head->next; curr->key != KEY_MAX; curr = curr->next) {
    BUNDLE_CLEAN_BUNDLE(curr->rqbundle);
  }
  recordmgr->enterQuiescentState(tid);
}

template <typename K, typename V, class RecManager>
bool bundle_lazylist<K, V, RecManager>::validateBundles(int tid) {
  nodeptr curr = head;
  nodeptr temp;
  timestamp_t ts;
  bool valid = true;
#ifdef BUNDLE_DEBUG
  while (curr->key < KEY_MAX) {
    temp = curr->rqbundle.first(ts);
    if (temp != curr->next) {
      std::cout << "Pointer mismatch! [key=" << curr->next->key
                << ",marked=" << curr->next->marked << "] " << curr->next
                << " vs. [key=" << temp->key << ",marked=" << temp->marked
                << "] " << curr->rqbundle.dump(0) << std::flush;
      valid = false;
    }
    curr = curr->next;
  }
#endif
  return valid;
}

template <typename K, typename V, class RecManager>
long long bundle_lazylist<K, V, RecManager>::debugKeySum(nodeptr head) {
  long long result = 0;
  nodeptr curr = head->next;
  while (curr->key < KEY_MAX) {
    result += curr->key;
    curr = curr->next;
  }
  return result;
}

template <typename K, typename V, class RecManager>
long long bundle_lazylist<K, V, RecManager>::debugKeySum() {
  return debugKeySum(head);
}

template <typename K, typename V, class RecManager>
inline bool bundle_lazylist<K, V, RecManager>::isLogicallyDeleted(
    const int tid, node_t<K, V> *node) {
  return node->isMarked(tid, rqProvider);
}
#endif /* LAZYLIST_IMPL_H */
