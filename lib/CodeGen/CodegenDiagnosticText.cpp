//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared diagnostic-text catalog for generated runtime contracts.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/CodegenDiagnosticText.h"
#include <string>

namespace llvmdsdl::codegen_diagnostic_text
{
namespace
{

std::string fieldLabel(const bool isUnionField)
{
    return isUnionField ? "union field '" : "field '";
}

}  // namespace

std::string serializationBufferTooSmall()
{
    return "serialization buffer too small";
}

std::string invalidUnionTagPrefix()
{
    return "invalid union tag ";
}

std::string decodedInvalidUnionTagPrefix()
{
    return "decoded invalid union tag ";
}

std::string unionFieldMissingForTag(const std::string& fieldName, const std::string& tagText)
{
    return "union field '" + fieldName + "' missing for tag " + tagText;
}

std::string fieldExpectsExactlyElements(const std::string& fieldName,
                                        const std::string& elementCountText,
                                        const bool         isUnionField)
{
    return fieldLabel(isUnionField) + fieldName + "' expects exactly " + elementCountText + " elements";
}

std::string fieldExpectsArray(const std::string& fieldName, const bool isUnionField)
{
    return fieldLabel(isUnionField) + fieldName + "' expects an array";
}

std::string fieldExceedsMaxLength(const std::string& fieldName,
                                  const std::string& maxLengthText,
                                  const bool         isUnionField)
{
    return fieldLabel(isUnionField) + fieldName + "' exceeds max length " + maxLengthText;
}

std::string decodedLengthExceedsMaxLength(const std::string& fieldName,
                                          const std::string& maxLengthText,
                                          const bool         isUnionField)
{
    return "decoded length for " + fieldLabel(isUnionField) + fieldName + "' exceeds max length " + maxLengthText;
}

std::string encodedCompositePayloadExceedsMaxPayloadBytes(const std::string& fieldName,
                                                          const std::string& maxPayloadBytesText)
{
    return "encoded payload for composite field '" + fieldName + "' exceeds max payload bytes " + maxPayloadBytesText;
}

std::string encodedCompositePayloadExceedsRemainingBufferSpace(const std::string& fieldName)
{
    return "encoded payload for composite field '" + fieldName + "' exceeds remaining buffer space";
}

std::string decodedCompositePayloadExceedsRemainingBufferSpace(const std::string& fieldName)
{
    return "decoded payload size for composite field '" + fieldName + "' exceeds remaining buffer space";
}

std::string mlirSchemaCoverageValidationFailedForEmission(const std::string& backendName)
{
    return "MLIR schema coverage validation failed for " + backendName + " emission";
}

}  // namespace llvmdsdl::codegen_diagnostic_text
