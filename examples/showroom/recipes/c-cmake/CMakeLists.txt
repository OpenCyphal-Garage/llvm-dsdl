cmake_minimum_required(VERSION 3.20)
project(lanyard_c_cmake C)

# find_package brings in the imported target llvm-dsdl::dsdlc and the dsdlc_generate() function.
# Point CMAKE_PREFIX_PATH at wherever llvm-dsdl was installed, or let it be found on the default
# search path if it went to /usr or /usr/local.
find_package(llvm-dsdl REQUIRED)

# One call. It asks dsdlc at configure time what the run would read and what it would produce, hands
# CMake the exact lists, and -- for the C and C++ backends -- builds a static library out of the
# generated sources with the output directory already on its interface include path.
#
# So there is no glob to keep in step, no stamp file, no hand-maintained list of generated files, and
# adding a definition to dsdl/lanyard/ is picked up on the next build with nothing edited here.
dsdlc_generate(lanyard
  LANGUAGE  c
  NAMESPACE "${CMAKE_CURRENT_SOURCE_DIR}/dsdl/lanyard")

add_executable(roundtrip src/c/roundtrip.c)
target_link_libraries(roundtrip PRIVATE lanyard)

# --------------------------------------------------------------------------------------------------
# The obj backend: the same types as a prebuilt archive rather than as sources.
#
# `-l obj` runs the C backend and then compiles it, handing back the headers, the objects, and a
# `.a`. That is what you want when the generated code should be built once -- for a target triple
# that is not this host, or by a party who ships the archive rather than the schema.
#
# One call, like the source build above: the headers are declared outputs published beside the
# archive, so linking the target brings its include path with it.
dsdlc_generate(lanyard_prebuilt
  LANGUAGE  obj
  NAMESPACE "${CMAKE_CURRENT_SOURCE_DIR}/dsdl/lanyard"
  # Required for obj. It selects the codegen strategy, not the wire format -- DSDL is little-endian
  # on every target either way. Add --target-triple here to build the archive for a different one,
  # after which it only links into a build using that same toolchain.
  OPTIONS   --target-endianness little)

add_executable(roundtrip_prebuilt src/c/roundtrip.c)
target_link_libraries(roundtrip_prebuilt PRIVATE lanyard_prebuilt)
