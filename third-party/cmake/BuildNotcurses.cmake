# BuildNotcurses(TARGET targetname CONFIGURE_COMMAND ... BUILD_COMMAND ... INSTALL_COMMAND ...)
# Reusable function to build notcurses, wraps ExternalProject_Add.
# Failing to pass a command argument will result in no command being run
function(BuildNotcurses)
  cmake_parse_arguments(_notcurses
  ""
  "TARGET"
  "PATCH_COMMAND;CONFIGURE_COMMAND;BUILD_COMMAND;INSTALL_COMMAND"
  ${ARGN})
  if(NOT _notcurses_CONFIGURE_COMMAND AND NOT _notcurses_BUILD_COMMAND
   AND NOT _notcurses_INSTALL_COMMAND)
    message(FATAL_ERROR "Must pass at least one of CONFIGURE_COMMAND, BUILD_COMMAND, INSTALL_COMMAND")
  endif()
  if(NOT _notcurses_TARGET)
    set(_notcurses_TARGET "notcurses")
  endif()

  ExternalProject_Add(${_notcurses_TARGET}
  PREFIX ${DEPS_BUILD_DIR}
  URL ${NOTCURSES_URL}
  DOWNLOAD_DIR ${DEPS_DOWNLOAD_DIR}/notcurses
  DOWNLOAD_COMMAND ${CMAKE_COMMAND}
  -DPREFIX=${DEPS_BUILD_DIR}
  -DDOWNLOAD_DIR=${DEPS_DOWNLOAD_DIR}/notcurses
  -DURL=${NOTCURSES_URL}
  -DEXPECTED_SHA256=${NOTCURSES_SHA256}
  -DTARGET=${_notcurses_TARGET}
  -DUSE_EXISTING_SRC_DIR=${USE_EXISTING_SRC_DIR}
  -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/DownloadAndExtractFile.cmake
  PATCH_COMMAND "${_notcurses_PATCH_COMMAND}"
  CONFIGURE_COMMAND "${_notcurses_CONFIGURE_COMMAND}"
  BUILD_IN_SOURCE 1
  BUILD_COMMAND "${_notcurses_BUILD_COMMAND}"
  INSTALL_COMMAND "${_notcurses_INSTALL_COMMAND}")

endfunction()

set(NO_STACK_CHECK "")
set(AMD64_ABI "")

try_compile(avframe_has_duration "${CMAKE_BINARY_DIR}/temp" ${CMAKE_CURRENT_SOURCE_DIR}/patches/test_avframe_has_duration.c)
set(NOTCURSES_PATCH_COMMAND
 patch -p0 < ${CMAKE_CURRENT_SOURCE_DIR}/patches/notcurses-bad-kitty.patch &&
 patch -p0 < ${CMAKE_CURRENT_SOURCE_DIR}/patches/notcurses-focus.patch)

set(NOTCURSES_CONFIGURE_COMMAND
 ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR}
 ${DEPS_BUILD_DIR}/src/notcurses
 -DCMAKE_BUILD_TYPE=RelWithDebInfo
 -DCMAKE_INSTALL_PREFIX=${DEPS_INSTALL_DIR}
 -DUSE_DOCTEST=off
 -DUSE_QRCODEGEN=off
 -DUSE_GPM=off
 -DUSE_CXX=off
 -DUSE_DOXYGEN=off
 -DUSE_PANDOC=off
 -DBUILD_EXECUTABLES=off
 -DBUILD_FFI_LIBRARY=off
 -DUSE_POC=off
 -DUSE_MULTIMEDIA=ffmpeg
 -DUSE_STATIC=on)

set(NOTCURSES_BUILD_COMMAND
 ${CMAKE_COMMAND} --build .)

set(NOTCURSES_INSTALL_COMMAND
 ${CMAKE_COMMAND} --build . --target install)

BuildNotcurses(PATCH_COMMAND ${NOTCURSES_PATCH_COMMAND}
 CONFIGURE_COMMAND ${NOTCURSES_CONFIGURE_COMMAND}
    BUILD_IN_SOURCE 1
 BUILD_COMMAND ${NOTCURSES_BUILD_COMMAND}
 INSTALL_COMMAND ${NOTCURSES_INSTALL_COMMAND})
