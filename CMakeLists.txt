cmake_minimum_required(VERSION 3.5.1)
project(lfm)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Point CMake at any custom modules we may ship
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_COMPILER /usr/bin/clang)

set(CMAKE_C_FLAGS "-Wall -Wextra -Wvla -pthread")
set(CMAKE_C_FLAGS_DEBUG "-DDEBUG -O2 -g")
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O2 -flto -g")

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C_FLAGS: ${CMAKE_C_FLAGS}")

file(GLOB LFM_SOURCES src/*.c /src/*.h)
add_executable(lfm ${LFM_SOURCES})

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

find_package(Notcurses REQUIRED)
include_directories(SYSTEM ${NOTCURSES_INCLUDE_DIRS})
set(NOTCURSES_DEPS m unistring ncursesw)

find_package(LuaJit)
include_directories(${LUAJIT_INCLUDE_DIRS})

find_package(ZLIB)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
set(PTHREAD_LIBRARY Threads::Threads)

set(EV_LIBRARY ev)

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
	magic
	pcre)

install(TARGETS lfm DESTINATION bin)
install(DIRECTORY lua DESTINATION share/lfm)
