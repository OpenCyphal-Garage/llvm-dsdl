//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>

#include "llvmdsdl/CodeGen/HelperBindingNaming.h"

#include "UnitTests.h"

bool runHelperBindingNamingTests()
{
    using llvmdsdl::CodegenNamingLanguage;
    using llvmdsdl::renderHelperBindingIdentifier;

    if (renderHelperBindingIdentifier(CodegenNamingLanguage::TypeScript, "for") != "mlir_for_")
    {
        std::cerr << "TypeScript helper binding keyword sanitization mismatch\n";
        return false;
    }
    if (renderHelperBindingIdentifier(CodegenNamingLanguage::Python, "9bad-name") != "mlir__9bad_name")
    {
        std::cerr << "Python helper binding sanitization mismatch\n";
        return false;
    }
    if (renderHelperBindingIdentifier(CodegenNamingLanguage::Cpp, "union") != "mlir_union_")
    {
        std::cerr << "C++ helper binding keyword sanitization mismatch\n";
        return false;
    }
    if (renderHelperBindingIdentifier(CodegenNamingLanguage::Rust, "self") != "mlir_self_")
    {
        std::cerr << "Rust helper binding keyword sanitization mismatch\n";
        return false;
    }
    if (renderHelperBindingIdentifier(CodegenNamingLanguage::Go, "channel-value") != "mlir_channel_value")
    {
        std::cerr << "Go helper binding sanitization mismatch\n";
        return false;
    }

    return true;
}
