cmake_minimum_required(VERSION 3.20)
project(kitbag_ninja_multi C)

find_package(llvm-dsdl REQUIRED)

# One output tree shared by every call below, and deliberately NOT under a per-configuration
# directory. Generated code is a function of the schema and the backend flags and of nothing else --
# no optimisation level, no NDEBUG. Putting it under $<CONFIG> would run identical codegen once per
# configuration to produce byte-identical trees, and would make the Debug and Release halves of this
# build disagree about where the headers are for no reason. Generate once; compile it as many times
# as the generator wants to.
set(GEN "${CMAKE_BINARY_DIR}/generated")

# Four calls into that one tree, because two namespaces cannot each be a single whole-namespace call.
# Both refer to uavcan.time.SynchronizedTimestamp, and both would emit the runtime header, so the
# naive spelling produces two rules for dsdl_runtime.h and two for the timestamp -- which Ninja
# refuses to build at all. dsdlc_generate() catches that at configure time and names both claimants,
# but the fix is to divide the work up front, along the seams dsdlc already has.

# The runtime header has exactly one owner.
dsdlc_generate(dsdl_support
  LANGUAGE c
  SUPPORT  only
  OUTDIR   "${GEN}")

# So does the standard type both namespaces refer to. `+` names the catalog compiled into dsdlc, so
# this needs no checkout of public_regulated_data_types.
dsdlc_generate(dsdl_builtin
  LANGUAGE c
  SUPPORT  never
  BUILTIN  +uavcan.time.SynchronizedTimestamp.1.0
  OUTDIR   "${GEN}")

# The namespaces then generate their own definitions and nothing else. Without OMIT_DEPENDENCIES each
# would pull the shared timestamp into its own output and collide with dsdl_builtin.
dsdlc_generate(kitbag
  LANGUAGE          c
  SUPPORT           never
  OMIT_DEPENDENCIES
  NAMESPACE         "${CMAKE_CURRENT_SOURCE_DIR}/dsdl/kitbag"
  OUTDIR            "${GEN}")

dsdlc_generate(gizmo
  LANGUAGE          c
  SUPPORT           never
  OMIT_DEPENDENCIES
  NAMESPACE         "${CMAKE_CURRENT_SOURCE_DIR}/dsdl/gizmo"
  OUTDIR            "${GEN}")

# dsdl_support carries the include path and dsdl_builtin the shared type, so both namespaces depend
# on both. Neither namespace depends on the other.
target_link_libraries(kitbag PUBLIC dsdl_support dsdl_builtin)
target_link_libraries(gizmo  PUBLIC dsdl_support dsdl_builtin)

add_executable(roundtrip src/c/roundtrip.c)
target_link_libraries(roundtrip PRIVATE kitbag)

# gizmo is not linked into the round-trip program -- nothing in it is called. It is built because a
# second namespace sharing the tree is the thing this cell exists to demonstrate, and a namespace
# that is generated and compiled has proven the point whether or not anything calls it.
add_dependencies(roundtrip gizmo)
