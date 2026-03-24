if(NOT DEFINED INPUT OR NOT DEFINED OUTPUT)
    message(STATUS "[vigilant] Symlink step skipped: INPUT/OUTPUT not provided.")
    return()
endif()

if(NOT EXISTS "${INPUT}")
    message(STATUS "[vigilant] Symlink step skipped: input binary not found at '${INPUT}'.")
    return()
endif()

get_filename_component(OUTPUT_DIR "${OUTPUT}" DIRECTORY)
if(NOT IS_DIRECTORY "${OUTPUT_DIR}")
    message(STATUS "[vigilant] Symlink step skipped: output directory '${OUTPUT_DIR}' does not exist.")
    return()
endif()

set(PROBE_FILE "${OUTPUT_DIR}/.vigilant_symlink_probe")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E touch "${PROBE_FILE}"
    RESULT_VARIABLE PROBE_RESULT
    OUTPUT_QUIET
    ERROR_QUIET
)

if(NOT PROBE_RESULT EQUAL 0)
    message(STATUS "[vigilant] Symlink step skipped: no write permission for '${OUTPUT_DIR}'.")
    return()
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${PROBE_FILE}"
    OUTPUT_QUIET
    ERROR_QUIET
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E rm -f "${OUTPUT}"
    OUTPUT_QUIET
    ERROR_QUIET
)

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E create_symlink "${INPUT}" "${OUTPUT}"
    RESULT_VARIABLE LINK_RESULT
    OUTPUT_QUIET
    ERROR_QUIET
)

if(LINK_RESULT EQUAL 0)
    message(STATUS "[vigilant] Global command symlink created: ${OUTPUT} -> ${INPUT}")
else()
    message(STATUS "[vigilant] Symlink step skipped: failed to create '${OUTPUT}'.")
endif()
