cmake_minimum_required(VERSION 3.16)
project(batchdownloadinstaller VERSION 1.2.0 LANGUAGES CXX C)

# One plugin DLL per Qt family (plan 2.2, 16.1). Qt 5 builds target MO2
# 2.4.4; Qt 6 builds target MO2 2.5.x and later. Never copy one DLL between
# the families.

option(BUILD_QT6 "Build against Qt 6 (modern MO2). Off = Qt 5 (legacy MO2)." ON)

if(BUILD_QT6)
  set(CMAKE_AUTOMOC ON)
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
  set(_qt_libs Qt6::Core Qt6::Widgets)
else()
  set(CMAKE_AUTOMOC ON)
  find_package(Qt5 REQUIRED COMPONENTS Core Widgets)
  set(_qt_libs Qt5::Core Qt5::Widgets)
endif()

include(cmake/SelectMo2Sdk.cmake)

add_library(BatchDownloadInstaller SHARED
  src/batchdownloadinstallerplugin.h
  src/batchdownloadinstallerplugin.cpp
  src/batchselection.h
  src/batchselection.cpp
  src/batchprogresspanel.h
  src/batchprogresspanel.cpp
  src/batchrefreshgate.h
  src/batchrefreshgate.cpp
  src/qt_compat.h
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hde/hde32.c
  third_party/minhook/src/hde/hde64.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  src/plugin.json
  src/version.rc
)

set(BATCH_CXX_STANDARD 20 CACHE STRING
  "C++ standard for the plugin build (20 for current uibase, 17 for legacy MO2 2.4.4 uibase)")

target_include_directories(BatchDownloadInstaller PRIVATE
  src
  third_party/minhook/include)
target_link_libraries(BatchDownloadInstaller PRIVATE
  BatchMo2::uibase BatchMo2::uibase_impl ${_qt_libs})

# When no uibase import library is available, compile VersionInfo into the plugin
# and stop treating uibase types as dllimport (local linkage for stub builds).
if(NOT UIBASE_IMPORT_LIB AND NOT UIBASE_STUB_IMPORT_LIB)
  target_sources(BatchDownloadInstaller PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/stub_versioninfo.cpp")
  target_compile_definitions(BatchDownloadInstaller PRIVATE _NODLL)
endif()

set_target_properties(BatchDownloadInstaller PROPERTIES
  CXX_STANDARD ${BATCH_CXX_STANDARD}
  CXX_STANDARD_REQUIRED ON
  PREFIX ""
)

# Qt 5 / Qt 6 family-specific artifact name (plan 2.2).
if(BUILD_QT6)
  set_target_properties(BatchDownloadInstaller PROPERTIES
    OUTPUT_NAME "BatchDownloadInstaller-qt6")
else()
  set_target_properties(BatchDownloadInstaller PROPERTIES
    OUTPUT_NAME "BatchDownloadInstaller-qt5")
endif()

if(WIN32)
  target_compile_definitions(BatchDownloadInstaller PRIVATE
    UNICODE _UNICODE NOMINMAX)
  target_link_libraries(BatchDownloadInstaller PRIVATE
    bcrypt crypt32 advapi32)
endif()

set_source_files_properties(
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hde/hde32.c
  third_party/minhook/src/hde/hde64.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  PROPERTIES LANGUAGE C)
