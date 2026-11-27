# Run by CPack once a package's files are staged, before it is made
# (CPACK_PRE_BUILD_SCRIPTS, cmake/SeventhSlidePackaging.cmake): no file or
# folder in it is writable but by its owner (root, once installed). The
# staged copies keep the modes of where they came from (Qt's folder) and of
# the umask of the build, which may let the group write.
execute_process(
    COMMAND chmod -R go-w "${CPACK_TEMPORARY_INSTALL_DIRECTORY}"
    RESULT_VARIABLE _ss_result
)
if(_ss_result)
    message(FATAL_ERROR "chmod on ${CPACK_TEMPORARY_INSTALL_DIRECTORY} failed: ${_ss_result}")
endif()
