# Names of the packaged desktop files. Only these change when the app id changes.
set(QS_APP_ID "top.suhasdissa.QuickShareLinux")
set(QS_DESKTOP_SRC "${PROJECT_SOURCE_DIR}/packaging/${QS_APP_ID}.desktop")
set(QS_METAINFO_SRC "${PROJECT_SOURCE_DIR}/packaging/${QS_APP_ID}.metainfo.xml")
set(QS_ICON_SRC "${PROJECT_SOURCE_DIR}/packaging/${QS_APP_ID}.svg")

add_library(quickshare_core STATIC
  application_controller.cc
  backend.cc
  file_dialogs.cc
  firewall.cc)
target_include_directories(quickshare_core PUBLIC "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}")
target_link_libraries(quickshare_core PUBLIC
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::DBus)

set(quickshare_sources main.cc resources.qrc)
# Google Sans is proprietary and is not in the repo. A local copy is embedded
# when ui/fonts/googlesans_var.ttf is present.
set(QS_FONT "${CMAKE_CURRENT_SOURCE_DIR}/fonts/googlesans_var.ttf")
if(EXISTS "${QS_FONT}")
  set(QS_FONT_QRC "${CMAKE_CURRENT_BINARY_DIR}/googlesans.qrc")
  file(WRITE "${QS_FONT_QRC}"
"<!DOCTYPE RCC><RCC version=\"1.0\">
<qresource prefix=\"/\">
    <file alias=\"googlesans_var.ttf\">${QS_FONT}</file>
</qresource>
</RCC>
")
  list(APPEND quickshare_sources "${QS_FONT_QRC}")
endif()
qt_add_executable(quickshare ${quickshare_sources})
# Fallback QML dir used by ui/main.cc when NEARBY_QML_SOURCE_DIR is unset.
# Packaged builds pass an empty value so the binary does not contain the
# build machine's source path.
set(QUICKSHARE_QML_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qml" CACHE STRING
  "QML directory used when NEARBY_QML_SOURCE_DIR is unset")
target_compile_definitions(quickshare PRIVATE
  "QUICKSHARE_QML_SOURCE_DIR=\"${QUICKSHARE_QML_SOURCE_DIR}\"")
target_link_libraries(quickshare PRIVATE quickshare_core)
qs_link_group(quickshare qs_app qs_sharing_linux qs_sharing qs_connections qs_platform qs_base qs_proto ukey2 murmur3 protoutil)
target_link_libraries(quickshare PRIVATE qs_platform_linux ${QS_ABSL_LIBS})
set_target_properties(quickshare PROPERTIES WIN32_EXECUTABLE OFF)

install(TARGETS quickshare RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${QS_DESKTOP_SRC}" DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES "${QS_METAINFO_SRC}" DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
# Flatpak's build-export only picks up icons named after the app id.
install(FILES "${QS_ICON_SRC}"
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
