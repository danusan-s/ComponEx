#pragma once

#include "componeng/net/address.hpp"

#include <string>

namespace componeng::net {

// A public address as a short code to read out or paste into a chat: how two
// peers that have never met learn where to send.
//
//   "194.12.8.40:5000"  ->  "0K10-R218-2E45-0"
//
// Crockford base32 (no I, L, O, U) with a check character, in groups of four. A
// mistyped character is always caught. Case, spaces and hyphens are ignored;
// O, I and L read as 0, 1, 1.
std::string encodeInvite(const Address &address);

bool decodeInvite(const std::string &code, Address &out);

} // namespace componeng::net
