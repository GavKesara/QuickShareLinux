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

#include "base/crypto/encryptor.h"

#include <stddef.h>
#include <stdint.h>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"  // nogncheck
#include "absl/log/log.h"  // nogncheck

#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "base/crypto/nearby_base.h"
#include "base/crypto/openssl_util.h"
#include "base/crypto/symmetric_key.h"
#include <openssl/evp.h>

namespace quickshare::base::crypto {

namespace {

const EVP_CIPHER* GetCipherForKey(const SymmetricKey* key) {
  switch (key->key().length()) {
    case 16:
      return EVP_aes_128_cbc();
    case 32:
      return EVP_aes_256_cbc();
    default:
      return nullptr;
  }
}

}  // namespace

/////////////////////////////////////////////////////////////////////////////
// Encryptor Implementation.

Encryptor::Encryptor() : key_(nullptr), mode_(CBC) {}

Encryptor::~Encryptor() = default;

bool Encryptor::Init(const SymmetricKey* key, Mode mode, absl::string_view iv) {
  return Init(key, mode, nearbybase::as_bytes(absl::MakeSpan(iv)));
}

bool Encryptor::Init(const SymmetricKey* key, Mode mode,
                     absl::Span<const uint8_t> iv) {
  DCHECK(key);
  DCHECK(mode == CBC || mode == CTR);

  EnsureOpenSSLInit();
  if (mode == CBC && iv.size() != 16) return false;
  // CTR mode passes the starting counter separately, via SetCounter().
  if (mode == CTR && !iv.empty()) return false;

  if (GetCipherForKey(key) == nullptr) return false;

  key_ = key;
  mode_ = mode;
  iv_.assign(iv.begin(), iv.end());
  return true;
}

bool Encryptor::Encrypt(absl::string_view plaintext, std::string* ciphertext) {
  DCHECK(!plaintext.empty() || mode_ == CBC);
  return CryptString(/*do_encrypt=*/true, plaintext, ciphertext);
}

bool Encryptor::Encrypt(absl::Span<const uint8_t> plaintext,
                        std::vector<uint8_t>* ciphertext) {
  DCHECK(!plaintext.empty() || mode_ == CBC);
  return CryptBytes(/*do_encrypt=*/true, plaintext, ciphertext);
}

bool Encryptor::Decrypt(absl::string_view ciphertext, std::string* plaintext) {
  DCHECK(!ciphertext.empty());
  return CryptString(/*do_encrypt=*/false, ciphertext, plaintext);
}

bool Encryptor::Decrypt(absl::Span<const uint8_t> ciphertext,
                        std::vector<uint8_t>* plaintext) {
  DCHECK(!ciphertext.empty());
  return CryptBytes(/*do_encrypt=*/false, ciphertext, plaintext);
}

bool Encryptor::SetCounter(absl::string_view counter) {
  return SetCounter(nearbybase::as_bytes(absl::MakeSpan(counter)));
}

bool Encryptor::SetCounter(absl::Span<const uint8_t> counter) {
  if (mode_ != CTR) return false;
  if (counter.size() != 16u) return false;

  iv_.assign(counter.begin(), counter.end());
  return true;
}

bool Encryptor::CryptString(bool do_encrypt, absl::string_view input,
                            std::string* output) {
  size_t out_size = MaxOutput(do_encrypt, input.size());
  CHECK_GT(out_size + 1, out_size);  // Overflow
  std::string result;
  uint8_t* out_ptr =
      reinterpret_cast<uint8_t*>(nearbybase::WriteInto(&result, out_size + 1));

  std::optional<size_t> len =
      (mode_ == CTR)
          ? CryptCTR(do_encrypt, nearbybase::as_bytes(absl::MakeSpan(input)),
                     absl::MakeSpan(out_ptr, out_size))
          : Crypt(do_encrypt, nearbybase::as_bytes(absl::MakeSpan(input)),
                  absl::MakeSpan(out_ptr, out_size));
  if (!len) return false;

  result.resize(*len);
  *output = std::move(result);
  return true;
}

bool Encryptor::CryptBytes(bool do_encrypt, absl::Span<const uint8_t> input,
                           std::vector<uint8_t>* output) {
  std::vector<uint8_t> result(MaxOutput(do_encrypt, input.size()));
  std::optional<size_t> len =
      (mode_ == CTR) ? CryptCTR(do_encrypt, input, absl::MakeSpan(result))
                     : Crypt(do_encrypt, input, absl::MakeSpan(result));
  if (!len) return false;

  result.resize(*len);
  *output = std::move(result);
  return true;
}

size_t Encryptor::MaxOutput(bool do_encrypt, size_t length) {
  size_t result = length + ((do_encrypt && mode_ == CBC) ? 16 : 0);
  CHECK_GE(result, length);  // Overflow
  return result;
}

std::optional<size_t> Encryptor::Crypt(bool do_encrypt,
                                       absl::Span<const uint8_t> input,
                                       absl::Span<uint8_t> output) {
  DCHECK(key_);  // Must call Init() before En/De-crypt.

  const EVP_CIPHER* cipher = GetCipherForKey(key_);
  DCHECK(cipher);  // Already handled in Init();

  const std::string& key = key_->key();
  DCHECK_EQ(EVP_CIPHER_iv_length(cipher), iv_.size());
  DCHECK_EQ(EVP_CIPHER_key_length(cipher), key.size());

  OpenSSLErrStackTracer err_tracer;
  ScopedEVP_CIPHER_CTX ctx(EVP_CIPHER_CTX_new());
  if (!ctx) return std::nullopt;
  if (!EVP_CipherInit_ex(ctx.get(), cipher, nullptr,
                         reinterpret_cast<const uint8_t*>(key.data()),
                         iv_.data(), do_encrypt)) {
    return std::nullopt;
  }

  // Encrypting needs a block size of space to allow for any padding.
  CHECK_GE(output.size(), input.size() + (do_encrypt ? iv_.size() : 0));
  int out_len;
  if (!EVP_CipherUpdate(ctx.get(), output.data(), &out_len, input.data(),
                        input.size()))
    return std::nullopt;

  // Write out the final block plus padding (if any) to the end of the data
  // just written.
  int tail_len;
  if (!EVP_CipherFinal_ex(ctx.get(), output.data() + out_len, &tail_len))
    return std::nullopt;

  out_len += tail_len;
  DCHECK_LE(out_len, static_cast<int>(output.size()));
  return out_len;
}

std::optional<size_t> Encryptor::CryptCTR(bool do_encrypt,
                                          absl::Span<const uint8_t> input,
                                          absl::Span<uint8_t> output) {
  if (iv_.size() != 16) {
    LOG(ERROR) << "Counter value not set in CTR mode.";
    return std::nullopt;
  }

  // |output| must have room for |input|.
  CHECK_GE(output.size(), input.size());
  const EVP_CIPHER* cipher =
      key_->key().length() == 16 ? EVP_aes_128_ctr() : EVP_aes_256_ctr();
  OpenSSLErrStackTracer err_tracer;
  ScopedEVP_CIPHER_CTX ctx(EVP_CIPHER_CTX_new());
  if (!ctx || !EVP_EncryptInit_ex(ctx.get(), cipher, nullptr,
                                  reinterpret_cast<const uint8_t*>(
                                      key_->key().data()),
                                  iv_.data())) {
    return std::nullopt;
  }
  // Like AES_ctr128_encrypt, the counter in |iv_| is advanced past the data
  // processed; the partial-block state is discarded.
  int out_len = 0;
  if (!input.empty() &&
      !EVP_EncryptUpdate(ctx.get(), output.data(), &out_len, input.data(),
                         static_cast<int>(input.size()))) {
    return std::nullopt;
  }
  const size_t blocks = (input.size() + 15) / 16;
  for (size_t i = 0; i < blocks; ++i) {
    for (int j = 15; j >= 0; --j) {
      if (++iv_[j] != 0) break;
    }
  }
  return input.size();
}

}  // namespace quickshare::base::crypto
