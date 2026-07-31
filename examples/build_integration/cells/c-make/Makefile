# dsdlc is expected on PATH. Override with `make DSDLC=/path/to/dsdlc`.
DSDLC ?= dsdlc

DSDL_ROOT := dsdl/kitbag
GEN       := generated

DSDLC_ARGS := --target-language c $(DSDL_ROOT) --outdir $(GEN)

# Ask the compiler what it will read and what it will write, before it has written anything. Both
# flags imply --dry-run and print a semicolon-separated list on stdout and nothing else -- the run
# summary goes to stderr -- so turning semicolons into spaces is the whole of the parsing.
#
# This is what makes the rest of the file free of hand-maintained lists: add a definition to
# dsdl/kitbag/ and it is compiled on the next make with nothing here edited.
DSDL_SOURCES := $(subst ;, ,$(shell $(DSDLC) $(DSDLC_ARGS) --list-inputs))
GEN_FILES    := $(subst ;, ,$(shell $(DSDLC) $(DSDLC_ARGS) --list-outputs))

GEN_SRCS := $(filter %.c,$(GEN_FILES))
GEN_OBJS := $(GEN_SRCS:.c=.o)
OBJS     := src/c/roundtrip.o $(GEN_OBJS)

CFLAGS += -std=c11 -Wall -Wextra -I$(GEN)

all: roundtrip

roundtrip: $(OBJS)
	$(CC) $(OBJS) -o $@

# One dsdlc run produces every generated file at once. GNU Make before 4.3 has no way to say "this
# one recipe makes all of these" (4.3's grouped targets, `&:`, would), and macOS still ships 3.81, so
# the run hangs off a stamp and the generated files are declared to depend on the stamp. The stamp
# depends on the definitions, which is what turns an edit into a regeneration.
# A stamp asserts that every generated file exists and is current. Two things can make that assertion
# false without the schema looking newer than the stamp:
#
#   - somebody deleted a generated file by hand;
#   - a definition was added in the same clock second the stamp was last touched, and make's
#     timestamp comparison is "strictly newer" -- GNU Make 3.81 stats at second granularity, so an
#     added file can tie with the stamp and lose.
#
# Either way an expected output is missing while the stamp claims otherwise, and the empty recipe
# below would then "produce" a file that is not there and hand a phantom to the compiler. Checking
# for missing outputs at parse time and forcing regeneration when there are any closes both.
MISSING := $(filter-out $(wildcard $(GEN_FILES)),$(GEN_FILES))

$(GEN)/.stamp: $(DSDL_SOURCES) $(if $(MISSING),FORCE)
	@mkdir -p $(GEN)
	$(DSDLC) $(DSDLC_ARGS) -MD
	@touch $@

$(GEN_FILES): $(GEN)/.stamp
	@:

FORCE:
.PHONY: FORCE

# Nothing compiles before the headers exist. Order-only (the `|`) is the right strength: on the
# first build it forces generation to happen first, and on later builds it does not by itself force
# a recompile -- the -MMD depfiles below already name the generated headers each object really
# includes, so a schema edit propagates through those instead of through a blanket rebuild.
$(OBJS): | $(GEN)/.stamp

%.o: %.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Two independent sets of dependency files, answering two different questions.
#
# dsdlc's -MD files say which definitions each generated file was produced from -- including
# transitively, so generated/kitbag/SensorFrame_1_0.c lists Mode.1.0.dsdl because the type it
# describes embeds a type that embeds Mode. That is schema-level dependency information no compiler
# could work out.
-include $(addsuffix .d,$(GEN_FILES))

# The C compiler's own -MMD files say which generated headers each object includes, which is the
# ordinary C dependency problem and nothing to do with DSDL.
-include $(OBJS:.o=.d)

clean:
	rm -rf $(GEN) roundtrip src/c/roundtrip.o src/c/roundtrip.d

.PHONY: all clean
