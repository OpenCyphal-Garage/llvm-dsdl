# dsdlc is expected on PATH. Override with `make DSDLC=/path/to/dsdlc`.
DSDLC ?= dsdlc

DSDL_ROOT := dsdl/lanyard
GEN       := generated
BASE      := --target-language c --outdir $(GEN)

# Grouped targets (`&:`) are how a rule says "this one command produces all of these". Without them
# the honest options are a stamp file standing in for the real outputs, or a rule that lies about
# what it makes -- and a stamp is exactly the kind of workaround that hides a missing output until
# the compiler trips over it.
ifeq ($(filter grouped-target,$(.FEATURES)),)
$(error This Makefile needs GNU Make 4.3 or newer for grouped targets; this is $(MAKE_VERSION))
endif

# --------------------------------------------------------------------------------------------------
# Three tranches, because dsdlc decomposes its own output along exactly these lines.
#
# `--generate-support` splits everything dsdlc can emit into code derived from a definition and code
# that is not -- runtime headers, package manifests, scaffolding. `+uavcan...` names the standard
# types compiled into the binary. Between them, one namespace becomes three independent rules with
# three independent lifetimes, and none of them needs a stamp or a hand-written file list.
#
# The support header and the standard types change only when the compiler does, while
# your own definitions change all day. Splitting them means editing a definition rebuilds one
# archive, not all of it -- and the standard types can be built once into a library that several
# components share, which is the usual reason to want this.

# 1. Support: everything not derived from a definition. Needs no positional target.
SUPPORT_OUT := $(subst ;, ,$(shell $(DSDLC) $(BASE) --generate-support only --list-outputs))

# 2. The standard types this namespace refers to, taken from the catalog compiled into dsdlc and
#    named with the `+` sigil rather than through a checkout of public_regulated_data_types.
# Every standard type the namespace reaches, listed exactly. `+uavcan.si.unit` would cover nine of
# these in one line, but it generates all forty-five types in that namespace -- five times the work
# for the same result. The list is the honest trade; ask dsdlc for it rather than reading the
# definitions, by generating once and reading back the uavcan paths it produced.
BUILTIN_SEL := +uavcan.diagnostic.Severity.1.0 \
                +uavcan.node.Health.1.0 \
                +uavcan.si.unit.angle.Quaternion.1.0 \
                +uavcan.si.unit.angle.Vector3.1.0 \
                +uavcan.si.unit.angular_velocity.Vector3.1.0 \
                +uavcan.si.unit.electric_current.Scalar.1.0 \
                +uavcan.si.unit.length.Scalar.1.0 \
                +uavcan.si.unit.length.WideScalar.1.0 \
                +uavcan.si.unit.temperature.Scalar.1.0 \
                +uavcan.si.unit.velocity.Vector3.1.0 \
                +uavcan.si.unit.voltage.Scalar.1.0 \
                +uavcan.time.SynchronizedTimestamp.1.0
BUILTIN_ARGS := $(BASE) --generate-support never $(BUILTIN_SEL)
BUILTIN_OUT := $(subst ;, ,$(shell $(DSDLC) $(BUILTIN_ARGS) --list-outputs))

# 3. Our own definitions. --omit-dependencies keeps the standard types out of this tranche; they are
#    tranche 2's job and generating them twice would be two rules fighting over the same files.
TYPES_ARGS := $(BASE) --generate-support never --omit-dependencies $(DSDL_ROOT)
TYPES_OUT  := $(subst ;, ,$(shell $(DSDLC) $(TYPES_ARGS) --list-outputs))
TYPES_IN   := $(subst ;, ,$(shell $(DSDLC) $(TYPES_ARGS) --list-inputs))

GEN_HDRS := $(filter %.h,$(SUPPORT_OUT) $(BUILTIN_OUT) $(TYPES_OUT))

BUILTIN_OBJS := $(patsubst %.c,%.o,$(filter %.c,$(BUILTIN_OUT)))
TYPES_OBJS   := $(patsubst %.c,%.o,$(filter %.c,$(TYPES_OUT)))

CFLAGS += -std=c11 -Wall -Wextra -I$(GEN)

all: roundtrip

# Two archives rather than one pile of objects, mirroring the tranches: the standard types are the
# library several components would share, and your own types are the one that changes.
roundtrip: src/c/roundtrip.o liblanyard.a libdsdlbuiltin.a
	$(CC) $^ -o $@

# Deleted first, then created. `ar rcs` *adds to* an archive that already exists, so without the
# rm a type you deleted keeps its object inside the library long after its source is gone. The
# signature file is a prerequisite for the same reason it is one for generation: losing a member is
# a change Make cannot see by comparing timestamps of the members that remain.
libdsdlbuiltin.a: $(BUILTIN_OBJS) $(TYPES_SIG)
	rm -f $@
	$(AR) rcs $@ $(BUILTIN_OBJS)

liblanyard.a: $(TYPES_OBJS) $(TYPES_SIG)
	rm -f $@
	$(AR) rcs $@ $(TYPES_OBJS)

# --------------------------------------------------------------------------------------------------
# Generation. One rule per tranche, each declaring every file it produces.

# --prune-manifest is what makes a deleted definition stop existing: without it the generated header
# survives in $(GEN), still on the include path, and code that includes a type nobody defines any
# more keeps compiling. One manifest per tranche, never one per directory -- a tranche owns only the
# files it emits, and one that swept $(GEN) would delete the other two tranches' work.
MANIFESTS := .dsdlc

# Pruning only happens when dsdlc runs, so deleting a definition has to make it run -- and on its
# own it does not. Make compares timestamps of the prerequisites a rule has *now*; a prerequisite
# that vanished is simply absent from the comparison, every surviving output is still newer than
# every surviving input, and the rule is considered up to date. The deleted type's header would
# survive in $(GEN) with nothing left to notice.
#
# So the input list is itself made an input. This rewrites the file only when the list changes,
# which makes adding or removing a definition a timestamp change the rule can see, while editing one
# leaves it alone (that case is already covered by $(TYPES_IN)).
TYPES_SIG := $(MANIFESTS)/types.inputs
$(shell mkdir -p $(MANIFESTS) && printf '%s\n' $(TYPES_IN) >$(TYPES_SIG).tmp && \
        { cmp -s $(TYPES_SIG).tmp $(TYPES_SIG) || mv $(TYPES_SIG).tmp $(TYPES_SIG); } ; \
        rm -f $(TYPES_SIG).tmp)

$(SUPPORT_OUT) &: $(DSDLC)
	$(DSDLC) $(BASE) --generate-support only -MD --prune-manifest $(MANIFESTS)/support

# The prerequisite is the compiler itself, and that is not a trick: these types are compiled into the
# binary, so the binary is their source. dsdlc's own -MD depfiles say the same thing, which is what
# makes a toolchain upgrade regenerate them instead of leaving a stale copy behind.
$(BUILTIN_OUT) &: $(DSDLC)
	$(DSDLC) $(BUILTIN_ARGS) -MD --prune-manifest $(MANIFESTS)/builtin

# $(TYPES_IN) covers editing a definition; $(TYPES_SIG) covers adding or removing one.
$(TYPES_OUT) &: $(TYPES_IN) $(TYPES_SIG) $(DSDLC)
	$(DSDLC) $(TYPES_ARGS) -MD --prune-manifest $(MANIFESTS)/types

# Nothing compiles before the headers of every tranche exist. Order-only: the -MMD depfiles below
# name the headers each object actually includes, so a schema edit propagates through those rather
# than through a blanket rebuild.
$(BUILTIN_OBJS) $(TYPES_OBJS) src/c/roundtrip.o: | $(GEN_HDRS)

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Two independent sets of depfiles, answering two different questions.
#
# dsdlc's -MD files say which definitions each generated file came from, transitively: the rule for
# SensorFrame_1_0.c lists Mode.1.0.dsdl because the type embeds a type that embeds Mode. For the
# builtin tranche they name the dsdlc binary instead, for the reason above. No compiler could work
# either of those out.
-include $(addsuffix .d,$(SUPPORT_OUT) $(BUILTIN_OUT) $(TYPES_OUT))

# The C compiler's own -MMD files say which generated headers each object includes, which is the
# ordinary C dependency problem and nothing to do with DSDL.
-include $(patsubst %.o,%.d,$(BUILTIN_OBJS) $(TYPES_OBJS) src/c/roundtrip.o)

clean:
	rm -rf $(GEN) $(MANIFESTS) roundtrip liblanyard.a libdsdlbuiltin.a src/c/roundtrip.o src/c/roundtrip.d

.PHONY: all clean
