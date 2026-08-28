//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>

#include "llvmdsdl/CodeGen/CodegenDiagnosticText.h"

#include "UnitTests.h"

bool runCodegenDiagnosticTextTests()
{
    namespace text = llvmdsdl::codegen_diagnostic_text;

    if (text::serializationBufferTooSmall() != "serialization buffer too small")
    {
        std::cerr << "serializationBufferTooSmall mismatch\n";
        return false;
    }
    if (text::invalidUnionTagPrefix() != "invalid union tag ")
    {
        std::cerr << "invalidUnionTagPrefix mismatch\n";
        return false;
    }
    if (text::decodedInvalidUnionTagPrefix() != "decoded invalid union tag ")
    {
        std::cerr << "decodedInvalidUnionTagPrefix mismatch\n";
        return false;
    }
    if (text::unionFieldMissingForTag("mode", "3") != "union field 'mode' missing for tag 3")
    {
        std::cerr << "unionFieldMissingForTag mismatch\n";
        return false;
    }
    if (text::fieldExpectsExactlyElements("payload", "16", false) != "field 'payload' expects exactly 16 elements")
    {
        std::cerr << "fieldExpectsExactlyElements(field) mismatch\n";
        return false;
    }
    if (text::fieldExpectsExactlyElements("payload", "16", true) != "union field 'payload' expects exactly 16 elements")
    {
        std::cerr << "fieldExpectsExactlyElements(union field) mismatch\n";
        return false;
    }
    if (text::fieldExpectsArray("payload", false) != "field 'payload' expects an array")
    {
        std::cerr << "fieldExpectsArray(field) mismatch\n";
        return false;
    }
    if (text::fieldExpectsArray("payload", true) != "union field 'payload' expects an array")
    {
        std::cerr << "fieldExpectsArray(union field) mismatch\n";
        return false;
    }
    if (text::fieldExceedsMaxLength("payload", "32", false) != "field 'payload' exceeds max length 32")
    {
        std::cerr << "fieldExceedsMaxLength(field) mismatch\n";
        return false;
    }
    if (text::fieldExceedsMaxLength("payload", "32", true) != "union field 'payload' exceeds max length 32")
    {
        std::cerr << "fieldExceedsMaxLength(union field) mismatch\n";
        return false;
    }
    if (text::decodedLengthExceedsMaxLength("payload", "32", false) !=
        "decoded length for field 'payload' exceeds max length 32")
    {
        std::cerr << "decodedLengthExceedsMaxLength(field) mismatch\n";
        return false;
    }
    if (text::decodedLengthExceedsMaxLength("payload", "32", true) !=
        "decoded length for union field 'payload' exceeds max length 32")
    {
        std::cerr << "decodedLengthExceedsMaxLength(union field) mismatch\n";
        return false;
    }
    if (text::encodedCompositePayloadExceedsMaxPayloadBytes("inner", "128") !=
        "encoded payload for composite field 'inner' exceeds max payload bytes 128")
    {
        std::cerr << "encodedCompositePayloadExceedsMaxPayloadBytes mismatch\n";
        return false;
    }
    if (text::encodedCompositePayloadExceedsRemainingBufferSpace("inner") !=
        "encoded payload for composite field 'inner' exceeds remaining buffer space")
    {
        std::cerr << "encodedCompositePayloadExceedsRemainingBufferSpace mismatch\n";
        return false;
    }
    if (text::decodedCompositePayloadExceedsRemainingBufferSpace("inner") !=
        "decoded payload size for composite field 'inner' exceeds remaining buffer space")
    {
        std::cerr << "decodedCompositePayloadExceedsRemainingBufferSpace mismatch\n";
        return false;
    }
    if (text::mlirSchemaCoverageValidationFailedForEmission("TypeScript") !=
        "MLIR schema coverage validation failed for TypeScript emission")
    {
        std::cerr << "mlirSchemaCoverageValidationFailedForEmission mismatch\n";
        return false;
    }

    return true;
}
