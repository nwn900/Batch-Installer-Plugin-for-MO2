# Locate the MO2 uibase SDK and expose an imported target.
#
# The plugin compiles against the uibase headers (iplugin.h, imoinfo.h,
# idownloadmanager.h, versioninfo.h) and links the uibase library at runtime
# through MO2. It must also see the batch interface headers in
# include/uibase of this project.
#
# Variables read:
#   MO2_UIBASE_PATH - root of the modorganizer-uibase source tree
#                     (contains include/uibase). Required.
#   MO2_SDK_DIR     - alternative: a folder that contains include/uibase.
#
# Defines:
#   BatchMo2::uibase - an INTERFACE library with the include directories.

if(NOT MO2_UIBASE_PATH AND NOT MO2_SDK_DIR)
  message(FATAL_ERROR
    "MO2 uibase not found. Pass -DMO2_UIBASE_PATH=<uibase source root> "
    "or -DMO2_SDK_DIR=<folder with include/uibase>.")
endif()

set(_uibase_root "${MO2_UIBASE_PATH}")
if(NOT _uibase_root)
  set(_uibase_root "${MO2_SDK_DIR}")
endif()

# uibase header layout differs across versions:
#   modern (master):   <root>/include/uibase/imoinfo.h  (plus <root>/include)
#   legacy (v2.5.x):   <root>/src/imoinfo.h
set(_uibase_include_dirs "")
if(EXISTS "${_uibase_root}/include/uibase/imoinfo.h")
  list(APPEND _uibase_include_dirs
    "${_uibase_root}/include"
    "${_uibase_root}/include/uibase")
elseif(EXISTS "${_uibase_root}/src/imoinfo.h")
  list(APPEND _uibase_include_dirs
    "${_uibase_root}/src"
    "${_uibase_root}/src/formatters"
    "${_uibase_root}/src/game_features")
else()
  message(FATAL_ERROR
    "uibase headers not found under ${_uibase_root} (neither "
    "include/uibase/imoinfo.h nor src/imoinfo.h). "
    "Check MO2_UIBASE_PATH / MO2_SDK_DIR.")
endif()

if(NOT TARGET BatchMo2::uibase)
  add_library(BatchMo2::uibase INTERFACE IMPORTED)
  target_include_directories(BatchMo2::uibase INTERFACE
    ${_uibase_include_dirs}
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
endif()

# UIBASE_IMPORT_LIB: path to uibase.lib (built uibase import library). Real
# MO2 plugin builds link the import lib that ships with the uibase build.
# When absent (header-only verification), link a stub import library instead
# so the plugin DLL can be produced locally.
if(NOT TARGET BatchMo2::uibase_impl)
  add_library(BatchMo2::uibase_impl INTERFACE IMPORTED)
  if(UIBASE_IMPORT_LIB)
    target_link_libraries(BatchMo2::uibase_impl INTERFACE "${UIBASE_IMPORT_LIB}")
  elseif(UIBASE_STUB_IMPORT_LIB)
    target_link_libraries(BatchMo2::uibase_impl INTERFACE "${UIBASE_STUB_IMPORT_LIB}")
  endif()
endif()
