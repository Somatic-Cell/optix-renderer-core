if(NOT DEFINED TRIANGLE_EXECUTABLE OR NOT EXISTS "${TRIANGLE_EXECUTABLE}")
    message(FATAL_ERROR "triangle_cli was not found; build the triangle_cli target first.")
endif()

if(NOT DEFINED TRIANGLE_SMOKE_CONFIG OR NOT EXISTS "${TRIANGLE_SMOKE_CONFIG}")
    message(FATAL_ERROR "The triangle smoke configuration was not found.")
endif()

get_filename_component(_smoke_directory "${TRIANGLE_SMOKE_CONFIG}" DIRECTORY)
set(_smoke_png "${_smoke_directory}/triangle_smoke.png")

# A previous run must not satisfy the output-file check.
file(REMOVE "${_smoke_png}")
if(EXISTS "${_smoke_png}")
    message(FATAL_ERROR "Cannot remove the previous smoke-test PNG: ${_smoke_png}")
endif()

execute_process(
    COMMAND "${TRIANGLE_EXECUTABLE}" --config "${TRIANGLE_SMOKE_CONFIG}"
    WORKING_DIRECTORY "${_smoke_directory}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    TIMEOUT 110
)

message(STATUS "${_stdout}")
if(NOT _stderr STREQUAL "")
    message(STATUS "triangle_cli stderr:\n${_stderr}")
endif()
if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "triangle_cli failed: ${_result}")
endif()
if(NOT _stdout MATCHES "Triangle check: PASS")
    message(FATAL_ERROR "The analytic triangle check did not report success.")
endif()
if(NOT _stdout MATCHES "Readback check: PASS")
    message(FATAL_ERROR "The RGB8 readback check did not report success.")
endif()
if(NOT EXISTS "${_smoke_png}")
    message(FATAL_ERROR "triangle_cli did not create its output PNG.")
endif()

# PNG signature, IHDR length/type, 800 x 600, RGB8, standard compression/filter,
# and no interlacing. The optional diagnostic checked eight downloaded pixels.
file(READ "${_smoke_png}" _png_header LIMIT 29 HEX)
string(TOLOWER "${_png_header}" _png_header)
if(NOT _png_header STREQUAL "89504e470d0a1a0a0000000d4948445200000320000002580802000000")
    message(FATAL_ERROR "The output is not an 800 x 600 RGB8 PNG with the expected IHDR.")
endif()

message(STATUS "PNG check: PASS (signature and IHDR: 800 x 600 RGB8)")
