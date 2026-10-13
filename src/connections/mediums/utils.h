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

#ifndef CORE_INTERNAL_MEDIUMS_UTILS_H_
#define CORE_INTERNAL_MEDIUMS_UTILS_H_

#include <cstddef>
#include <string>

#include "platform/api/byte_array.h"

namespace quickshare::connections {

class Utils {
 public:
  static platform::ByteArray GenerateRandomBytes(size_t length);
  static platform::ByteArray Sha256Hash(const platform::ByteArray& source, size_t length);
  static platform::ByteArray Sha256Hash(const std::string& source, size_t length);
  static std::string GenerateSalt();
  static std::string GenerateSalt(size_t length);
};

}  // namespace quickshare::connections

#endif  // CORE_INTERNAL_MEDIUMS_UTILS_H_
