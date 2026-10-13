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

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/log/check.h"  // nogncheck

#include "absl/strings/string_view.h"
#include "base/crypto/hkdf.h"
#include "base/crypto/hmac.h"
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>

namespace quickshare::base::crypto {

namespace {

bool Hkdf(const uint8_t* secret, size_t secret_len, const uint8_t* salt,
          size_t salt_len, const uint8_t* info, size_t info_len, uint8_t* out,
          size_t out_len) {
  static const uint8_t kEmpty[1] = {0};
  if (secret == nullptr) secret = kEmpty;
  if (salt == nullptr) salt = kEmpty;
  if (info == nullptr) info = kEmpty;
  std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> kdf(
      EVP_KDF_fetch(nullptr, "HKDF", nullptr), EVP_KDF_free);
  if (!kdf) return false;
  std::unique_ptr<EVP_KDF_CTX, decltype(&EVP_KDF_CTX_free)> ctx(
      EVP_KDF_CTX_new(kdf.get()), EVP_KDF_CTX_free);
  if (!ctx) return false;
  char digest[] = "SHA256";
  OSSL_PARAM params[] = {
      OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
      OSSL_PARAM_construct_octet_string(
          OSSL_KDF_PARAM_KEY, const_cast<uint8_t*>(secret), secret_len),
      OSSL_PARAM_construct_octet_string(
          OSSL_KDF_PARAM_SALT, const_cast<uint8_t*>(salt), salt_len),
      OSSL_PARAM_construct_octet_string(
          OSSL_KDF_PARAM_INFO, const_cast<uint8_t*>(info), info_len),
      OSSL_PARAM_construct_end()};
  return EVP_KDF_derive(ctx.get(), out, out_len, params) == 1;
}

}  // namespace

std::string HkdfSha256(absl::string_view secret, absl::string_view salt,
                       absl::string_view info, size_t derived_key_size) {
  std::string key;
  key.resize(derived_key_size);
  int result = Hkdf(reinterpret_cast<const uint8_t*>(secret.data()),
                    secret.size(),
                    reinterpret_cast<const uint8_t*>(salt.data()), salt.size(),
                    reinterpret_cast<const uint8_t*>(info.data()), info.size(),
                    reinterpret_cast<uint8_t*>(&key[0]), derived_key_size);
  DCHECK(result);
  return key;
}

std::vector<uint8_t> HkdfSha256(absl::Span<const uint8_t> secret,
                                absl::Span<const uint8_t> salt,
                                absl::Span<const uint8_t> info,
                                size_t derived_key_size) {
  std::vector<uint8_t> ret;
  ret.resize(derived_key_size);
  int result = Hkdf(secret.data(), secret.size(), salt.data(), salt.size(),
                    info.data(), info.size(), ret.data(), derived_key_size);
  DCHECK(result);
  return ret;
}

}  // namespace quickshare::base::crypto
