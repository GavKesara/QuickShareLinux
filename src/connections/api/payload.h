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

#ifndef CORE_PAYLOAD_H_
#define CORE_PAYLOAD_H_

#include <cstddef>
#include <memory>
#include <string>
#include <variant>

#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "connections/api/payload_type.h"
#include "platform/api/byte_array.h"
#include "platform/api/file.h"
#include "platform/api/input_stream.h"
#include "platform/api/payload_id.h"

namespace quickshare::connections {

// Payload is default-constructible, and moveable, but not copyable container
// that holds at most one instance of one of:
// ByteArray, InputStream, or InputFile.
class Payload {
 public:
  using Id = platform::PayloadId;
  // Order of types in variant, and values in Type enum is important.
  // Enum values must match respective variant types.
  using Content = std::variant<std::monostate, platform::ByteArray,
                               std::unique_ptr<platform::InputStream>, platform::InputFile>;

  Payload(Payload&& other) noexcept;
  ~Payload();
  Payload& operator=(Payload&& other) noexcept;

  // Default (invalid) payload.
  Payload();

  // Constructors for outgoing payloads.
  explicit Payload(platform::ByteArray&& bytes);
  explicit Payload(const platform::ByteArray& bytes);

  // InputFile is just "a pointer to a file on your disc", a wrapper around a
  // file name or file descriptor. It has no understanding that Nearby is going
  // to create a copy of it on the remote device.
  //
  // FileName and ParentFolder are what Nearby is saying the remote device
  // should save this incoming payload as.
  //
  // Notably, FileName does not have to be respected (you could ask to save it
  // as "photo.png" but instead the recipient saves it as "photo (1).png").
  //
  // ParentFolder must be a relative path, not a full path.

  explicit Payload(std::string parent_folder, std::string file_name,
                   platform::InputFile file);

  explicit Payload(std::unique_ptr<platform::InputStream> stream);

  // Constructors for incoming payloads.
  Payload(Id id, platform::ByteArray&& bytes);
  Payload(Id id, const platform::ByteArray& bytes);
  Payload(Id id, platform::InputFile file);
  Payload(Id id, std::string parent_folder, std::string file_name,
          platform::InputFile input_file);
  Payload(Id id, std::unique_ptr<platform::InputStream> stream);

  // Returns ByteArray payload, if it has been defined, or empty ByteArray.
  const platform::ByteArray& AsBytes() const&;
  // Returns InputStream* payload, if it has been defined, or nullptr.
  platform::InputStream* AsStream();
  // Returns InputFile* payload, if it has been defined, or nullptr.
  platform::InputFile* AsFile();

  // Returns Payload unique ID.
  Id GetId() const;

  // Returns Payload type.
  PayloadType GetType() const;

  // Sets the payload offset in bytes
  void SetOffset(size_t offset);

  size_t GetOffset();

  // Generate Payload Id; to be passed to outgoing file constructor.
  static Id GenerateId();

  const std::string& GetFileName() const;
  const std::string& GetParentFolder() const;
  absl::Time GetLastModifiedTime() const { return last_modified_time_; }

 private:
  void InitFilePayload(Id id, std::string parent_folder, std::string file_name,
                       platform::InputFile input_file);
  PayloadType FindType() const;

  Id id_{GenerateId()};
  size_t offset_{0};

  std::string parent_folder_;
  std::string file_name_;
  absl::Time last_modified_time_ = absl::Now();

  PayloadType type_{FindType()};
  Content content_;
};

}  // namespace quickshare::connections

#endif  // CORE_PAYLOAD_H_
