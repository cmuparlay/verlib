#ifndef SKIPLIST_H
#define SKIPLIST_H

#include <stack>
#include <type_traits>
#include <unordered_set>

#ifndef MAX_NODES_INSERTED_OR_DELETED_ATOMICALLY
// define BEFORE including rq_provider.h
#define MAX_NODES_INSERTED_OR_DELETED_ATOMICALLY 4
#endif
#include "plaf.h"
#include "random.h"
#include "rq_bundle.h"

using namespace std;

/////////////////////////////////////////////////////////
// DEFINES
/////////////////////////////////////////////////////////
#ifdef SKIPLIST_DEBUGGING_FLATTEN_MAX_LEVEL
#define SKIPLIST_MAX_LEVEL (1)
#else
#define SKIPLIST_MAX_LEVEL (20)
#endif

/////////////////////////////////////////////////////////
// TYPES
/////////////////////////////////////////////////////////
template <typename K, typename V>
class node_t {
 public:
  BUNDLE_TYPE_DECL<node_t<K, V>> rqbundle;
  struct {
   public:
    volatile long lock;
    volatile K key;
    volatile V val;
    volatile int topLevel;
    volatile long long
        marked;  // stored as long long simply so it is large enough to be used
                 // with the lock-free RQProvider (which requires all fields
                 // that are modified at linearization points of operations to
                 // occupy a machine word)
    volatile long long
        fullyLinked;  // stored as long long simply so it is large enough to be
                      // used with the lock-free RQProvider (which requires all
                      // fields that are modified at linearization points of
                      // operations to occupy a machine word)

    node_t<K, V>* volatile p_next[SKIPLIST_MAX_LEVEL];
  };

  bool validate() {
    timestamp_t ts;
    return p_next[0] == rqbundle.first(ts);
  }

  template <typename RQProvider>
  bool isMarked(const int tid, RQProvider* const prov) {
    return (prov->read_addr(tid, &marked) == 1);
  }
};

#define nodeptr node_t<K, V>*

template <typename K, typename V, class RecManager>
class bundle_skiplist {
 private:
  volatile char padding0[PREFETCH_SIZE_BYTES];
  nodeptr volatile p_head;
  // volatile char padding1[PREFETCH_SIZE_BYTES];
  nodeptr volatile p_tail;
  volatile char padding2[PREFETCH_SIZE_BYTES];

  const int NUM_PROCESSES;
  RecManager* const recmgr;
  Random* const
      threadRNGs;  // threadRNGs[tid * PREFETCH_SIZE_WORDS] = rng for thread tid
  RQProvider<K, V, node_t<K, V>, bundle_skiplist<K, V, RecManager>, RecManager,
             true, false>* rqProvider;
#ifdef USE_DEBUGCOUNTERS
  debugCounters* const counters;
#endif

  nodeptr allocateNode(const int tid);

  void initNode(const int tid, nodeptr p_node, K key, V value, int height);
  int find_impl(const int tid, K key, nodeptr* p_preds, nodeptr* p_succs,
                nodeptr* p_found);
  V doInsert(const int tid, const K& key, const V& value, bool onlyIfAbsent);

  int init[MAX_TID_POW2] = {
      0,
  };

 public:
  const K KEY_MIN;
  const K KEY_MAX;
  const V NO_VALUE;
  volatile char padding3[PREFETCH_SIZE_BYTES];

  bundle_skiplist(const int numProcesses, const K _KEY_MIN, const K _KEY_MAX,
                  const V NO_VALUE, Random* const threadRNGs);
  ~bundle_skiplist();

  bool contains(const int tid, K key);
  const pair<V, bool> find(const int tid, const K& key);
  V insert(const int tid, const K& key, const V& value) {
    return doInsert(tid, key, value, false);
  }
  V insertIfAbsent(const int tid, const K& key, const V& value) {
    return doInsert(tid, key, value, true);
  }
  V erase(const int tid, const K& key);
  int rangeQuery(const int tid, const K& lo, const K& hi, K* const resultKeys,
                 V* const resultValues);

  void cleanup(int tid);

  void initThread(const int tid);
  void deinitThread(const int tid);
#ifdef USE_DEBUGCOUNTERS
  debugCounters* debugGetCounters() { return counters; }
  void clearCounters() { counters->clear(); }
#endif
  long long getSizeInNodes() {
    long long size = 0;
    for (nodeptr curr = p_head->p_next[0]; curr->key != KEY_MAX;
         curr = curr->p_next[0]) {
      ++size;
    }
    return size;
  }
  // warning: this can only be used when there are no other threads accessing
  // the data structure
  long long getSize() {
    long long size = 0;
    for (nodeptr curr = p_head->p_next[0]; curr->key != KEY_MAX;
         curr = curr->p_next[0]) {
      size += (!curr->marked);
    }
    return size;
  }
  string getSizeString() {
    stringstream ss;
    ss << getSizeInNodes() << " nodes in data structure";
    return ss.str();
  }

  RecManager* const debugGetRecMgr() { return recmgr; }

  inline int getKeys(const int tid, node_t<K, V>* node, K* const outputKeys,
                     V* const outputValues) {
    outputKeys[0] = node->key;
    outputValues[0] = node->val;
    return 1;
  }

  bool isInRange(const K& key, const K& lo, const K& hi) {
    return (lo <= key && key <= hi);
  }
  inline bool isLogicallyDeleted(const int tid, node_t<K, V>* node) {
    return (rqProvider->read_addr(tid, &node->marked));
  }

  inline bool isLogicallyInserted(const int tid, node_t<K, V>* node) {
    return (rqProvider->read_addr(tid, &node->fullyLinked));
  }

  bool validate(const long long keysum, const bool checkkeysum) { return true; }

  node_t<K, V>* debug_getEntryPoint() { return p_head; }

 private:
  // warning: this can only be used when there are no other threads accessing
  // the data structure
  long long debugKeySum(nodeptr head) {
    long long result = 0;
    // traverse lowest level
    nodeptr curr = (nodeptr)head->p_next[0];
    while (curr->key < KEY_MAX) {
      if (!curr->marked) {
        result += curr->key;
      }
      curr = curr->p_next[0];
    }
    return result;
  }

 public:
  long long debugKeySum() { return debugKeySum(p_head); }

  void startCleanup() { rqProvider->startCleanup(); }

  void stopCleanup() { rqProvider->stopCleanup(); }

  bool validateBundles(int tid);

  string getBundleStatsString() {
    unsigned int max = 0;
    nodeptr max_node = nullptr;
    long total = 0;
    stack<nodeptr> s;
    unordered_set<node_t<K, V>*> unique;
    nodeptr curr = p_head;
    nodeptr prev;
    s.push(curr);
    while (!s.empty()) {
      // Try to add the current node to set of unique nodes.
      curr = s.top();
      s.pop();
      auto result = unique.insert(curr);
      if (result.second && curr->key != KEY_MAX) {
        int size;
        std::pair<nodeptr, timestamp_t>* entries = curr->rqbundle.get(size);
        // #ifdef NO_FREE
        //         for (int i = 0; i < size; ++i) {
        //           s.push(entries[i].first);
        //         }
        // #else
        s.push(entries[0].first);
        // #endif

        if (size > max) {
          max = size;
          max_node = curr;
        }
        total += size;
        delete[] entries;
      }
      prev = curr;
    }

    stringstream ss;
    ss << "total reachable nodes         : " << unique.size() << endl;
    ss << "average bundle size           : " << (total / (double)unique.size())
       << endl;
    ss << "max bundle size               : " << max << endl;
    // ss << "max is marked                 : " << max_node->marked << endl;
    // ss << max_node->rqbundle.dump(0) << endl;
    return ss.str();
  }
};

#endif  // SKIPLIST_H