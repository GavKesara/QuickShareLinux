// Copyright 2022 Google LLC
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

#include "sharing/domain/certificates/common.h"

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "absl/time/time.h"
#include "absl/types/span.h"
#include "base/crypto/encryptor.h"
#include "base/crypto/hkdf.h"
#include "base/crypto/symmetric_key.h"
#include "platform/api/crypto.h"
#include "sharing/domain/certificates/constants.h"
#include "sharing/api/public/logging.h"

namespace quickshare::sharing {

bool IsNearbyShareCertificateExpired(absl::Time current_time,
                                     absl::Time not_after,
                                     bool use_public_certificate_tolerance) {
  absl::Duration tolerance =
      use_public_certificate_tolerance
          ? kNearbySharePublicCertificateValidityBoundOffsetTolerance
          : absl::ZeroDuration();

  return current_time >= not_after + tolerance;
}

bool IsNearbyShareCertificateWithinValidityPeriod(
    absl::Time current_time, absl::Time not_before, absl::Time not_after,
    bool use_public_certificate_tolerance) {
  absl::Duration tolerance =
      use_public_certificate_tolerance
          ? kNearbySharePublicCertificateValidityBoundOffsetTolerance
          : absl::ZeroDuration();

  return current_time >= not_before - tolerance &&
         !IsNearbyShareCertificateExpired(current_time, not_after,
                                          use_public_certificate_tolerance);
}

std::vector<uint8_t> DeriveNearbyShareKey(absl::Span<const uint8_t> key,
                                          size_t new_num_bytes) {
  return base::crypto::HkdfSha256(key,
                            /*salt=*/absl::Span<const uint8_t>(),
                            /*info=*/absl::Span<const uint8_t>(),
                            new_num_bytes);
}

std::vector<uint8_t> ComputeAuthenticationTokenHash(
    absl::Span<const uint8_t> authentication_token,
    absl::Span<const uint8_t> secret_key) {
  return base::crypto::HkdfSha256(authentication_token, secret_key,
                            /*info=*/absl::Span<const uint8_t>(),
                            kNearbyShareNumBytesAuthenticationTokenHash);
}

std::vector<uint8_t> GenerateRandomBytes(size_t num_bytes) {
  std::vector<uint8_t> bytes(num_bytes);
  platform::RandBytes(absl::Span<uint8_t>(bytes));
  return bytes;
}

std::unique_ptr<base::crypto::Encryptor> CreateNearbyShareCtrEncryptor(
    const base::crypto::SymmetricKey* secret_key, absl::Span<const uint8_t> salt) {
  DCHECK(secret_key);
  DCHECK_EQ(kNearbyShareNumBytesSecretKey, secret_key->key().size());
  DCHECK_EQ(kNearbyShareNumBytesMetadataEncryptionKeySalt, salt.size());

  auto encryptor = std::make_unique<base::crypto::Encryptor>();

  // For CTR mode, the iv input to Init() must be empty. Instead, the iv is
  // set via SetCounter().
  if (!encryptor->Init(secret_key, base::crypto::Encryptor::Mode::CTR,
                       /*iv=*/absl::Span<const uint8_t>())) {
    LOG(ERROR) << "Encryptor could not be initialized.";
    return nullptr;
  }

  std::vector<uint8_t> iv =
      DeriveNearbyShareKey(salt, kNearbyShareNumBytesAesCtrIv);
  if (!encryptor->SetCounter(iv)) {
    LOG(ERROR) << "Could not set encryptor counter.";
    return nullptr;
  }

  return encryptor;
}

absl::Time FromJavaTime(int64_t ms_since_epoch) {
  return absl::UnixEpoch() + absl::Milliseconds(ms_since_epoch);
}

int64_t ToJavaTime(absl::Time time) {
  // Preserve 0 so the invalid result doesn't depend on the platform.
  if (time == absl::InfiniteFuture()) {
    return std::numeric_limits<int64_t>::max();
  } else if (time == absl::InfinitePast()) {
    return std::numeric_limits<int64_t>::min();
  } else {
    return (time - absl::UnixEpoch()) / absl::Milliseconds(1);
  }
}

}  // namespace quickshare::sharing
