set(QS_APP_SOURCES
  native_file_logging.cc
  quickshare.cc
)

add_library(qs_app STATIC ${QS_APP_SOURCES})
target_include_directories(qs_app PUBLIC "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}")
target_link_libraries(qs_app PUBLIC
  qs_sharing_linux
  qs_sharing
  qs_connections
  qs_platform_linux
  qs_platform
  qs_base
  qs_proto
  ${QS_ABSL_LIBS})
