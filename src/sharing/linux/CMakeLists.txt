set(QS_SHARING_LINUX_SOURCES
  nearby_fast_init_manager.cc
  platform/linux_account_manager.cc
  platform/linux_platform_components.cc
  platform/linux_preference_manager.cc
  platform/linux_sharing_platform.cc
  platform/platform_util.cc
)

add_library(qs_sharing_linux STATIC ${QS_SHARING_LINUX_SOURCES})
target_include_directories(qs_sharing_linux PUBLIC "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}")
target_link_libraries(qs_sharing_linux PUBLIC
  qs_sharing
  qs_platform_linux
  qs_proto
  nlohmann_json::nlohmann_json
  ${QS_ABSL_LIBS})
