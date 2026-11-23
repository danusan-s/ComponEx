#include "componeng/net/message_registry.hpp"

namespace componeng::net {

MessageRegistry::MessageId MessageRegistry::idOf(const char *name) {
  // FNV-1a folded to 16 bits: easy to reproduce; clashes are reported when
  // registering.
  uint32_t hash = 2166136261u;
  for (const char *p = name; *p; ++p) {
    hash ^= static_cast<uint8_t>(*p);
    hash *= 16777619u;
  }
  return static_cast<MessageId>((hash >> 16) ^ (hash & 0xFFFF));
}

MessageRegistry::DecodeResult MessageRegistry::decode(const Address &from,
                                                      Channel channel,
                                                      const uint8_t *data,
                                                      size_t size) const {
  ByteReader reader(data, size);
  MessageId id = 0;
  reader(id);
  if (reader.failed()) {
    return DecodeResult::Malformed;
  }
  auto it = m_entries.find(id);
  if (it == m_entries.end()) {
    return DecodeResult::UnknownType;
  }
  if (it->second.channel != channel) {
    return DecodeResult::WrongChannel;
  }
  return it->second.decode(from, reader) ? DecodeResult::Ok
                                         : DecodeResult::Malformed;
}

} // namespace componeng::net
