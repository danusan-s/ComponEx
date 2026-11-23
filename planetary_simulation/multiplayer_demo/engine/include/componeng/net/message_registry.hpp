#pragma once

#include "componeng/net/address.hpp"
#include "componeng/net/archive.hpp"
#include "componeng/net/transport.hpp"
#include "componeng/utils/logger.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace componeng::net {

// Gives each message type a 2-byte id on the wire and routes arriving bytes to
// typed handlers. A message is a struct with a static kName and a serialize()
// (see archive.hpp). The id is a hash of the name, so peers agree without a
// shared table; a clash is caught when the second type is added (rename one).
// Change a message's fields and name together.
class MessageRegistry {
public:
  using MessageId = uint16_t;

  template <typename T>
  using Handler = std::function<void(const Address &, const T &)>;

  enum class DecodeResult {
    Ok,
    UnknownType,  // no message registered under that id
    WrongChannel, // known, but it arrived on the other channel than registered
    Malformed     // known, but its bytes do not parse
  };

  static MessageId idOf(const char *name);

  // False (nothing registered) if another type holds the id, or T is already
  // registered on a different channel.
  template <typename T> bool add(Channel channel = Channel::Reliable) {
    static_assert(std::is_default_constructible_v<T>,
                  "a message type must be default constructible to be read");
    auto existing = m_byType.find(std::type_index(typeid(T)));
    if (existing != m_byType.end()) {
      return m_entries.at(existing->second).channel == channel;
    }

    MessageId id = idOf(T::kName);
    auto clash = m_entries.find(id);
    if (clash != m_entries.end()) {
      LOG_ERROR("Message '%s' has the same id as '%s'; rename one of them",
                T::kName, clash->second.name.c_str());
      return false;
    }

    auto handlers = std::make_shared<std::vector<Handler<T>>>();
    Entry entry;
    entry.name = T::kName;
    entry.channel = channel;
    entry.handlers = handlers;
    entry.decode = [handlers](const Address &from, ByteReader &reader) {
      T message{};
      reader(message);
      // Leftover bytes mean the sender's idea of the message differs from ours.
      if (reader.failed() || reader.remaining() != 0) {
        return false;
      }
      // By index: a handler may add handlers.
      for (size_t i = 0; i < handlers->size(); ++i) {
        (*handlers)[i](from, message);
      }
      return true;
    };
    m_entries.emplace(id, std::move(entry));
    m_byType.emplace(std::type_index(typeid(T)), id);
    return true;
  }

  template <typename T> bool contains() const {
    return m_byType.count(std::type_index(typeid(T))) != 0;
  }

  // Handlers run in the order added.
  template <typename T> bool onReceive(Handler<T> handler) {
    auto it = m_byType.find(std::type_index(typeid(T)));
    if (it == m_byType.end()) {
      return false;
    }
    auto *handlers = static_cast<std::vector<Handler<T>> *>(
        m_entries.at(it->second).handlers.get());
    handlers->push_back(std::move(handler));
    return true;
  }

  // Writes the id and fields and reports the channel. False if T is not
  // registered.
  template <typename T>
  bool encode(const T &message, ByteWriter &out, Channel &channel) const {
    auto it = m_byType.find(std::type_index(typeid(T)));
    if (it == m_byType.end()) {
      return false;
    }
    channel = m_entries.at(it->second).channel;
    MessageId id = it->second;
    out(id, message);
    return true;
  }

  DecodeResult decode(const Address &from, Channel channel, const uint8_t *data,
                      size_t size) const;

private:
  struct Entry {
    std::string name;
    Channel channel = Channel::Reliable;
    std::function<bool(const Address &, ByteReader &)> decode;
    // A std::vector<Handler<T>>, type-erased so all T share one map.
    std::shared_ptr<void> handlers;
  };

  std::unordered_map<MessageId, Entry> m_entries;
  std::unordered_map<std::type_index, MessageId> m_byType;
};

} // namespace componeng::net
