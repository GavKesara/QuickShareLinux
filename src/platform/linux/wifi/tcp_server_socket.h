// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef PLATFORM_IMPL_LINUX_TCP_SERVER_SOCKET_H_
#define PLATFORM_IMPL_LINUX_TCP_SERVER_SOCKET_H_

#include <arpa/inet.h>
#include <netinet/in.h>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <functional>
#include <utility>
#include <unistd.h>

#include "platform/api/exception.h"
#include "platform/linux/io/stream.h"
#include "platform/api/logging.h"

namespace quickshare::platform {
namespace linux {
class TCPSocket {
 public:
  explicit TCPSocket(int fd)
      : fd_(fd), closed_(false), output_stream_(fd), input_stream_(fd) {}
  TCPSocket(const TCPSocket&) = delete;
  TCPSocket& operator=(const TCPSocket&) = delete;
  TCPSocket(TCPSocket&& other) noexcept
      : fd_(std::exchange(other.fd_, -1)),
        closed_(std::exchange(other.closed_, true)),
        output_stream_(fd_),
        input_stream_(fd_) {}
  TCPSocket& operator=(TCPSocket&& other) noexcept {
    if (this == &other) return *this;
    Close();
    fd_ = std::exchange(other.fd_, -1);
    closed_ = std::exchange(other.closed_, true);
    output_stream_ = OutputStream(fd_);
    input_stream_ = InputStream(fd_);
    return *this;
  }
  ~TCPSocket() { Close(); }

  static std::optional<TCPSocket> Connect(const std::string& ip_address,
                                          int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      LOG(ERROR) << __func__
                 << ": Error opening socket: " << std::strerror(errno);
      return std::nullopt;
    }

    LOG(INFO) << __func__ << ": Connecting to " << ip_address << ":" << port;
    struct sockaddr_in addr;
    addr.sin_addr.s_addr = inet_addr(ip_address.c_str());
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    auto ret =
        connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (ret < 0) {
      LOG(ERROR) << __func__
                 << ": Error connecting to socket: " << std::strerror(errno);
      close(sock);
      return std::nullopt;
    }

    return TCPSocket(sock);
  }

  InputStream& GetInputStream() { return input_stream_; }
  OutputStream& GetOutputStream() { return output_stream_; }

  Exception Close() {
    if (closed_) return {Exception::kSuccess};

    closed_ = true;
    input_stream_.Close();
    output_stream_.Close();
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }

    return {Exception::kSuccess};
  };

 private:
  int fd_;
  bool closed_;

  OutputStream output_stream_;
  InputStream input_stream_;
};

class TCPServerSocket {
 public:
  explicit TCPServerSocket(int fd) : fd_(fd) {}

  static std::optional<TCPServerSocket> Listen(
      std::optional<const std::reference_wrapper<std::string>> ip_address,
      int port) {
    auto sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      LOG(ERROR) << __func__
                 << ": Error opening socket: " << std::strerror(errno);
      return std::nullopt;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (ip_address.has_value())
      addr.sin_addr.s_addr = inet_addr(ip_address->get().c_str());
    else
      addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    auto ret =
        bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (ret < 0) {
      LOG(ERROR) << __func__ << ": Error binding to port " << port << ": "
                 << std::strerror(errno);
      close(sock);
      return std::nullopt;
    }

    ret = listen(sock, 0);
    if (ret < 0) {
      LOG(ERROR) << __func__
                 << ": Error listening on socket: " << std::strerror(errno);
      close(sock);
      return std::nullopt;
    }

    return TCPServerSocket(sock);
  }
  std::optional<TCPSocket> Accept() {
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);

    auto conn = accept(fd_, reinterpret_cast<struct sockaddr*>(&addr), &len);
    if (conn < 0) {
      LOG(ERROR) << __func__
                 << ": Error accepting incoming connections on socket " << fd_
                 << ": " << std::strerror(errno);
      return std::nullopt;
    }

    return TCPSocket(conn);
  };

  Exception Close() {
    if (fd_ < 0) {
      return {Exception::kSuccess};
    }
    int fd = fd_;
    fd_ = -1;
    shutdown(fd, SHUT_RDWR);
    auto ret = close(fd);
    if (ret < 0) {
      LOG(ERROR) << __func__ << ": Error closing socket " << fd << ": "
                 << std::strerror(errno);
      return {Exception::kFailed};
    }

    return {Exception::kSuccess};
  };

  int GetPort() const {
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    auto ret = getsockname(fd_, reinterpret_cast<struct sockaddr*>(&sin), &len);
    if (ret < 0) {
      LOG(ERROR) << __func__ << ": Error getting information for socket " << fd_
                 << ": " << std::strerror(errno);
      return 0;
    }

    return ntohs(sin.sin_port);
  }

 private:
  int fd_;
};
}  // namespace linux
}  // namespace quickshare::platform

#endif
