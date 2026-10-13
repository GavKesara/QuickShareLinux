add_executable(nearby_sharing_cli nearby_sharing_cli.cc)
target_include_directories(nearby_sharing_cli PRIVATE "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}")
qs_link_group(nearby_sharing_cli
  qs_sharing_linux qs_sharing qs_connections qs_platform qs_base qs_proto ukey2 murmur3 protoutil)
target_link_libraries(nearby_sharing_cli PRIVATE qs_platform_linux ${QS_ABSL_LIBS})

install(TARGETS nearby_sharing_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
