#pragma once

#include "componeng/net/archive.hpp"
#include "componeng/net/object_sync_messages.hpp"
#include "componeng/net/session.hpp"
#include "componeng/net/session_types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace componeng::net {

// A set of objects every peer of a session holds a copy of. Each object is
// free or owned by exactly one peer, and only the owner may change it, so a
// piece dragged by one player cannot be dragged by another.
//
// Moves untyped bytes (SharedObjects<T> is the typed form):
//  - claim() asks the session leader, who grants the first asker and denies
//    the rest: onOwnerChanged (all peers) / onClaimDenied (the loser).
//  - The owner update()s freely (unreliable); release() sends the final state
//    reliably and frees the object. A leaving owner's objects are freed.
//  - A joining peer is sent the whole set by the leader.
// Each object has an epoch that rises on every change of owner; older messages
// are dropped, which keeps peers consistent under reordering and duplication.
//
// Callbacks report other peers' changes and ownership decisions, not this
// peer's own actions. `name` identifies the set on the wire (unique per set,
// same on every peer). Ownership needs an active session. Peers are trusted.
class ObjectSyncCore {
public:
  using Bytes = std::vector<uint8_t>;

  ObjectSyncCore(Session &session, const std::string &name);
  ~ObjectSyncCore();
  ObjectSyncCore(ObjectSyncCore &&) noexcept;
  ObjectSyncCore &operator=(ObjectSyncCore &&) noexcept;
  ObjectSyncCore(const ObjectSyncCore &) = delete;
  ObjectSyncCore &operator=(const ObjectSyncCore &) = delete;

  uint32_t setId() const;

  // Includes this peer's id, so no two peers pick the same.
  ObjectId newId();

  // Creates it everywhere, unowned. False if the id is taken or the state is
  // too big. Ids must be unique across peers: use newId() or agree on a scheme.
  bool add(ObjectId id, Bytes state);
  // Free objects, or ones this peer owns.
  bool remove(ObjectId id);

  // True if the request was sent (decided at once on the leader); the outcome
  // comes as a callback. False if there is no such object or no active session.
  bool claim(ObjectId id);
  // Sets a final state, or keeps the current one. False unless this peer owns
  // it.
  bool release(ObjectId id, Bytes finalState);
  bool release(ObjectId id);
  // Unreliable, so it must fit one packet. False unless this peer owns it or
  // the state is too big.
  bool update(ObjectId id, Bytes state);

  bool contains(ObjectId id) const;
  PeerId ownerOf(ObjectId id) const;
  bool ownedByMe(ObjectId id) const;
  const Bytes *stateOf(ObjectId id) const;
  uint64_t versionOf(ObjectId id) const;
  std::vector<ObjectId> ids() const;
  size_t size() const;
  // False from joining until the leader's copy has arrived; contents may be
  // incomplete until then.
  bool synced() const;

  void onAdded(std::function<void(ObjectId, const Bytes &)> handler);
  void onRemoved(std::function<void(ObjectId)> handler);
  void onChanged(std::function<void(ObjectId, const Bytes &)> handler);
  void onOwnerChanged(std::function<void(ObjectId, PeerId)> handler);
  void onClaimDenied(std::function<void(ObjectId, PeerId)> handler);
  void onSynced(std::function<void()> handler);

private:
  class Impl;
  std::shared_ptr<Impl> m_impl;
};

// ObjectSyncCore with values of your own type T (needs serialize() and a
// default constructor):
//   SharedObjects<Piece> pieces(session, "jigsaw.pieces");
//   pieces.claim(id); pieces.update(id, p) while owned; pieces.release(id,
//   last); pieces.get(id)
// A state that does not decode (a peer on another version) reads as absent.
template <typename T> class SharedObjects {
public:
  SharedObjects(Session &session, const std::string &name)
      : m_core(session, name), m_cache(std::make_shared<Cache>()) {
    std::shared_ptr<Cache> cache = m_cache;
    m_core.onRemoved([cache](ObjectId id) { cache->erase(id); });
  }

  ObjectId newId() {
    return m_core.newId();
  }
  bool add(ObjectId id, const T &value) {
    return m_core.add(id, toBytes(value));
  }
  bool remove(ObjectId id) {
    bool removed = m_core.remove(id);
    if (removed) {
      m_cache->erase(id);
    }
    return removed;
  }
  bool claim(ObjectId id) {
    return m_core.claim(id);
  }
  bool release(ObjectId id) {
    return m_core.release(id);
  }
  bool release(ObjectId id, const T &finalValue) {
    return m_core.release(id, toBytes(finalValue));
  }
  bool update(ObjectId id, const T &value) {
    return m_core.update(id, toBytes(value));
  }

  // Null if there is no such object or its state does not decode; valid until
  // that object next changes.
  const T *get(ObjectId id) const {
    const ObjectSyncCore::Bytes *bytes = m_core.stateOf(id);
    if (!bytes) {
      m_cache->erase(id);
      return nullptr;
    }
    uint64_t version = m_core.versionOf(id);
    auto it = m_cache->find(id);
    if (it == m_cache->end() || it->second.version != version) {
      Entry entry;
      entry.version = version;
      entry.valid = fromBytes(*bytes, entry.value);
      it = m_cache->insert_or_assign(id, std::move(entry)).first;
    }
    return it->second.valid ? &it->second.value : nullptr;
  }

  bool contains(ObjectId id) const {
    return m_core.contains(id);
  }
  PeerId ownerOf(ObjectId id) const {
    return m_core.ownerOf(id);
  }
  bool ownedByMe(ObjectId id) const {
    return m_core.ownedByMe(id);
  }
  std::vector<ObjectId> ids() const {
    return m_core.ids();
  }
  size_t size() const {
    return m_core.size();
  }
  bool synced() const {
    return m_core.synced();
  }

  void onAdded(std::function<void(ObjectId, const T &)> handler) {
    m_core.onAdded(decoding(std::move(handler)));
  }
  void onChanged(std::function<void(ObjectId, const T &)> handler) {
    m_core.onChanged(decoding(std::move(handler)));
  }
  void onRemoved(std::function<void(ObjectId)> handler) {
    m_core.onRemoved(std::move(handler));
  }
  void onOwnerChanged(std::function<void(ObjectId, PeerId)> handler) {
    m_core.onOwnerChanged(std::move(handler));
  }
  void onClaimDenied(std::function<void(ObjectId, PeerId)> handler) {
    m_core.onClaimDenied(std::move(handler));
  }
  void onSynced(std::function<void()> handler) {
    m_core.onSynced(std::move(handler));
  }

  ObjectSyncCore &core() {
    return m_core;
  }

private:
  struct Entry {
    uint64_t version = 0;
    bool valid = false;
    T value{};
  };
  using Cache = std::unordered_map<ObjectId, Entry>;

  static std::function<void(ObjectId, const ObjectSyncCore::Bytes &)>
  decoding(std::function<void(ObjectId, const T &)> handler) {
    return [handler](ObjectId id, const ObjectSyncCore::Bytes &bytes) {
      T value{};
      if (fromBytes(bytes, value)) {
        handler(id, value);
      }
    };
  }

  ObjectSyncCore m_core;
  // shared_ptr so the callback that prunes it survives a move.
  mutable std::shared_ptr<Cache> m_cache;
};

} // namespace componeng::net
