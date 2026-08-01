module example.com/roundtrip

go 1.22

// Idiom A. dsdlc writes generated/go.mod declaring this module path, so it is a module in its own
// right and the only wiring needed is a replace directive pointing at where it was generated.
//
// The require line names a version that will never be fetched -- the replace takes precedence and
// resolves the module from disk. That is the standard way to depend on a module that is not
// published, and it is what makes the generated tree consumable without a registry, a vendor
// directory, or a network.
require example.com/lanyard v0.0.0

replace example.com/lanyard => ./generated
