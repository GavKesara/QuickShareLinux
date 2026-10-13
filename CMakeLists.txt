cmake_minimum_required(VERSION 3.24)
project(QuickShareLinux VERSION 0.1.0 LANGUAGES C CXX)

option(QUICKSHARE_BUILD_TESTS "Build the unit tests" ON)
option(QUICKSHARE_BUILD_CLI "Build the nearby_sharing_cli tool" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Required: under gnu++ the predefined macro `linux` breaks `namespace linux`.
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fvisibility-inlines-hidden>")

include(GNUInstallDirs)
include(cmake/Dependencies.cmake)
include(cmake/ProtobufGen.cmake)
include(cmake/DBusCodegen.cmake)

qt_standard_project_setup(REQUIRES 6.10)
set(CMAKE_AUTORCC ON)

if(QUICKSHARE_BUILD_TESTS)
  enable_testing()
endif()

qs_protobuf_generate()
qs_dbus_codegen(TARGET qs_dbus_generated XML_DIR "${PROJECT_SOURCE_DIR}/src/platform/linux/dbus/xml")

add_subdirectory(third_party)
add_subdirectory(src/base)
add_subdirectory(src/platform/api)
add_subdirectory(src/platform/linux)
add_subdirectory(src/connections)
add_subdirectory(src/sharing)
add_subdirectory(src/sharing/linux)
add_subdirectory(src/app)
if(QUICKSHARE_BUILD_CLI)
  add_subdirectory(tools/cli)
endif()
add_subdirectory(ui)

if(QUICKSHARE_BUILD_TESTS)
  add_subdirectory(tests)
endif()

install(FILES
  LICENSE
  LICENSE.Apache-2.0
  NOTICE
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/quickshare-linux)
