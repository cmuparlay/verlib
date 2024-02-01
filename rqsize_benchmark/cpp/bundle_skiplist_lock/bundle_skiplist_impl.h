// Jacob Nelson
//
// This file contains the implementation of a bundled version of the skip-list
// provided in the original benchmark by Arbel-Raviv and Brown. It is an
// optimistic skip-list with lazy locking. We have modified it to utilize our
// bundle interface to provide linearizable range queries.

#ifndef SKIPLIST_LOCK_IMPL_H
#define SKIPLIST_LOCK_IMPL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bundle_skiplist.h"

#define CAS __sync_val_compare_and_swap

template <typename K, typename V>
static void sl_node_lock(nodeptr p_node) {
  while (1) {
    long cur_lock = p_node->lock;
    if (likely(cur_lock == 0)) {
      if (likely(CAS(&(p_node->lock), 0, 1) == 0)) {
        return;
      }
    }
    CPU_RELAX;
  }
}

template <typename K, typename V>
static void sl_node_unlock(nodeptr p_node) {
  p_node->lock = 0;
  SOFTWARE_BARRIER;
}

static int sl_randomLevel(const int tid, Random* const threadRNGs) {
  //    int level = 1;
  //    while (((threadRNGs[tid*PREFETCH_SIZE_WORDS].nextNatural() % 100) < 50)
  //    == 0 && level < SKIPLIST_MAX_LEVEL) {
  //        level++;
  //    }
  //    return level - 1;

  // Trevor: let's optimize with a bit hack from:
  // https://graphics.stanford.edu/~seander/bithacks.html#ZerosOnRightLinear
  // idea: new node level is the number of trailing zero bits in a random #.
  unsigned int v =
      threadRNGs[tid * PREFETCH_SIZE_WORDS]
          .nextNatural();  // 32-bit word input to count zero bits on right
  unsigned int c = 32;     // c will be the number of zero bits on the right
  v &= -signed(v);
  if (v) c--;
  if (v & 0x0000FFFF) c -= 16;
  if (v & 0x00FF00FF) c -= 8;
  if (v & 0x0F0F0F0F) c -= 4;
  if (v & 0x33333333) c -= 2;
  if (v & 0x55555555) c -= 1;
  return (c < SKIPLIST_MAX_LEVEL) ? c : SKIPLIST_MAX_LEVEL - 1;
}

template <typename K, typename V, class RecordMgr>
void bundle_skiplist<K, V, RecordMgr>::initNode(const int tid, nodeptr p_node,
                                                K key, V value, int height) {
  p_node->rqbundle.init();
  p_node->key = key;
  p_node->val = value;
  p_node->topLevel = height;
  p_node->lock = 0;
  p_node->marked = (long long)0;
  p_node->fullyLinked = (long long)0;
}

template <typename K, typename V, class RecordMgr>
nodeptr bundle_skiplist<K, V, RecordMgr>::allocateNode(const int tid) {
  nodeptr nnode = recmgr->template allocate<node_t<K, V>>(tid);
  if (nnode == NULL) {
    cout << "ERROR: out of memory" << endl;
    exit(-1);
  }
  return nnode;
}

template <typename K, typename V, class RecordMgr>
int bundle_skiplist<K, V, RecordMgr>::find_impl(const int tid, K key,
                                                nodeptr* p_preds,
                                                nodeptr* p_succs,
                                                nodeptr* p_found) {
  int level;
  int l_found = -1;
  nodeptr p_pred = NULL;
  nodeptr p_curr = NULL;

  p_pred = p_head;

  for (level = SKIPLIST_MAX_LEVEL - 1; level >= 0; level--) {
    p_curr = p_pred->p_next[level];
    while (key > p_curr->key) {
      p_pred = p_curr;
      p_curr = p_pred->p_next[level];
    }
    if (l_found == -1 && key == p_curr->key) {
      l_found = level;
    }
    p_preds[level] = p_pred;
    p_succs[level] = p_curr;
  }
  if (p_found) *p_found = p_curr;
  return l_found;
}

template <typename K, typename V, class RecManager>
bundle_skiplist<K, V, RecManager>::bundle_skiplist(const int numProcesses,
                                                   const K _KEY_MIN,
                                                   const K _KEY_MAX,
                                                   const V NO_VALUE,
                                                   Random* const threadRNGs)
    : NUM_PROCESSES(numProcesses),
      recmgr(new RecManager(numProcesses, 0)),
      threadRNGs(threadRNGs)
#ifdef USE_DEBUGCOUNTERS
      ,
      counters(new debugCounters(numProcesses))
#endif
      ,
      KEY_MIN(_KEY_MIN),
      KEY_MAX(_KEY_MAX),
      NO_VALUE(NO_VALUE) {

  int i;
  const int dummyTid = 0;
  recmgr->initThread(dummyTid);

  rqProvider =
      new RQProvider<K, V, node_t<K, V>, bundle_skiplist<K, V, RecManager>,
                     RecManager, true, false>(numProcesses, this, recmgr);

  p_tail = allocateNode(dummyTid);
  initNode(dummyTid, p_tail, KEY_MAX, NO_VALUE, SKIPLIST_MAX_LEVEL - 1);

  p_head = allocateNode(dummyTid);
  initNode(dummyTid, p_head, KEY_MIN, NO_VALUE, SKIPLIST_MAX_LEVEL - 1);

  BUNDLE_TYPE_DECL<node_t<K, V>>* bundles[] = {&p_head->rqbundle, nullptr};
  nodeptr ptrs[] = {p_tail, nullptr};
  rqProvider->prepare_bundles(bundles, ptrs);
  timestamp_t ts = rqProvider->get_update_lin_time(dummyTid);

  for (i = 0; i < SKIPLIST_MAX_LEVEL; i++) {
    p_head->p_next[i] = p_tail;
  }

  rqProvider->finalize_bundles(bundles, ts);
}

template <typename K, typename V, class RecManager>
bundle_skiplist<K, V, RecManager>::~bundle_skiplist() {
  const int dummyTid = 0;
  nodeptr curr = p_head;
  while (curr->key < KEY_MAX) {
    auto tmp = curr;
    curr = curr->p_next[0];
    recmgr->retire(dummyTid, tmp);
  }
  recmgr->retire(dummyTid, curr);
  delete rqProvider;
  recmgr->printStatus();
  delete recmgr;
#ifdef USE_DEBUGCOUNTERS
  delete counters;
#endif
}

template <typename K, typename V, class RecManager>
void bundle_skiplist<K, V, RecManager>::initThread(const int tid) {
  if (init[tid])
    return;
  else
    init[tid] = !init[tid];

  recmgr->initThread(tid);
  rqProvider->initThread(tid);
}

template <typename K, typename V, class RecManager>
void bundle_skiplist<K, V, RecManager>::deinitThread(const int tid) {
  if (!init[tid])
    return;
  else
    init[tid] = !init[tid];

  recmgr->deinitThread(tid);
  rqProvider->deinitThread(tid);
}

template <typename K, typename V, class RecManager>
bool bundle_skiplist<K, V, RecManager>::contains(const int tid, K key) {
  nodeptr p_preds[SKIPLIST_MAX_LEVEL] = {
      0,
  };
  nodeptr p_succs[SKIPLIST_MAX_LEVEL] = {
      0,
  };
  nodeptr p_found = NULL;
  nodeptr pred;
  nodeptr curr;
  int lFound;
  bool res;

  while (true) {
    recmgr->leaveQuiescentState(tid, true);
    lFound = find_impl(tid, key, p_preds, p_succs, &p_found);

    if (lFound >= 0) {
      pred = p_preds[lFound];
      bool ok = pred->rqbundle.getPtr(tid, &curr);
      assert(ok);
      while (curr->key < key) {
        ok = curr->rqbundle.getPtr(tid, &curr);
        assert(ok);
      }
      if (curr->key == key) {
        res = true;
      } else {
        res = false;
      }
    } else {
      res = false;
    }
    rqProvider->end_traversal(tid);
    recmgr->enterQuiescentState(tid);
    return res;
  }
}

template <typename K, typename V, class RecManager>
const pair<V, bool> bundle_skiplist<K, V, RecManager>::find(const int tid,
                                                            const K& key) {
  nodeptr p_preds[SKIPLIST_MAX_LEVEL] = {
      0,
  };
  nodeptr p_succs[SKIPLIST_MAX_LEVEL] = {
      0,
  };
  nodeptr p_found = NULL;
  int lFound;
  bool res;
  recmgr->leaveQuiescentState(tid, true);
  lFound = find_impl(tid, key, p_preds, p_succs, &p_found);
  res = (lFound != -1) && p_succs[lFound]->fullyLinked &&
        !p_succs[lFound]->marked;
#ifdef RQ_SNAPCOLLECTOR
  if (lFound != -1) rqProvider->search_report_target_key(tid, key, p_found);
#endif
  recmgr->enterQuiescentState(tid);
  if (res) {
    return pair<V, bool>(p_found->val, true);
  } else {
    return pair<V, bool>(NO_VALUE, false);
  }
}

template <typename K, typename V, class RecManager>
V bundle_skiplist<K, V, RecManager>::doInsert(const int tid, const K& key,
                                              const V& value,
                                              bool onlyIfAbsent) {
  nodeptr p_preds[SKIPLIST_MAX_LEVEL] = {
      0,
  };
  nodeptr p_succs[SKIPLIST_MAX_LEVEL] = {
      0,
  };
  nodeptr p_node_found = NULL;
  nodeptr p_pred = NULL;
  nodeptr p_succ = NULL;
  nodeptr p_new_node = NULL;
  V ret = NO_VALUE;
  int level;
  int topLevel = -1;
  int lFound = -1;
  int done = 0;

  topLevel = sl_randomLevel(tid, threadRNGs);
  while (!done) {
    recmgr->leaveQuiescentState(tid);
    lFound = find_impl(tid, key, p_preds, p_succs, NULL);
    if (lFound != -1) {
      p_node_found = p_succs[lFound];
      if (!p_node_found->marked) {
        while (!p_node_found->fullyLinked) {
          CPU_RELAX;
        }  // keep spinning
        recmgr->enterQuiescentState(tid);

        // node is found and fully linked!
        if (onlyIfAbsent) {
          ret = p_node_found->val;
#ifdef RQ_SNAPCOLLECTOR
          rqProvider->insert_readonly_report_target_key(tid, p_node_found);
#endif
          return ret;
        } else {
          cout << "ERROR: insert-replace functionality not implemented for "
                  "bundle_skiplist_impl"
               << endl;
          exit(-1);
        }
      }
      recmgr->enterQuiescentState(tid);
      continue;  // try again
    }

    int highestLocked = -1;
    int valid = 1;
    for (level = 0; valid && (level <= topLevel); level++) {
      p_pred = p_preds[level];
      p_succ = p_succs[level];
      if (level == 0 || p_preds[level] != p_preds[level - 1]) {
        // don't try to lock same node twice
        sl_node_lock(p_pred);
      }
      highestLocked = level;
      // make sure nothing has changed in between
      valid = (!p_pred->marked && !p_succ->marked &&
               (p_pred->p_next[level] == p_succ));
    }

    if (valid) {
      p_new_node = allocateNode(tid);  // shmem_none->allocateNode(tid);
#ifdef __HANDLE_STATS
      GSTATS_APPEND(tid, node_allocated_addresses,
                    ((long long)p_new_node) % (1 << 12));
#endif
      initNode(tid, p_new_node, key, value, topLevel);
      sl_node_lock(p_new_node);
      p_new_node->topLevel = topLevel;
      for (level = 0; level <= topLevel; level++) {
        p_new_node->p_next[level] = p_succs[level];
      }

      // Bundle preparation must occur before the node is connected.
      BUNDLE_TYPE_DECL<node_t<K, V>>* bundles[] = {
          &p_preds[0]->rqbundle, &p_new_node->rqbundle, nullptr};
      nodeptr ptrs[] = {p_new_node, p_succs[0], nullptr};
      rqProvider->prepare_bundles(bundles, ptrs);

      SOFTWARE_BARRIER;
      timestamp_t lin_time = rqProvider->get_update_lin_time(tid);
      SOFTWARE_BARRIER;
      for (level = 0; level <= topLevel; level++) {
        p_preds[level]->p_next[level] = p_new_node;
      }
      // timestamp_t lin_time = rqProvider->linearize_update_at_write(
      //     tid, &p_new_node->fullyLinked, (long long)1);
      p_new_node->fullyLinked = 1;
      SOFTWARE_BARRIER;
      rqProvider->finalize_bundles(bundles, lin_time);
#ifdef __HANDLE_STATS
      GSTATS_ADD_IX(tid, skiplist_inserted_on_level, 1, topLevel);
#endif
      // p_new_node->fullyLinked = 1;
      done = 1;
      sl_node_unlock(p_new_node);
    }

    // unlock everything here
    for (level = 0; level <= highestLocked; level++) {
      if (level == 0 || p_preds[level] != p_preds[level - 1]) {
        // don't try to unlock the same node twice
        sl_node_unlock(p_preds[level]);
      }
    }

    recmgr->enterQuiescentState(tid);
  }
  return ret;
}

template <typename K, typename V, class RecManager>
V bundle_skiplist<K, V, RecManager>::erase(const int tid, const K& key) {
  nodeptr p_preds[SKIPLIST_MAX_LEVEL] = {
      0,
  };
  nodeptr p_succs[SKIPLIST_MAX_LEVEL] = {
      0,
  };
  nodeptr p_victim = NULL;
  nodeptr p_pred = NULL;
  int i;
  int level;
  int lFound;
  int highestLocked;
  int valid;
  int isMarked = 0;
  int topLevel = -1;
  V ret = NO_VALUE;

  while (1) {
    recmgr->leaveQuiescentState(tid);

    lFound = find_impl(tid, key, p_preds, p_succs, NULL);
    if (lFound == -1) {
      recmgr->enterQuiescentState(tid);
      break;
    }
    p_victim = p_succs[lFound];

    if ((!isMarked) || (p_victim->fullyLinked &&
                        (p_victim->topLevel == lFound) && !p_victim->marked)) {
      if (!isMarked) {
        topLevel = p_victim->topLevel;
        sl_node_lock(p_victim);
        if (p_victim->marked) {
          sl_node_unlock(p_victim);
          // ret = 0; ret is already NO_VALUE = fail
          recmgr->enterQuiescentState(tid);
          break;
        }
      }

      highestLocked = -1;
      valid = 1;

      for (level = 0; valid && (level <= topLevel); level++) {
        p_pred = p_preds[level];
        if (level == 0 ||
            p_preds[level] != p_preds[level - 1]) {  // don't do twice
          sl_node_lock(p_pred);
        }
        highestLocked = level;
        valid = (!p_pred->marked && (p_pred->p_next[level] == p_victim));
      }

      if (valid) {
        BUNDLE_TYPE_DECL<node_t<K, V>>* bundles[] = {&p_preds[0]->rqbundle,
                                                     nullptr};
        nodeptr ptrs[] = {p_victim->p_next[0], nullptr};
        rqProvider->prepare_bundles(bundles, ptrs);
        timestamp_t lin_time = rqProvider->linearize_update_at_write(
            tid, &p_victim->marked, (long long)1);
        rqProvider->finalize_bundles(bundles, lin_time);
        for (level = topLevel; level >= 0; level--) {
          p_preds[level]->p_next[level] = p_victim->p_next[level];
        }
        nodeptr deletedNodes[] = {p_victim, nullptr};
        rqProvider->physical_deletion_succeeded(tid, deletedNodes);
#ifdef BUNDLE_DEBUG
        if (!p_preds[0]->validate()) {
          timestamp_t unused_ts;
          nodeptr bundle_head = p_preds[0]->rqbundle.first(unused_ts);
          std::cout << "[DELETE " << p_victim
                    << "] Pointer mismatch! [key=" << p_preds[0]->p_next[0]->key
                    << ",marked=" << p_preds[0]->p_next[0]->marked << "] "
                    << p_preds[0]->p_next[0] << " vs. [key=" << bundle_head->key
                    << ",marked=" << bundle_head->marked << "] " << bundle_head
                    << " " << p_preds[0]->rqbundle.dump(0) << std::flush;
          exit(1);
        }
#endif
        ret = p_victim->val;
        sl_node_unlock(p_victim);
      } else {
        sl_node_unlock(p_victim);
      }

      // unlock mutexes
      for (i = 0; i <= highestLocked; i++) {
        if (i == 0 || p_preds[i] != p_preds[i - 1]) {
          sl_node_unlock(p_preds[i]);
        }
      }

      if (valid) {
        recmgr->enterQuiescentState(tid);
        break;
      }
    }
    recmgr->enterQuiescentState(tid);
  }
  //    if (ret != NO_VALUE) {
  //        recmgr->retire(tid, (nodeptr) p_victim);
  //    }
  return ret;
}

template <typename K, typename V, class RecManager>
int bundle_skiplist<K, V, RecManager>::rangeQuery(const int tid, const K& lo,
                                                  const K& hi,
                                                  K* const resultKeys,
                                                  V* const resultValues) {
  //    cout<<"rangeQuery(lo="<<lo<<" hi="<<hi<<")"<<endl;
  timestamp_t ts;
  bool ok;
  long i = 0;
  while (true) {
    int cnt = 0;
    recmgr->leaveQuiescentState(tid, true);
    nodeptr pred = p_head;
    nodeptr curr = nullptr;
    // Phase 1. Pre-range traversal
    for (int level = SKIPLIST_MAX_LEVEL - 1; level >= 0; level--) {
      curr = pred->p_next[level];
      while (curr->key < lo) {
        pred = curr;
        curr = curr->p_next[level];
      }
    }

    // Phase 2. Enter snapshot
    ts = rqProvider->start_traversal(tid);
    if (unlikely(!pred->rqbundle.getPtrByTimestamp(tid, ts, &curr))) {
#ifdef __HANDLE_STATS
      GSTATS_ADD(tid, bundle_restarts, 1);
#endif
      rqProvider->end_traversal(tid);
      recmgr->enterQuiescentState(tid);
      continue;
    }

    // Phase 3. Collect range
    while (curr != nullptr && curr->key <= hi) {
      if (curr->key >= lo) {
        cnt += getKeys(tid, curr, resultKeys + cnt, resultValues + cnt);
      }
      ok = curr->rqbundle.getPtrByTimestamp(tid, ts, &curr);
      assert(ok);
    }
    rqProvider->end_traversal(tid);
    recmgr->enterQuiescentState(tid);

    // Traversal successful.
    if (curr != nullptr) {
      return cnt;
    }
  }
}

template <typename K, typename V, class RecManager>
void bundle_skiplist<K, V, RecManager>::cleanup(int tid) {
  recmgr->leaveQuiescentState(tid);
  BUNDLE_INIT_CLEANUP(rqProvider);
  BUNDLE_CLEAN_BUNDLE(p_head->rqbundle);
  for (nodeptr curr = p_head->p_next[0]; curr->key != KEY_MAX;
       curr = curr->p_next[0]) {
    if (!curr->marked) {
      BUNDLE_CLEAN_BUNDLE(curr->rqbundle);
    }
  }
  recmgr->enterQuiescentState(tid);
}

template <typename K, typename V, class RecManager>
bool bundle_skiplist<K, V, RecManager>::validateBundles(int tid) {
  bool valid = true;
#ifdef BUNDLE_DEBUG
  for (nodeptr curr = p_head->p_next[0]; curr->key != KEY_MAX;
       curr = curr->p_next[0]) {
    timestamp_t ts;
    nodeptr ptr = curr->rqbundle->first(ts);
    if (ptr != curr->p_next[0]) {
      std::cout << "Pointer mismatch! [key=" << curr->p_next[0]->key
                << ",marked=" << curr->p_next[0]->marked << "] "
                << curr->p_next[0] << " vs. [key=" << ptr->key
                << ",marked=" << ptr->marked << "] " << curr->rqbundle->dump(0)
                << std::flush;
      valid = false;
    }
#ifdef BUNDLE_CLEANUP
    if (curr->rqbundle->size() > 1) {
      std::cout << curr->rqbundle->dump(0) << std::flush;
      return false;
    }
#endif
  }
#endif
  return valid;
}

#endif /* SKIPLIST_LOCK_IMPL_H */
