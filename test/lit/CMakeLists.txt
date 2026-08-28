find_program(LLVM_LIT_EXECUTABLE
  NAMES llvm-lit lit
  HINTS ${LLVM_TOOLS_BINARY_DIR} /opt/homebrew/opt/llvm/bin
  NO_CACHE)

set(LLVM_LIT_COMMAND "")
if(LLVM_LIT_EXECUTABLE)
  set(LLVM_LIT_COMMAND "${LLVM_LIT_EXECUTABLE}")
else()
  find_program(PYTHON3_EXECUTABLE python3)
  if(PYTHON3_EXECUTABLE)
    execute_process(
      COMMAND ${PYTHON3_EXECUTABLE} -c "import lit; print('ok')"
      RESULT_VARIABLE _lit_module_ok
      OUTPUT_QUIET
      ERROR_QUIET)
    if(_lit_module_ok EQUAL 0)
      set(LLVM_LIT_COMMAND "${PYTHON3_EXECUTABLE};-m;lit.main")
    endif()
  endif()
endif()

if(NOT LLVM_LIT_COMMAND)
  message(WARNING "Skipping lit tests because neither llvm-lit, lit, nor python -m lit.main is available")
  return()
endif()

set(LLVMDSDL_LIT_TEST_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(LLVMDSDL_LIT_TEST_EXEC_ROOT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
set(LLVMDSDL_LIT_DSDLC "$<TARGET_FILE:dsdlc>")
set(LLVMDSDL_LIT_DSDLOPT "$<TARGET_FILE:dsdl-opt>")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/lit.site.cfg.py.in
               ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py.in.gen @ONLY)

file(GENERATE
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/lit.site.cfg.py"
  INPUT "${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg.py.in.gen")

add_test(NAME llvmdsdl-lit
  COMMAND ${LLVM_LIT_COMMAND} -sv ${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>)

# The help text and the --target-language parser are both read out of the built binary and held
# against each other, so a new lane needs no edit here. See the script's docstring for why this is
# not a check on the help text itself.
find_program(LLVMDSDL_PYTHON3_FOR_CLI_CONTRACT NAMES python3 python)
if(LLVMDSDL_PYTHON3_FOR_CLI_CONTRACT)
  add_test(NAME llvmdsdl-cli-language-contract
    COMMAND "${LLVMDSDL_PYTHON3_FOR_CLI_CONTRACT}"
            "${LLVMDSDL_SOURCE_DIR}/tools/dsdlc/check_language_contract.py"
            --dsdlc $<TARGET_FILE:dsdlc>)
  set_tests_properties(llvmdsdl-cli-language-contract PROPERTIES LABELS "cli;contract")
else()
  message(STATUS "Skipping CLI language contract test: python3 unavailable")
endif()
