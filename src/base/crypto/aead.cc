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

#include "base/crypto/aead.h"

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <vector>

#include "absl/log/check.h"  // nogncheck

#include "absl/types/span.h"
#include "base/crypto/nearby_base.h"
#include "base/crypto/openssl_util.h"
#include <openssl/evp.h>
#include <string.h>

namespace quickshare::base::crypto {

namespace {

constexpr size_t kTagLength = 16;
constexpr size_t kNonceLength = 12;

}  // namespace

Aead::Aead(AeadAlgorithm algorithm) : algorithm_(algorithm), cipher_(nullptr) {
  EnsureOpenSSLInit();
  switch (algorithm) {
    case AES_256_GCM:
      cipher_ = EVP_aes_256_gcm();
      break;
    case CHACHA20_POLY1305:
      cipher_ = EVP_chacha20_poly1305();
      break;
    case AES_128_CTR_HMAC_SHA256:
    case AES_256_GCM_SIV:
      // Not provided by OpenSSL; Seal/Open fail for these.
      cipher_ = nullptr;
      break;
  }
}

Aead::~Aead() = default;

void Aead::Init(absl::Span<const uint8_t> key) {
  DCHECK(!key_);
  DCHECK_EQ(KeyLength(), key.size());
  key_ = key;
}

static absl::Span<const uint8_t> ToSpan(absl::string_view sp) {
  return nearbybase::as_bytes(absl::MakeSpan(sp));
}

void Aead::Init(const std::string* key) { Init(ToSpan(*key)); }

std::vector<uint8_t> Aead::Seal(
    absl::Span<const uint8_t> plaintext, absl::Span<const uint8_t> nonce,
    absl::Span<const uint8_t> additional_data) const {
  const size_t max_output_length =
      kTagLength + plaintext.size();
  CHECK(max_output_length >= plaintext.size());
  std::vector<uint8_t> ret;
  ret.resize(max_output_length);

  size_t output_length;
  CHECK(Seal(plaintext, nonce, additional_data, ret.data(), &output_length,
             max_output_length));
  ret.resize(output_length);
  return ret;
}

bool Aead::Seal(absl::string_view plaintext, absl::string_view nonce,
                absl::string_view additional_data,
                std::string* ciphertext) const {
  const size_t max_output_length =
      kTagLength + plaintext.size();
  CHECK(max_output_length + 1 >= plaintext.size());
  uint8_t* out_ptr = reinterpret_cast<uint8_t*>(
      nearbybase::WriteInto(ciphertext, max_output_length + 1));

  size_t output_length;
  if (!Seal(ToSpan(plaintext), ToSpan(nonce), ToSpan(additional_data), out_ptr,
            &output_length, max_output_length)) {
    ciphertext->clear();
    return false;
  }

  ciphertext->resize(output_length);
  return true;
}

std::optional<std::vector<uint8_t>> Aead::Open(
    absl::Span<const uint8_t> ciphertext, absl::Span<const uint8_t> nonce,
    absl::Span<const uint8_t> additional_data) const {
  const size_t max_output_length = ciphertext.size();
  std::vector<uint8_t> ret;
  ret.resize(max_output_length);

  size_t output_length;
  if (!Open(ciphertext, nonce, additional_data, ret.data(), &output_length,
            max_output_length)) {
    return std::nullopt;
  }

  ret.resize(output_length);
  return ret;
}

bool Aead::Open(absl::string_view ciphertext, absl::string_view nonce,
                absl::string_view additional_data,
                std::string* plaintext) const {
  const size_t max_output_length = ciphertext.size();
  CHECK(max_output_length + 1 > max_output_length);
  uint8_t* out_ptr = reinterpret_cast<uint8_t*>(
      nearbybase::WriteInto(plaintext, max_output_length + 1));

  size_t output_length;
  if (!Open(ToSpan(ciphertext), ToSpan(nonce), ToSpan(additional_data), out_ptr,
            &output_length, max_output_length)) {
    plaintext->clear();
    return false;
  }

  plaintext->resize(output_length);
  return true;
}

size_t Aead::KeyLength() const {
  return algorithm_ == AES_128_CTR_HMAC_SHA256 ? 48 : 32;
}

size_t Aead::NonceLength() const { return kNonceLength; }

bool Aead::Seal(absl::Span<const uint8_t> plaintext,
                absl::Span<const uint8_t> nonce,
                absl::Span<const uint8_t> additional_data, uint8_t* out,
                size_t* output_length, size_t max_output_length) const {
  DCHECK(key_);
  DCHECK_EQ(NonceLength(), nonce.size());
  if (cipher_ == nullptr || key_->size() != KeyLength() ||
      nonce.size() != kNonceLength ||
      max_output_length < plaintext.size() + kTagLength) {
    return false;
  }
  OpenSSLErrStackTracer err_tracer;
  ScopedEVP_CIPHER_CTX ctx(EVP_CIPHER_CTX_new());
  if (!ctx) return false;

  int len = 0;
  if (!EVP_EncryptInit_ex(ctx.get(), cipher_, nullptr, nullptr, nullptr) ||
      !EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_IVLEN, kNonceLength,
                           nullptr) ||
      !EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key_->data(),
                          nonce.data())) {
    return false;
  }
  if (!additional_data.empty() &&
      !EVP_EncryptUpdate(ctx.get(), nullptr, &len, additional_data.data(),
                         static_cast<int>(additional_data.size()))) {
    return false;
  }
  size_t total = 0;
  if (!plaintext.empty()) {
    if (!EVP_EncryptUpdate(ctx.get(), out, &len, plaintext.data(),
                           static_cast<int>(plaintext.size()))) {
      return false;
    }
    total = len;
  }
  if (!EVP_EncryptFinal_ex(ctx.get(), out + total, &len)) return false;
  total += len;
  if (!EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_GET_TAG, kTagLength,
                           out + total)) {
    return false;
  }
  *output_length = total + kTagLength;
  DCHECK_LE(*output_length, max_output_length);
  return true;
}

bool Aead::Open(absl::Span<const uint8_t> ciphertext,
                absl::Span<const uint8_t> nonce,
                absl::Span<const uint8_t> additional_data, uint8_t* out,
                size_t* output_length, size_t max_output_length) const {
  DCHECK(key_);
  DCHECK_EQ(NonceLength(), nonce.size());
  if (cipher_ == nullptr || key_->size() != KeyLength() ||
      nonce.size() != kNonceLength || ciphertext.size() < kTagLength) {
    return false;
  }
  const size_t body_len = ciphertext.size() - kTagLength;
  if (max_output_length < body_len) return false;
  OpenSSLErrStackTracer err_tracer;
  ScopedEVP_CIPHER_CTX ctx(EVP_CIPHER_CTX_new());
  if (!ctx) return false;

  int len = 0;
  uint8_t tag[kTagLength];
  memcpy(tag, ciphertext.data() + body_len, kTagLength);
  if (!EVP_DecryptInit_ex(ctx.get(), cipher_, nullptr, nullptr, nullptr) ||
      !EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_IVLEN, kNonceLength,
                           nullptr) ||
      !EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key_->data(),
                          nonce.data())) {
    return false;
  }
  if (!additional_data.empty() &&
      !EVP_DecryptUpdate(ctx.get(), nullptr, &len, additional_data.data(),
                         static_cast<int>(additional_data.size()))) {
    return false;
  }
  size_t total = 0;
  if (body_len != 0) {
    if (!EVP_DecryptUpdate(ctx.get(), out, &len, ciphertext.data(),
                           static_cast<int>(body_len))) {
      return false;
    }
    total = len;
  }
  if (!EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_AEAD_SET_TAG, kTagLength, tag) ||
      EVP_DecryptFinal_ex(ctx.get(), out + total, &len) <= 0) {
    return false;
  }
  total += len;
  *output_length = total;
  DCHECK_LE(*output_length, max_output_length);
  return true;
}

}  // namespace quickshare::base::crypto
