cmake_minimum_required(VERSION 3.5.1)
project(lfm)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_COMPILER /usr/bin/clang)

set(CMAKE_C_FLAGS "-std=gnu11 -Wall -Wextra -g -pthread -Wvla")
set(CMAKE_C_FLAGS_DEBUG "-DDEBUG -O2")
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O2 -flto")

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C_FLAGS: ${CMAKE_C_FLAGS}")
message(STATUS "LIBRARAY PATH: ${CMAKE_LIBRARY_PATH}")

file(GLOB LFM_SRC src/*.c /src/*.h)
add_executable(lfm ${LFM_SRC})

find_library(LIB_PTHREAD pthread libpthread REQUIRED)
find_library(LIB_NOTCURSES notcurses REQUIRED)
find_library(LIB_NOTCURSES_CORE notcurses-core REQUIRED)
find_library(LIB_EV ev REQUIRED)

include_directories(/usr/include/luajit-2.0)
find_library(LIB_LUAJIT luajit-5.1 REQUIRED)

target_link_libraries(lfm
	${LIB_PTHREAD}
	${LIB_EV}
	${LIB_LUAJIT}
	${LIB_NOTCURSES}
	${LIB_NOTCURSES_CORE}
	)

install(TARGETS lfm DESTINATION bin)
install(DIRECTORY lua DESTINATION share/lfm)
