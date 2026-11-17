function(expect_cli_error expected_message)
    execute_process(
        COMMAND "${GSYSTEMCTL_BINARY}" ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT result EQUAL 2)
        message(FATAL_ERROR "Expected exit code 2, got '${result}'; stdout: ${stdout}; stderr: ${stderr}")
    endif()

    string(CONCAT output "${stdout}" "${stderr}")
    string(FIND "${output}" "${expected_message}" message_position)
    if(message_position EQUAL -1)
        message(FATAL_ERROR "Expected '${expected_message}' in output; got: ${output}")
    endif()

    string(FIND "${output}" "Usage: g-systemctl [options]" usage_position)
    if(usage_position EQUAL -1)
        message(FATAL_ERROR "Expected usage line in output; got: ${output}")
    endif()
endfunction()

expect_cli_error("unknown option: --bogus" --bogus)
expect_cli_error("option -f needs a value" -f)
expect_cli_error("unexpected argument: stray" stray)
