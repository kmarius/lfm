cmake_minimum_required(VERSION 3.5.1)
project(lfm)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Point CMake at any custom modules we may ship
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS "-Wall -Wextra -Wvla -Werror=implicit -pthread -rdynamic")
set(CMAKE_C_FLAGS_DEBUG "-DDEBUG -Og -g")
set(CMAKE_C_FLAGS_ASAN "${CMAKE_C_FLAGS_DEBUG}")
set(CMAKE_C_FLAGS_UBSAN "${CMAKE_C_FLAGS_DEBUG} -O1")
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG -O2 -flto -g")

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

# if asan errors on program exit, run with: ASAN_OPTIONS=use_sigaltstack=0 ./lfm
if (CMAKE_BUILD_TYPE STREQUAL "Asan")
	add_compile_options(-fsanitize=address)
	add_link_options(-fsanitize=address)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "UBSan")
	add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
	add_link_options(-fsanitize=undefined -fno-omit-frame-pointer)
endif()

# We use this to remove the source prefix from the __FILE__ macro
# -ffile-prefix-map should work but it breaks lookups of functions from luajit's ffi
string(LENGTH "${CMAKE_SOURCE_DIR}/" SOURCE_PATH_SIZE)
add_definitions("-DSOURCE_PATH_SIZE=${SOURCE_PATH_SIZE}")

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
option(DEV "dev" OFF)

set(LUA_PRG luajit)
set(LUAC_PRG "${LUA_PRG} -b -s %s -")

# how to use ${CMAKE_INSTALL_FULL_DATAROOTDIR}
set(LFM_DATA_DIR ${CMAKE_INSTALL_PREFIX}/share/lfm)

if(DEV STREQUAL "ON")
	set(LFM_LUA_DIR "${CMAKE_SOURCE_DIR}/runtime/lua")
else()
	set(LFM_LUA_DIR "${LFM_DATA_DIR}/runtime/lua")
endif()

message(STATUS "lua dir: ${LFM_LUA_DIR}")
message(STATUS "data dir: ${LFM_DATA_DIR}")

include(GetGitRevisionDescription)
git_describe(GIT_SHA1)
git_revcount(GIT_REVCOUNT)
git_get_branch(GIT_BRANCH)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(LFM_VERSION "r${GIT_REVCOUNT}.${GIT_SHA1}-debug")
else()
	set(LFM_VERSION "r${GIT_REVCOUNT}.${GIT_SHA1}")
endif()

set(LFM_COMMIT "${GIT_SHA1}")
set(LFM_REVCOUNT "${GIT_REVCOUNT}")
set(LFM_BRANCH "${GIT_BRANCH}")

file(GLOB LFM_SOURCES ${PROJECT_SOURCE_DIR}/src/*.c ${PROJECT_SOURCE_DIR}/src/lua/*.c)
file(GLOB LFM_HEADERS ${PROJECT_SOURCE_DIR}/src/*.h ${PROJECT_SOURCE_DIR}/src/lua/*.h)

add_subdirectory(config)
list(APPEND LFM_GENERATED_SOURCES
  "${PROJECT_BINARY_DIR}/config/auto/pathdef.c"
)
include_directories(${PROJECT_BINARY_DIR}/config)

# lua module pre-compilation
set(CHAR_BLOB_GENERATOR ${PROJECT_SOURCE_DIR}/gen/gen_char_blob.lua)
set(LFM_MODULE_FILE ${PROJECT_BINARY_DIR}/lua/lfm_module.generated.h)
set(LUA_COLORS_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/colors.lua)
set(LUA_COMPL_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/compl.lua)
set(LUA_FLATTEN_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/flatten.lua)
set(LUA_FUNCTIONS_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/functions.lua)
set(LUA_GLOB_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/glob.lua)
set(LUA_INSPECT_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/inspect.lua)
set(LUA_JUMPLIST_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/jumplist.lua)
set(LUA_QUICKMARKS_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/quickmarks.lua)
set(LUA_RIFLE_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/rifle.lua)
set(LUA_MODES_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/modes.lua)
set(LUA_SHELL_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/shell.lua)
set(LUA_UTIL_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/util.lua)
set(LUA_CORE_MODULE_SOURCE ${PROJECT_SOURCE_DIR}/runtime/lua/lfm/core.lua)

file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/lua)
add_custom_command(
	OUTPUT ${LFM_MODULE_FILE}
	COMMAND ${CMAKE_COMMAND} -E env
	"LUAC_PRG=${LUAC_PRG}"
	${LUA_PRG} ${CHAR_BLOB_GENERATOR} -c ${LFM_MODULE_FILE}
	${LUA_COLORS_MODULE_SOURCE} "lfm.colors"
	${LUA_COMPL_MODULE_SOURCE} "lfm.compl"
	${LUA_FLATTEN_MODULE_SOURCE} "lfm.flatten"
	${LUA_FUNCTIONS_MODULE_SOURCE} "lfm.functions"
	${LUA_GLOB_MODULE_SOURCE} "lfm.glob"
	${LUA_INSPECT_MODULE_SOURCE} "lfm.inspect"
	${LUA_JUMPLIST_MODULE_SOURCE} "lfm.jumplist"
	${LUA_QUICKMARKS_MODULE_SOURCE} "lfm.quickmarks"
	${LUA_RIFLE_MODULE_SOURCE} "lfm.rifle"
  ${LUA_MODES_MODULE_SOURCE} "lfm.modes"
	${LUA_SHELL_MODULE_SOURCE} "lfm.shell"
	${LUA_UTIL_MODULE_SOURCE} "lfm.util"
	${LUA_CORE_MODULE_SOURCE} "lfm._core"
	DEPENDS
	${CHAR_BLOB_GENERATOR}
	${LUA_COLORS_MODULE_SOURCE}
	${LUA_COMPL_MODULE_SOURCE}
	${LUA_FLATTEN_MODULE_SOURCE}
	${LUA_FUNCTIONS_MODULE_SOURCE}
	${LUA_GLOB_MODULE_SOURCE}
	${LUA_INSPECT_MODULE_SOURCE}
	${LUA_JUMPLIST_MODULE_SOURCE}
	${LUA_QUICKMARKS_MODULE_SOURCE}
	${LUA_RIFLE_MODULE_SOURCE}
  ${LUA_MODES_MODULE_SOURCE}
	${LUA_SHELL_MODULE_SOURCE}
	${LUA_UTIL_MODULE_SOURCE}
	${LUA_CORE_MODULE_SOURCE}
	VERBATIM
	)
include_directories(${PROJECT_BINARY_DIR})
list(APPEND LFM_GENERATED_SOURCES "${LFM_MODULE_FILE}")

add_executable(lfm ${LFM_SOURCES} ${LFM_HEADERS} ${LFM_GENERATED_SOURCES})

find_package(Notcurses REQUIRED)
include_directories(SYSTEM ${NOTCURSES_INCLUDE_DIRS})

find_package(LuaJit)
include_directories(${LUAJIT_INCLUDE_DIRS})

find_package(Magic REQUIRED)
find_package(Pcre REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Ev REQUIRED)
find_package(FFmpeg REQUIRED COMPONENTS AVCODEC AVFORMAT AVUTIL SWSCALE)
set(NOTCURSES_DEPS m unistring ncursesw readline deflate)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
set(PTHREAD_LIBRARY Threads::Threads)

target_link_libraries(lfm
	PRIVATE
	${EV_LIBRARY}
	${FFMPEG_LIBRARIES}
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
install(DIRECTORY runtime DESTINATION share/lfm)
install(DIRECTORY doc/EmmyLua DESTINATION share/lfm)
install(FILES runtime/preview.sh DESTINATION share/lfm/runtime
  PERMISSIONS
  OWNER_READ OWNER_WRITE OWNER_EXECUTE
  GROUP_READ GROUP_EXECUTE
  WORLD_READ WORLD_EXECUTE)
