if(NOT DEFINED TEST_BINARY OR NOT DEFINED TEST_DIR OR NOT DEFINED TEST_FIXTURE)
    message(FATAL_ERROR "TEST_BINARY, TEST_DIR and TEST_FIXTURE are required")
endif()
file(MAKE_DIRECTORY "${TEST_DIR}")
file(COPY "${TEST_BINARY}" DESTINATION "${TEST_DIR}")
# Portable mode keeps the sheet views and recents in the test folder
file(WRITE "${TEST_DIR}/settings.ini" "[Settings]
hasAskedFileAssociation=1
checkUpdates=0
")
get_filename_component(TEST_NAME "${TEST_BINARY}" NAME)
# The test works on a copy of the fixture it makes itself
execute_process(COMMAND "${TEST_DIR}/${TEST_NAME}" --sheet-tests "${TEST_FIXTURE}"
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Sheet tests failed: ${result}")
endif()
