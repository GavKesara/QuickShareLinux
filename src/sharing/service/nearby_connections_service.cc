// Copyright 2022 Google LLC
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

#include "sharing/service/nearby_connections_service.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "connections/api/listeners.h"
#include "connections/api/payload.h"
#include "connections/api/payload_type.h"
#include "connections/api/status.h"
#include "connections/api/strategy.h"
#include "base/base/file_path.h"
#include "platform/api/byte_array.h"
#include "platform/api/file.h"
#include "sharing/api/public/logging.h"
#include "sharing/domain/nearby_connections_types.h"

namespace quickshare::sharing {

using ::quickshare::connections::PayloadType;
using ::quickshare::connections::ResultCallback;

using NcPayload = ::quickshare::connections::Payload;
using NcStatus = ::quickshare::connections::Status;
using NcStrategy = ::quickshare::connections::Strategy;

class VectorInputStream final : public platform::InputStream {
 public:
  explicit VectorInputStream(std::vector<uint8_t> bytes)
      : bytes_(std::move(bytes)) {}

  platform::ExceptionOr<platform::ByteArray> Read(std::int64_t size) override {
    if (closed_) {
      return platform::ExceptionOr<platform::ByteArray>(platform::Exception::kIo);
    }
    if (offset_ >= bytes_.size() || size <= 0) {
      return platform::ExceptionOr<platform::ByteArray>(platform::ByteArray());
    }

    const size_t remaining = bytes_.size() - offset_;
    const size_t chunk_size = std::min<size_t>(remaining, static_cast<size_t>(size));
    platform::ByteArray chunk(reinterpret_cast<const char*>(bytes_.data() + offset_),
                    chunk_size);
    offset_ += chunk_size;
    return platform::ExceptionOr<platform::ByteArray>(std::move(chunk));
  }

  platform::Exception Close() override {
    closed_ = true;
    return {platform::Exception::kSuccess};
  }

 private:
  std::vector<uint8_t> bytes_;
  size_t offset_ = 0;
  bool closed_ = false;
};

class SharedInputStream final : public platform::InputStream {
 public:
  explicit SharedInputStream(std::shared_ptr<platform::InputStream> stream)
      : stream_(std::move(stream)) {}

  platform::ExceptionOr<platform::ByteArray> Read(std::int64_t size) override {
    if (stream_ == nullptr) {
      return platform::ExceptionOr<platform::ByteArray>(platform::Exception::kIo);
    }
    return stream_->Read(size);
  }

  platform::Exception Close() override {
    if (stream_ == nullptr) {
      return {platform::Exception::kIo};
    }
    return stream_->Close();
  }

 private:
  std::shared_ptr<platform::InputStream> stream_;
};

Status ConvertToStatus(NcStatus status) {
  return static_cast<Status>(status.value);
}

Payload ConvertToPayload(NcPayload payload) {
  switch (payload.GetType()) {
    case PayloadType::kBytes: {
      const platform::ByteArray& bytes = payload.AsBytes();
      std::string data = std::string(bytes);
      return Payload(payload.GetId(),
                     std::vector<uint8_t>(data.begin(), data.end()));
    }
    case PayloadType::kFile: {
      std::string file_path = payload.AsFile()->GetFilePath();
      std::string parent_folder = payload.GetParentFolder();
      VLOG(1) << __func__ << ": Payload file_path=" << file_path
              << ", parent_folder = " << parent_folder;
      return Payload(payload.GetId(), base::FilePath(file_path), parent_folder);
    }
    default:
      return Payload();
  }
}

NcPayload ConvertToServicePayload(Payload payload) {
  switch (payload.content.type) {
    case PayloadContent::Type::kFile: {
      std::string file_path = payload.content.file_payload.file_path.ToString();
      std::string file_name =
          payload.content.file_payload.file_path.GetFileName().ToString();
      std::string parent_folder = payload.content.file_payload.parent_folder;
      std::replace(parent_folder.begin(), parent_folder.end(), '\\', '/');
      VLOG(1) << __func__ << ": NC Payload file_path=" << file_path
              << ", parent_folder = " << parent_folder;
      platform::InputFile input_file(file_path);
      NcPayload nc_payload(payload.id, parent_folder, file_name,
                           std::move(input_file));
      return nc_payload;
    }
    case PayloadContent::Type::kBytes: {
      std::vector<uint8_t> bytes = payload.content.bytes_payload.bytes;
      return NcPayload(payload.id,
                       platform::ByteArray(std::string(bytes.begin(), bytes.end())));
    }
    case PayloadContent::Type::kStream: {
      if (payload.content.stream_payload.input_stream != nullptr) {
        return NcPayload(
            payload.id,
            std::make_unique<SharedInputStream>(
                std::move(payload.content.stream_payload.input_stream)));
      }
      auto stream = std::make_unique<VectorInputStream>(
          std::move(payload.content.stream_payload.bytes));
      return NcPayload(payload.id, std::move(stream));
    }
    default:
      return NcPayload();
  }
}

ResultCallback BuildResultCallback(
    std::function<void(Status status)> callback) {
  return ResultCallback{[&, callback = std::move(callback)](NcStatus status) {
    callback(ConvertToStatus(status));
  }};
}

NcStrategy ConvertToServiceStrategy(Strategy strategy) {
  switch (strategy) {
    case Strategy::kP2pCluster:
      return NcStrategy::kP2pCluster;
    case Strategy::kP2pPointToPoint:
      return NcStrategy::kP2pPointToPoint;
    case Strategy::kP2pStar:
      return NcStrategy::kP2pStar;
  }
}

}  // namespace quickshare::sharing
