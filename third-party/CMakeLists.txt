# https://github.com/neovim/neovim/blob/master/third-party/CMakeLists.txt

# This is not meant to be included by the top-level.
cmake_minimum_required (VERSION 2.8.12)
project(LFM_DEPS C)

# Needed for: check_c_compiler_flag()
include(CheckCCompilerFlag)

# Point CMake at any custom modules we may ship
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" "${PROJECT_SOURCE_DIR}/../cmake")

# In Windows/MSVC CMAKE_BUILD_TYPE changes the paths/linking of the build
# recipes (libuv, msgpack), make sure it is set
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(DEFAULT_MAKE_CFLAGS CFLAGS+=-g)

check_c_compiler_flag(-Og HAS_OG_FLAG)
if(HAS_OG_FLAG)
  set(DEFAULT_MAKE_CFLAGS CFLAGS+=-Og ${DEFAULT_MAKE_CFLAGS})
endif()

set(DEPS_INSTALL_DIR "${CMAKE_BINARY_DIR}/usr" CACHE PATH "Dependencies install directory.")

set(DEPS_BIN_DIR "${DEPS_INSTALL_DIR}/bin" CACHE PATH "Dependencies binary install directory.")
set(DEPS_LIB_DIR "${DEPS_INSTALL_DIR}/lib" CACHE PATH "Dependencies library install directory.")
set(DEPS_BUILD_DIR "${CMAKE_BINARY_DIR}/build" CACHE PATH "Dependencies build directory.")
set(DEPS_DOWNLOAD_DIR "${DEPS_BUILD_DIR}/downloads" CACHE PATH "Dependencies download directory.")

option(USE_BUNDLED "Use bundled dependencies." ON)

option(USE_BUNDLED_NOTCURSES "Use the bundled notcurses." ${USE_BUNDLED})
# option(USE_BUNDLED_LUAJIT "Use the bundled version of luajit." ${USE_BUNDLED})
# option(USE_BUNDLED_LIBEV "Use the bundled version of libev." ${USE_BUNDLED})

option(USE_EXISTING_SRC_DIR "Skip download of deps sources in case of existing source directory." OFF)

find_program(MAKE_PRG NAMES gmake make)
if(MAKE_PRG)
  execute_process(
    COMMAND "${MAKE_PRG}" --version
    OUTPUT_VARIABLE MAKE_VERSION_INFO)
  if(NOT "${OUTPUT_VARIABLE}" MATCHES ".*GNU.*")
    unset(MAKE_PRG)
  endif()
endif()
if(NOT MAKE_PRG)
  message(FATAL_ERROR "GNU Make is required to build the dependencies.")
else()
  message(STATUS "Found GNU Make at ${MAKE_PRG}")
endif()

# When using make, use the $(MAKE) variable to avoid warning about the job
# server.
if(CMAKE_GENERATOR MATCHES "Makefiles")
  set(MAKE_PRG "$(MAKE)")
endif()

if(CMAKE_C_COMPILER_ARG1)
  set(DEPS_C_COMPILER "${CMAKE_C_COMPILER} ${CMAKE_C_COMPILER_ARG1}")
else()
  set(DEPS_C_COMPILER "${CMAKE_C_COMPILER}")
endif()

# Cross compiling: use these for dependencies built for the
# HOST system, when not crosscompiling these should be the
# same as DEPS_*. Except when targeting Unix in which case
# want all the dependencies to use the same compiler.
if(CMAKE_CROSSCOMPILING AND NOT UNIX)
  set(HOSTDEPS_INSTALL_DIR "${CMAKE_BINARY_DIR}/host")
  set(HOSTDEPS_BIN_DIR "${HOSTDEPS_INSTALL_DIR}/bin")
  set(HOSTDEPS_LIB_DIR "${HOSTDEPS_INSTALL_DIR}/lib")
  set(HOSTDEPS_C_COMPILER "${HOST_C_COMPILER}")
  set(HOSTDEPS_CXX_COMPILER "${HOST_CXX_COMPILER}")
else()
  set(HOSTDEPS_INSTALL_DIR "${DEPS_INSTALL_DIR}")
  set(HOSTDEPS_BIN_DIR "${DEPS_BIN_DIR}")
  set(HOSTDEPS_LIB_DIR "${DEPS_LIB_DIR}")
  set(HOSTDEPS_C_COMPILER "${DEPS_C_COMPILER}")
  set(HOSTDEPS_CXX_COMPILER "${DEPS_CXX_COMPILER}")
endif()

include(ExternalProject)

set(LIBEV_URL http://dist.schmorp.de/libev/libev-4.33.tar.gz)
set(LIBEV_SHA256 507eb7b8d1015fbec5b935f34ebed15bf346bed04a11ab82b8eee848c4205aea)

# https://github.com/LuaJIT/LuaJIT/tree/v2.1
set(LUAJIT_URL https://github.com/LuaJIT/LuaJIT/archive/1d20f33d2905db55fb7191076bbac10f570f9175.tar.gz)
set(LUAJIT_SHA256 4b6e2fc726ed8bf51fe461d90db9fffbc5c894bbdc862a67d17a70190f1fb07f)

set(NOTCURSES_URL https://github.com/dankamongmen/notcurses/archive/refs/tags/v3.0.10.tar.gz)
set(NOTCURSES_SHA256 f35fc0916afaa8978c23c386f0c107e4597db6e0a0511c33375fc09080887512)

if(USE_BUNDLED_LIBEV)
  include(BuildLibev)
endif()

if(USE_BUNDLED_LUAJIT)
  include(BuildLuajit)
endif()

if(USE_BUNDLED_NOTCURSES)
  include(BuildNotcurses)
endif()

add_custom_target(clean-shared-libraries
  COMMAND ${CMAKE_COMMAND}
    -DREMOVE_FILE_GLOB=${DEPS_INSTALL_DIR}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}*${CMAKE_SHARED_LIBRARY_SUFFIX}*
    -P ${PROJECT_SOURCE_DIR}/cmake/RemoveFiles.cmake
  DEPENDS ${THIRD_PARTY_DEPS}
)
set(ALL_DEPS clean-shared-libraries)

add_custom_target(third-party ALL
  COMMAND ${CMAKE_COMMAND} -E touch .third-party
  DEPENDS ${ALL_DEPS}
)
