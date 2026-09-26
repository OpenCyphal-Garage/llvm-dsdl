//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>

#include "llvmdsdl/CodeGen/StorageTypeTokens.h"
#include "llvmdsdl/Support/Language.h"

#include "UnitTests.h"

bool runStorageTypeTokensTests()
{
    using llvmdsdl::Language;
    using llvmdsdl::renderSignedStorageToken;
    using llvmdsdl::renderUnsignedStorageToken;

    if (renderUnsignedStorageToken(Language::C, 9) != "uint16_t" ||
        renderSignedStorageToken(Language::C, 9) != "int16_t")
    {
        std::cerr << "C storage token mapping mismatch\n";
        return false;
    }
    if (renderUnsignedStorageToken(Language::Cpp, 33) != "std::uint64_t" ||
        renderSignedStorageToken(Language::Cpp, 33) != "std::int64_t")
    {
        std::cerr << "C++ storage token mapping mismatch\n";
        return false;
    }
    if (renderUnsignedStorageToken(Language::Rust, 32) != "u32" ||
        renderSignedStorageToken(Language::Rust, 32) != "i32")
    {
        std::cerr << "Rust storage token mapping mismatch\n";
        return false;
    }
    if (renderUnsignedStorageToken(Language::Go, 7) != "uint8" || renderSignedStorageToken(Language::Go, 7) != "int8")
    {
        std::cerr << "Go storage token mapping mismatch\n";
        return false;
    }

    return true;
}
