#include "componeng/net/object_sync.hpp"

#include "componeng/net/object_sync_messages.hpp"

#include "componeng/utils/logger.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace componeng::net {

namespace {

// Keeps one snapshot page small.
constexpr size_t kSnapshotPageBytes = 8000;
// Messages held while awaiting the snapshot; beyond this the oldest go (the
// snapshot covers them).
constexpr size_t kMaxBuffered = 8192;
// Layout of an id from newId(): the peer in the top bits, a counter below.
constexpr uint32_t kCounterBits = 20;

} // namespace

class ObjectSyncCore::Impl {
public:
  Impl(Session &session, const std::string &name)
      : m_session(session), m_setId(syncSetId(name)) {
  }

  void attach(const std::weak_ptr<Impl> &weak) {
    using namespace sync_msg;
    constexpr auto kReliable = Channel::Reliable;
    bool ok = m_session.registerMessage<Add>(kReliable) &&
              m_session.registerMessage<Remove>(kReliable) &&
              m_session.registerMessage<Claim>(kReliable) &&
              m_session.registerMessage<Owner>(kReliable) &&
              m_session.registerMessage<Deny>(kReliable) &&
              m_session.registerMessage<Release>(kReliable) &&
              m_session.registerMessage<Update>(Channel::Unreliable) &&
              m_session.registerMessage<SnapshotRequest>(kReliable) &&
              m_session.registerMessage<Snapshot>(kReliable);
    if (!ok) {
      LOG_ERROR("Could not register the object sync messages");
    }
    hook<Add>(weak);
    hook<Remove>(weak);
    hook<Claim>(weak);
    hook<Owner>(weak);
    hook<Deny>(weak);
    hook<Release>(weak);
    hook<Update>(weak);
    hook<SnapshotRequest>(weak);
    hook<Snapshot>(weak);

    m_session.onJoined([weak] {
      if (auto self = weak.lock()) {
        self->onJoined();
      }
    });
    m_session.onPeerLeft([weak](PeerId peer, DisconnectReason) {
      if (auto self = weak.lock()) {
        self->onPeerLeft(peer);
      }
    });
    m_session.onLeaderChanged([weak](PeerId) {
      if (auto self = weak.lock()) {
        self->onLeaderChanged();
      }
    });

    // A set made inside a running session needs the leader's copy; otherwise
    // there is nothing to wait for.
    m_synced = !(m_session.isActive() && !m_session.isLeader());
    if (!m_synced) {
      requestSnapshot();
    }
  }

  // ---- Public operations ----

  uint32_t setId() const {
    return m_setId;
  }

  ObjectId newId() {
    PeerId self = m_session.self();
    uint32_t counter = m_nextLocal++ & ((1u << kCounterBits) - 1);
    return (static_cast<uint32_t>(self & 0xFFF) << kCounterBits) | counter;
  }

  bool add(ObjectId id, Bytes state) {
    if (m_objects.count(id) != 0) {
      return false;
    }
    sync_msg::Add message{m_setId, id, state};
    if (m_session.isActive() && !fitsReliably(message)) {
      return false;
    }
    Object object;
    object.state = std::move(state);
    m_objects.emplace(id, std::move(object));
    if (m_session.isActive()) {
      m_session.broadcast(message);
    }
    return true;
  }

  bool remove(ObjectId id) {
    auto it = m_objects.find(id);
    if (it == m_objects.end() ||
        (it->second.owner != kNoPeer && !mine(it->second))) {
      return false;
    }
    uint32_t epoch = it->second.epoch;
    m_objects.erase(it);
    m_pendingClaims.erase(id);
    if (m_session.isActive()) {
      m_session.broadcast(sync_msg::Remove{m_setId, id, epoch});
    }
    return true;
  }

  bool claim(ObjectId id) {
    if (!m_session.isActive() || m_objects.count(id) == 0) {
      return false;
    }
    if (mine(m_objects[id])) {
      return true;
    }
    if (m_session.isLeader()) {
      decideClaim(m_session.self(), id);
      flushNotices();
      return true;
    }
    m_pendingClaims.insert(id);
    if (!m_session.sendToLeader(sync_msg::Claim{m_setId, id})) {
      m_pendingClaims.erase(id);
      return false;
    }
    return true;
  }

  bool release(ObjectId id, Bytes finalState) {
    auto it = m_objects.find(id);
    if (!m_session.isActive() || it == m_objects.end() || !mine(it->second)) {
      return false;
    }
    Object &object = it->second;
    sync_msg::Release message{m_setId, id, object.epoch + 1, finalState};
    if (!fitsReliably(message)) {
      return false;
    }
    object.state = std::move(finalState);
    object.owner = kNoPeer;
    object.epoch = message.epoch;
    object.seq = 0;
    ++object.version;
    m_session.broadcast(message);
    return true;
  }

  bool release(ObjectId id) {
    auto it = m_objects.find(id);
    if (it == m_objects.end()) {
      return false;
    }
    return release(id, it->second.state);
  }

  bool update(ObjectId id, Bytes state) {
    auto it = m_objects.find(id);
    if (!m_session.isActive() || it == m_objects.end() || !mine(it->second)) {
      return false;
    }
    Object &object = it->second;
    sync_msg::Update message{m_setId, id, object.epoch, object.seq + 1, state};
    // Two bytes of message id come before the fields.
    if (toBytes(message).size() + 2 > kMaxUnreliableMessageSize) {
      return false;
    }
    object.state = std::move(state);
    object.seq = message.seq;
    ++object.version;
    m_session.broadcast(message);
    return true;
  }

  bool contains(ObjectId id) const {
    return m_objects.count(id) != 0;
  }
  PeerId ownerOf(ObjectId id) const {
    auto it = m_objects.find(id);
    return it == m_objects.end() ? kNoPeer : it->second.owner;
  }
  bool ownedByMe(ObjectId id) const {
    auto it = m_objects.find(id);
    return it != m_objects.end() && mine(it->second);
  }
  const Bytes *stateOf(ObjectId id) const {
    auto it = m_objects.find(id);
    return it == m_objects.end() ? nullptr : &it->second.state;
  }
  uint64_t versionOf(ObjectId id) const {
    auto it = m_objects.find(id);
    return it == m_objects.end() ? 0 : it->second.version;
  }
  std::vector<ObjectId> ids() const {
    std::vector<ObjectId> result;
    for (const auto &entry : m_objects) {
      result.push_back(entry.first);
    }
    return result;
  }
  size_t size() const {
    return m_objects.size();
  }
  bool synced() const {
    // Joining is the start of waiting, even before this set has noticed.
    return m_synced && m_session.state() != Session::State::Joining;
  }

  std::vector<std::function<void(ObjectId, const Bytes &)>> onAddedHandlers;
  std::vector<std::function<void(ObjectId)>> onRemovedHandlers;
  std::vector<std::function<void(ObjectId, const Bytes &)>> onChangedHandlers;
  std::vector<std::function<void(ObjectId, PeerId)>> onOwnerChangedHandlers;
  std::vector<std::function<void(ObjectId, PeerId)>> onClaimDeniedHandlers;
  std::vector<std::function<void()>> onSyncedHandlers;

  // Callbacks are queued, not run mid-change: the code changing the set holds
  // references into it, and a callback may delete the very object it is told
  // about. Notices carry a copy of the state and run once the operation has
  // finished; a callback that changes the set queues more, run in the same
  // loop.
  struct Notice {
    enum class Kind { Added, Removed, Changed, OwnerChanged, Denied, Synced };
    Kind kind = Kind::Synced;
    ObjectId id = 0;
    PeerId peer = kNoPeer;
    Bytes state;
  };

  void notify(Notice::Kind kind, ObjectId id, PeerId peer = kNoPeer,
              Bytes state = Bytes()) {
    m_notices.push_back(Notice{kind, id, peer, std::move(state)});
  }

  void flushNotices() {
    if (m_flushing) {
      return;
    }
    m_flushing = true;
    while (!m_notices.empty()) {
      std::vector<Notice> batch;
      batch.swap(m_notices);
      for (const Notice &notice : batch) {
        dispatch(notice);
      }
    }
    m_flushing = false;
  }

private:
  struct Object {
    Bytes state;
    PeerId owner = kNoPeer;
    // Rises on every change of owner; anything older than what a peer has seen
    // is stale.
    uint32_t epoch = 0;
    // Orders the updates within one epoch.
    uint32_t seq = 0;
    uint64_t version = 1;
  };

  bool mine(const Object &object) const {
    return object.owner != kNoPeer && object.owner == m_session.self();
  }

  template <typename Message> bool fitsReliably(const Message &message) const {
    return toBytes(message).size() + 2 <= m_session.maxReliableMessageSize();
  }

  // Routes one message type to this set's handler, if the message is for it.
  template <typename Message> void hook(const std::weak_ptr<Impl> &weak) {
    m_session.onMessage<Message>(std::function<void(PeerId, const Message &)>(
        [weak](PeerId from, const Message &message) {
          if (auto self = weak.lock()) {
            if (message.setId == self->m_setId) {
              self->receive(from, message);
            }
          }
        }));
  }

  // While the snapshot is pending, changes wait: applied to a partial copy they
  // would be lost, and the snapshot may be older.
  template <typename Message>
  void receive(PeerId from, const Message &message) {
    refreshSyncState();
    if (m_synced) {
      apply(from, message);
      flushNotices();
      return;
    }
    m_buffer.push_back([this, from, message] { apply(from, message); });
    if (m_buffer.size() > kMaxBuffered) {
      m_buffer.pop_front();
    }
  }
  // These do not touch the set's contents, so they never wait.
  void receive(PeerId from, const sync_msg::Claim &message) {
    applyClaim(from, message);
    flushNotices();
  }
  void receive(PeerId from, const sync_msg::SnapshotRequest &) {
    sendSnapshot(from);
  }
  void receive(PeerId from, const sync_msg::Snapshot &message) {
    applySnapshot(from, message);
    flushNotices();
  }

  // A set outside any session has nothing to wait for; mid-join it has
  // everything to wait for.
  void refreshSyncState() {
    Session::State state = m_session.state();
    if (state == Session::State::Idle && !m_synced) {
      m_synced = true;
      m_buffer.clear();
    } else if (state == Session::State::Joining && m_synced) {
      m_synced = false;
    }
  }

  // ---- Incoming changes ----

  void apply(PeerId, const sync_msg::Add &m) {
    if (m_objects.count(m.id) != 0) {
      return;
    }
    Object object;
    object.state = m.state;
    auto it = m_objects.emplace(m.id, std::move(object)).first;
    notify(Notice::Kind::Added, m.id, kNoPeer, it->second.state);
  }

  void apply(PeerId, const sync_msg::Remove &m) {
    auto it = m_objects.find(m.id);
    if (it == m_objects.end() || m.epoch < it->second.epoch) {
      return;
    }
    m_objects.erase(it);
    m_pendingClaims.erase(m.id);
    notify(Notice::Kind::Removed, m.id);
  }

  void apply(PeerId, const sync_msg::Owner &m) {
    auto it = m_objects.find(m.id);
    if (it == m_objects.end()) {
      return;
    }
    adoptOwner(m.id, it->second, m.owner, m.epoch);
  }

  void apply(PeerId, const sync_msg::Deny &m) {
    auto it = m_objects.find(m.id);
    if (it == m_objects.end()) {
      return;
    }
    adoptOwner(m.id, it->second, m.owner, m.epoch);
    m_pendingClaims.erase(m.id);
    notify(Notice::Kind::Denied, m.id, m.owner);
  }

  void apply(PeerId, const sync_msg::Release &m) {
    auto it = m_objects.find(m.id);
    if (it == m_objects.end() || m.epoch <= it->second.epoch) {
      return;
    }
    Object &object = it->second;
    object.state = m.state;
    ++object.version;
    notify(Notice::Kind::Changed, m.id, kNoPeer, object.state);
    adoptOwner(m.id, object, kNoPeer, m.epoch);
  }

  void apply(PeerId from, const sync_msg::Update &m) {
    auto it = m_objects.find(m.id);
    if (it == m_objects.end()) {
      return;
    }
    Object &object = it->second;
    // An update from an unseen epoch is itself news that `from` owns the object
    // (the leader's announcement is still on its way).
    if (m.epoch > object.epoch) {
      adoptOwner(m.id, object, from, m.epoch);
    }
    if (m.epoch != object.epoch || object.owner != from ||
        m.seq <= object.seq) {
      return;
    }
    object.state = m.state;
    object.seq = m.seq;
    ++object.version;
    notify(Notice::Kind::Changed, m.id, kNoPeer, object.state);
  }

  // Takes news of a new owner if it is newer than what is known.
  void adoptOwner(ObjectId id, Object &object, PeerId owner, uint32_t epoch) {
    if (epoch <= object.epoch) {
      return;
    }
    PeerId previous = object.owner;
    object.owner = owner;
    object.epoch = epoch;
    object.seq = 0;
    // Whatever the outcome, this peer's request has been decided.
    m_pendingClaims.erase(id);
    if (owner != previous) {
      notify(Notice::Kind::OwnerChanged, id, owner);
    }
  }

  // ---- Arbitration (the leader's job) ----

  void applyClaim(PeerId from, const sync_msg::Claim &m) {
    // A claim sent to a former leader; the claimant resends to the new one.
    if (m_session.isLeader()) {
      decideClaim(from, m.id);
    }
  }

  void decideClaim(PeerId requester, ObjectId id) {
    auto it = m_objects.find(id);
    if (it == m_objects.end()) {
      return;
    }
    Object &object = it->second;
    PeerId self = m_session.self();

    if (object.owner == kNoPeer) {
      PeerId owner = requester;
      uint32_t epoch = object.epoch + 1;
      m_session.broadcast(sync_msg::Owner{m_setId, id, owner, epoch});
      adoptOwner(id, object, owner, epoch);
    } else if (object.owner == requester) {
      // Asked again: say again.
      if (requester != self) {
        m_session.send(requester, sync_msg::Owner{m_setId, id, object.owner,
                                                  object.epoch});
      }
    } else if (requester == self) {
      notify(Notice::Kind::Denied, id, object.owner);
    } else {
      m_session.send(requester,
                     sync_msg::Deny{m_setId, id, object.owner, object.epoch});
    }
  }

  // Frees what `peer` owned, or all that is owned by someone no longer here.
  void freeOrphans(PeerId onlyPeer) {
    for (auto &entry : m_objects) {
      Object &object = entry.second;
      if (object.owner == kNoPeer || mine(object)) {
        continue;
      }
      bool orphaned = onlyPeer != kNoPeer ? object.owner == onlyPeer
                                          : !m_session.isPeer(object.owner);
      if (!orphaned) {
        continue;
      }
      uint32_t epoch = object.epoch + 1;
      m_session.broadcast(
          sync_msg::Owner{m_setId, entry.first, kNoPeer, epoch});
      adoptOwner(entry.first, object, kNoPeer, epoch);
    }
  }

  // ---- Snapshots ----

  void requestSnapshot() {
    m_session.sendToLeader(sync_msg::SnapshotRequest{m_setId});
  }

  void sendSnapshot(PeerId to) {
    if (!m_session.isLeader()) {
      return;
    }
    sync_msg::Snapshot page;
    page.setId = m_setId;
    size_t bytes = 0;
    auto flush = [&](bool last) {
      page.last = last;
      m_session.send(to, page);
      page.entries.clear();
      bytes = 0;
    };
    for (const auto &entry : m_objects) {
      const Object &object = entry.second;
      page.entries.push_back(
          {entry.first, object.owner, object.epoch, object.state});
      bytes += object.state.size() + 16;
      if (bytes >= kSnapshotPageBytes) {
        flush(false);
      }
    }
    flush(true); // possibly empty, but it is what ends the transfer
  }

  void applySnapshot(PeerId from, const sync_msg::Snapshot &m) {
    // Only the leader's word counts, and only when asked for it.
    if (from != m_session.leader() || m_session.isLeader()) {
      return;
    }
    for (const auto &entry : m.entries) {
      auto it = m_objects.find(entry.id);
      if (it == m_objects.end()) {
        Object object;
        object.state = entry.state;
        object.owner = entry.owner;
        object.epoch = entry.epoch;
        auto added = m_objects.emplace(entry.id, std::move(object)).first;
        notify(Notice::Kind::Added, entry.id, kNoPeer, added->second.state);
        if (entry.owner != kNoPeer) {
          notify(Notice::Kind::OwnerChanged, entry.id, entry.owner);
        }
      } else if (entry.epoch > it->second.epoch) {
        // Newer than ours; at the same epoch our copy is at least as fresh (it
        // may hold updates the snapshot predates).
        Object &object = it->second;
        object.state = entry.state;
        ++object.version;
        notify(Notice::Kind::Changed, entry.id, kNoPeer, object.state);
        adoptOwner(entry.id, object, entry.owner, entry.epoch);
      }
    }
    if (m.last && !m_synced) {
      finishSync();
    }
  }

  // The snapshot is in: now let what happened meanwhile take effect on top.
  void finishSync() {
    m_synced = true;
    std::deque<std::function<void()>> held;
    held.swap(m_buffer);
    for (auto &replay : held) {
      replay();
    }
    notify(Notice::Kind::Synced, 0);
  }

  // ---- Session events ----

  void onJoined() {
    // This peer takes the session's contents, not what it had on its own.
    std::vector<ObjectId> old = ids();
    m_objects.clear();
    m_pendingClaims.clear();
    for (ObjectId id : old) {
      notify(Notice::Kind::Removed, id);
    }
    m_synced = false;
    requestSnapshot();
    flushNotices();
  }

  void onPeerLeft(PeerId peer) {
    if (m_session.isLeader()) {
      freeOrphans(peer);
      flushNotices();
    }
  }

  void onLeaderChanged() {
    if (m_session.isLeader()) {
      // Taking over: what this peer holds is the session's copy now.
      if (!m_synced) {
        finishSync();
      }
      freeOrphans(kNoPeer);
      flushNotices();
      return;
    }
    // The new leader never saw requests that went to the old one.
    if (!m_synced) {
      requestSnapshot();
    }
    for (ObjectId id : m_pendingClaims) {
      m_session.sendToLeader(sync_msg::Claim{m_setId, id});
    }
  }

  void dispatch(const Notice &n) {
    using Kind = Notice::Kind;
    // By index: a callback may add another callback.
    switch (n.kind) {
      case Kind::Added:
        for (size_t i = 0; i < onAddedHandlers.size(); ++i) {
          onAddedHandlers[i](n.id, n.state);
        }
        break;
      case Kind::Removed:
        for (size_t i = 0; i < onRemovedHandlers.size(); ++i) {
          onRemovedHandlers[i](n.id);
        }
        break;
      case Kind::Changed:
        for (size_t i = 0; i < onChangedHandlers.size(); ++i) {
          onChangedHandlers[i](n.id, n.state);
        }
        break;
      case Kind::OwnerChanged:
        for (size_t i = 0; i < onOwnerChangedHandlers.size(); ++i) {
          onOwnerChangedHandlers[i](n.id, n.peer);
        }
        break;
      case Kind::Denied:
        for (size_t i = 0; i < onClaimDeniedHandlers.size(); ++i) {
          onClaimDeniedHandlers[i](n.id, n.peer);
        }
        break;
      case Kind::Synced:
        for (size_t i = 0; i < onSyncedHandlers.size(); ++i) {
          onSyncedHandlers[i]();
        }
        break;
    }
  }

  Session &m_session;
  uint32_t m_setId;
  std::vector<Notice> m_notices;
  bool m_flushing = false;
  std::map<ObjectId, Object> m_objects;
  std::set<ObjectId> m_pendingClaims;
  bool m_synced = true;
  uint32_t m_nextLocal = 1;
  std::deque<std::function<void()>> m_buffer;
};

ObjectSyncCore::ObjectSyncCore(Session &session, const std::string &name)
    : m_impl(std::make_shared<Impl>(session, name)) {
  m_impl->attach(m_impl);
}

ObjectSyncCore::~ObjectSyncCore() = default;
ObjectSyncCore::ObjectSyncCore(ObjectSyncCore &&) noexcept = default;
ObjectSyncCore &ObjectSyncCore::operator=(ObjectSyncCore &&) noexcept = default;

uint32_t ObjectSyncCore::setId() const {
  return m_impl->setId();
}
ObjectId ObjectSyncCore::newId() {
  return m_impl->newId();
}
bool ObjectSyncCore::add(ObjectId id, Bytes state) {
  return m_impl->add(id, std::move(state));
}
bool ObjectSyncCore::remove(ObjectId id) {
  return m_impl->remove(id);
}
bool ObjectSyncCore::claim(ObjectId id) {
  return m_impl->claim(id);
}
bool ObjectSyncCore::release(ObjectId id, Bytes finalState) {
  return m_impl->release(id, std::move(finalState));
}
bool ObjectSyncCore::release(ObjectId id) {
  return m_impl->release(id);
}
bool ObjectSyncCore::update(ObjectId id, Bytes state) {
  return m_impl->update(id, std::move(state));
}
bool ObjectSyncCore::contains(ObjectId id) const {
  return m_impl->contains(id);
}
PeerId ObjectSyncCore::ownerOf(ObjectId id) const {
  return m_impl->ownerOf(id);
}
bool ObjectSyncCore::ownedByMe(ObjectId id) const {
  return m_impl->ownedByMe(id);
}
const ObjectSyncCore::Bytes *ObjectSyncCore::stateOf(ObjectId id) const {
  return m_impl->stateOf(id);
}
uint64_t ObjectSyncCore::versionOf(ObjectId id) const {
  return m_impl->versionOf(id);
}
std::vector<ObjectId> ObjectSyncCore::ids() const {
  return m_impl->ids();
}
size_t ObjectSyncCore::size() const {
  return m_impl->size();
}
bool ObjectSyncCore::synced() const {
  return m_impl->synced();
}

void ObjectSyncCore::onAdded(
    std::function<void(ObjectId, const Bytes &)> handler) {
  m_impl->onAddedHandlers.push_back(std::move(handler));
}
void ObjectSyncCore::onRemoved(std::function<void(ObjectId)> handler) {
  m_impl->onRemovedHandlers.push_back(std::move(handler));
}
void ObjectSyncCore::onChanged(
    std::function<void(ObjectId, const Bytes &)> handler) {
  m_impl->onChangedHandlers.push_back(std::move(handler));
}
void ObjectSyncCore::onOwnerChanged(
    std::function<void(ObjectId, PeerId)> handler) {
  m_impl->onOwnerChangedHandlers.push_back(std::move(handler));
}
void ObjectSyncCore::onClaimDenied(
    std::function<void(ObjectId, PeerId)> handler) {
  m_impl->onClaimDeniedHandlers.push_back(std::move(handler));
}
void ObjectSyncCore::onSynced(std::function<void()> handler) {
  m_impl->onSyncedHandlers.push_back(std::move(handler));
}

} // namespace componeng::net
