# ukey2 (securegcm + securemessage). Upstream Bazel targets are mirrored per BUILD_NOTES.md.
set(QS_UKEY2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ukey2")
add_library(ukey2 STATIC
  ukey2/src/main/cpp/src/securegcm/d2d_connection_context_v1.cc
  ukey2/src/main/cpp/src/securegcm/d2d_crypto_ops.cc
  ukey2/src/main/cpp/src/securegcm/java_util.cc
  ukey2/src/main/cpp/src/securegcm/ukey2_handshake.cc
  ukey2/src/securemessage/src/securemessage/byte_buffer.cc
  ukey2/src/securemessage/src/securemessage/crypto_ops.cc
  ukey2/src/securemessage/src/securemessage/raw_secure_message_parser.cc
  ukey2/src/securemessage/src/securemessage/util.cc
  ukey2/src/securemessage/src/securemessage/public_key_proto_util.cc
  ukey2/src/securemessage/src/securemessage/secure_message_builder.cc
  ukey2/src/securemessage/src/securemessage/secure_message_parser.cc
  ukey2/src/securemessage/src/securemessage/secure_message_wrapper.cc
  ukey2/src/securemessage/src/securemessage/crypto_ops_openssl.cc)
target_include_directories(ukey2 PUBLIC
  "${QS_UKEY2_DIR}"
  "${QS_UKEY2_DIR}/src/main/cpp/include"
  "${QS_UKEY2_DIR}/src/securemessage/include")
target_compile_options(ukey2 PRIVATE -Wno-deprecated-declarations)
target_link_libraries(ukey2 PUBLIC qs_ukey2_proto OpenSSL::Crypto absl::strings)

# murmur3 (MurmurHash3.cc, renamed from .cpp upstream).
add_library(murmur3 STATIC murmur3/MurmurHash3.cc)
target_include_directories(murmur3 PUBLIC "${PROJECT_SOURCE_DIR}")
target_compile_options(murmur3 PRIVATE -Wno-implicit-fallthrough)

add_library(protoutil STATIC protoutil/protoutil.cc)
target_include_directories(protoutil PUBLIC "${PROJECT_SOURCE_DIR}")
target_link_libraries(protoutil PUBLIC protobuf::libprotobuf ${QS_ABSL_LIBS})
