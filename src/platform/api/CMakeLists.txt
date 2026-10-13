set(QS_PLATFORM_API_SOURCES
  base64_utils.cc
  ble.cc
  ble_connection_info.cc
  blocking_queue_stream.cc
  bluetooth_classic.cc
  bluetooth_connection_info.cc
  byte_utils.cc
  cancellation_flag.cc
  clock_impl.cc
  connection_info.cc
  credential_storage_impl.cc
  error_code_recorder.cc
  file.cc
  implementation/wifi_utils.cc
  input_stream.cc
  mac_address.cc
  monitored_runnable.cc
  pending_job_registry.cc
  pipe.cc
  prng.cc
  service_address.cc
  shared/count_down_latch.cc
  shared/crypto.cc
  shared/file.cc
  stream_reader.cc
  stream_writer.cc
  task_runner_impl.cc
  timer_impl.cc
  uuid.cc
  wifi_direct.cc
  wifi_hotspot.cc
  wifi_lan.cc
  wifi_lan_connection_info.cc
)

add_library(qs_platform STATIC ${QS_PLATFORM_API_SOURCES})
target_include_directories(qs_platform PUBLIC "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}")
target_link_libraries(qs_platform PUBLIC qs_base qs_proto OpenSSL::Crypto ${QS_ABSL_LIBS})
