/**
 * Preliminary C++ implementation of binary search tree using LLX/SCX.
 *
 * Copyright (C) 2014 Trevor Brown
 *
 */

#ifndef NODE_H
#define NODE_H

#include <atomic>
#include <iomanip>
#include <iostream>
#include <set>

#include "linked_bundle.h"
#include "rq_provider.h"
#include "scxrecord.h"
#ifdef USE_RECLAIMER_RCU
#include <urcu.h>
#define RECLAIM_RCU_RCUHEAD_DEFN struct rcu_head rcuHeadField
#else
#define RECLAIM_RCU_RCUHEAD_DEFN
#endif
using namespace std;

namespace bundle_bst_ns {

#define nodeptr Node<K, V>* volatile

template <class K, class V>
class Node {
 public:
  V value;
  K key;
  atomic_uintptr_t scxRecord;
  atomic_bool
      marked;  // might be able to combine this elegantly with scx record
               // pointer... (maybe we can piggyback on the version number
               // mechanism, using the same bit to indicate ver# OR marked)
  nodeptr left;
  nodeptr right;

  volatile long long itime;  // for use by range query algorithm
  volatile long long dtime;  // for use by range query algorithm
  RECLAIM_RCU_RCUHEAD_DEFN;

  BUNDLE_TYPE_DECL<Node<K, V>> left_bundle;
  BUNDLE_TYPE_DECL<Node<K, V>> right_bundle;

  void validate() {
    timestamp_t ts;
    assert(left == left_bundle.first(ts));
    assert(right == right_bundle.first(ts));
  }

  friend ostream& operator<<(ostream& os, const Node<K, V>& obj) {}
  void printTreeFile(ostream& os) {}
  void printTreeFileWeight(ostream& os, set<Node<K, V>*>* seen) {}
  void printTreeFileWeight(ostream& os) {}
};
}  // namespace bundle_bst_ns

#endif /* NODE_H */
