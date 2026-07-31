# When generating or modifying showroom DSDL types

Every comment block reaches the generated source in all six languages, attached to the type, field,
or constant it documents. That is why the definitions are commented as heavily as they are: the DSDL
is the only place the documentation is written, and the generated code is where most people read it.

**Wrap comment lines at 72 columns.** The documentation site gives a code block about eighty
monospace characters before it scrolls horizontally, and that width does not grow with the window —
extra width goes to the sidebars. The widest comment prefix any backend adds is eight characters
(`  /* … */` in C, `    /// ` in Rust), so 72 columns of DSDL is what survives the trip. Generated
*code* may exceed it and scroll — some serializer signatures are unavoidably long — but a comment
that scrolls is a defect in the definition, since its width is ours to choose. `showroom-docs`
enforces this and fails the build on a violation.