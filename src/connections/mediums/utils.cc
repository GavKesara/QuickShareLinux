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

#include "connections/mediums/utils.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "platform/api/base64_utils.h"
#include "platform/api/byte_array.h"
#include "platform/api/crypto.h" //NOLINT
#include "platform/api/prng.h"

namespace quickshare::connections {

namespace {
constexpr int kDefaultSaltLength = 16;
}  // namespace

platform::ByteArray Utils::GenerateRandomBytes(size_t length) {
  platform::Prng prng;
  std::string data;
  data.reserve(length);

  // Adds 4 random bytes per iteration.
  while (length > 0) {
    std::uint32_t val = prng.NextUint32();
    for (int i = 0; i < 4; i++) {
      data += val & 0xFF;
      val >>= 8;
      length--;

      if (!length) break;
    }
  }

  return platform::ByteArray(data);
}

platform::ByteArray Utils::Sha256Hash(const platform::ByteArray& source, size_t length) {
  return Utils::Sha256Hash(std::string(source), length);
}

platform::ByteArray Utils::Sha256Hash(const std::string& source, size_t length) {
  platform::ByteArray full_hash(length);
  full_hash.CopyAt(0, platform::Crypto::Sha256(source));
  return full_hash;
}

// Generates salts.
std::string Utils::GenerateSalt() { return GenerateSalt(kDefaultSaltLength); }

std::string Utils::GenerateSalt(size_t length) {
  platform::ByteArray salt = GenerateRandomBytes(length);
  return platform::Base64Utils::Encode(salt);
}

}  // namespace quickshare::connections
