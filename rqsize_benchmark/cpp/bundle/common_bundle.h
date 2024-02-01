// Jacob Nelson
//
// This file defines the standard interface for a bundle. It is used by the
// bundle range query provider to ensure linearizable range queries.

#ifndef BUNDLE_COMMON_BUNDLE_H
#define BUNDLE_COMMON_BUNDLE_H

typedef long long timestamp_t;
#define BUNDLE_PENDING_TIMESTAMP LLONG_MAX
#define BUNDLE_NULL_TIMESTAMP 0LL
#define BUNDLE_MIN_TIMESTAMP 1LL
#define BUNDLE_MAX_TIMESTAMP (LLONG_MAX - 1)

template <typename NodeType>
class BundleEntryBase {
 public:
  NodeType *ptr_ = nullptr;                 // Reference at timestamp, ts.
  timestamp_t ts_ = BUNDLE_NULL_TIMESTAMP;  // Timestamp of change.
  bool marked_ = true;                      // True if not valid or is deleted.
};

template <typename NodeType>
class BundleInterface {
 public:
  virtual void init() = 0;

  // Prepares the next bundle entry by setting the timestamp to a pending state.
  virtual void prepare(NodeType *ptr) = 0;

  // Finalizes the bundle entry previously prepared by setting the timestamp to
  // ts, which is the linearization timestamp of the update.
  virtual void finalize(timestamp_t ts) = 0;

  // Returns a reference to the node which was valid at timestamp, ts.
  virtual NodeType *getPtrByTimestamp(timestamp_t ts) = 0;

  // Removes all entries that are older than the first entry that is at least as
  // old as the provided timestamp.
  virtual void reclaimEntries(timestamp_t ts) = 0;

  // Returns the number of valid bundle entries in the bundle.
  virtual int size() = 0;

  // Returns the node's most recent reference and timestamp.
  virtual NodeType *first(timestamp_t &ts) = 0;

  // Returns all valid (unmarked) bundle entries as an array of pairs of node
  // references and the associated timestamp. Function parameter length is
  // update to reflect the number of entries returned.
  virtual std::pair<NodeType *, timestamp_t> *get(int &length) = 0;

  // Prints information about the bundle.
  virtual string dump(timestamp_t ts) = 0;

  virtual ~BundleInterface() {}
};

#endif  // BUNDLE_COMMON_BUNDLE_H
