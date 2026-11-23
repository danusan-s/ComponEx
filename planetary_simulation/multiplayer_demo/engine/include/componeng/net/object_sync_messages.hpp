#pragma once

// Wire format of object sync. Public because it is the protocol, and for tests
// that send messages as a misbehaving peer would.

#include "componeng/net/session_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace componeng::net {

using ObjectId = uint32_t;

inline uint32_t syncSetId(const std::string &name) {
  uint32_t hash = 2166136261u;
  for (char c : name) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  return hash;
}

// All carry the set id: sets share the messages and ignore others'.
namespace sync_msg {

struct Add {
  static constexpr const char *kName = "componeng.sync.add";
  uint32_t setId = 0;
  ObjectId id = 0;
  std::vector<uint8_t> state;
  template <typename Ar> void serialize(Ar &ar) {
    ar(setId, id, state);
  }
};

struct Remove {
  static constexpr const char *kName = "componeng.sync.remove";
  uint32_t setId = 0;
  ObjectId id = 0;
  uint32_t epoch = 0;
  template <typename Ar> void serialize(Ar &ar) {
    ar(setId, id, epoch);
  }
};

// Peer to leader: may I have it?
struct Claim {
  static constexpr const char *kName = "componeng.sync.claim";
  uint32_t setId = 0;
  ObjectId id = 0;
  template <typename Ar> void serialize(Ar &ar) {
    ar(setId, id);
  }
};

// Leader to everyone: the object now has this owner (kNoPeer: nobody).
struct Owner {
  static constexpr const char *kName = "componeng.sync.owner";
  uint32_t setId = 0;
  ObjectId id = 0;
  PeerId owner = kNoPeer;
  uint32_t epoch = 0;
  template <typename Ar> void serialize(Ar &ar) {
    ar(setId, id, owner, epoch);
  }
};

// Leader to the asker: no, it is held by `owner`.
struct Deny {
  static constexpr const char *kName = "componeng.sync.deny";
  uint32_t setId = 0;
  ObjectId id = 0;
  PeerId owner = kNoPeer;
  uint32_t epoch = 0;
  template <typename Ar> void serialize(Ar &ar) {
    ar(setId, id, owner, epoch);
  }
};

// Owner to everyone: I am done with it, and this is how I left it. The epoch is
// the one the object has from now on, which is how it ends up free.
struct Release {
  static constexpr const char *kName = "componeng.sync.release";
  uint32_t setId = 0;
  ObjectId id = 0;
  uint32_t epoch = 0;
  std::vector<uint8_t> state;
  template <typename Ar> void serialize(Ar &ar) {
    ar(setId, id, epoch, state);
  }
};

// Owner to everyone, unreliably, as often as it likes.
struct Update {
  static constexpr const char *kName = "componeng.sync.update";
  uint32_t setId = 0;
  ObjectId id = 0;
  uint32_t epoch = 0;
  uint32_t seq = 0;
  std::vector<uint8_t> state;
  template <typename Ar> void serialize(Ar &ar) {
    ar(setId, id, epoch, seq, state);
  }
};

struct SnapshotRequest {
  static constexpr const char *kName = "componeng.sync.snapshot_request";
  uint32_t setId = 0;
  template <typename Ar> void serialize(Ar &ar) {
    ar(setId);
  }
};

struct SnapshotEntry {
  ObjectId id = 0;
  PeerId owner = kNoPeer;
  uint32_t epoch = 0;
  std::vector<uint8_t> state;
  template <typename Ar> void serialize(Ar &ar) {
    ar(id, owner, epoch, state);
  }
};

// The leader's whole copy of the set, in as many pages as it takes.
struct Snapshot {
  static constexpr const char *kName = "componeng.sync.snapshot";
  uint32_t setId = 0;
  bool last = false;
  std::vector<SnapshotEntry> entries;
  template <typename Ar> void serialize(Ar &ar) {
    ar(setId, last, entries);
  }
};

} // namespace sync_msg

} // namespace componeng::net
