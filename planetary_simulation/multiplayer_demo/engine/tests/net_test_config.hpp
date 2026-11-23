#pragma once

#include "componeng/net/transport.hpp"

// Network::open() requires a protocolId; tests share this one.
inline componeng::net::TransportConfig testConfig() {
  componeng::net::TransportConfig config;
  config.protocolId = 0x54455354;
  return config;
}
