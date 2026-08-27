//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared scalar storage token rendering for native backends.
///
/// The token renderer maps normalized scalar storage widths to backend-local
/// type token spellings.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/StorageTypeTokens.h"

#include "llvmdsdl/CodeGen/TypeStorage.h"
#include <cstdint>
#include <string>

namespace llvmdsdl
{
namespace
{

std::string renderToken(const StorageTokenLanguage language, const std::uint32_t storageBits, const bool signedToken)
{
    switch (language)
    {
    case StorageTokenLanguage::C:
        if (signedToken)
        {
            switch (storageBits)
            {
            case 8:
                return "int8_t";
            case 16:
                return "int16_t";
            case 32:
                return "int32_t";
            default:
                return "int64_t";
            }
        }
        switch (storageBits)
        {
        case 8:
            return "uint8_t";
        case 16:
            return "uint16_t";
        case 32:
            return "uint32_t";
        default:
            return "uint64_t";
        }

    case StorageTokenLanguage::Cpp:
        if (signedToken)
        {
            switch (storageBits)
            {
            case 8:
                return "std::int8_t";
            case 16:
                return "std::int16_t";
            case 32:
                return "std::int32_t";
            default:
                return "std::int64_t";
            }
        }
        switch (storageBits)
        {
        case 8:
            return "std::uint8_t";
        case 16:
            return "std::uint16_t";
        case 32:
            return "std::uint32_t";
        default:
            return "std::uint64_t";
        }

    case StorageTokenLanguage::Rust:
        if (signedToken)
        {
            switch (storageBits)
            {
            case 8:
                return "i8";
            case 16:
                return "i16";
            case 32:
                return "i32";
            default:
                return "i64";
            }
        }
        switch (storageBits)
        {
        case 8:
            return "u8";
        case 16:
            return "u16";
        case 32:
            return "u32";
        default:
            return "u64";
        }

    case StorageTokenLanguage::Go:
        if (signedToken)
        {
            switch (storageBits)
            {
            case 8:
                return "int8";
            case 16:
                return "int16";
            case 32:
                return "int32";
            default:
                return "int64";
            }
        }
        switch (storageBits)
        {
        case 8:
            return "uint8";
        case 16:
            return "uint16";
        case 32:
            return "uint32";
        default:
            return "uint64";
        }
    }
    return signedToken ? "int64_t" : "uint64_t";
}

}  // namespace

std::string renderUnsignedStorageToken(const StorageTokenLanguage language, const std::uint32_t bitLength)
{
    return renderToken(language, scalarStorageBits(bitLength), /*signedToken=*/false);
}

std::string renderSignedStorageToken(const StorageTokenLanguage language, const std::uint32_t bitLength)
{
    return renderToken(language, scalarStorageBits(bitLength), /*signedToken=*/true);
}

}  // namespace llvmdsdl
