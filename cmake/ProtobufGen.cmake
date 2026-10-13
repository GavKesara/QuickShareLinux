set(QS_GEN_DIR "${CMAKE_BINARY_DIR}/gen")

set(QS_NEARBY_PROTO_FILES
  proto/connections/connections_enums.proto
  proto/connections/offline_wire_formats.proto
  proto/credentials/credential.proto
  proto/credentials/local_credential.proto
  proto/errorcode/error_code_enums.proto
  proto/mediums/ble_frames.proto
  proto/sharing/certificate_rpc.proto
  proto/sharing/contact_rpc.proto
  proto/sharing/device_rpc.proto
  proto/sharing/encrypted_metadata.proto
  proto/sharing/enums.proto
  proto/sharing/field_mask.proto
  proto/sharing/rpc_resources.proto
  proto/sharing/settings_observer_data.proto
  proto/sharing/sharing_enums.proto
  proto/sharing/timestamp.proto
  proto/sharing/wire_format.proto)

set(QS_UKEY2_PROTO_FILES
  device_to_device_messages.proto
  securegcm.proto
  securemessage.proto
  ukey.proto)

# Nearby protos import each other as "proto/...", so the import root is the
# repo root and headers land at gen/proto/<group>/<name>.pb.h.
function(qs_protobuf_generate)
  list(TRANSFORM QS_NEARBY_PROTO_FILES PREPEND "${PROJECT_SOURCE_DIR}/" OUTPUT_VARIABLE nearby_abs)
  protobuf_generate(
    LANGUAGE cpp
    OUT_VAR qs_nearby_proto_srcs
    PROTOC_OUT_DIR "${QS_GEN_DIR}"
    IMPORT_DIRS "${PROJECT_SOURCE_DIR}"
    PROTOS ${nearby_abs})
  add_library(qs_proto STATIC ${qs_nearby_proto_srcs})
  target_include_directories(qs_proto PUBLIC "${QS_GEN_DIR}")
  target_link_libraries(qs_proto PUBLIC protobuf::libprotobuf)

  # ukey2 protos are included bare ("ukey.pb.h") per BUILD_NOTES.md, so they are
  # generated at the root of gen/ with the ukey2 proto dir as import root.
  set(ukey2_proto_dir "${PROJECT_SOURCE_DIR}/third_party/ukey2/src/main/proto")
  list(TRANSFORM QS_UKEY2_PROTO_FILES PREPEND "${ukey2_proto_dir}/" OUTPUT_VARIABLE ukey2_abs)
  protobuf_generate(
    LANGUAGE cpp
    OUT_VAR qs_ukey2_proto_srcs
    PROTOC_OUT_DIR "${QS_GEN_DIR}"
    IMPORT_DIRS "${ukey2_proto_dir}"
    PROTOS ${ukey2_abs})
  add_library(qs_ukey2_proto STATIC ${qs_ukey2_proto_srcs})
  target_include_directories(qs_ukey2_proto PUBLIC "${QS_GEN_DIR}")
  target_link_libraries(qs_ukey2_proto PUBLIC protobuf::libprotobuf)
endfunction()
