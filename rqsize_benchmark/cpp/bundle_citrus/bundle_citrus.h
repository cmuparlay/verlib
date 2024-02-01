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

#ifndef _DICTIONARY_H_
#define _DICTIONARY_H_

#include <signal.h>
#include <stdbool.h>

#include <sstream>
#include <stack>
#include <unordered_set>
#include <utility>

#include "plaf.h"

#ifndef MAX_NODES_INSERTED_OR_DELETED_ATOMICALLY
// define BEFORE including rq_provider.h
#define MAX_NODES_INSERTED_OR_DELETED_ATOMICALLY 4
#endif
#include "rq_bundle.h"
using namespace std;

#define LOGICAL_DELETION_USAGE false

//#define INSERT_REPLACE

template <typename K, typename V>
struct node_t {
  struct {
    K key;
    V value;
    node_t<K, V>* volatile child[2];
    int tag[2];
    volatile int lock;
    bool marked;
  };
  BUNDLE_TYPE_DECL<node_t<K, V>> rqbundle[2];

  ~node_t() {}

  bool validate() {
    bool valid = true;
    node_t<K, V>* ptr;
    timestamp_t ts;
    ptr = rqbundle[0].first(ts);
    if (child[0] != ptr) {
      std::cout << "Pointer mismatch! " << child[0] << " vs. "
                << rqbundle[0].dump(0) << std::flush;
      valid = false;
    }

    ptr = rqbundle[1].first(ts);
    if (child[1] != ptr) {
      std::cout << "Pointer mismatch!  " << child[1] << " vs. "
                << rqbundle[1].dump(0) << std::flush;
      valid = false;
    }
    return valid;
  }
};

#define nodeptr node_t<K, V>*

template <typename K, typename V, class RecManager>
class bundle_citrustree {
 private:
  RecManager* const recordmgr;
  RQProvider<K, V, node_t<K, V>, bundle_citrustree<K, V, RecManager>,
             RecManager, LOGICAL_DELETION_USAGE, false>* const rqProvider;

  volatile char padding0[PREFETCH_SIZE_BYTES];
  nodeptr root;
  volatile char padding1[PREFETCH_SIZE_BYTES];

#ifdef USE_DEBUGCOUNTERS
  debugCounters* const counters;
#endif

  inline nodeptr newNode(const int tid, K key, V value);
  long long debugKeySum(nodeptr root);

  bool validate(const int tid, nodeptr prev, int tag, nodeptr curr,
                int direction);

  void dfsDeallocateBottomUp(nodeptr const u, int* numNodes) {
    if (u == NULL) return;
    if (u->child[0]) dfsDeallocateBottomUp(u->child[0], numNodes);
    if (u->child[1]) dfsDeallocateBottomUp(u->child[1], numNodes);
    MEMORY_STATS++(*numNodes);
    recordmgr->deallocate(0 /* tid */, u);
    // delete u;
  }

  const V doInsert(const int tid, const K& key, const V& value,
                   bool onlyIfAbsent);
  int init[MAX_TID_POW2] = {
      0,
  };

 public:
  const K NO_KEY;
  const V NO_VALUE;
  bundle_citrustree(const K max_key, const V NO_VALUE, int numProcesses);
  ~bundle_citrustree();

  const V insert(const int tid, const K& key, const V& value);
  const V insertIfAbsent(const int tid, const K& key, const V& value);
  const pair<V, bool> erase(const int tid, const K& key);
  const pair<V, bool> find(const int tid, const K& key);
  int rangeQuery(const int tid, const K& lo, const K& hi, K* const resultKeys,
                 V* const resultValues);
  void cleanup(int tid);
  void startCleanup() { rqProvider->startCleanup(); }
  void stopCleanup() { rqProvider->stopCleanup(); }
  bool contains(const int tid, const K& key);
  int size();  // warning: this is a linear time operation, and is not
               // linearizable

  node_t<K, V>* debug_getEntryPoint() { return root; }

  /**
   * BEGIN FUNCTIONS FOR RANGE QUERY SUPPORT
   */

  inline bool isLogicallyDeleted(const int tid, nodeptr node) { return false; }

  inline bool isLogicallyInserted(const int tid, nodeptr node) { return true; }

  inline int getKeys(const int tid, node_t<K, V>* node, K* const outputKeys,
                     V* const outputValues) {
    if (node->key >= NO_KEY) return 0;
    outputKeys[0] = node->key;
    outputValues[0] = node->value;
    return 1;
  }

  bool isInRange(const K& key, const K& lo, const K& hi) {
    return (key != NO_KEY && lo <= key && key <= hi);
  }

  /**
   * END FUNCTIONS FOR RANGE QUERY SUPPORT
   */
#ifdef USE_DEBUGCOUNTERS
  debugCounters* debugGetCounters() { return counters; }
#endif
  long long debugKeySum();
  void clearCounters() {
#ifdef USE_DEBUGCOUNTERS
    counters->clear();
#endif
    // recmgr->clearCounters();
  }

  bool validate(int unused, bool unused2) { return true; }

  RecManager* const debugGetRecMgr() { return recordmgr; }

  long long getSizeInNodes(nodeptr const u) {
    if (u == NULL) return 0;
    return 1 + getSizeInNodes(u->child[0]) + getSizeInNodes(u->child[1]);
  }
  long long getSizeInNodes() { return getSizeInNodes(root); }
  string getSizeString() {
    stringstream ss;
    ss << getSizeInNodes() << " nodes in data structure";
    return ss.str();
  }
  long long getSize() { return getSizeInNodes(); }

  bool validateBundles(int tid);

  string getBundleStatsString() {
    unsigned int max = 0;
    nodeptr max_node = nullptr;
    int max_direction = 0;
    long left_total = 0;
    long right_total = 0;
    stack<nodeptr> s;
    unordered_set<node_t<K, V>*> unique;
    nodeptr curr = root;
    s.push((nodeptr)curr->child[0]);  // Two sentinals are at the root.
    while (!s.empty()) {
      // Try to add the current node to set of unique nodes.
      curr = s.top();
      s.pop();
      auto result = unique.insert(curr);
      if (result.second && curr != nullptr) {
        // If the node is unseen, then get its bundles' entries and add them to
        // the stack, then update bundle statistics.

        int left;
        int right;
        std::pair<nodeptr, timestamp_t>* left_entries =
            curr->rqbundle[0].get(left);
        std::pair<nodeptr, timestamp_t>* right_entries =
            curr->rqbundle[1].get(right);
#ifdef NO_FREE
        for (int i = 0; i < left; ++i) {
          if (left_entries[i].first != nullptr) {
            assert(left_entries[i].first->validate());
            s.push(left_entries[i].first);
          }
        }
        for (int i = 0; i < right; ++i) {
          if (right_entries[i].first != nullptr) {
            assert(right_entries[i].first->validate());
            s.push(right_entries[i].first);
          }
        }
#else
        if (left > 0 && left_entries[0].first != nullptr) {
          assert(left_entries[0].first->validate());
          s.push(left_entries[0].first);
        }
        if (right > 0 && right_entries[0].first != nullptr) {
          assert(right_entries[0].first->validate());
          s.push(right_entries[0].first);
        }
#endif

        if (left >= right && left > max) {
          max = left;
          max_node = curr;
          max_direction = 0;
        } else if (right > max) {
          max = right;
          max_node = curr;
          max_direction = 1;
        }
        left_total += left;
        right_total += right;

        delete[] left_entries;
        delete[] right_entries;
      }
    }

    stringstream ss;
    ss << "total reachable nodes         : " << unique.size() << endl;
    ss << "average bundle size           : "
       << ((left_total + right_total) / (double)unique.size()) << endl;
    ss << "average left bundle size      : "
       << (left_total / (double)unique.size()) << endl;
    ss << "average right bundle size     : "
       << (right_total / (double)unique.size()) << endl;
    ss << "max bundle size               : " << max << endl;
    ss << max_node->rqbundle[max_direction].dump(0);
    return ss.str();
  }

  /**
   * This function must be called once by each thread that will
   * invoke any functions on this class.
   *
   * It must be okay that we do this with the main thread and later with another
   * thread!!!
   */
  void initThread(const int tid) {
    if (init[tid])
      return;
    else
      init[tid] = !init[tid];

    recordmgr->initThread(tid);
    rqProvider->initThread(tid);
  }

  void deinitThread(const int tid) {
    if (!init[tid])
      return;
    else
      init[tid] = !init[tid];

    rqProvider->deinitThread(tid);
    recordmgr->deinitThread(tid);
  }
};

#endif
