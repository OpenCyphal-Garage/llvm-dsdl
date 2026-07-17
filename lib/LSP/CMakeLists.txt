#===----------------------------------------------------------------------===#
##
## @file
## Build rules for the LSP server support library.
##
## This target packages JSON-RPC transport, session state, request scheduling,
## and telemetry helpers used by the `dsdld` tool.
##
#===----------------------------------------------------------------------===#

add_library(llvmdsdlLSP
  AI.cpp
  Analysis.cpp
  DocumentStore.cpp
  Index.cpp
  Logging.cpp
  JsonRpcIO.cpp
  Lint.cpp
  Ranking.cpp
  RequestScheduler.cpp
  Server.cpp
  ServerConfig.cpp
  Telemetry.cpp
)

target_link_libraries(llvmdsdlLSP
  PUBLIC
  llvmdsdlLowering
  llvmdsdlSupport
  LLVMSupport
  ${CMAKE_DL_LIBS}
)

target_include_directories(llvmdsdlLSP PUBLIC
  ${LLVMDSDL_SOURCE_DIR}/include
  ${LLVMDSDL_BINARY_DIR}/include
)
