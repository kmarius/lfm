# # generate configuration header and update include directories
# configure_file (
#   "${PROJECT_SOURCE_DIR}/config/config.h.in"
#   "${PROJECT_BINARY_DIR}/config/auto/config.h"
#   )

configure_file (
  "${PROJECT_SOURCE_DIR}/config/versiondef.h.in"
  "${PROJECT_BINARY_DIR}/config/auto/versiondef.h"
  )

configure_file (
  "${PROJECT_SOURCE_DIR}/config/pathdef.c.in"
  "${PROJECT_BINARY_DIR}/config/auto/pathdef.c"
  ESCAPE_QUOTES)
