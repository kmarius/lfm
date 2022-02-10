# https://github.com/neovim/neovim/blob/master/third-party/cmake/RemoveFiles.cmake

file(GLOB_RECURSE FILES_TO_REMOVE ${REMOVE_FILE_GLOB})

if(FILES_TO_REMOVE)
  file(REMOVE ${FILES_TO_REMOVE})
endif()
