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

#include "base/crypto/rsa_private_key.h"

#include <stdint.h>

#include <memory>
#include <utility>
#include <vector>

#include "absl/log/check.h"  // nogncheck

#include "absl/types/span.h"
#include "base/crypto/openssl_util.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

namespace quickshare::base::crypto {

// static
std::unique_ptr<RSAPrivateKey> RSAPrivateKey::Create(uint16_t num_bits) {
  OpenSSLErrStackTracer err_tracer;

  // Public exponent defaults to 65537.
  ScopedEVP_PKEY pkey(EVP_RSA_gen(num_bits));
  if (!pkey) return nullptr;

  std::unique_ptr<RSAPrivateKey> result(new RSAPrivateKey);
  result->key_ = std::move(pkey);
  return result;
}

// static
std::unique_ptr<RSAPrivateKey> RSAPrivateKey::CreateFromPrivateKeyInfo(
    absl::Span<const uint8_t> input) {
  OpenSSLErrStackTracer err_tracer;

  const uint8_t* p = input.data();
  OpenSSLUniquePtr<PKCS8_PRIV_KEY_INFO, PKCS8_PRIV_KEY_INFO_free> p8(
      d2i_PKCS8_PRIV_KEY_INFO(nullptr, &p, input.size()));
  if (!p8 || p != input.data() + input.size()) return nullptr;
  ScopedEVP_PKEY pkey(EVP_PKCS82PKEY(p8.get()));
  if (!pkey || EVP_PKEY_id(pkey.get()) != EVP_PKEY_RSA) return nullptr;

  std::unique_ptr<RSAPrivateKey> result(new RSAPrivateKey);
  result->key_ = std::move(pkey);
  return result;
}

// static
std::unique_ptr<RSAPrivateKey> RSAPrivateKey::CreateFromKey(EVP_PKEY* key) {
  DCHECK(key);
  if (EVP_PKEY_id(key) != EVP_PKEY_RSA) return nullptr;
  std::unique_ptr<RSAPrivateKey> copy(new RSAPrivateKey);
  copy->key_ = UpRefEVP_PKEY(key);
  return copy;
}

RSAPrivateKey::RSAPrivateKey() = default;

RSAPrivateKey::~RSAPrivateKey() = default;

std::unique_ptr<RSAPrivateKey> RSAPrivateKey::Copy() const {
  std::unique_ptr<RSAPrivateKey> copy(new RSAPrivateKey);
  copy->key_ = UpRefEVP_PKEY(key_.get());
  if (!copy->key_) return nullptr;
  return copy;
}

bool RSAPrivateKey::ExportPrivateKey(std::vector<uint8_t>* output) const {
  OpenSSLErrStackTracer err_tracer;
  OpenSSLUniquePtr<PKCS8_PRIV_KEY_INFO, PKCS8_PRIV_KEY_INFO_free> p8(
      EVP_PKEY2PKCS8(key_.get()));
  if (!p8) return false;
  int len = i2d_PKCS8_PRIV_KEY_INFO(p8.get(), nullptr);
  if (len <= 0) return false;
  output->resize(len);
  uint8_t* p = output->data();
  return i2d_PKCS8_PRIV_KEY_INFO(p8.get(), &p) == len;
}

bool RSAPrivateKey::ExportPublicKey(std::vector<uint8_t>* output) const {
  OpenSSLErrStackTracer err_tracer;
  int len = i2d_PUBKEY(key_.get(), nullptr);
  if (len <= 0) return false;
  output->resize(len);
  uint8_t* p = output->data();
  return i2d_PUBKEY(key_.get(), &p) == len;
}

}  // namespace quickshare::base::crypto
