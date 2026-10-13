# ukey2 build notes

Upstream Bazel files are not vendored. Targets below are mirrored from
`src/main/cpp/BUILD.bazel` and `src/securemessage/BUILD.bazel` (root `BUILD.bazel` defines the proto rules).

## a. Source files per library target

`securemessage_common` (`src/securemessage/BUILD.bazel`)
- srcs: `src/securemessage/src/securemessage/{byte_buffer,crypto_ops,raw_secure_message_parser,util}.cc`
- hdrs: `src/securemessage/include/securemessage/{byte_buffer,common,crypto_ops,raw_secure_message_parser,util}.h`
- srcs also list `secure_message_wrapper.h` (header, compile-only)
- deps: none beyond system libs

`securemessage_openssl` (crypto implementation)
- srcs: `src/securemessage/src/securemessage/crypto_ops_openssl.cc`
- deps: `securemessage_common`, system OpenSSL 3 `crypto` (`-lcrypto`)
- copts: `-Wno-deprecated-declarations` (still uses legacy `RSA`/`EC_KEY` APIs, deprecated since OpenSSL 3.0)
- `crypto_ops.cc` is the crypto-agnostic part of `CryptoOps` (HKDF, purpose derivation, Encrypt/Decrypt dispatch). It is only a partial implementation; `crypto_ops_openssl.cc` supplies the remaining primitives (SHA, HMAC, AES-CBC, ECDSA, ECDH, RSA). `crypto_ops_openssl.cc` is the one to link; do not link a second implementation.

`securemessage_cpp_protobuf` (protobuf-dependent code)
- srcs: `src/securemessage/src/securemessage/{public_key_proto_util,secure_message_builder,secure_message_parser,secure_message_wrapper}.cc`
- hdrs: `src/securemessage/include/securemessage/{public_key_proto_util,secure_message_builder,secure_message_parser,secure_message_wrapper}.h`
- deps: `securemessage_common`, `cc_proto`

`cpp` / `ukey2` (`src/main/cpp/BUILD.bazel`, alias `//:ukey2`)
- srcs: `src/main/cpp/src/securegcm/{d2d_connection_context_v1,d2d_crypto_ops,java_util,ukey2_handshake}.cc`
- hdrs: `src/main/cpp/include/securegcm/*.h`
- deps: `cc_proto`, `absl/strings:string_view`, `securemessage_common`, `securemessage_cpp_protobuf`, `securemessage_openssl`

Not built by the library: `src/main/cpp/src/securegcm/ukey2_shell.cc` (cc_binary; intentionally not vendored).

## b. Include directories

- `third_party/ukey2`: resolves `"src/securemessage/include/..."`, `"src/main/cpp/include/..."`
- `third_party/ukey2/src/main/cpp/include`: resolves `"securegcm/..."` (e.g. `securegcm/ukey2_handshake.h` as used by nearby code)
- `third_party/ukey2/src/securemessage/include`: resolves `"securemessage/..."` if a consumer uses that form (no upstream source does)
- `<protoc output dir>`: directory containing the generated `*.pb.h` (see c)
- Abseil, Protobuf (`protobuf/src`), OpenSSL 3 headers, GoogleTest for tests

## c. Proto import root and generated include strings

Bazel's `proto_library` uses `strip_import_prefix = 'src/main/proto/'`, so protoc is run with `-I src/main/proto`. Generated headers are therefore at the output root, and the expected include strings are bare:

- `ukey.pb.h`
- `securegcm.pb.h`
- `securemessage.pb.h`
- `device_to_device_messages.pb.h`

Proto imports: `device_to_device_messages.proto` imports `"securemessage.proto"`. The other three have no imports.

Equivalent protoc command:
`protoc -I third_party/ukey2/src/main/proto --cpp_out=<gen> third_party/ukey2/src/main/proto/{device_to_device_messages,securegcm,securemessage,ukey}.proto`

Known broken include: `src/securemessage/test/securemessage/secure_message_wrapper_test.cc:21` includes `"src/main/proto/securemessage.pb.h"`, which does not match the generated layout above. Not fixed here (vendoring only). Change it to `"securemessage.pb.h"` before building that test.

Consumers in this repo (`/home/suhasdissa/Projects/nearby`) do not include any `*.pb.h` directly. They include `securegcm/ukey2_handshake.h` (needs `-I .../src/main/cpp/include`) and `third_party/ukey2/src/main/cpp/include/securegcm/ukey2_handshake.h` (needs the repo-root `-I`). `ukey2_handshake.h` itself includes `"ukey.pb.h"`, so the proto output dir must be on the path too.

## d. Defines and copts

- `securemessage_openssl`: `-Wno-deprecated-declarations` (no `-DOPENSSL_IS_BORINGSSL`; the file targets OpenSSL 3)
- Global (from `.bazelrc`): `-std=c++17` (`--cxxopt` and `--host_cxxopt`)
- No per-target copts in the upstream `cpp` or `securemessage` targets.
