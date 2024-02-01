#ifndef BUNDLE_CIRCULAR_BUNDLE_H
#define BUNDLE_CIRCULAR_BUNDLE_H

#include <pthread.h>
#include <sys/types.h>
#include <atomic>
#include <mutex>

#include "plaf.h"
#include "rq_debugging.h"

#define CPU_RELAX asm volatile("pause\n" ::: "memory")
#define BUNDLE_INIT_CAPACITY 5

// Valid bundle states:
// ----------------------
// NORMAL_STATE -- No operations being performed on bundle.
// PENDING_STATE -- An update is pending.
// RECLAIM_STATE -- Bundle entries are being reclaimed.
// RQ_STATE -- A range query is reading the bundle.
// PENDING_STATE | RESIZE_STATE -- A pending update is resizing the bundle.
// PENDING_STATE | RECLAIM_STATE -- An update and cleanup are concurrent.
// PENDING_STATE | RQ_STATE -- An update and range query are concurrent.
// PENDING_STATE | RECLAIM_STATE | RQ_STATE -- An update, cleanup and rangequery
// are concurrent. RECLAIM_STATE | RQ_STATE -- Cleanup and a range query are
// concurrent.

#define NORMAL_STATE 0
#define PENDING_STATE 1
#define RESIZE_STATE 2
#define RECLAIM_STATE 4
#define RQ_STATE (~7)

#define DEBUG_PRINT(str)                         \
  if ((i + 1) % 10000 == 0) {                    \
    std::cout << str << std::endl << std::flush; \
  }                                              \
  ++i;

enum op { NOP, INSERT, REMOVE };
// Stores pointers to nodes and associated timestamps.

template <typename NodeType>
class BundleEntry : public BundleEntryBase<NodeType> {};

template <typename NodeType>
class Bundle : public Bundle<NodeType> {
 private:
  // The lower three bits are reserved for marking an ongoing update, resize or
  // reclaimer. The remaining bits are devoted to keeping track of the number of
  // concurrent range queries.
  std::atomic<unsigned int> state_;
  int volatile base_;      // Index of oldest entry.
  int volatile curr_;      // Index of newest entry.
  int volatile capacity_;  // Maximum number of entries.
  // Entry array and metadata.
  BundleEntry<NodeType> *volatile entries_;  // Bundle entry array

#ifdef BUNDLE_DEBUG
  int its_;
  int last_base_;
  int updates_;
#endif

  inline void start_rq() {
    unsigned int expected_state, upper, lower, new_state;
    while (true) {
      expected_state = state_ & ~RESIZE_STATE;
      upper = expected_state >> 3;             // Get the upper bits of state.
      lower = expected_state & 7;              // Get lower bits.
      new_state = ((upper + 1) << 3) | lower;  // Increment number of rqs.
      if (state_.compare_exchange_weak(expected_state, new_state)) {
        assert(state_ & RQ_STATE && state_ & ~RESIZE_STATE);
        break;
      }
    }
  }

  inline void end_rq() {
    unsigned int expected_state, upper, lower, new_state;
    while (true) {
      expected_state = state_ & ~RESIZE_STATE;
      assert(state_ & RQ_STATE);
      upper = expected_state >> 3;             // Get the upper bits of state.
      lower = expected_state & 7;              // Get lower bits.
      new_state = ((upper - 1) << 3) | lower;  // Increment number of rqs.
      if (state_.compare_exchange_weak(expected_state, new_state)) {
        break;
      }
    }
  }

  // Doubles the capacity of the bundle. Only called when a new entry is added,
  // meaning no updates will be lost although some cleanup may be missed.
  void grow() {
    unsigned int expected;
    while (true) {
      while (state_ & (RECLAIM_STATE | RQ_STATE))
        ;  // Wait for a concurrent reclaimer or rq.
      // Move state from pending to pending and resizing to block reclaimers and
      // range queries.
      expected = PENDING_STATE;
      if (state_.compare_exchange_weak(expected, expected | RESIZE_STATE)) {
        break;
      }
    }
    assert(state_ & (PENDING_STATE | RESIZE_STATE));

    BundleEntry<NodeType> *new_entries =
        new BundleEntry<NodeType>[capacity_ * 2];
    int i = base_;
    int end = curr_;
    if (i > end) {
      // Buffer wraps so take into account.
      end = capacity_ + end;
    }
    for (; i <= end; ++i) {
      // Copy entries to new array.
      new_entries[i] = entries_[i % capacity_];
      if (i >= capacity_) {
        new_entries[i % capacity_] = entries_[i % capacity_];
      }
    }

    // Finish the resize.
    capacity_ = capacity_ * 2;
    curr_ = end;
    SOFTWARE_BARRIER;
    BundleEntry<NodeType> *old_entries = entries_;  // Swap entry arrays.
    entries_ = new_entries;
    delete[] old_entries;

    state_.fetch_and(~RESIZE_STATE);
  }

  void shrink() {
    // TODO: If the capacity is much larger than the number of valid entries, we
    // may want to reduce the size.
  }

 public:
  ~Bundle() { delete[] entries_; }

  void init() {
    state_ = NORMAL_STATE;
    base_ = 0;
    curr_ = -1;
    capacity_ = BUNDLE_INIT_CAPACITY;
    entries_ = new BundleEntry<NodeType>[BUNDLE_INIT_CAPACITY];
  }

  // Returns a reference to the node that satisfies the snapshot at the given
  // timestamp. If all
  inline NodeType *getPtrByTimestamp(timestamp_t ts) override {
    // NOTE: Range queries may continuously block updaters from resizing, but
    // this is expected to be common.
    start_rq();

    int i = curr_;
    int end = base_;  // NOTE: We don't have to worry about the base changing
                      // because we will always find our entry before the base,
                      // or the base will not be changed.

    // Locate first entry that satisfies snapshot, starting from newest.
    NodeType *ptr = nullptr;
    if (i != -1) {
      // If an entry exists in this bundle.
      if (end > i) {
        // Buffer wraps so take into account.
        i = capacity_ + i;
      }
      for (; i >= end; --i) {
        if (entries_[i % capacity_].ts_ <= ts) {
          ptr = entries_[i % capacity_].ptr_;
          break;
        }
      }
    }

    end_rq();
    return ptr;
  }

  inline void reclaimEntries(timestamp_t ts) {
    unsigned int expected;
    while (true) {
      while (state_ & (RESIZE_STATE | RECLAIM_STATE))
        ;  // Wait until resize is done.
      expected = (state_ & ~RESIZE_STATE & ~RECLAIM_STATE);
      // Move state to reclaim only if it is not resizing.
      if (state_.compare_exchange_weak(expected, expected | RECLAIM_STATE)) {
        break;
      }
    }
    assert(state_ & (~RESIZE_STATE));
    assert(state_ & RECLAIM_STATE);

    int i = curr_;
    int end = base_;
    if (i == end || i == -1) {
      state_.fetch_and(~RECLAIM_STATE);
      return;
    }

    // Handle wrapping.
    if (end > i) {
      i = capacity_ + i;
    }

    if (ts == BUNDLE_NULL_TIMESTAMP) {
      // No active range queries.
      base_ = curr_;
      for (i = i - 1; i >= end; --i) {
        entries_[i % capacity_].marked_ = true;
      }
    } else {
      // Locate reclaimable entries.
      bool set = false;
      for (; i >= end; --i) {
        if (entries_[i % capacity_].ts_ != BUNDLE_NULL_TIMESTAMP &&
            entries_[i % capacity_].ts_ <= ts) {
          // Found first entry to retire at index i - 1, so set base to i.
          if (!set) {
#ifdef BUNDLE_DEBUG
            last_base_ = base_;
#endif
            base_ = i % capacity_;
            assert(base_ >= 0);
            assert(entries_[base_].marked_ == false);
            set = true;
          } else {
            entries_[i % capacity_].marked_ = true;
          }
        }
      }
    }

    state_.fetch_and(~RECLAIM_STATE);
  }

  inline void prepare(NodeType *const ptr) {
    unsigned int expected;
    while (true) {
      while (state_ & PENDING_STATE)
        ;  // Wait for ongoing op to finish.
      expected = state_ & ~PENDING_STATE;
      if (state_.compare_exchange_weak(expected, expected | PENDING_STATE)) {
        break;
      }
    }
    assert(state_ & PENDING_STATE);

    if (curr_ != -1 && (curr_ + 1) % capacity_ == base_) {
      // Resize if we have run out of room.
      grow();
    }
    SOFTWARE_BARRIER;
    entries_[(curr_ + 1) % capacity_].ptr_ = ptr;
  }

  inline void finalize(timestamp_t ts) {
    // Assumes the bundles are in the pending state.
    assert(state_ & PENDING_STATE && !(state_ & (RESIZE_STATE)));
    assert(curr_ == -1 || (curr_ + 1) % capacity_ != base_);
#ifdef BUNDLE_DEBUG
    if (curr_ == -1) {
      its_ = ts;
      assert(its_ != 0);
    }
    ++updates_;
#endif
    entries_[(curr_ + 1) % capacity_].ts_ = ts;
    entries_[(curr_ + 1) % capacity_].marked_ = false;
    SOFTWARE_BARRIER;
    curr_ = (curr_ + 1) % capacity_;
    state_.fetch_and(~PENDING_STATE);
  }

  // [UNSAFE]. Returns the number of entries in the bundle.
  inline int size() override {
    if (curr_ == -1) {
      return 0;
    } else if (base_ > curr_) {
      return (capacity_ - base_) + curr_;
    } else {
      return curr_ - base_;
    }
  }

  NodeType *first(timestamp_t &ts) override {
    // If the bundle has no entries, return nullptr.
    if (curr_ == -1) {
      ts = -1;
      return nullptr;
    }
    ts = entries_[curr_].ts_;
    return entries_[curr_].ptr_;
  }

  // [UNSAFE]
  std::pair<NodeType *, timestamp_t> *get(int &length) override {
    std::pair<NodeType *, timestamp_t> *retarr =
        new std::pair<NodeType *, timestamp_t>[capacity_];
    if (curr_ == -1) {
      length = 0;
      return retarr;
    }
    NodeType *ptr;
    timestamp_t ts;
    int pos = 0;
    for (int i = base_; i <= (base_ > curr_ ? capacity_ + curr_ : curr_);
         ++i, ++pos) {
      ptr = entries_[i % capacity_].ptr_;
      ts = entries_[i % capacity_].ts_;
      retarr[pos] = std::pair<NodeType *, timestamp_t>(ptr, ts);
    }
    length = pos;
    return retarr;
  }

  string __attribute__((noinline)) dump(timestamp_t ts) override {
    std::stringstream ss;
    ss << "(ts=" << ts << ") : ";
    long i = 0;
    for (int i = 0; i < capacity_; ++i) {
      ss << "<" << entries_[i].ts_ << "," << entries_[i].ptr_ << ","
         << entries_[i].marked_ << ">"
         << ", ";
    }
    ss << " [curr_ = " << curr_ << ", base_ = " << base_
#ifdef BUNDLE_DEBUG
       << ", updates_ = " << updates_ << ", last_base_ = " << last_base_
#endif
       << "]" << std::endl;
    return ss.str();
  }
};

#endif  // BUNDLE_CIRCULAR_BUNDLE_H