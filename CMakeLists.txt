cmake_minimum_required(VERSION 3.5.1)
project(lfm)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Point CMake at any custom modules we may ship
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS "-Wall -Wextra -Wvla -pthread")
set(CMAKE_C_FLAGS_DEBUG "-DDEBUG -O2 -g")
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O2 -flto -g")


if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C_FLAGS: ${CMAKE_C_FLAGS}")

set(DEPS_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/.deps/usr")

list(INSERT CMAKE_PREFIX_PATH 0 ${DEPS_PREFIX})
set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${DEPS_PREFIX}/lib/pkgconfig")

if(NOT EXISTS ${DEPS_PREFIX})
    message(STATUS "Building dependencies...")
	set(DEPS_BUILD_DIR ${PROJECT_SOURCE_DIR}/.deps)
    file(MAKE_DIRECTORY ${DEPS_BUILD_DIR})
    execute_process(
      COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR}
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_C_FLAGS=${CMAKE_C_FLGS}
        -DCMAKE_C_FLAGS_DEBUG=${CMAKE_C_FLAGS_DEBUG}
        -DCMAKE_C_FLAGS_MINSIZEREL=${CMAKE_C_FLAGS_MINSIZEREL}
        -DCMAKE_C_FLAGS_RELWITHDEBINFO=${CMAKE_C_FLAGS_RELWITHDEBINFO}
        -DCMAKE_C_FLAGS_RELEASE=${CMAKE_C_FLAGS_RELEASE}
        -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
        ${PROJECT_SOURCE_DIR}/third-party
      WORKING_DIRECTORY ${DEPS_BUILD_DIR})
    execute_process(
      COMMAND ${CMAKE_COMMAND} --build ${DEPS_BUILD_DIR}
        --config ${CMAKE_BUILD_TYPE})
    set(DEPS_PREFIX ${DEPS_BUILD_DIR}/usr)
endif()

file(GLOB LFM_SOURCES src/*.c /src/*.h)

option(DEV "dev" OFF)

# how to use ${CMAKE_INSTALL_FULL_DATAROOTDIR}
set(LFM_DATA_DIR ${CMAKE_INSTALL_PREFIX}/share/lfm)

if(DEV STREQUAL "ON")
	set(LFM_LUA_DIR "${CMAKE_SOURCE_DIR}/src/lua")
else()
	set(LFM_LUA_DIR "${LFM_DATA_DIR}/lua")
endif()

message(STATUS "lua dir: ${LFM_LUA_DIR}")
message(STATUS "data dir: ${LFM_DATA_DIR}")

include(GetGitRevisionDescription)
git_describe(GIT_SHA1)
git_revcount(GIT_REVCOUNT)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(LFM_VERSION "r${GIT_REVCOUNT}.${GIT_SHA1}-debug")
else()
	set(LFM_VERSION "r${GIT_REVCOUNT}.${GIT_SHA1}")
endif()

add_subdirectory(config)
list(APPEND LFM_GENERATED_SOURCES
  "${PROJECT_BINARY_DIR}/config/auto/pathdef.c"
)

add_executable(lfm ${LFM_SOURCES} ${LFM_GENERATED_SOURCES})

find_package(Notcurses REQUIRED)
include_directories(SYSTEM ${NOTCURSES_INCLUDE_DIRS})
set(NOTCURSES_DEPS m unistring ncursesw)

find_package(LuaJit)
include_directories(${LUAJIT_INCLUDE_DIRS})

include_directories(build/config)

find_package(Magic REQUIRED)
find_package(Pcre REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Ev REQUIRED)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
set(PTHREAD_LIBRARY Threads::Threads)

target_link_libraries(lfm
	PRIVATE
	${EV_LIBRARY}
	${LUAJIT_LIBRARY}
	${NOTCURSES_LIBRARY}
	${NOTCURSES_CORE_LIBRARY}
	${NOTCURSES_DEPS}
	${ZLIB_LIBRARY}
	${EV_LIBRARY}
	${PTHREAD_LIBRARY}
	${MAGIC_LIBRARY}
	${PCRE_LIBRARY})

install(TARGETS lfm DESTINATION bin)
install(DIRECTORY src/lua DESTINATION share/lfm)
