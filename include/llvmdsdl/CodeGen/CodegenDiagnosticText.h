//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Shared diagnostic-text catalog for generated runtime contracts.
///
/// The functions in this file provide parity-locked diagnostic text fragments
/// used across multiple language emitters. Emitters remain responsible for
/// language-specific wrapping (e.g. `throw`, `raise`, error-code mapping).
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_CODEGEN_DIAGNOSTIC_TEXT_H
#define LLVMDSDL_CODEGEN_CODEGEN_DIAGNOSTIC_TEXT_H

#include <string>

namespace llvmdsdl::codegen_diagnostic_text
{

/// @brief Returns canonical serialization buffer-capacity failure text.
/// @return Diagnostic text.
std::string serializationBufferTooSmall();

/// @brief Returns canonical invalid-union-tag text prefix.
/// @return Diagnostic prefix that expects a tag value suffix.
std::string invalidUnionTagPrefix();

/// @brief Returns canonical decoded-invalid-union-tag text prefix.
/// @return Diagnostic prefix that expects a decoded tag value suffix.
std::string decodedInvalidUnionTagPrefix();

/// @brief Returns canonical missing-union-field-for-tag text.
/// @param[in] fieldName Field identifier.
/// @param[in] tagText Tag literal text.
/// @return Diagnostic text.
std::string unionFieldMissingForTag(const std::string& fieldName, const std::string& tagText);

/// @brief Returns canonical fixed-array cardinality expectation text.
/// @param[in] fieldName Field identifier.
/// @param[in] elementCountText Required element count text.
/// @param[in] isUnionField True for union-field wording.
/// @return Diagnostic text.
std::string fieldExpectsExactlyElements(const std::string& fieldName,
                                        const std::string& elementCountText,
                                        bool               isUnionField);

/// @brief Returns canonical array-type expectation text.
/// @param[in] fieldName Field identifier.
/// @param[in] isUnionField True for union-field wording.
/// @return Diagnostic text.
std::string fieldExpectsArray(const std::string& fieldName, bool isUnionField);

/// @brief Returns canonical array-capacity violation text.
/// @param[in] fieldName Field identifier.
/// @param[in] maxLengthText Max-length literal text.
/// @param[in] isUnionField True for union-field wording.
/// @return Diagnostic text.
std::string fieldExceedsMaxLength(const std::string& fieldName, const std::string& maxLengthText, bool isUnionField);

/// @brief Returns canonical decoded-length-capacity violation text.
/// @param[in] fieldName Field identifier.
/// @param[in] maxLengthText Max-length literal text.
/// @param[in] isUnionField True for union-field wording.
/// @return Diagnostic text.
std::string decodedLengthExceedsMaxLength(const std::string& fieldName,
                                          const std::string& maxLengthText,
                                          bool               isUnionField);

/// @brief Returns canonical encoded-delimited-payload max-size violation text.
/// @param[in] fieldName Composite field identifier.
/// @param[in] maxPayloadBytesText Max payload bytes literal text.
/// @return Diagnostic text.
std::string encodedCompositePayloadExceedsMaxPayloadBytes(const std::string& fieldName,
                                                          const std::string& maxPayloadBytesText);

/// @brief Returns canonical encoded-delimited-payload remaining-space violation text.
/// @param[in] fieldName Composite field identifier.
/// @return Diagnostic text.
std::string encodedCompositePayloadExceedsRemainingBufferSpace(const std::string& fieldName);

/// @brief Returns canonical decoded-delimited-payload remaining-space violation text.
/// @param[in] fieldName Composite field identifier.
/// @return Diagnostic text.
std::string decodedCompositePayloadExceedsRemainingBufferSpace(const std::string& fieldName);

/// @brief Returns canonical MLIR schema-coverage preflight failure text.
/// @param[in] backendName Backend display label.
/// @return Diagnostic text.
std::string mlirSchemaCoverageValidationFailedForEmission(const std::string& backendName);

}  // namespace llvmdsdl::codegen_diagnostic_text

#endif  // LLVMDSDL_CODEGEN_CODEGEN_DIAGNOSTIC_TEXT_H
