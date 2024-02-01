/**
 * Copyright 2015
 * Maya Arbel (mayaarl [at] cs [dot] technion [dot] ac [dot] il).
 * Adam Morrison (mad [at] cs [dot] technion [dot] ac [dot] il).
 *
 * This file is part of Predicate RCU.
 *
 * Predicate RCU is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors Maya Arbel and Adam Morrison
 * Converted into a class and implemented as a 3-path algorithm by Trevor Brown
 */

// Jacob Nelson
//
// This implementation has been extended to utilize bundling to provide
// linearizable range queries.

#ifndef BUNDLE_CITRUS_IMPL_H
#define BUNDLE_CITRUS_IMPL_H

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <utility>

#include "blockbag.h"
#include "bundle_citrus.h"
#include "locks_impl.h"
#include "urcu.h"
using namespace std;
using namespace urcu;

template <typename K, typename V, class RecManager>
nodeptr bundle_citrustree<K, V, RecManager>::newNode(const int tid, K key,
                                                     V value) {
  nodeptr nnode = recordmgr->template allocate<node_t<K, V>>(tid);
  if (nnode == NULL) {
    printf("out of memory\n");
    exit(1);
  }
  nnode->key = key;
  nnode->marked = false;
  nnode->child[0] = (nodeptr) nullptr;
  nnode->child[1] = (nodeptr) nullptr;
  nnode->tag[0] = 0;
  nnode->tag[1] = 0;
  nnode->value = value;
  nnode->lock = false;
  nnode->rqbundle[0].init();
  nnode->rqbundle[1].init();
#ifdef __HANDLE_STATS
  GSTATS_APPEND(tid, node_allocated_addresses, ((long long)nnode) % (1 << 12));
#endif
  return nnode;
}

template <typename K, typename V, class RecManager>
bundle_citrustree<K, V, RecManager>::bundle_citrustree(
    const K bigger_than_max_key, const V _NO_VALUE, const int numProcesses)
    : recordmgr(new RecManager(numProcesses, SIGQUIT)),
      rqProvider(new RQProvider<K, V, node_t<K, V>,
                                bundle_citrustree<K, V, RecManager>, RecManager,
                                LOGICAL_DELETION_USAGE, false>(numProcesses,
                                                               this, recordmgr))
#ifdef USE_DEBUGCOUNTERS
      ,
      counters(new debugCounters(numProcesses))
#endif
      ,
      NO_KEY(bigger_than_max_key),
      NO_VALUE(_NO_VALUE) {
  const int tid = 0;
  initThread(tid);
  // finish initializing RCU

#if 1
  // need to simulate real insertion of root and rootchild,
  // since range queries will actually try to add these nodes,
  // and we don't want blocking rq providers to spin forever
  // waiting for their itimes to be set to a positive number.
  nodeptr _rootchild = newNode(tid, NO_KEY, NO_VALUE);
  nodeptr _root = newNode(tid, NO_KEY, NO_VALUE);
  _root->child[0] = _rootchild;
  root = NULL;  // to prevent reading from uninitialized root pointer in the
  // following call (which, depending on the rq provider, may read
  // root, e.g., to perform a cas)

  // Prepare bundles for "real" insertion.
  BUNDLE_TYPE_DECL<node_t<K, V>>* bundles[] = {
      &_root->rqbundle[0], &_root->rqbundle[1], &_rootchild->rqbundle[0],
      &_rootchild->rqbundle[1], nullptr};
  nodeptr ptrs[] = {_rootchild, nullptr, nullptr, nullptr, nullptr};
  rqProvider->prepare_bundles(bundles, ptrs);

  // Perform linearization point.
  timestamp_t lin_time =
      rqProvider->linearize_update_at_write(tid, &root, _root);

  // Finalize the bundles.
  rqProvider->finalize_bundles(bundles, lin_time);
#else
  root = newNode(tid, NO_KEY, NO_VALUE);
  root->child[0] = newNode(tid, NO_KEY, NO_VALUE);
#endif
}

template <typename K, typename V, class RecManager>
bundle_citrustree<K, V, RecManager>::~bundle_citrustree() {
  int numNodes = 0;
  delete rqProvider;
  dfsDeallocateBottomUp(root, &numNodes);
  VERBOSE DEBUG COUTATOMIC(" deallocated nodes " << numNodes << endl);
  recordmgr->printStatus();
  delete recordmgr;
#ifdef USE_DEBUGCOUNTERS
  delete counters;
#endif
}

template <typename K, typename V, class RecManager>
const pair<V, bool> bundle_citrustree<K, V, RecManager>::find(const int tid,
                                                              const K& key) {
  recordmgr->leaveQuiescentState(tid, true);
  readLock();
  nodeptr curr = root->child[0];
  K ckey = curr->key;
  while (curr != NULL && ckey != key) {
    if (ckey > key) curr = curr->child[0];
    if (ckey < key) curr = curr->child[1];
    if (curr != NULL) ckey = curr->key;
  }
  readUnlock();
  if (curr == NULL) {
    recordmgr->enterQuiescentState(tid);
    return pair<V, bool>(NO_VALUE, false);
  }
  V result = curr->value;
  recordmgr->enterQuiescentState(tid);
  return pair<V, bool>(result, true);
}

template <typename K, typename V, class RecManager>
bool bundle_citrustree<K, V, RecManager>::contains(const int tid,
                                                   const K& key) {
  // return find(tid, key).second;
  while (true) {
    recordmgr->leaveQuiescentState(tid, true);
    readLock();
    nodeptr curr = root->child[0];
    nodeptr pred = root;
    // bool in_snapshot = false;
    int child = 0;
    K ckey = curr->key;
    bool ok;
    while (curr != NULL && ckey != key) {
      pred = curr;
      if (ckey > key) {
        child = 0;
        curr = curr->child[0];
      }
      if (ckey < key) {
        child = 1;
        curr = curr->child[1];
      }
      if (curr != NULL) ckey = curr->key;
    }

    // Find key using bundles.
    if (curr != NULL)
      ckey = curr->key;  // Necessary because previous ckey may not reflect node
                         // pointed to by the bundle
    while (curr != NULL && ckey != key) {
      if (ckey > key) {
        ok = curr->rqbundle[0].getPtr(tid, &curr);
        assert(ok);
      }
      if (ckey < key) {
        ok = curr->rqbundle[1].getPtr(tid, &curr);
        assert(ok);
      }
      if (curr != NULL) ckey = curr->key;
    }

    readUnlock();
    if (curr == NULL) {
      rqProvider->end_traversal(tid);
      recordmgr->enterQuiescentState(tid);
      return false;
    }
    V result = curr->value;
    rqProvider->end_traversal(tid);
    recordmgr->enterQuiescentState(tid);
    return true;
  }
}

template <typename K, typename V, class RecManager>
bool bundle_citrustree<K, V, RecManager>::validate(const int tid, nodeptr prev,
                                                   int tag, nodeptr curr,
                                                   int direction) {
  if (curr == NULL) {
    return (!prev->marked && (prev->child[direction] == curr) &&
            (prev->tag[direction] == tag));
  } else {
    return (!prev->marked && !curr->marked && prev->child[direction] == curr);
  }
}

#define SEARCH                          \
  prev = root;                          \
  curr = root->child[0];                \
  direction = 0;                        \
  ckey = curr->key;                     \
  while (curr != NULL && ckey != key) { \
    prev = curr;                        \
    if (ckey > key) {                   \
      curr = curr->child[0];            \
      direction = 0;                    \
    }                                   \
    if (ckey < key) {                   \
      curr = curr->child[1];            \
      direction = 1;                    \
    }                                   \
    if (curr != NULL) ckey = curr->key; \
  }

template <typename K, typename V, class RecManager>
const V bundle_citrustree<K, V, RecManager>::doInsert(const int tid,
                                                      const K& key,
                                                      const V& value,
                                                      bool onlyIfAbsent) {
  nodeptr prev;
  nodeptr curr;
  int direction;
  K ckey;
  int tag;

retry:
  recordmgr->leaveQuiescentState(tid);
  readLock();
  SEARCH;
  tag = prev->tag[direction];
  readUnlock();
  if (curr != NULL) {
    if (onlyIfAbsent) {
      V result = curr->value;
      recordmgr->enterQuiescentState(tid);
      assert(result != NO_VALUE);
      return result;
    } else {
      std::cerr << "In place updates are not supported by bundle_citrus_impl "
                   "at this time..."
                << std::endl;
      exit(1);
    }
  }

  acquireLock(&(prev->lock));
  if (validate(tid, prev, tag, curr, direction)) {
    nodeptr nnode = newNode(tid, key, value);
    acquireLock(&(nnode->lock));

    // Prepare the bundles.
    BUNDLE_TYPE_DECL<node_t<K, V>>* bundles[] = {
        &nnode->rqbundle[0], &nnode->rqbundle[1], &prev->rqbundle[direction],
        nullptr};
    nodeptr ptrs[] = {nullptr, nullptr, nnode, nullptr};
    rqProvider->prepare_bundles(bundles, ptrs);

    // Perform linearization.
    timestamp_t lin_time = rqProvider->linearize_update_at_write(
        tid, &prev->child[direction], nnode);

    // Finalize the bundles.
    rqProvider->finalize_bundles(bundles, lin_time);

    releaseLock(&(nnode->lock));
    releaseLock(&(prev->lock));
    recordmgr->enterQuiescentState(tid);
    return NO_VALUE;
  } else {
    releaseLock(&(prev->lock));
    recordmgr->enterQuiescentState(tid);
    goto retry;
  }
}

template <class K, class V, class RecManager>
const V bundle_citrustree<K, V, RecManager>::insertIfAbsent(const int tid,
                                                            const K& key,
                                                            const V& val) {
  return doInsert(tid, key, val, true);
}

template <class K, class V, class RecManager>
const V bundle_citrustree<K, V, RecManager>::insert(const int tid, const K& key,
                                                    const V& val) {
  return doInsert(tid, key, val, false);
}

template <typename K, typename V, class RecManager>
const pair<V, bool> bundle_citrustree<K, V, RecManager>::erase(const int tid,
                                                               const K& key) {
  nodeptr prev;
  nodeptr curr;
  int direction;
  K ckey;

  nodeptr prevSucc;
  nodeptr succ;
  nodeptr next;
  nodeptr nnode;

  int min_bucket;

retry:
  recordmgr->leaveQuiescentState(tid);
  readLock();
  SEARCH;
  readUnlock();
  if (curr == NULL) {
    recordmgr->enterQuiescentState(tid);
    return pair<V, bool>(NO_VALUE, false);
  }
  acquireLock(&(prev->lock));
  acquireLock(&(curr->lock));
  if (!validate(tid, prev, 0, curr, direction)) {
    releaseLock(&(prev->lock));
    releaseLock(&(curr->lock));
    recordmgr->enterQuiescentState(tid);
    goto retry;
  }
  if (curr->child[0] == NULL) {
    curr->marked = true;

    // Prepare bundles.
    BUNDLE_TYPE_DECL<node_t<K, V>>* bundles[] = {&prev->rqbundle[direction],
                                                 nullptr};
    nodeptr ptrs[] = {curr->child[1], nullptr};
    rqProvider->prepare_bundles(bundles, ptrs);

    // Perform linearization.
    timestamp_t lin_time = rqProvider->linearize_update_at_write(
        tid, &prev->child[direction], (nodeptr)curr->child[1]);

    // Finalize bundles.
    rqProvider->finalize_bundles(bundles, lin_time);

    nodeptr deletedNodes[] = {curr, nullptr};
    rqProvider->physical_deletion_succeeded(tid, deletedNodes);

    if (prev->child[direction] == NULL) {
      prev->tag[direction]++;
    }

    V result = curr->value;

    releaseLock(&(prev->lock));
    releaseLock(&(curr->lock));
    recordmgr->enterQuiescentState(tid);
    return pair<V, bool>(result, true);
  }
  if (curr->child[1] == NULL) {
    curr->marked = true;

    // Prepare bundles.
    BUNDLE_TYPE_DECL<node_t<K, V>>* bundles[] = {&prev->rqbundle[direction],
                                                 nullptr};
    nodeptr ptrs[] = {curr->child[0], nullptr};
    rqProvider->prepare_bundles(bundles, ptrs);

    // Perform linearization.
    timestamp_t lin_time = rqProvider->linearize_update_at_write(
        tid, &prev->child[direction], (nodeptr)curr->child[0]);

    // Finalize bundles.
    rqProvider->finalize_bundles(bundles, lin_time);

    nodeptr deletedNodes[] = {curr, nullptr};
    rqProvider->physical_deletion_succeeded(tid, deletedNodes);

    if (prev->child[direction] == NULL) {
      prev->tag[direction]++;
    }

    V result = curr->value;

    releaseLock(&(prev->lock));
    releaseLock(&(curr->lock));
    recordmgr->enterQuiescentState(tid);
    return pair<V, bool>(result, true);
  }
  prevSucc = curr;
  succ = curr->child[1];
  next = succ->child[0];
  while (next != NULL) {
    prevSucc = succ;
    succ = next;
    next = next->child[0];
  }
  int succDirection = 1;
  if (prevSucc != curr) {
    acquireLock(&(prevSucc->lock));
    succDirection = 0;
  }
  acquireLock(&(succ->lock));
  if (validate(tid, prevSucc, 0, succ, succDirection) &&
      validate(tid, succ, succ->tag[0], NULL, 0)) {
    curr->marked = true;
    nnode = newNode(tid, succ->key, succ->value);
    nnode->child[0] = curr->child[0];
    nnode->child[1] = curr->child[1];
    acquireLock(&(nnode->lock));

    // Prepare bundles. Note that if the successor's parent is the node being
    // deleted then the new node (which is a copy of the successor) needs to
    // point to it previous right-hand branch. Otherwise, the successor's
    // parents left-hand branch will point to it. In the original
    // implementation, this is updated after `synchronize()` but we perform this
    // check here to ensure that range queries follow the correct path.
    BUNDLE_TYPE_DECL<node_t<K, V>>* bundles[] = {
        &prev->rqbundle[direction], &nnode->rqbundle[0], &nnode->rqbundle[1],
        (prevSucc != curr ? &prevSucc->rqbundle[0] : nullptr), nullptr};
    nodeptr ptrs[] = {nnode, curr->child[0],
                      (prevSucc != curr ? curr->child[1] : succ->child[1]),
                      (prevSucc != curr ? succ->child[1] : nullptr), nullptr};
    rqProvider->prepare_bundles(bundles, ptrs);

    // Perform linearization.
    timestamp_t lin_time = rqProvider->linearize_update_at_write(
        tid, &prev->child[direction], nnode);

    // Finalize bundles.
    rqProvider->finalize_bundles(bundles, lin_time);

    nodeptr deletedNodes[] = {curr, succ, nullptr};
    rqProvider->physical_deletion_succeeded(tid, deletedNodes);

    synchronize();

    succ->marked = true;
    if (prevSucc == curr) {
      nnode->child[1] = succ->child[1];
      if (nnode->child[1] == NULL) {
        nnode->tag[1]++;
      }
    } else {
      prevSucc->child[0] = succ->child[1];
      if (prevSucc->child[0] == NULL) {
        prevSucc->tag[0]++;
      }
    }

    V result = curr->value;

    releaseLock(&(prev->lock));
    releaseLock(&(nnode->lock));
    releaseLock(&(curr->lock));
    if (prevSucc != curr) releaseLock(&(prevSucc->lock));
    releaseLock(&(succ->lock));
    recordmgr->enterQuiescentState(tid);
    return pair<V, bool>(result, true);
  }
  releaseLock(&(prev->lock));
  releaseLock(&(curr->lock));
  if (prevSucc != curr) releaseLock(&(prevSucc->lock));
  releaseLock(&(succ->lock));
  recordmgr->enterQuiescentState(tid);
  goto retry;
}

template <typename K, typename V, class RecManager>
int bundle_citrustree<K, V, RecManager>::rangeQuery(const int tid, const K& lo,
                                                    const K& hi,
                                                    K* const resultKeys,
                                                    V* const resultValues) {
  // Traverse tree until the root of the subtree defining the range is found.
  while (true) {
    recordmgr->leaveQuiescentState(tid, true);
    nodeptr curr = root->child[0];
    nodeptr pred = curr;
    nodeptr left;
    nodeptr right;
    timestamp_t ts;
    int direction = 0;
    bool restart = false;
    bool ok;
    while (curr != nullptr) {
      if (curr->key >= lo && curr->key <= hi) {
        break;
      } else if (curr->key < lo) {
        // Phase 1. Search right subtree.
        pred = curr;
        curr = curr->child[1];
        direction = 1;
      } else {
        // Phase 1. Search left subtree.
        pred = curr;
        curr = curr->child[0];
        direction = 0;
      }
    }

    // Phase 2. Enter snapshot.
    ts = rqProvider->start_traversal(tid);
    if (unlikely(
            !pred->rqbundle[direction].getPtrByTimestamp(tid, ts, &curr))) {
      // A concurrent update may have inserted a new node immediately
      // preceding the range and we must start over.
#ifdef __HANDLE_STATE
      GSTATS_ADD(tid, bundle_restarts, 1);
#endif
      continue;
    }

    // Phase 3. Enter range.
    while (curr != nullptr) {
      if (curr->key >= lo && curr->key <= hi) {
        break;
      } else if (curr->key < lo) {
        ok = pred->rqbundle[1].getPtrByTimestamp(tid, ts, &curr);
        assert(ok);
        pred = curr;
      } else {
        ok = pred->rqbundle[0].getPtrByTimestamp(tid, ts, &curr);
        assert(ok);
        pred = curr;
      }
    }

    // If curr is not `nullptr` then the range is contained in the subtree
    // rooted at this node.
    if (curr != nullptr) {
      // Phase 3. Collect the result set.
      block<node_t<K, V>> stack(nullptr);
      int size = 0;
      stack.push(curr);
      while (!stack.isEmpty()) {
        nodeptr node = stack.pop();

        // what (if anything) we need to do with CITRUS' validation function?
        // answer: nothing, because searches don't need to do anything with
        // it.

        // If the key is in the range, add it to the result set.
        rqProvider->traversal_try_add(tid, node, resultKeys, resultValues,
                                      &size, lo, hi);

        // Explore subtrees with DFS based on timestamp and range.
        ok = node->rqbundle[0].getPtrByTimestamp(tid, ts, &left);
        assert(ok);
        ok = node->rqbundle[1].getPtrByTimestamp(tid, ts, &right);
        assert(ok);
        if (left != nullptr && lo < node->key) {
          stack.push(left);
        }
        if (right != nullptr && hi > node->key) {
          stack.push(right);
        }
      }
      rqProvider->end_traversal(tid);
      recordmgr->enterQuiescentState(tid);
      return size;
    } else {
      rqProvider->end_traversal(tid);
      recordmgr->enterQuiescentState(tid);
      return 0;
    }
  }
}

template <typename K, typename V, class RecManager>
void bundle_citrustree<K, V, RecManager>::cleanup(int tid) {
  recordmgr->leaveQuiescentState(tid, true);
  BUNDLE_INIT_CLEANUP(rqProvider);
  nodeptr curr = root->child[0];

  block<node_t<K, V>> stack(nullptr);
  stack.push(curr);
  while (!stack.isEmpty()) {
    // Get the next node to process.
    nodeptr node = stack.pop();

    // Add its children to the stack.
    nodeptr left = node->child[0];
    nodeptr right = node->child[1];
    if (left != nullptr) {
      stack.push(left);
    }
    if (right != nullptr) {
      stack.push(right);
    }

    // Clean up the bundles.
    BUNDLE_CLEAN_BUNDLE(node->rqbundle[0]);
    BUNDLE_CLEAN_BUNDLE(node->rqbundle[1]);
  }
  recordmgr->enterQuiescentState(tid);
}

template <typename K, typename V, class RecManager>
bool bundle_citrustree<K, V, RecManager>::validateBundles(int tid) {
  nodeptr curr = root->child[0];
  bool valid = true;
  block<node_t<K, V>> stack(nullptr);
  stack.push(curr);
  while (!stack.isEmpty()) {
    nodeptr node = stack.pop();

    // Validate the bundles.
    nodeptr ptr;
    timestamp_t ts;
    ptr = node->rqbundle[0].first(ts);
    if (node->child[0] != ptr) {
      std::cout << "Pointer mismatch! [key=" << node->child[0]->key
                << ",marked=" << node->child[0]->marked << "] "
                << node->child[0] << " vs. [key=" << ptr->key
                << ",marked=" << ptr->marked << "] "
                << node->rqbundle[0].dump(0) << std::flush;
      valid = false;
    }

    ptr = node->rqbundle[1].first(ts);
    if (node->child[1] != ptr) {
      std::cout << "Pointer mismatch! [key=" << node->child[1]->key
                << ",marked=" << node->child[1]->marked << "] "
                << node->child[1] << " vs. [key=" << ptr->key
                << ",marked=" << ptr->marked << "] "
                << node->rqbundle[1].dump(0) << std::flush;
      valid = false;
    }

    // Add its children to the stack.
    nodeptr left = node->child[0];
    nodeptr right = node->child[1];
    if (left != nullptr) {
      stack.push(left);
    }
    if (right != nullptr) {
      stack.push(right);
    }
  }
  return valid;
}

template <typename K, typename V, class RecManager>
long long bundle_citrustree<K, V, RecManager>::debugKeySum(nodeptr root) {
  if (root == NULL) return 0;
  return root->key + debugKeySum(root->child[0]) + debugKeySum(root->child[1]);
}

template <typename K, typename V, class RecManager>
long long bundle_citrustree<K, V, RecManager>::debugKeySum() {
  return debugKeySum(root->child[0]->child[0]);
}

#endif