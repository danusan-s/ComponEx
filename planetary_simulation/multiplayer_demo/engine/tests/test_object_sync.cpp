#include "componeng/net/impaired_socket.hpp"
#include "componeng/net/loopback_socket.hpp"
#include "componeng/net/network.hpp"
#include "componeng/net/object_sync.hpp"
#include "componeng/net/object_sync_messages.hpp"
#include "componeng/net/session.hpp"
#include "net_test_config.hpp"
#include "gtest/gtest.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace componeng::net;

namespace {

struct Piece {
  float x = 0;
  float y = 0;
  uint8_t rotation = 0;
  bool placed = false;
  template <typename Ar> void serialize(Ar &ar) {
    ar(x, y, rotation, placed);
  }
  bool operator==(const Piece &o) const {
    return x == o.x && y == o.y && rotation == o.rotation && placed == o.placed;
  }
};

struct Blob {
  std::vector<uint8_t> bytes;
  template <typename Ar> void serialize(Ar &ar) {
    ar(bytes);
  }
};

// A loopback socket whose outgoing datagrams can be made lossy, late and
// duplicated; it owns what it wraps, as Network wants to own its socket.
class BadSocket : public IDatagramSocket {
public:
  BadSocket(std::unique_ptr<LoopbackSocket> inner, const Impairment &impairment)
      : m_inner(std::move(inner)), m_impaired(*m_inner, impairment) {
  }
  void update(double now) {
    m_impaired.update(now);
  }
  IoStatus sendTo(const Address &to, const void *data, size_t size) override {
    return m_impaired.sendTo(to, data, size);
  }
  IoStatus recvFrom(void *buffer, size_t capacity, size_t &received,
                    Address &from) override {
    return m_impaired.recvFrom(buffer, capacity, received, from);
  }
  bool waitReadable(int timeoutMs) override {
    return m_impaired.waitReadable(timeoutMs);
  }
  Address localAddress() const override {
    return m_impaired.localAddress();
  }
  bool isOpen() const override {
    return m_impaired.isOpen();
  }
  void close() override {
    m_impaired.close();
  }
  const std::string &lastError() const override {
    return m_impaired.lastError();
  }

private:
  std::unique_ptr<LoopbackSocket> m_inner;
  ImpairedSocket m_impaired;
};

// One peer: network, session and a set of pieces, with a log of what the set
// reported.
struct Member {
  Network net;
  Session session;
  SharedObjects<Piece> pieces;
  BadSocket *socket = nullptr; // owned by net
  std::vector<ObjectId> added, removed, changed;
  std::vector<std::pair<ObjectId, PeerId>> ownerChanges, denied;
  int synced = 0;
  bool paused = false;

  Member(LoopbackNetwork &loopback, const Impairment &impairment)
      : session(net), pieces(session, "test.pieces") {
    auto bad = std::make_unique<BadSocket>(loopback.open(), impairment);
    socket = bad.get();
    EXPECT_TRUE(net.open(std::move(bad), testConfig()));
    pieces.onAdded([this](ObjectId id, const Piece &) { added.push_back(id); });
    pieces.onRemoved([this](ObjectId id) { removed.push_back(id); });
    pieces.onChanged(
        [this](ObjectId id, const Piece &) { changed.push_back(id); });
    pieces.onOwnerChanged([this](ObjectId id, PeerId owner) {
      ownerChanges.emplace_back(id, owner);
    });
    pieces.onClaimDenied(
        [this](ObjectId id, PeerId owner) { denied.emplace_back(id, owner); });
    pieces.onSynced([this] { ++synced; });
  }

  Address address() const {
    return Address::loopback(net.localAddress().port());
  }
  std::map<ObjectId, std::pair<PeerId, Piece>> view() const {
    std::map<ObjectId, std::pair<PeerId, Piece>> result;
    for (ObjectId id : pieces.ids()) {
      const Piece *p = pieces.get(id);
      result[id] = {pieces.ownerOf(id), p ? *p : Piece()};
    }
    return result;
  }
  bool lastOwnerChangeIs(ObjectId id, PeerId owner) const {
    for (auto it = ownerChanges.rbegin(); it != ownerChanges.rend(); ++it) {
      if (it->first == id) {
        return it->second == owner;
      }
    }
    return false;
  }
};

struct Rig {
  LoopbackNetwork loopback;
  std::vector<std::unique_ptr<Member>> members;
  double now = 0.0;

  Member &add(const Impairment &impairment = Impairment()) {
    members.push_back(std::make_unique<Member>(loopback, impairment));
    return *members.back();
  }
  SessionConfig config() {
    SessionConfig c;
    c.advertiseOnLan = false;
    return c;
  }
  void step(double dt = 0.01) {
    now += dt;
    for (auto &m : members) {
      if (m->paused) {
        continue;
      }
      m->socket->update(now);
      m->net.update(now);
      m->session.update(now);
    }
  }
  void run(double seconds) {
    for (double end = now + seconds; now < end;) {
      step();
    }
  }
  bool runUntil(const std::function<bool()> &done, double seconds = 20.0) {
    for (double end = now + seconds; now < end;) {
      if (done()) {
        return true;
      }
      step();
    }
    return done();
  }
  void join(Member &joiner, Member &leader) {
    ASSERT_TRUE(joiner.session.join(leader.address(), config()));
    ASSERT_TRUE(runUntil([&] { return joiner.session.isActive(); }));
    ASSERT_TRUE(runUntil([&] { return joiner.pieces.synced(); }));
  }
  bool allMeshed() {
    size_t active = 0;
    for (auto &m : members) {
      active += m->session.isActive();
    }
    for (auto &m : members) {
      if (m->session.isActive() && m->session.peerCount() != active) {
        return false;
      }
    }
    return true;
  }
  bool consistent() {
    const Member *first = nullptr;
    for (auto &m : members) {
      if (!m->session.isActive()) {
        continue;
      }
      if (!first) {
        first = m.get();
      } else if (m->view() != first->view()) {
        return false;
      }
    }
    return true;
  }
};

Impairment badNetwork() {
  Impairment i;
  i.loss = 0.2;
  i.duplicate = 0.1;
  i.latency = 0.01;
  i.jitter = 0.04;
  return i;
}

} // namespace

// ---- Creating and sharing ----

TEST(ObjectSync, ObjectsAddedByAPeerAppearEverywhere) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);

  ObjectId id = a.pieces.newId();
  ASSERT_TRUE(a.pieces.add(id, Piece{1, 2, 3, false}));
  EXPECT_FALSE(a.pieces.add(id, Piece{})); // taken
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.contains(id); }));
  ASSERT_NE(b.pieces.get(id), nullptr);
  EXPECT_EQ(*b.pieces.get(id), (Piece{1, 2, 3, false}));
  EXPECT_EQ(b.pieces.ownerOf(id), kNoPeer);
  EXPECT_EQ(b.added, std::vector<ObjectId>{id});
  EXPECT_TRUE(a.added.empty()); // one's own changes are not reported back
}

TEST(ObjectSync, IdsFromDifferentPeersNeverClash) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  std::set<ObjectId> seen;
  for (int i = 0; i < 100; ++i) {
    EXPECT_TRUE(seen.insert(a.pieces.newId()).second);
    EXPECT_TRUE(seen.insert(b.pieces.newId()).second);
  }
}

TEST(ObjectSync, ObjectsCanBeRemoved) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.contains(id); }));
  EXPECT_TRUE(a.pieces.remove(id));
  EXPECT_FALSE(a.pieces.contains(id));
  EXPECT_FALSE(a.pieces.remove(id));
  ASSERT_TRUE(rig.runUntil([&] { return !b.pieces.contains(id); }));
  EXPECT_EQ(b.removed, std::vector<ObjectId>{id});
  EXPECT_EQ(b.pieces.get(id), nullptr);
}

TEST(ObjectSync, AnObjectOwnedByAnotherPeerCannotBeRemoved) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.contains(id); }));
  ASSERT_TRUE(b.pieces.claim(id));
  ASSERT_TRUE(rig.runUntil([&] { return a.pieces.ownerOf(id) == 1; }));
  EXPECT_FALSE(a.pieces.remove(id));
  EXPECT_TRUE(b.pieces.remove(id)); // its owner can
}

// ---- Ownership ----

TEST(ObjectSync, ClaimingMakesTheClaimantTheOwnerEverywhere) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return c.pieces.contains(id); }));

  ASSERT_TRUE(c.pieces.claim(id));
  EXPECT_FALSE(c.pieces.ownedByMe(id)); // not until the leader says so
  ASSERT_TRUE(rig.runUntil([&] { return c.pieces.ownedByMe(id); }));
  ASSERT_TRUE(rig.runUntil(
      [&] { return a.pieces.ownerOf(id) == 2 && b.pieces.ownerOf(id) == 2; }));
  EXPECT_TRUE(c.lastOwnerChangeIs(id, 2));
  EXPECT_TRUE(a.lastOwnerChangeIs(id, 2));
  EXPECT_TRUE(b.lastOwnerChangeIs(id, 2));
  EXPECT_TRUE(c.denied.empty());
}

TEST(ObjectSync, TheLeaderCanClaimToo) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(a.pieces.claim(id));
  EXPECT_TRUE(a.pieces.ownedByMe(id)); // decided on the spot
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.ownerOf(id) == 0; }));
}

TEST(ObjectSync, OnlyOneOfTwoSimultaneousClaimsWins) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil(
      [&] { return b.pieces.contains(id) && c.pieces.contains(id); }));

  ASSERT_TRUE(b.pieces.claim(id));
  ASSERT_TRUE(c.pieces.claim(id));
  rig.run(1.0);

  PeerId owner = a.pieces.ownerOf(id);
  ASSERT_TRUE(owner == 1 || owner == 2);
  EXPECT_EQ(b.pieces.ownerOf(id), owner);
  EXPECT_EQ(c.pieces.ownerOf(id), owner);
  Member &winner = owner == 1 ? b : c;
  Member &loser = owner == 1 ? c : b;
  EXPECT_TRUE(winner.pieces.ownedByMe(id));
  EXPECT_FALSE(loser.pieces.ownedByMe(id));
  EXPECT_TRUE(winner.denied.empty());
  ASSERT_EQ(loser.denied.size(), 1u);
  EXPECT_EQ(loser.denied[0], std::make_pair(id, owner));
}

TEST(ObjectSync, AClaimOnAnOwnedObjectIsDeniedWithTheOwner) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.contains(id); }));
  b.pieces.claim(id);
  ASSERT_TRUE(rig.runUntil([&] { return c.pieces.ownerOf(id) == 1; }));

  c.pieces.claim(id);
  a.pieces.claim(id); // the leader's own claim is refused too
  ASSERT_TRUE(rig.runUntil([&] { return !c.denied.empty(); }));
  EXPECT_EQ(c.denied[0], std::make_pair(id, PeerId{1}));
  EXPECT_EQ(a.denied[0], std::make_pair(id, PeerId{1}));
  EXPECT_EQ(a.pieces.ownerOf(id), 1);
}

TEST(ObjectSync, OwnershipOperationsNeedTheOwnership) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.contains(id); }));

  EXPECT_FALSE(b.pieces.update(id, Piece{9, 9, 0, false})); // free
  EXPECT_FALSE(b.pieces.release(id));
  EXPECT_FALSE(b.pieces.claim(9999)); // no such object
  b.pieces.claim(id);
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.ownedByMe(id); }));
  EXPECT_FALSE(a.pieces.update(id, Piece{})); // someone else's
  EXPECT_FALSE(a.pieces.release(id));
  EXPECT_TRUE(b.pieces.update(id, Piece{1, 1, 0, false}));
}

TEST(ObjectSync, TheOwnersUpdatesReachEveryone) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return c.pieces.contains(id); }));
  b.pieces.claim(id);
  ASSERT_TRUE(rig.runUntil([&] {
    return b.pieces.ownedByMe(id) && a.pieces.ownerOf(id) == 1 &&
           c.pieces.ownerOf(id) == 1;
  }));

  for (int i = 1; i <= 30; ++i) {
    ASSERT_TRUE(b.pieces.update(id, Piece{float(i), float(2 * i), 0, false}));
    rig.step();
  }
  rig.run(0.5);
  EXPECT_EQ(*a.pieces.get(id), (Piece{30, 60, 0, false}));
  EXPECT_EQ(*c.pieces.get(id), (Piece{30, 60, 0, false}));
  EXPECT_FALSE(a.changed.empty());
}

TEST(ObjectSync, UpdatesTooBigForOnePacketAreRefused) {
  Rig rig;
  Member &a = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  SharedObjects<Blob> blobs(a.session, "test.blobs");
  ObjectId id = blobs.newId();
  ASSERT_TRUE(blobs.add(id, Blob{std::vector<uint8_t>(5000)}));
  ASSERT_TRUE(blobs.claim(id));
  EXPECT_FALSE(blobs.update(id, Blob{std::vector<uint8_t>(5000)}));
  EXPECT_TRUE(blobs.update(id, Blob{std::vector<uint8_t>(500)}));
  EXPECT_TRUE(blobs.release(id, Blob{std::vector<uint8_t>(5000)}));
}

TEST(ObjectSync, ReleaseLeavesTheFinalStateAndFreesTheObject) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return c.pieces.contains(id); }));
  b.pieces.claim(id);
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.ownedByMe(id); }));
  b.pieces.update(id, Piece{5, 5, 0, false});

  ASSERT_TRUE(b.pieces.release(id, Piece{7, 8, 2, true}));
  EXPECT_EQ(b.pieces.ownerOf(id), kNoPeer);
  ASSERT_TRUE(rig.runUntil([&] {
    return a.pieces.ownerOf(id) == kNoPeer && c.pieces.ownerOf(id) == kNoPeer;
  }));
  rig.run(0.3);
  EXPECT_EQ(*a.pieces.get(id), (Piece{7, 8, 2, true}));
  EXPECT_EQ(*c.pieces.get(id), (Piece{7, 8, 2, true}));
  EXPECT_TRUE(a.lastOwnerChangeIs(id, kNoPeer));

  c.pieces.claim(id);
  ASSERT_TRUE(rig.runUntil([&] { return c.pieces.ownedByMe(id); }));
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.ownerOf(id) == 2; }));
}

TEST(ObjectSync, ReleaseWithoutAStateKeepsTheLastOne) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.contains(id); }));
  b.pieces.claim(id);
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.ownedByMe(id); }));
  b.pieces.update(id, Piece{3, 4, 0, false});
  ASSERT_TRUE(b.pieces.release(id));
  ASSERT_TRUE(rig.runUntil([&] { return a.pieces.ownerOf(id) == kNoPeer; }));
  rig.run(0.2);
  EXPECT_EQ(*a.pieces.get(id), (Piece{3, 4, 0, false}));
}

TEST(ObjectSync, ObjectsOfAPeerThatLeavesAreFreed) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return c.pieces.contains(id); }));
  c.pieces.claim(id);
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.ownerOf(id) == 2; }));
  c.pieces.update(id, Piece{6, 6, 0, false});
  rig.run(0.2);

  c.session.leave();
  ASSERT_TRUE(rig.runUntil([&] {
    return a.pieces.ownerOf(id) == kNoPeer && b.pieces.ownerOf(id) == kNoPeer;
  }));
  EXPECT_EQ(*b.pieces.get(id), (Piece{6, 6, 0, false}));
  b.pieces.claim(id);
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.ownedByMe(id); }));
}

TEST(ObjectSync, ArbitrationPassesToTheNewLeader) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));
  ObjectId held = a.pieces.newId();
  ObjectId free = a.pieces.newId();
  a.pieces.add(held, Piece{});
  a.pieces.add(free, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return c.pieces.contains(free); }));
  ASSERT_TRUE(a.pieces.claim(held));
  ASSERT_TRUE(rig.runUntil([&] { return c.pieces.ownerOf(held) == 0; }));

  a.session.leave();
  ASSERT_TRUE(rig.runUntil([&] {
    return b.session.isLeader() && b.pieces.ownerOf(held) == kNoPeer &&
           c.pieces.ownerOf(held) == kNoPeer;
  }));
  ASSERT_TRUE(c.pieces.claim(free));
  ASSERT_TRUE(rig.runUntil([&] { return c.pieces.ownedByMe(free); }));
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.ownerOf(free) == 2; }));
  ASSERT_TRUE(b.pieces.claim(held));
  EXPECT_TRUE(b.pieces.ownedByMe(held));
}

// ---- Late join ----

TEST(ObjectSync, ALateJoinerReceivesTheWholeSetWithItsOwnership) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);

  std::vector<ObjectId> ids;
  for (int i = 0; i < 10; ++i) {
    ObjectId id = a.pieces.newId();
    ids.push_back(id);
    a.pieces.add(id, Piece{float(i), float(-i), uint8_t(i), i % 2 == 0});
  }
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.size() == 10; }));
  b.pieces.claim(ids[3]);
  a.pieces.claim(ids[5]);
  ASSERT_TRUE(rig.runUntil([&] {
    return b.pieces.ownedByMe(ids[3]) && b.pieces.ownerOf(ids[5]) == 0;
  }));
  ASSERT_TRUE(b.pieces.update(ids[3], Piece{33, 33, 0, false}));
  rig.run(0.3);

  Member &late = rig.add();
  ASSERT_TRUE(late.session.join(a.address(), rig.config()));
  ASSERT_TRUE(rig.runUntil([&] { return late.pieces.synced(); }));
  EXPECT_EQ(late.synced, 1);
  EXPECT_EQ(late.pieces.size(), 10u);
  EXPECT_EQ(late.pieces.ownerOf(ids[3]), 1);
  EXPECT_EQ(late.pieces.ownerOf(ids[5]), 0);
  EXPECT_EQ(late.pieces.ownerOf(ids[0]), kNoPeer);
  EXPECT_EQ(*late.pieces.get(ids[3]), (Piece{33, 33, 0, false}));
  EXPECT_EQ(*late.pieces.get(ids[7]), (Piece{7, -7, 7, false}));
  EXPECT_TRUE(rig.runUntil([&] { return rig.consistent(); }));

  late.pieces.claim(ids[3]);
  late.pieces.claim(ids[0]);
  ASSERT_TRUE(rig.runUntil([&] { return late.pieces.ownedByMe(ids[0]); }));
  ASSERT_TRUE(rig.runUntil([&] { return !late.denied.empty(); }));
  EXPECT_EQ(late.denied[0].first, ids[3]);
}

TEST(ObjectSync, ABigSetArrivesInPages) {
  Rig rig;
  Member &a = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  const int count = 600;
  for (int i = 0; i < count; ++i) {
    a.pieces.add(a.pieces.newId(), Piece{float(i), 0, 0, false});
  }
  Member &late = rig.add();
  ASSERT_TRUE(late.session.join(a.address(), rig.config()));
  ASSERT_TRUE(rig.runUntil([&] { return late.pieces.synced(); }));
  EXPECT_EQ(late.pieces.size(), size_t(count));
  EXPECT_EQ(late.view(), a.view());
}

TEST(ObjectSync, AnEmptySetStillSyncs) {
  Rig rig;
  Member &a = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  Member &late = rig.add();
  ASSERT_TRUE(late.session.join(a.address(), rig.config()));
  ASSERT_TRUE(rig.runUntil([&] { return late.pieces.synced(); }));
  EXPECT_EQ(late.pieces.size(), 0u);
  EXPECT_EQ(late.synced, 1);
}

TEST(ObjectSync, ChangesDuringTheJoinAreNotLost) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  std::vector<ObjectId> ids;
  for (int i = 0; i < 8; ++i) {
    ObjectId id = a.pieces.newId();
    ids.push_back(id);
    a.pieces.add(id, Piece{});
  }
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.size() == 8; }));

  Member &late = rig.add();
  ASSERT_TRUE(late.session.join(a.address(), rig.config()));
  // While it joins, b keeps claiming, moving and dropping pieces and a keeps
  // adding new ones.
  int tick = 0;
  ASSERT_TRUE(rig.runUntil([&] {
    ++tick;
    ObjectId id = ids[tick % ids.size()];
    if (tick % 7 == 0) {
      b.pieces.claim(id);
    }
    if (b.pieces.ownedByMe(id)) {
      b.pieces.update(id, Piece{float(tick), 0, 0, false});
      if (tick % 5 == 0) {
        b.pieces.release(id, Piece{float(tick), 1, 1, true});
      }
    }
    if (tick % 13 == 0) {
      ObjectId extra = a.pieces.newId();
      ids.push_back(extra);
      a.pieces.add(extra, Piece{float(tick), 0, 0, false});
    }
    return late.pieces.synced() && tick > 300;
  }));
  for (ObjectId id : ids) {
    if (b.pieces.ownedByMe(id)) {
      b.pieces.release(id);
    }
  }
  ASSERT_TRUE(rig.runUntil([&] { return rig.consistent(); }, 10.0));
  EXPECT_EQ(late.pieces.size(), a.pieces.size());
}

// ---- Robustness ----

TEST(ObjectSync, StaysConsistentOnABadNetwork) {
  Rig rig;
  Member &a = rig.add(badNetwork());
  Member &b = rig.add(badNetwork());
  Member &c = rig.add(badNetwork());
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));

  std::vector<ObjectId> ids;
  for (int i = 0; i < 6; ++i) {
    ObjectId id = a.pieces.newId();
    ids.push_back(id);
    a.pieces.add(id, Piece{});
  }
  ASSERT_TRUE(rig.runUntil(
      [&] { return b.pieces.size() == 6 && c.pieces.size() == 6; }));

  Member *players[] = {&b, &c};
  for (int tick = 0; tick < 1500; ++tick) {
    for (int p = 0; p < 2; ++p) {
      Member &m = *players[p];
      ObjectId id = ids[(tick / 3 + p) % ids.size()];
      if (m.pieces.ownedByMe(id)) {
        m.pieces.update(id, Piece{float(tick), float(p), 0, false});
        if (tick % 11 == p) {
          m.pieces.release(id, Piece{float(tick), float(p), 1, true});
        }
      } else if (tick % 4 == p) {
        m.pieces.claim(id);
      }
    }
    rig.step();
  }
  rig.run(1.0);
  for (Member *m : players) {
    for (ObjectId id : ids) {
      if (m->pieces.ownedByMe(id)) {
        m->pieces.release(id);
      }
    }
  }
  ASSERT_TRUE(rig.runUntil([&] { return rig.consistent(); }, 30.0));
  rig.run(1.0);
  for (ObjectId id : ids) {
    EXPECT_EQ(a.pieces.ownerOf(id), kNoPeer) << "object " << id;
  }
  EXPECT_TRUE(rig.consistent());
}

TEST(ObjectSync, TheFinalStateOfARelease) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.contains(id); }));
  ASSERT_TRUE(b.pieces.claim(id));
  ASSERT_TRUE(rig.runUntil([&] { return a.pieces.ownerOf(id) == 1; }));

  b.pieces.update(id, Piece{1, 0, 0, false});
  rig.run(0.2);
  b.pieces.release(id, Piece{9, 9, 9, true});
  rig.run(0.5);
  EXPECT_EQ(*a.pieces.get(id), (Piece{9, 9, 9, true}));
  EXPECT_EQ(a.pieces.ownerOf(id), kNoPeer);
}

// Messages sent by hand, as a slow or confused peer would, to check what the
// rules do with them.
TEST(ObjectSync, StaleAndForeignMessagesAreIgnored) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  const uint32_t set = b.pieces.core().setId();
  auto bytes = [](Piece p) { return toBytes(p); };

  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{});
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.contains(id); }));
  ASSERT_TRUE(b.pieces.claim(id));
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.ownedByMe(id); }));
  b.pieces.update(id, Piece{1, 1, 0, false});
  rig.run(0.2);
  ASSERT_TRUE(b.pieces.release(id, Piece{5, 5, 5, true}));
  ASSERT_TRUE(rig.runUntil([&] { return a.pieces.ownerOf(id) == kNoPeer; }));
  rig.run(0.2);
  const Piece settled{5, 5, 5, true};
  ASSERT_EQ(*a.pieces.get(id), settled);
  // The object is at epoch 2 now (granted at 1, released into 2).

  const Piece junk{99, 99, 99, false};
  // An update from the drag that has already ended.
  b.session.send(0, sync_msg::Update{set, id, 1, 999, bytes(junk)});
  // A grant, and a release, that are not newer than what is known.
  b.session.send(0, sync_msg::Owner{set, id, 1, 1});
  b.session.send(0, sync_msg::Owner{set, id, 1, 2});
  b.session.send(0, sync_msg::Release{set, id, 1, bytes(junk)});
  b.session.send(0, sync_msg::Release{set, id, 2, bytes(junk)});
  // The same object created again, and removed by an older epoch.
  b.session.send(0, sync_msg::Add{set, id, bytes(junk)});
  b.session.send(0, sync_msg::Remove{set, id, 1});
  // For a set that a has no such object in.
  b.session.send(0, sync_msg::Update{set + 1, id, 5, 1, bytes(junk)});
  rig.run(0.5);
  EXPECT_TRUE(a.pieces.contains(id));
  EXPECT_EQ(*a.pieces.get(id), settled);
  EXPECT_EQ(a.pieces.ownerOf(id), kNoPeer);

  // Something newer does go through (the control for all of the above).
  b.session.send(0, sync_msg::Release{set, id, 3, bytes(Piece{7, 7, 7, true})});
  ASSERT_TRUE(rig.runUntil([&] {
    return a.pieces.get(id) && *a.pieces.get(id) == (Piece{7, 7, 7, true});
  }));
}

TEST(ObjectSync, OnlyTheOwnersUpdatesCount) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  const uint32_t set = b.pieces.core().setId();
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{1, 1, 1, false});
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.contains(id); }));
  ASSERT_TRUE(a.pieces.claim(id));
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.ownerOf(id) == 0; }));

  // b is not the owner, whatever epoch it writes.
  b.session.send(
      0, sync_msg::Update{set, id, 1, 50, toBytes(Piece{9, 9, 9, true})});
  rig.run(0.5);
  EXPECT_EQ(*a.pieces.get(id), (Piece{1, 1, 1, false}));
  EXPECT_EQ(*b.pieces.get(id), (Piece{1, 1, 1, false}));
}

TEST(ObjectSync, ChangesThatArriveBeforeTheSnapshotLandAfterIt) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  const uint32_t set = b.pieces.core().setId();
  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{1, 1, 1, false});
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.contains(id); }));

  Member &late = rig.add();
  ASSERT_TRUE(late.session.join(a.address(), rig.config()));
  // The moment it is in, the leader goes quiet: its snapshot is held up.
  ASSERT_TRUE(rig.runUntil([&] {
    if (late.session.isActive()) {
      a.paused = true;
      return true;
    }
    return false;
  }));
  ASSERT_FALSE(late.pieces.synced());

  // Meanwhile b, which the joiner is already connected to, changes the object
  // in a way the leader's snapshot (made before this) will not show.
  ASSERT_TRUE(b.session.send(
      late.session.self(),
      sync_msg::Release{set, id, 1, toBytes(Piece{8, 8, 8, true})}));
  rig.run(0.3);
  EXPECT_FALSE(late.pieces.synced());
  EXPECT_FALSE(
      late.pieces.contains(id)); // nothing applied to a half-built copy

  a.paused = false;
  ASSERT_TRUE(rig.runUntil([&] { return late.pieces.synced(); }));
  ASSERT_TRUE(late.pieces.contains(id));
  EXPECT_EQ(*late.pieces.get(id), (Piece{8, 8, 8, true}));
}

// Callbacks run in the middle of the set changing, and a game is free to answer
// one by changing the set (for example by deleting an object that has just
// become free). That must be safe whichever way the callback was triggered.
TEST(ObjectSync, CallbacksMayRemoveObjectsWhileTheSetIsChanging) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  Member &c = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  rig.join(c, a);
  ASSERT_TRUE(rig.runUntil([&] { return rig.allMeshed(); }));

  std::vector<ObjectId> ids;
  for (int i = 0; i < 6; ++i) {
    ids.push_back(a.pieces.newId());
    a.pieces.add(ids.back(), Piece{float(i), 0, 0, false});
  }
  ASSERT_TRUE(rig.runUntil([&] { return c.pieces.size() == 6; }));

  for (Member *m : {&a, &b}) {
    m->pieces.onOwnerChanged([m](ObjectId id, PeerId owner) {
      if (owner == kNoPeer) {
        m->pieces.remove(id);
      }
    });
    m->pieces.onChanged([m](ObjectId id, const Piece &) {
      if (m->pieces.ownedByMe(id)) {
        return;
      }
      m->pieces.remove(id);
    });
  }
  for (int i = 0; i < 4; ++i) {
    c.pieces.claim(ids[i]);
  }
  ASSERT_TRUE(rig.runUntil([&] {
    for (int i = 0; i < 4; ++i) {
      if (a.pieces.ownerOf(ids[i]) != 2) {
        return false;
      }
    }
    return true;
  }));
  c.session.leave();
  ASSERT_TRUE(rig.runUntil([&] { return a.pieces.size() <= 2; }));
  EXPECT_EQ(a.pieces.size(), 2u);

  ASSERT_TRUE(b.pieces.claim(ids[4]));
  ASSERT_TRUE(rig.runUntil([&] { return b.pieces.ownedByMe(ids[4]); }));
  ASSERT_TRUE(b.pieces.release(ids[4], Piece{9, 9, 9, true}));
  ASSERT_TRUE(rig.runUntil([&] { return !a.pieces.contains(ids[4]); }));
}

TEST(ObjectSync, SetsWithDifferentNamesDoNotInterfere) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  SharedObjects<Piece> otherA(a.session, "test.other");
  SharedObjects<Piece> otherB(b.session, "test.other");
  rig.run(0.3);

  ObjectId id = a.pieces.newId();
  a.pieces.add(id, Piece{1, 1, 1, false});
  otherA.add(otherA.newId(), Piece{2, 2, 2, false});
  ASSERT_TRUE(
      rig.runUntil([&] { return b.pieces.size() == 1 && otherB.size() == 1; }));
  EXPECT_EQ(a.pieces.size(), 1u);
  EXPECT_EQ(otherA.size(), 1u);
  EXPECT_EQ(*otherB.get(otherB.ids()[0]), (Piece{2, 2, 2, false}));
}

TEST(ObjectSync, StateThatDoesNotDecodeReadsAsAbsent) {
  Rig rig;
  Member &a = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  ObjectId id = a.pieces.newId();
  // A peer on another version sent a state of a different shape.
  ASSERT_TRUE(a.pieces.core().add(id, {1, 2, 3}));
  EXPECT_TRUE(a.pieces.contains(id));
  EXPECT_EQ(a.pieces.get(id), nullptr);
}

TEST(ObjectSync, WorksOnItsOwnBeforeJoiningAnything) {
  Rig rig;
  Member &alone = rig.add();
  ObjectId id = alone.pieces.newId();
  EXPECT_TRUE(alone.pieces.synced());
  ASSERT_TRUE(alone.pieces.add(id, Piece{4, 4, 0, false}));
  EXPECT_EQ(*alone.pieces.get(id), (Piece{4, 4, 0, false}));
  EXPECT_FALSE(alone.pieces.claim(id));
  EXPECT_FALSE(alone.pieces.update(id, Piece{}));

  Member &host = rig.add();
  ASSERT_TRUE(host.session.host(rig.config()));
  ObjectId theirs = host.pieces.newId();
  host.pieces.add(theirs, Piece{8, 8, 0, false});
  ASSERT_TRUE(alone.session.join(host.address(), rig.config()));
  ASSERT_TRUE(rig.runUntil(
      [&] { return alone.pieces.synced() && alone.session.isActive(); }));
  ASSERT_TRUE(rig.runUntil([&] { return alone.pieces.contains(theirs); }));
  EXPECT_FALSE(alone.pieces.contains(id));
  EXPECT_EQ(alone.removed, std::vector<ObjectId>{id});
}

TEST(ObjectSync, ASetMadeInsideARunningSessionCatchesUp) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  SharedObjects<Piece> early(a.session, "test.early");
  early.add(early.newId(), Piece{1, 1, 1, false});
  early.add(early.newId(), Piece{2, 2, 2, false});
  rig.run(0.3);

  SharedObjects<Piece> late(b.session, "test.early");
  EXPECT_FALSE(late.synced());
  ASSERT_TRUE(rig.runUntil([&] { return late.synced(); }));
  EXPECT_EQ(late.size(), 2u);
}

TEST(ObjectSync, SurvivesBeingMoved) {
  Rig rig;
  Member &a = rig.add();
  Member &b = rig.add();
  ASSERT_TRUE(a.session.host(rig.config()));
  rig.join(b, a);
  SharedObjects<Piece> original(a.session, "test.moved");
  SharedObjects<Piece> moved(std::move(original));
  SharedObjects<Piece> peer(b.session, "test.moved");
  ObjectId id = moved.newId();
  ASSERT_TRUE(moved.add(id, Piece{3, 3, 3, false}));
  ASSERT_TRUE(rig.runUntil([&] { return peer.contains(id); }));
  EXPECT_EQ(*peer.get(id), (Piece{3, 3, 3, false}));
}
