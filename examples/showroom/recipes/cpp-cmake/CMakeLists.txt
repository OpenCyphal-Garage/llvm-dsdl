cmake_minimum_required(VERSION 3.24)
project(lanyard_cpp_cmake CXX)

# find_package brings in the imported target llvm-dsdl::dsdlc and the dsdlc_generate() function.
find_package(llvm-dsdl REQUIRED)

# The autosar profile targets C++14 and takes its span from CETL, which this build fetches at the tag
# the vocabulary was written against. CETL is header-only.
include(FetchContent)
FetchContent_Declare(cetl
  GIT_REPOSITORY https://github.com/OpenCyphal/CETL.git
  GIT_TAG        v1.4.2
  GIT_SHALLOW    TRUE
  GIT_SUBMODULES "")
FetchContent_MakeAvailable(cetl)
add_library(cetl INTERFACE)
target_include_directories(cetl INTERFACE "${cetl_SOURCE_DIR}/include")

# One call, as for C. VOCABULARY names the file binding the span the generated accessors take; dsdlc
# reports it among the run's inputs, so it joins the dependencies the way the definitions do and a
# change to the binding regenerates. Without it the autosar profile has no span and dsdlc refuses to
# generate.
dsdlc_generate(lanyard
  LANGUAGE   cpp
  NAMESPACE  "${CMAKE_CURRENT_SOURCE_DIR}/dsdl/lanyard"
  VOCABULARY "${CMAKE_CURRENT_SOURCE_DIR}/vocabulary.yaml"
  OPTIONS    --cpp-profile autosar)

add_executable(roundtrip src/cpp/roundtrip.cpp)
target_compile_features(roundtrip PRIVATE cxx_std_14)
target_link_libraries(roundtrip PRIVATE lanyard cetl)
