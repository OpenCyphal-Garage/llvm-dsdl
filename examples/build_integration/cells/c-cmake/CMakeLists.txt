cmake_minimum_required(VERSION 3.20)
project(kitbag_c_cmake C)

# find_package brings in the imported target llvm-dsdl::dsdlc and the dsdlc_generate() function.
# Point CMAKE_PREFIX_PATH at wherever llvm-dsdl was installed, or let it be found on the default
# search path if it went to /usr or /usr/local.
find_package(llvm-dsdl REQUIRED)

# One call. It asks dsdlc at configure time what the run would read and what it would produce, hands
# CMake the exact lists, and -- for the C and C++ backends -- builds a static library out of the
# generated sources with the output directory already on its interface include path.
#
# So there is no glob to keep in step, no stamp file, no hand-maintained list of generated files, and
# adding a definition to dsdl/kitbag/ is picked up on the next build with nothing edited here.
dsdlc_generate(kitbag
  LANGUAGE  c
  NAMESPACE "${CMAKE_CURRENT_SOURCE_DIR}/dsdl/kitbag")

add_executable(roundtrip src/c/roundtrip.c)
target_link_libraries(roundtrip PRIVATE kitbag)
