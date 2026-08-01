cmake_minimum_required(VERSION 3.20)
project(lanyard_ninja_multi C)

find_package(llvm-dsdl REQUIRED)

# One output tree shared by every call below, and deliberately NOT under a per-configuration
# directory. Generated code is a function of the schema and the backend flags and of nothing else --
# no optimisation level, no NDEBUG. Putting it under $<CONFIG> would run identical codegen once per
# configuration to produce byte-identical trees, and would make the Debug and Release halves of this
# build disagree about where the headers are for no reason. Generate once; compile it as many times
# as the generator wants to.
set(GEN "${CMAKE_BINARY_DIR}/generated")

# Three calls into that one tree, because several calls sharing an output directory is exactly the
# situation Ninja refuses when two of them claim the same file. Left alone, every call would emit the
# runtime header and the namespace call would pull in the standard types it reaches, so the work is
# divided up front along the seams dsdlc already has. dsdlc_generate() checks for overlap itself and
# fails at configure time naming both claimants.

# The runtime header has exactly one owner.
dsdlc_generate(dsdl_support
  LANGUAGE c
  SUPPORT  only
  OUTDIR   "${GEN}")

# So do the standard types this namespace reaches. `+` names the catalog compiled into dsdlc, so
# none of this needs a checkout of public_regulated_data_types.
#
# They are listed exactly rather than by namespace selector: `+uavcan.si.unit` would cover nine of
# them in one line and generate all forty-five types in that namespace, which is five times the work
# for the same result. Ask dsdlc for the list rather than reading the definitions -- generate once
# and read back the `uavcan/` paths it produced.
dsdlc_generate(dsdl_builtin
  LANGUAGE c
  SUPPORT  never
  OUTDIR   "${GEN}"
  BUILTIN
    +uavcan.diagnostic.Severity.1.0
    +uavcan.node.Health.1.0
    +uavcan.si.unit.angle.Quaternion.1.0
    +uavcan.si.unit.angle.Vector3.1.0
    +uavcan.si.unit.angular_velocity.Vector3.1.0
    +uavcan.si.unit.electric_current.Scalar.1.0
    +uavcan.si.unit.length.Scalar.1.0
    +uavcan.si.unit.length.WideScalar.1.0
    +uavcan.si.unit.temperature.Scalar.1.0
    +uavcan.si.unit.velocity.Vector3.1.0
    +uavcan.si.unit.voltage.Scalar.1.0
    +uavcan.time.SynchronizedTimestamp.1.0)

# The namespace then generates its own definitions and nothing else. Without OMIT_DEPENDENCIES it
# would pull the standard types into its own output and collide with dsdl_builtin.
dsdlc_generate(lanyard
  LANGUAGE          c
  SUPPORT           never
  OMIT_DEPENDENCIES
  NAMESPACE         "${CMAKE_CURRENT_SOURCE_DIR}/dsdl/lanyard"
  OUTDIR            "${GEN}")

target_link_libraries(lanyard PUBLIC dsdl_support dsdl_builtin)

add_executable(roundtrip src/c/roundtrip.c)
target_link_libraries(roundtrip PRIVATE lanyard)
