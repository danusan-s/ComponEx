#include "componeng/net/udp_socket.hpp"

#include "componeng/utils/logger.hpp"
#include "platform.hpp"

#include <cstring>
#include <utility>

#ifdef _WIN32
// Not in winsock2.h: it is defined in mstcpip.h.
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
#endif

namespace componeng::net {

namespace {

using platform::SocketHandle;
using platform::SockLen;

bool acquireSockets() {
  if (!platform::acquireNetwork()) {
    LOG_ERROR("The network stack failed to start");
    return false;
  }
  return true;
}

void releaseSockets() {
  platform::releaseNetwork();
}

bool setNonBlocking(SocketHandle handle) {
#ifdef _WIN32
  u_long enable = 1;
  return ioctlsocket(handle, FIONBIO, &enable) == 0;
#else
  int flags = fcntl(handle, F_GETFL, 0);
  return flags != -1 && fcntl(handle, F_SETFL, flags | O_NONBLOCK) != -1;
#endif
}

SockLen toSockaddr(const Address &address, sockaddr_storage &out) {
  std::memset(&out, 0, sizeof(out));
  if (address.family() == Address::Family::IPv4) {
    auto &in = reinterpret_cast<sockaddr_in &>(out);
    in.sin_family = AF_INET;
    in.sin_port = htons(address.port());
    std::memcpy(&in.sin_addr, address.bytes(), 4);
    return static_cast<SockLen>(sizeof(sockaddr_in));
  }
  auto &in6 = reinterpret_cast<sockaddr_in6 &>(out);
  in6.sin6_family = AF_INET6;
  in6.sin6_port = htons(address.port());
  std::memcpy(&in6.sin6_addr, address.bytes(), 16);
  return static_cast<SockLen>(sizeof(sockaddr_in6));
}

Address fromSockaddr(const sockaddr_storage &storage) {
  if (storage.ss_family == AF_INET) {
    const auto &in = reinterpret_cast<const sockaddr_in &>(storage);
    return Address::fromBytes(Address::Family::IPv4,
                              reinterpret_cast<const uint8_t *>(&in.sin_addr),
                              ntohs(in.sin_port));
  }
  if (storage.ss_family == AF_INET6) {
    const auto &in6 = reinterpret_cast<const sockaddr_in6 &>(storage);
    return Address::fromBytes(Address::Family::IPv6,
                              reinterpret_cast<const uint8_t *>(&in6.sin6_addr),
                              ntohs(in6.sin6_port));
  }
  return Address();
}

// Returns the size, or -1 (cause in platform::lastError()). Sets `truncated`
// when it did not fit: POSIX flags the message, Winsock fails the call.
long long receiveDatagram(SocketHandle handle, void *buffer, size_t capacity,
                          sockaddr_storage &from, bool &truncated) {
  truncated = false;
#ifdef _WIN32
  int fromLength = static_cast<int>(sizeof(from));
  int received =
      recvfrom(handle, static_cast<char *>(buffer), static_cast<int>(capacity),
               0, reinterpret_cast<sockaddr *>(&from), &fromLength);
  if (received == SOCKET_ERROR && WSAGetLastError() == WSAEMSGSIZE) {
    truncated = true;
    return 0;
  }
  return received;
#else
  iovec iov{buffer, capacity};
  msghdr message{};
  message.msg_name = &from;
  message.msg_namelen = sizeof(from);
  message.msg_iov = &iov;
  message.msg_iovlen = 1;
  ssize_t received = recvmsg(handle, &message, 0);
  if (received >= 0 && (message.msg_flags & MSG_TRUNC)) {
    truncated = true;
  }
  return received;
#endif
}

} // namespace

UdpSocket::~UdpSocket() {
  close();
}

UdpSocket::UdpSocket(UdpSocket &&other) noexcept
    : m_handle(std::exchange(other.m_handle, kNoHandle)),
      m_local(other.m_local), m_lastError(std::move(other.m_lastError)) {
}

UdpSocket &UdpSocket::operator=(UdpSocket &&other) noexcept {
  if (this != &other) {
    close();
    m_handle = std::exchange(other.m_handle, kNoHandle);
    m_local = other.m_local;
    m_lastError = std::move(other.m_lastError);
  }
  return *this;
}

bool UdpSocket::open(const Address &local, bool shareAddress) {
  close();
  m_lastError.clear();
  if (!local.isValid()) {
    m_lastError = "cannot open a socket on an invalid address";
    LOG_ERROR("%s", m_lastError.c_str());
    return false;
  }
  if (!acquireSockets()) {
    m_lastError = "network stack failed to start";
    return false;
  }

  const bool v6 = local.family() == Address::Family::IPv6;
  SocketHandle handle =
      socket(v6 ? AF_INET6 : AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (handle == platform::kInvalidSocket) {
    m_lastError =
        "socket() failed: " + platform::errorMessage(platform::lastError());
    LOG_ERROR("%s", m_lastError.c_str());
    releaseSockets();
    return false;
  }

  auto fail = [&](const char *what) {
    m_lastError = std::string(what) +
                  " failed: " + platform::errorMessage(platform::lastError());
    LOG_ERROR("%s (%s)", m_lastError.c_str(), local.toString().c_str());
    platform::closeSocket(handle);
    releaseSockets();
    return false;
  };

  if (!setNonBlocking(handle)) {
    return fail("setting non-blocking mode");
  }
  if (v6) {
    // IPv6-only on every OS (the default differs).
    int only = 1;
    if (setsockopt(handle, IPPROTO_IPV6, IPV6_V6ONLY,
                   reinterpret_cast<const char *>(&only), sizeof(only)) != 0) {
      return fail("setting IPV6_V6ONLY");
    }
  }
  // Needed to send to the broadcast address (discovery).
  int enable = 1;
  if (setsockopt(handle, SOL_SOCKET, SO_BROADCAST,
                 reinterpret_cast<const char *>(&enable),
                 sizeof(enable)) != 0) {
    return fail("setting SO_BROADCAST");
  }
  if (shareAddress) {
    if (setsockopt(handle, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&enable),
                   sizeof(enable)) != 0) {
      return fail("setting SO_REUSEADDR");
    }
#if !defined(_WIN32) && defined(SO_REUSEPORT)
    // BSD/macOS need this too for broadcasts to reach every socket.
    setsockopt(handle, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
#endif
  }
#ifdef _WIN32
  // Windows would otherwise turn an ICMP "port unreachable" into a recv error,
  // breaking receives from the other peers.
  BOOL reportReset = FALSE;
  DWORD returned = 0;
  WSAIoctl(handle, SIO_UDP_CONNRESET, &reportReset, sizeof(reportReset),
           nullptr, 0, &returned, nullptr, nullptr);
#endif

  sockaddr_storage storage;
  SockLen length = toSockaddr(local, storage);
  if (bind(handle, reinterpret_cast<const sockaddr *>(&storage), length) != 0) {
    return fail("bind");
  }

  // Read back: holds the real port when `local` asked for 0.
  sockaddr_storage bound;
  SockLen boundLength = static_cast<SockLen>(sizeof(bound));
  if (getsockname(handle, reinterpret_cast<sockaddr *>(&bound), &boundLength) !=
      0) {
    return fail("getsockname");
  }

  m_handle = static_cast<intptr_t>(handle);
  m_local = fromSockaddr(bound);
  return true;
}

void UdpSocket::close() {
  if (m_handle == kNoHandle) {
    return;
  }
  platform::closeSocket(static_cast<SocketHandle>(m_handle));
  releaseSockets();
  m_handle = kNoHandle;
  m_local = Address();
}

IoStatus UdpSocket::sendTo(const Address &to, const void *data, size_t size) {
  if (!isOpen()) {
    m_lastError = "socket is not open";
    return IoStatus::Error;
  }
  if (size > kMaxPacketSize) {
    m_lastError = "datagram of " + std::to_string(size) + " bytes exceeds " +
                  std::to_string(kMaxPacketSize);
    return IoStatus::Error;
  }
  if (to.family() != m_local.family()) {
    m_lastError =
        "destination is of a different address family than the socket";
    return IoStatus::Error;
  }

  sockaddr_storage storage;
  SockLen length = toSockaddr(to, storage);
  const SocketHandle handle = static_cast<SocketHandle>(m_handle);
  while (true) {
#ifdef _WIN32
    int sent =
        sendto(handle, static_cast<const char *>(data), static_cast<int>(size),
               0, reinterpret_cast<const sockaddr *>(&storage), length);
#else
    ssize_t sent = sendto(handle, data, size, 0,
                          reinterpret_cast<const sockaddr *>(&storage), length);
#endif
    if (sent >= 0) {
      return IoStatus::Ok;
    }
    int error = platform::lastError();
    if (platform::isInterrupted(error)) {
      continue;
    }
    if (platform::isWouldBlock(error)) {
      return IoStatus::WouldBlock;
    }
    // Not logged: an unreachable network would repeat it every send.
    m_lastError = "sendto failed: " + platform::errorMessage(error);
    return IoStatus::Error;
  }
}

IoStatus UdpSocket::recvFrom(void *buffer, size_t capacity, size_t &received,
                             Address &from) {
  received = 0;
  if (!isOpen()) {
    m_lastError = "socket is not open";
    return IoStatus::Error;
  }

  const SocketHandle handle = static_cast<SocketHandle>(m_handle);
  while (true) {
    sockaddr_storage storage;
    std::memset(&storage, 0, sizeof(storage));
    bool truncated = false;
    long long size =
        receiveDatagram(handle, buffer, capacity, storage, truncated);
    if (size >= 0) {
      Address sender = fromSockaddr(storage);
      // Too big, or an unrepresentable family: skip to the next datagram.
      if (truncated || !sender.isValid()) {
        continue;
      }
      received = static_cast<size_t>(size);
      from = sender;
      return IoStatus::Ok;
    }
    int error = platform::lastError();
    if (platform::isInterrupted(error)) {
      continue;
    }
    if (platform::isWouldBlock(error)) {
      return IoStatus::WouldBlock;
    }
#ifdef _WIN32
    // A stray reset from an earlier send says nothing about waiting datagrams.
    if (error == WSAECONNRESET) {
      continue;
    }
#endif
    m_lastError = "recvfrom failed: " + platform::errorMessage(error);
    return IoStatus::Error;
  }
}

bool UdpSocket::waitReadable(int timeoutMs) {
  if (!isOpen()) {
    return false;
  }
  pollfd descriptor{};
  descriptor.fd = static_cast<SocketHandle>(m_handle);
  descriptor.events = POLLIN;
#ifdef _WIN32
  int ready = WSAPoll(&descriptor, 1, timeoutMs);
#else
  int ready = poll(&descriptor, 1, timeoutMs);
  // A signal ends the wait early; report "not readable" and let the caller
  // decide.
#endif
  return ready > 0 && (descriptor.revents & POLLIN) != 0;
}

} // namespace componeng::net
