find_package(absl CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(OpenSSL 3 REQUIRED)
find_package(sdbus-c++ CONFIG REQUIRED)
find_package(sdbus-c++-tools CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(BLUEZ REQUIRED IMPORTED_TARGET bluez)
find_package(Threads REQUIRED)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Widgets Quick Qml DBus)

if(QUICKSHARE_BUILD_TESTS)
  find_package(GTest REQUIRED)
endif()

set(QS_ABSL_LIBS
  absl::strings absl::status absl::statusor absl::time absl::synchronization
  absl::flat_hash_map absl::flat_hash_set absl::node_hash_map absl::btree
  absl::log absl::check absl::random_random absl::any_invocable absl::span
  absl::base absl::hash absl::str_format absl::cleanup)

# Cycle handling: qs_platform (api) calls ImplementationPlatform::Create*, which
# is defined in qs_platform_linux, and qs_platform_linux depends back on
# qs_platform. qs_platform_linux is an OBJECT library so its objects (including
# crypto.cc and system_clock.cc, which nothing references by name) are always
# linked, matching the old Bazel alwayslink. The remaining archives form cycles
# between each other, so every executable and test wraps them in LINK_GROUP
# RESCAN via qs_link_group().
function(qs_link_group target)
  target_link_libraries(${target} PRIVATE "$<LINK_GROUP:RESCAN,${ARGN}>")
endfunction()
