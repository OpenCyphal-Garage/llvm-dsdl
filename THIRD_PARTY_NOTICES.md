# Third-Party Notices

This project is licensed under MIT. See `LICENSE.md`.

The project depends on third-party software that is distributed under separate
licenses:

1. LLVM/MLIR
   - License: Apache-2.0 WITH LLVM-exception
   - Source of license text included at:
     `LICENSES/LLVM-Apache-2.0-with-LLVM-exception.txt`
2. OpenCyphal `libudpard` submodule
   - License: MIT
   - License file:
     `submodules/libudpard/LICENSE`
3. OpenCyphal public regulated data types submodule
   - License: MIT
   - License file:
     `submodules/public_regulated_data_types/LICENSE`
4. OpenCyphal `CETL` submodule
   - License: MIT
   - License file:
     `submodules/CETL/LICENSE`

When distributing self-contained tool bundles produced by this project, include
this notice file and the referenced license files in the distribution package.
The `bundle-tools-self-contained` target copies these notice/license files into
the bundle output directory.
