//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>

#include "llvmdsdl/CodeGen/StorageTypeTokens.h"

#include "UnitTests.h"

bool runStorageTypeTokensTests()
{
    using llvmdsdl::StorageTokenLanguage;
    using llvmdsdl::renderSignedStorageToken;
    using llvmdsdl::renderUnsignedStorageToken;

    if (renderUnsignedStorageToken(StorageTokenLanguage::C, 9) != "uint16_t" ||
        renderSignedStorageToken(StorageTokenLanguage::C, 9) != "int16_t")
    {
        std::cerr << "C storage token mapping mismatch\n";
        return false;
    }
    if (renderUnsignedStorageToken(StorageTokenLanguage::Cpp, 33) != "std::uint64_t" ||
        renderSignedStorageToken(StorageTokenLanguage::Cpp, 33) != "std::int64_t")
    {
        std::cerr << "C++ storage token mapping mismatch\n";
        return false;
    }
    if (renderUnsignedStorageToken(StorageTokenLanguage::Rust, 32) != "u32" ||
        renderSignedStorageToken(StorageTokenLanguage::Rust, 32) != "i32")
    {
        std::cerr << "Rust storage token mapping mismatch\n";
        return false;
    }
    if (renderUnsignedStorageToken(StorageTokenLanguage::Go, 7) != "uint8" ||
        renderSignedStorageToken(StorageTokenLanguage::Go, 7) != "int8")
    {
        std::cerr << "Go storage token mapping mismatch\n";
        return false;
    }

    return true;
}
