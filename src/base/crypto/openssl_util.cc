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

#include "base/crypto/openssl_util.h"

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "absl/log/log.h"  // nogncheck
#include "absl/strings/string_view.h"
#include <openssl/crypto.h>
#include <openssl/err.h>

namespace quickshare::base::crypto {

void EnsureOpenSSLInit() {
  // OpenSSL 3 initializes itself lazily and thread-safely.
  OPENSSL_init_crypto(0, nullptr);
}

void ClearOpenSSLERRStack() {
    ERR_clear_error();
}

}  // namespace quickshare::base::crypto
