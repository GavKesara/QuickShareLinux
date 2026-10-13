set(QS_BASE_SOURCES
  base/file_path.cc
  base/files.cc
  base/masker.cc
  crypto/aead.cc
  crypto/ec_private_key.cc
  crypto/ec_signature_creator.cc
  crypto/ec_signature_creator_impl.cc
  crypto/encryptor.cc
  crypto/hkdf.cc
  crypto/hmac.cc
  crypto/nearby_base.cc
  crypto/openssl_util.cc
  crypto/rsa_private_key.cc
  crypto/secure_hash.cc
  crypto/secure_util.cc
  crypto/sha2.cc
  crypto/signature_verifier.cc
  crypto/symmetric_key.cc
  flags/nearby_flags.cc
  network/url.cc
  network/utils.cc
)

add_library(qs_base STATIC ${QS_BASE_SOURCES})
target_include_directories(qs_base PUBLIC "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}")
target_link_libraries(qs_base PUBLIC OpenSSL::Crypto ${QS_ABSL_LIBS})

set(QS_BASE_CRYPTO_SOURCES ${QS_BASE_SOURCES})
list(FILTER QS_BASE_CRYPTO_SOURCES INCLUDE REGEX "^crypto/")
set_source_files_properties(${QS_BASE_CRYPTO_SOURCES} PROPERTIES COMPILE_DEFINITIONS CRYPTO_IMPLEMENTATION)

set(QS_BASE_NETWORK_SOURCES ${QS_BASE_SOURCES})
list(FILTER QS_BASE_NETWORK_SOURCES INCLUDE REGEX "^network/")
set_source_files_properties(${QS_BASE_NETWORK_SOURCES} PROPERTIES COMPILE_DEFINITIONS _SILENCE_CLANG_COROUTINE_MESSAGE)
