file(MAKE_DIRECTORY "${TEST_DIR}")
file(COPY "${TEST_BINARY}" DESTINATION "${TEST_DIR}")
file(WRITE "${TEST_DIR}/settings.ini" "[Settings]\nhasAskedFileAssociation=1\ncheckUpdates=0\nlanguage=en\n")
get_filename_component(TEST_NAME "${TEST_BINARY}" NAME)
execute_process(COMMAND "${TEST_DIR}/${TEST_NAME}" --print-export-tests
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Print and export tests failed: ${result}")
endif()
