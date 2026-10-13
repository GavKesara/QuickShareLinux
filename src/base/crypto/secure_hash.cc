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

#include "base/crypto/secure_hash.h"

#include <stddef.h>

#include <memory>

#include "base/crypto/openssl_util.h"
#include <openssl/evp.h>
#include <openssl/sha.h>

namespace quickshare::base::crypto {

namespace {

class SecureHashSHA256 : public SecureHash {
 public:
  SecureHashSHA256() : ctx_(EVP_MD_CTX_new()) {
    EnsureOpenSSLInit();
    EVP_DigestInit_ex(ctx_.get(), EVP_sha256(), nullptr);
  }

  SecureHashSHA256(const SecureHashSHA256& other) : ctx_(EVP_MD_CTX_new()) {
    EVP_MD_CTX_copy_ex(ctx_.get(), other.ctx_.get());
  }

  ~SecureHashSHA256() override = default;

  void Update(const void* input, size_t len) override {
    EVP_DigestUpdate(ctx_.get(), input, len);
  }

  void Finish(void* output, size_t len) override {
    ScopedOpenSSLSafeSizeBuffer<SHA256_DIGEST_LENGTH> result(
        static_cast<unsigned char*>(output), len);
    EVP_DigestFinal_ex(ctx_.get(), result.safe_buffer(), nullptr);
  }

  std::unique_ptr<SecureHash> Clone() const override {
    return std::make_unique<SecureHashSHA256>(*this);
  }

  size_t GetHashLength() const override { return SHA256_DIGEST_LENGTH; }

 private:
  ScopedEVP_MD_CTX ctx_;
};

}  // namespace

std::unique_ptr<SecureHash> SecureHash::Create(Algorithm algorithm) {
  switch (algorithm) {
    case SHA256:
      return std::make_unique<SecureHashSHA256>();
    default:
      //  NOTIMPLEMENTED();
      return nullptr;
  }
}

}  // namespace quickshare::base::crypto
