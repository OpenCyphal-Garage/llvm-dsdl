set(LLVMDSDL_PING_PONG_DSDL_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/dsdl")
set(LLVMDSDL_PING_PONG_GEN_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(LLVMDSDL_PING_PONG_UDPARD_C
    "${CMAKE_SOURCE_DIR}/submodules/libudpard/libudpard/udpard.c")
set(LLVMDSDL_PING_PONG_UDPARD_INCLUDE
    "${CMAKE_SOURCE_DIR}/submodules/libudpard/libudpard")

if(NOT EXISTS "${LLVMDSDL_PING_PONG_UDPARD_C}")
  message(WARNING
    "Skipping cyphal ping-pong demo target: libudpard submodule source "
    "not found at ${LLVMDSDL_PING_PONG_UDPARD_C}")
  return()
endif()

set(LLVMDSDL_PING_PONG_GENERATED_FILES
    "${LLVMDSDL_PING_PONG_GEN_DIR}/dsdl/demo/ping/PingPong_1_0.hpp"
    "${LLVMDSDL_PING_PONG_GEN_DIR}/dsdl_runtime.h"
    "${LLVMDSDL_PING_PONG_GEN_DIR}/dsdl_runtime.hpp")

add_custom_command(
  OUTPUT
    ${LLVMDSDL_PING_PONG_GENERATED_FILES}
  COMMAND ${CMAKE_COMMAND} -E rm -rf "${LLVMDSDL_PING_PONG_GEN_DIR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${LLVMDSDL_PING_PONG_GEN_DIR}"
  COMMAND
    $<TARGET_FILE:dsdlc>
      --target-language cpp --experimental-languages
      "${LLVMDSDL_PING_PONG_DSDL_ROOT}"
      --cpp-profile std
      --outdir "${LLVMDSDL_PING_PONG_GEN_DIR}"
  DEPENDS
    dsdlc
    "${LLVMDSDL_PING_PONG_DSDL_ROOT}/demo/ping/PingPong.1.0.dsdl"
  VERBATIM
  COMMENT "Generating cyphal ping-pong C++ DSDL artifacts")

add_custom_target(generate-cyphal-ping-pong-cpp
  DEPENDS ${LLVMDSDL_PING_PONG_GENERATED_FILES})

add_executable(cyphal-ping-pong-node
  src/ping_pong_node.cpp
  src/net/udp_posix.c
  "${LLVMDSDL_PING_PONG_UDPARD_C}")

add_dependencies(cyphal-ping-pong-node
  generate-cyphal-ping-pong-cpp)

target_include_directories(cyphal-ping-pong-node PRIVATE
  "${LLVMDSDL_PING_PONG_GEN_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/net"
  "${LLVMDSDL_PING_PONG_UDPARD_INCLUDE}")

set_target_properties(cyphal-ping-pong-node PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON)
