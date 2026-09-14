file(MAKE_DIRECTORY "${TEST_DIR}")
file(COPY "${TEST_BINARY}" DESTINATION "${TEST_DIR}")
file(WRITE "${TEST_DIR}/settings.ini" "[Settings]\nhasAskedFileAssociation=1\ncheckUpdates=0\n")
get_filename_component(TEST_NAME "${TEST_BINARY}" NAME)
execute_process(COMMAND "${TEST_DIR}/${TEST_NAME}" --wrapped-cursor-tests
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Wrapped cursor tests failed: ${result}")
endif()
