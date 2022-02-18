# defines
#  MAGIC_FOUND
#  MAGIC_INCLUDE_DIRS
#  MAGIC_LIBRARIES

find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
	pkg_check_modules(PC_MAGIC QUIET magic)
endif()

set(MAGIC_DEFINITIONS ${PC_MAGIC_CFLAGS_OTHER})

find_path(MAGIC_INCLUDE_DIR magic.h
	PATHS ${PC_MAGIC_INCLUDEDIR} ${PC_MAGIC_INCLUDE_DIRS}
	PATH_SUFFIXES)

list(APPEND MAGIC_NAMES magic)

find_library(MAGIC_LIBRARY NAMES ${MAGIC_NAMES}
	PATHS ${PC_MAGIC_LIBDIR} ${PC_MAGIC_LIBRARY_DIRS})

set(MAGIC_LIBRARIES ${MAGIC_LIBRARY})
set(MAGIC_INCLUDE_DIRS ${MAGIC_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Magic DEFAULT_MSG
	MAGIC_LIBRARY MAGIC_INCLUDE_DIR)

mark_as_advanced(MAGIC_INCLUDE_DIR MAGIC_LIBRARY)