# defines
#  EV_FOUND
#  EV_INCLUDE_DIRS
#  EV_LIBRARIES

find_package(PkgConfig)
if (PKG_CONFIG_FOUND)
	pkg_check_modules(PC_EV QUIET ev)
endif()

set(EV_DEFINITIONS ${PC_EV_CFLAGS_OTHER})

find_path(EV_INCLUDE_DIR ev.h
	PATHS ${PC_EV_INCLUDEDIR} ${PC_EV_INCLUDE_DIRS}
	PATH_SUFFIXES)

list(APPEND EV_NAMES ev)

find_library(EV_LIBRARY NAMES ${EV_NAMES}
	PATHS ${PC_EV_LIBDIR} ${PC_EV_LIBRARY_DIRS})

set(EV_LIBRARIES ${EV_LIBRARY})
set(EV_INCLUDE_DIRS ${EV_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Ev DEFAULT_MSG
	EV_LIBRARY EV_INCLUDE_DIR)

mark_as_advanced(EV_INCLUDE_DIR EV_LIBRARY)
