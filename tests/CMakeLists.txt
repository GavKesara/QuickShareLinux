include(GoogleTest)

# Not built, per the old Bazel test wiring (internal/platform/implementation/linux/BUILD
# impl_test is "notap" and omits these): bluetooth_adapter_test, executor_test,
# file_path_test, http_loader_test, preferences_manager_test,
# preferences_repository_test, submittable_executor_test, thread_pool_test.
# Their sources are not in this tree anyway.
#
# secure_message_wrapper_test.cc (ukey2) is excluded: it includes
# "src/main/proto/securemessage.pb.h", which does not match the generated layout.

# tests/platform/linux (old impl_test)
add_executable(platform_linux_tests
  platform/linux/atomic_boolean_test.cc
  platform/linux/atomic_reference_test.cc
  platform/linux/mutex_test.cc
  platform/linux/utils_test.cc
  platform/linux/wifi_direct_utils_test.cc
  platform/linux/ble_l2cap_socket_test.cc
  platform/linux/bluetooth_classic_socket_test.cc
  platform/linux/crypto_test.cc
  platform/linux/scheduled_executor_test.cc
  platform/linux/timer_test.cc)
target_include_directories(platform_linux_tests PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}")
qs_link_group(platform_linux_tests qs_platform qs_base)
target_link_libraries(platform_linux_tests PRIVATE qs_platform_linux GTest::gtest_main ${QS_ABSL_LIBS})
gtest_discover_tests(platform_linux_tests)

# tests/sharing/linux
add_executable(sharing_linux_tests sharing/linux/nearby_fast_init_manager_test.cc)
target_include_directories(sharing_linux_tests PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}")
qs_link_group(sharing_linux_tests qs_sharing_linux qs_sharing qs_connections qs_platform qs_base qs_proto ukey2 murmur3 protoutil)
target_link_libraries(sharing_linux_tests PRIVATE qs_platform_linux GTest::gtest_main ${QS_ABSL_LIBS})
gtest_discover_tests(sharing_linux_tests)

# tests/app
add_executable(app_tests app/native_file_logging_test.cc)
target_include_directories(app_tests PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}")
qs_link_group(app_tests qs_app qs_sharing_linux qs_sharing qs_connections qs_platform qs_base qs_proto ukey2 murmur3 protoutil)
target_link_libraries(app_tests PRIVATE qs_platform_linux GTest::gtest_main ${QS_ABSL_LIBS})
gtest_discover_tests(app_tests)

# tests/ui: links quickshare_core and the C API fake, not qs_app.
qt_add_executable(ui_tests
  ui/application_controller_test.cc
  ui/backend_test.cc
  ui/fake_quickshare.cc)
target_include_directories(ui_tests PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}")
target_link_libraries(ui_tests PRIVATE quickshare_core GTest::gtest_main)
gtest_discover_tests(ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# ukey2 (third_party/ukey2 upstream tests). Two binaries because test names may collide.
add_executable(ukey2_test
  ../third_party/ukey2/src/main/cpp/test/securegcm/d2d_connection_context_v1_test.cc
  ../third_party/ukey2/src/main/cpp/test/securegcm/d2d_crypto_ops_test.cc
  ../third_party/ukey2/src/main/cpp/test/securegcm/java_util_test.cc)
target_link_libraries(ukey2_test PRIVATE ukey2 GTest::gtest_main)
gtest_discover_tests(ukey2_test)

add_executable(ukey2_securemessage_test
  ../third_party/ukey2/src/securemessage/test/securemessage/builder_parser_test.cc
  ../third_party/ukey2/src/securemessage/test/securemessage/crypto_ops_test.cc
  ../third_party/ukey2/src/securemessage/test/securemessage/public_key_proto_util_test.cc
  ../third_party/ukey2/src/securemessage/test/securemessage/secure_message_parser_test.cc)
target_link_libraries(ukey2_securemessage_test PRIVATE ukey2 GTest::gtest_main)
gtest_discover_tests(ukey2_securemessage_test)
