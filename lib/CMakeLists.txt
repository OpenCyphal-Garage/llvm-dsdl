#===----------------------------------------------------------------------===#
##
## @file
## Defines the top-level library subtree for llvm-dsdl.
##
## This build entry point composes support, frontend, semantics, IR, lowering, transform, and codegen library targets.
##
#===----------------------------------------------------------------------===#

add_subdirectory(Support)
add_subdirectory(LSP)
add_subdirectory(Frontend)
add_subdirectory(Semantics)
add_subdirectory(SerDes)
add_subdirectory(IR)
add_subdirectory(Lowering)
add_subdirectory(Transforms)
add_subdirectory(CodeGen)
