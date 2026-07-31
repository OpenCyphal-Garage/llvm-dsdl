# Shared round-trip programs

One program per language, shared by every cell in that language's row.

Each one does the same thing: build a `kitbag.SensorFrame.1.0` with a non-empty array of readings and
a non-empty `source` string, serialise it, deserialise it into a fresh value, assert the two agree
field by field, and exit zero. Nothing more -- correctness of the serialiser is what `test/lit` and
`test/integration` are for. What these programs prove is that the generated code was found,
compiled, linked, and could be called, which is the entire claim a build integration makes.

They live here rather than in the cells because of the invariant described in the example README:
cells carry build wiring and nothing else, so that two cells in the same row differ only by the thing
you are comparing them for. Five Python cells share one `test_roundtrip.py`; four C and C++ cells
share one `roundtrip.c` and one `roundtrip.cpp`.

Each cell stages this directory next to itself as `src/`, so a cell's build files refer to
`src/c/roundtrip.c` and friends by that relative path.

## Why one subdirectory per language

The programs started out flat -- `src/roundtrip.c` beside `src/roundtrip.go` -- and Go put a stop to
it: a directory is a package, and `go build` refuses one containing C sources it has not been told to
treat as cgo. Rather than special-case the offender, every language gets its own directory. It also
means a cell's `include`, `path`, or glob can name a directory rather than a single file, which is
what most build systems would rather be given.

Populated per phase, as each row's first cell lands.
