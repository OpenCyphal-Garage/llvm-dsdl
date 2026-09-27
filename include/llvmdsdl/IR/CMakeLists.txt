#===----------------------------------------------------------------------===#
##
## @file
## CMake declarations for generating and exposing DSDL dialect IR headers.
##
#===----------------------------------------------------------------------===#

set(LLVM_TARGET_DEFINITIONS DSDLOps.td)
mlir_tablegen(DSDLOps.h.inc -gen-op-decls)
mlir_tablegen(DSDLOps.cpp.inc -gen-op-defs)
mlir_tablegen(DSDLDialect.h.inc -gen-dialect-decls -dialect=dsdl)
mlir_tablegen(DSDLDialect.cpp.inc -gen-dialect-defs -dialect=dsdl)

set(LLVM_TARGET_DEFINITIONS DSDLTypes.td)
mlir_tablegen(DSDLTypes.h.inc -gen-typedef-decls -typedefs-dialect=dsdl)
mlir_tablegen(DSDLTypes.cpp.inc -gen-typedef-defs -typedefs-dialect=dsdl)

set(LLVM_TARGET_DEFINITIONS DSDLAttrs.td)
mlir_tablegen(DSDLAttrs.h.inc -gen-attrdef-decls -attrdefs-dialect=dsdl)
mlir_tablegen(DSDLAttrs.cpp.inc -gen-attrdef-defs -attrdefs-dialect=dsdl)
mlir_tablegen(DSDLEnums.h.inc -gen-enum-decls)
mlir_tablegen(DSDLEnums.cpp.inc -gen-enum-defs)

add_public_tablegen_target(LLVMDSDLIRIncGen)
add_dependencies(mlir-headers LLVMDSDLIRIncGen)
