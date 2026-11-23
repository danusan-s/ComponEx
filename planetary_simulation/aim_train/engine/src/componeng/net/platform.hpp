#pragma once

// Private to net: the only place that includes the OS socket headers, so games
// never see winsock macros.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cerrno>
#endif

#include <mutex>
#include <string>
#include <system_error>

namespace componeng::net::platform {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
using SockLen = int;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
using SockLen = socklen_t;
#endif

inline int lastError() {
#ifdef _WIN32
  return WSAGetLastError();
#else
  return errno;
#endif
}

// Winsock errors are Win32 codes, so system_category covers both platforms.
inline std::string errorMessage(int error) {
  return std::system_category().message(error);
}

inline bool isWouldBlock(int error) {
#ifdef _WIN32
  return error == WSAEWOULDBLOCK;
#else
  return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

inline bool isInterrupted(int error) {
#ifdef _WIN32
  (void)error;
  return false;
#else
  return error == EINTR;
#endif
}

inline void closeSocket(SocketHandle handle) {
#ifdef _WIN32
  closesocket(handle);
#else
  ::close(handle);
#endif
}

// Winsock must be started before the first socket or lookup and stopped after
// the last, so users are counted.
inline std::mutex &stackMutex() {
  static std::mutex mutex;
  return mutex;
}
inline int &stackUsers() {
  static int users = 0;
  return users;
}

// Each true must be paired with releaseNetwork().
inline bool acquireNetwork() {
#ifdef _WIN32
  std::lock_guard<std::mutex> lock(stackMutex());
  if (stackUsers() == 0) {
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      return false;
    }
  }
  ++stackUsers();
#endif
  return true;
}

inline void releaseNetwork() {
#ifdef _WIN32
  std::lock_guard<std::mutex> lock(stackMutex());
  if (--stackUsers() == 0) {
    WSACleanup();
  }
#endif
}

} // namespace componeng::net::platform
