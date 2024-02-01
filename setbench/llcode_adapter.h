
#ifndef LLCODE_ADAPTER_H
#define LLCODE_ADAPTER_H

#include "random_fnv1a.h"
#include "adapter.h"

#include <verlib/verlib.h>
#include <parlay/primitives.h>
#include <optional>

thread_local int _tid = -1;
std::atomic<int> num_initialized_threads(0);

template <typename K, typename V, class alloc>
class ordered_map {
private:
  using adapter_t = ds_adapter<K,V,reclaimer_debra<K>,alloc,pool_none<K>>;
  inline static const K KEY_NEG_INFTY = std::numeric_limits<K>::min()+1;
  inline static const K KEY_POS_INFTY = std::numeric_limits<K>::max()-1;
  adapter_t* my_map;

public:
  
  static void reserve(size_t n) {
    adapter_t::reserve(n);
  }

  static void shuffle(size_t n) {
    adapter_t::shuffle(n);
  }

  ordered_map(size_t n) {
    my_map = new adapter_t(parlay::num_workers(), KEY_NEG_INFTY, KEY_POS_INFTY, 0, nullptr);
  }

  std::optional<V> find(const K& key) {
    init_thread(my_map);
    V val = my_map->find(_tid, key);
    if (val == (V) my_map->getNoValue()) return {};
    else return val;
  } 

  std::optional<V> find_(const K& key) {
    return find(key);
  }

  bool insert(const K& key, const V& val) {
    init_thread(my_map);
    assert(key != 0);
    V r = my_map->insertIfAbsent(_tid, key, val); 
    return (r == (V) my_map->getNoValue());
  }

  V remove(const K key) {
    init_thread(my_map);
    V r = my_map->erase(_tid, key);
    return (r != (V) my_map->getNoValue());
  }

  void print() {  }

  ~ordered_map() {
    init_thread(my_map);
    for(int i = 0; i < num_initialized_threads; i++)
      my_map->deinitThread(i); // all data structures have an additional check to avoid deinitializing twice
    // num_initialized_threads = 0;
    delete my_map;
  }

  static void clear() {
    // TODO: you may want to clear memory pools here
  }

  size_t check() {
    init_thread(my_map);
    auto tree_stats = my_map->createTreeStats(KEY_NEG_INFTY, KEY_POS_INFTY);
    // std::cout << "average height: " << tree_stats->getAverageKeyDepth() << std::endl;
    size_t size = tree_stats->getKeys();
    delete tree_stats;
    return size;
  }

  static void stats() {
    adapter_t::stats();
  }

private:
  static inline void init_thread(adapter_t* ds) {
    if(_tid == -1) {
      _tid = num_initialized_threads.fetch_add(1);
      // std::cout << "thread id: " << _tid << std::endl;
    }
    ds->initThread(_tid);  // all data structures have an additional check to avoid initializing twice
  }
};

#endif // LLCODE_ADAPTER_H
