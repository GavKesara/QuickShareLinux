// Copyright 2020 Google LLC
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

#include "connections/core/internal_payload_factory.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "connections/core/internal_payload.h"
#include "proto/connections/offline_wire_formats.pb.h"
#include "connections/api/payload.h"
#include "connections/api/payload_type.h"
#include "platform/api/byte_array.h"
#include "platform/api/exception.h"
#include "platform/api/expected.h"
#include "platform/api/file.h"
#include "platform/api/implementation/platform.h"
#include "platform/api/input_stream.h"
#include "platform/api/logging.h"
#include "platform/api/output_stream.h"
#include "platform/api/pipe.h"

namespace quickshare::connections {

namespace {
using ::location::nearby::connections::PayloadTransferFrame;
using ::location::nearby::proto::connections::OperationResultCode;

// if custom_save_path is empty, default download path is used
std::string make_path(const std::string& custom_save_path,
                      const std::string& parent_folder,
                      const std::string& file_name) {
  if (!custom_save_path.empty()) {
    std::string path = absl::StrCat(custom_save_path, "/", parent_folder);
    return platform::api::ImplementationPlatform::GetCustomSavePath(path, file_name);
  }
  return platform::api::ImplementationPlatform::GetDownloadPath(parent_folder, file_name);
}

class BytesInternalPayload : public InternalPayload {
 public:
  explicit BytesInternalPayload(Payload payload)
      : InternalPayload(std::move(payload)),
        total_size_(payload_.AsBytes().size()),
        detached_only_chunk_(false) {}

  location::nearby::connections::PayloadTransferFrame::PayloadHeader::
      PayloadType
      GetType() const override {
    return location::nearby::connections::PayloadTransferFrame::PayloadHeader::
        BYTES;
  }

  std::int64_t GetTotalSize() const override { return total_size_; }

  // Relinquishes ownership of the payload_; retrieves and returns the stored
  // ByteArray.
  platform::ByteArray DetachNextChunk(int chunk_size) override {
    if (detached_only_chunk_) {
      return {};
    }

    detached_only_chunk_ = true;
    return std::move(payload_).AsBytes();
  }

  // Does nothing.
  platform::Exception AttachNextChunk(absl::string_view chunk) override {
    return {platform::Exception::kSuccess};
  }

  platform::ExceptionOr<size_t> SkipToOffset(size_t offset) override {
    LOG(WARNING) << "Bytes payload does not support offsets";
    return {platform::Exception::kIo};
  }

 private:
  // We're caching the total size here because the backing payload will be
  // moved to another owner during the lifetime of an incoming
  // InternalPayload.
  const std::int64_t total_size_;
  bool detached_only_chunk_;
};

class OutgoingStreamInternalPayload : public InternalPayload {
 public:
  explicit OutgoingStreamInternalPayload(Payload payload)
      : InternalPayload(std::move(payload)) {}

  location::nearby::connections::PayloadTransferFrame::PayloadHeader::
      PayloadType
      GetType() const override {
    return location::nearby::connections::PayloadTransferFrame::PayloadHeader::
        STREAM;
  }

  std::int64_t GetTotalSize() const override { return -1; }

  platform::ByteArray DetachNextChunk(int chunk_size) override {
    platform::InputStream* input_stream = payload_.AsStream();
    if (!input_stream) return {};

    platform::ExceptionOr<platform::ByteArray> bytes_read = input_stream->Read(chunk_size);
    if (!bytes_read.ok()) {
      input_stream->Close();
      return {};
    }

    platform::ByteArray scoped_bytes_read = std::move(bytes_read.result());

    if (scoped_bytes_read.Empty()) {
      LOG(INFO) << "No more data for outgoing payload " << this
                << ", closing InputStream.";

      input_stream->Close();
      return {};
    }

    return scoped_bytes_read;
  }

  platform::Exception AttachNextChunk(absl::string_view chunk) override {
    return {platform::Exception::kIo};
  }

  platform::ExceptionOr<size_t> SkipToOffset(size_t offset) override {
    platform::InputStream* stream = payload_.AsStream();
    if (stream == nullptr) return {platform::Exception::kIo};

    platform::ExceptionOr<size_t> real_offset = stream->Skip(offset);
    if (real_offset.ok() && real_offset.GetResult() == offset) {
      return real_offset;
    }
    // Close the outgoing stream on any error
    stream->Close();
    if (!real_offset.ok()) {
      return real_offset;
    }
    LOG(WARNING) << "Skip offset: " << real_offset.GetResult()
                 << ", expected offset: " << offset << " for payload " << this;
    return {platform::Exception::kIo};
  }

  void Close() override {
    // Ignore the potential Exception returned by close(), as a counterpart
    // to Java's closeQuietly().
    platform::InputStream* stream = payload_.AsStream();
    if (stream) stream->Close();
  }
};

class IncomingStreamInternalPayload : public InternalPayload {
 public:
  IncomingStreamInternalPayload(Payload payload,
                                std::unique_ptr<platform::OutputStream> output)
      : InternalPayload(std::move(payload)), output_(std::move(output)) {}

  PayloadTransferFrame::PayloadHeader::PayloadType GetType() const override {
    return PayloadTransferFrame::PayloadHeader::STREAM;
  }

  std::int64_t GetTotalSize() const override { return -1; }

  platform::ByteArray DetachNextChunk(int chunk_size) override { return {}; }

  platform::Exception AttachNextChunk(absl::string_view chunk) override {
    if (chunk.empty()) {
      LOG(INFO) << "Received null last chunk for incoming payload " << this
                << ", closing OutputStream.";
      Close();
      return {platform::Exception::kSuccess};
    }

    return output_->Write(chunk);
  }

  platform::ExceptionOr<size_t> SkipToOffset(size_t offset) override {
    LOG(WARNING) << "Cannot skip offset for an incoming Payload " << this;
    return {platform::Exception::kIo};
  }

  void Close() override { output_->Close(); }

 private:
  std::unique_ptr<platform::OutputStream> output_;
};

class OutgoingFileInternalPayload : public InternalPayload {
 public:
  explicit OutgoingFileInternalPayload(Payload payload)
      : InternalPayload(std::move(payload)),
        total_size_{payload_.AsFile()->GetTotalSize()} {}

  location::nearby::connections::PayloadTransferFrame::PayloadHeader::
      PayloadType
      GetType() const override {
    return location::nearby::connections::PayloadTransferFrame::PayloadHeader::
        FILE;
  }

  std::int64_t GetTotalSize() const override { return total_size_; }

  platform::ByteArray DetachNextChunk(int chunk_size) override {
    platform::InputFile* file = payload_.AsFile();
    if (!file) return {};

    platform::ExceptionOr<platform::ByteArray> bytes_read = file->Read(chunk_size);
    if (!bytes_read.ok()) {
      return {};
    }

    platform::ByteArray bytes = std::move(bytes_read.result());

    if (bytes.Empty()) {
      // No more data for outgoing payload.

      file->Close();
      return {};
    }

    return bytes;
  }

  platform::Exception AttachNextChunk(absl::string_view chunk) override {
    return {platform::Exception::kIo};
  }

  platform::ExceptionOr<size_t> SkipToOffset(size_t offset) override {
    LOG(INFO) << "SkipToOffset " << offset;
    platform::InputFile* file = payload_.AsFile();
    if (!file) {
      return {platform::Exception::kIo};
    }

    platform::ExceptionOr<size_t> real_offset = file->Skip(offset);
    if (real_offset.ok() && real_offset.GetResult() == offset) {
      return real_offset;
    }
    // Close the outgoing file on any error
    file->Close();
    if (!real_offset.ok()) {
      return real_offset;
    }
    LOG(WARNING) << "Skip offset: " << real_offset.GetResult()
                 << ", expected offset: " << offset << " for file payload "
                 << this;
    return {platform::Exception::kIo};
  }

  void Close() override {
    platform::InputFile* file = payload_.AsFile();
    if (file) file->Close();
  }

 private:
  std::int64_t total_size_;
};

class IncomingFileInternalPayload : public InternalPayload {
 public:
  IncomingFileInternalPayload(Payload payload, platform::OutputFile output_file,
                              absl::Time last_modified_time,
                              std::int64_t total_size)
      : InternalPayload(std::move(payload)),
        output_file_(std::move(output_file)),
        last_modified_time_(last_modified_time),
        total_size_(total_size) {}

  location::nearby::connections::PayloadTransferFrame::PayloadHeader::
      PayloadType
      GetType() const override {
    return location::nearby::connections::PayloadTransferFrame::PayloadHeader::
        FILE;
  }

  std::int64_t GetTotalSize() const override { return total_size_; }

  platform::ByteArray DetachNextChunk(int chunk_size) override { return {}; }

  platform::Exception AttachNextChunk(absl::string_view chunk) override {
    if (chunk.empty()) {
      // Received null last chunk for incoming payload.
      Close();
      return {platform::Exception::kSuccess};
    }

    return output_file_.Write(chunk);
  }

  platform::ExceptionOr<size_t> SkipToOffset(size_t offset) override {
    LOG(WARNING) << "Cannot skip offset for an incoming file Payload " << this;
    return {platform::Exception::kIo};
  }

  void Close() override {
    output_file_.SetLastModifiedTime(last_modified_time_);
    output_file_.Close();
  }

 private:
  platform::OutputFile output_file_;
  absl::Time last_modified_time_;
  const std::int64_t total_size_;
};

}  // namespace


platform::ErrorOr<std::unique_ptr<InternalPayload>> CreateOutgoingInternalPayload(
    Payload payload) {
  switch (payload.GetType()) {
    case PayloadType::kBytes:
      return {std::make_unique<BytesInternalPayload>(std::move(payload))};

    case PayloadType::kFile: {
      return {
          std::make_unique<OutgoingFileInternalPayload>(std::move(payload))};
    }

    case PayloadType::kStream:
      return {
          std::make_unique<OutgoingStreamInternalPayload>(std::move(payload))};

    default:
      DCHECK(false);  // This should never happen.
      return {platform::Error(OperationResultCode::DETAIL_UNKNOWN)};
  }
}

platform::ErrorOr<std::unique_ptr<InternalPayload>> CreateIncomingInternalPayload(
    const location::nearby::connections::PayloadTransferFrame& frame,
    const std::string& custom_save_path) {
  if (frame.packet_type() !=
      location::nearby::connections::PayloadTransferFrame::DATA) {
    return {platform::Error(
        OperationResultCode::NEARBY_GENERIC_INCOMING_PAYLOAD_NOT_DATA_TYPE)};
  }

  const Payload::Id payload_id = frame.payload_header().id();
  switch (frame.payload_header().type()) {
    case PayloadTransferFrame::PayloadHeader::BYTES: {
      return {std::make_unique<BytesInternalPayload>(
          Payload(payload_id, platform::ByteArray(frame.payload_chunk().body())))};
    }

    case PayloadTransferFrame::PayloadHeader::STREAM: {
      auto [input, output] = platform::CreatePipe();

      return {std::make_unique<IncomingStreamInternalPayload>(
          Payload(payload_id, std::move(input)), std::move(output))};
    }

    case PayloadTransferFrame::PayloadHeader::FILE: {
      std::string parent_folder;
      std::string file_name;
      std::string file_path;

      int64_t total_size = 0;

      if (frame.payload_header().has_parent_folder()) {
        parent_folder = frame.payload_header().parent_folder();
      }

      if (frame.payload_header().has_file_name()) {
        file_name = frame.payload_header().file_name();
        // if custom_save_path is empty, default download path is used
        file_path = make_path(custom_save_path, parent_folder, file_name);
      } else {
        if (frame.payload_header().has_id()) {
          file_name = std::to_string(frame.payload_header().id());
          // if custom_save_path is empty, default download path is used
          file_path = make_path(custom_save_path, parent_folder, file_name);
        } else {
          // This is an error condition, we don't have any way to generate a
          // file name for the output file.
          LOG(ERROR) << "File name not found in incoming file Payload, "
                        "and the Id wasn't found.";
          return {platform::Error(OperationResultCode::IO_FILE_OPENING_ERROR)};
        }
      }

      if (frame.payload_header().has_total_size()) {
        total_size = frame.payload_header().total_size();
      }
      absl::Time last_modified_time = absl::Now();
      if (frame.payload_header().has_last_modified_timestamp_millis()) {
        last_modified_time = absl::FromUnixMillis(
            frame.payload_header().last_modified_timestamp_millis());
        VLOG(1) << "Received last modified time: " << last_modified_time
                << " for file: " << file_name;
      }

      // These are ordered, the output file must be created first otherwise
      // there will be no input file to open.
      platform::OutputFile output_file(file_path);
      if (!output_file.IsValid()) {
        LOG(ERROR) << "Output file payload path is not valid: " << file_path;
        return {platform::Error(OperationResultCode::IO_FILE_OPENING_ERROR)};
      }
      return {std::make_unique<IncomingFileInternalPayload>(
          Payload(payload_id, parent_folder, file_name, platform::InputFile(file_path)),
          std::move(output_file), last_modified_time, total_size)};
    }
    default:
      DCHECK(false);  // This should never happen.
      return {platform::Error(OperationResultCode::DETAIL_UNKNOWN)};
  }
}

}  // namespace quickshare::connections
