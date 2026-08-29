# Decoding a delimited composite written by a newer peer.
#
# A delimited nested type is preceded by its length in bytes. When the sender appended fields
# this reader has no name for, the nested decode consumes fewer bytes than the header declares,
# and the outer offset must advance by the header regardless -- otherwise every field after it
# is read from inside the part that was not understood.
#
# This is a behavioural check rather than a reading of the generated text: the same property
# has to hold whichever way the body was produced, and the wire below distinguishes the two
# outcomes by a whole byte.

if(NOT DEFINED DSDLC OR NOT DEFINED OUT_DIR OR NOT DEFINED C_COMPILER)
  message(FATAL_ERROR "DSDLC, OUT_DIR and C_COMPILER are required")
endif()

file(REMOVE_RECURSE "${OUT_DIR}")
file(MAKE_DIRECTORY "${OUT_DIR}/dsdl/dl")

# The reader's view of Inner is two bytes; its extent allows a sender to write more.
file(WRITE "${OUT_DIR}/dsdl/dl/Inner.1.0.dsdl" "uint8 x\nuint8 y\n@extent 64 * 8\n")
file(WRITE "${OUT_DIR}/dsdl/dl/Outer.1.0.dsdl" "dl.Inner.1.0 inner\nuint8 tail\n@sealed\n")

execute_process(
  COMMAND "${DSDLC}" --target-language c --outdir "${OUT_DIR}/c" "${OUT_DIR}/dsdl"
  RESULT_VARIABLE generate_result
  OUTPUT_VARIABLE generate_output
  ERROR_VARIABLE generate_output)
if(NOT generate_result EQUAL 0)
  message(FATAL_ERROR "generation failed:\n${generate_output}")
endif()

# Header declares four bytes of Inner; this reader understands the first two. `tail` follows
# the declared four, so a reader that advanced by what it consumed would return 0xAA.
file(WRITE "${OUT_DIR}/driver.c" "
#include <stdio.h>
#include <string.h>
#include \"dsdl/dl/Outer_1_0.h\"
int main(void)
{
    const uint8_t wire[] = {0x04u, 0x00u, 0x00u, 0x00u, 0x01u, 0x02u, 0xAAu, 0xBBu, 0x77u};
    dsdl__dl__Outer decoded;
    memset(&decoded, 0, sizeof decoded);
    size_t size = sizeof wire;
    const int8_t rc = dsdl__dl__Outer__deserialize_(&decoded, wire, &size);
    if (rc != 0) {
        printf(\"deserialize failed: %d\\n\", (int) rc);
        return 1;
    }
    if ((decoded.inner.x != 0x01u) || (decoded.inner.y != 0x02u)) {
        printf(\"nested fields wrong: %02X %02X\\n\", decoded.inner.x, decoded.inner.y);
        return 1;
    }
    if (decoded.tail != 0x77u) {
        printf(\"advanced by consumed, not by the declared header: tail=%02X\\n\", decoded.tail);
        return 1;
    }
    printf(\"ok\\n\");
    return 0;
}
")

execute_process(
  COMMAND "${C_COMPILER}" -std=c11 -Wall -Wextra -Werror
          "-I${OUT_DIR}/c" -o "${OUT_DIR}/driver"
          "${OUT_DIR}/driver.c"
          "${OUT_DIR}/c/dsdl/dl/Outer_1_0.c"
          "${OUT_DIR}/c/dsdl/dl/Inner_1_0.c"
  RESULT_VARIABLE compile_result
  OUTPUT_VARIABLE compile_output
  ERROR_VARIABLE compile_output)
if(NOT compile_result EQUAL 0)
  message(FATAL_ERROR "driver failed to compile:\n${compile_output}")
endif()

execute_process(
  COMMAND "${OUT_DIR}/driver"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_output
  ERROR_VARIABLE run_output)
if(NOT run_result EQUAL 0)
  message(FATAL_ERROR "delimited forward-compatibility skip is wrong:\n${run_output}")
endif()
message(STATUS "delimited forward-compat skip: ${run_output}")
