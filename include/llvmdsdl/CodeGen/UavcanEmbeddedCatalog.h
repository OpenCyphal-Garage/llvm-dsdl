//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Embedded UAVCAN catalog loader interfaces.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_CODEGEN_UAVCAN_EMBEDDED_CATALOG_H
#define LLVMDSDL_CODEGEN_UAVCAN_EMBEDDED_CATALOG_H

#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "llvmdsdl/Semantics/Model.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/OwningOpRef.h"

namespace mlir
{
class Operation;
class MLIRContext;
}  // namespace mlir

namespace llvmdsdl
{

class DiagnosticEngine;

/// @brief Synthetic source-path prefix used for embedded UAVCAN definitions.
inline constexpr const char* kEmbeddedUavcanSyntheticPathPrefix = "<embedded-uavcan>:";

/// @brief Parsed embedded UAVCAN catalog.
struct UavcanEmbeddedCatalog final
{
    /// @brief Reconstructed semantic module for embedded UAVCAN definitions.
    SemanticModule semantic;

    /// @brief Parsed MLIR module containing all embedded `dsdl.schema` ops.
    mlir::OwningOpRef<mlir::ModuleOp> module;

    /// @brief Embedded type keys (`full_name:major:minor`), ordered.
    ///
    /// Ordered rather than hashed because this set is iterated -- by
    /// suggestionsForSelector and by selector expansion -- and an unordered
    /// container's iteration order is a property of the standard library
    /// implementation, not of the data. Emitted text must not be able to
    /// inherit it. The callers currently funnel every such iteration through an
    /// ordered container of their own, so this changes no output; it removes
    /// the obligation to keep doing so. 189 entries, one lookup against three
    /// iterations, so the hashed container was not buying anything either.
    std::set<std::string> typeKeys;

    /// @brief Embedded schema ops keyed by canonical type key.
    std::unordered_map<std::string, mlir::Operation*> schemaByKey;
};

/// @brief Verifies that `mlirText` hashes to `expectedSha256Hex` (lowercase hex SHA-256).
///
/// The embedded UAVCAN MLIR blob is compiled into the binary alongside a SHA-256 of the exact text
/// it was generated from. Checking the two agree at load time is a defense-in-depth integrity guard:
/// it catches binary/memory corruption, tampering, and a build where the text and its recorded hash
/// drifted out of sync. Pure and side-effect-free so it can be unit-tested with arbitrary pairs.
[[nodiscard]] bool verifyEmbeddedCatalogIntegrity(llvm::StringRef mlirText, llvm::StringRef expectedSha256Hex);

/// @brief Verifies the integrity of the catalog blob actually compiled into this binary.
/// @return True when the embedded MLIR text matches its recorded SHA-256.
[[nodiscard]] bool embeddedUavcanCatalogIntegrityOk();

/// @brief Returns the SHA-256 recorded next to the catalog blob, as lowercase hex.
[[nodiscard]] llvm::StringRef embeddedUavcanCatalogRecordedSha256();

/// @brief Hashes the catalog blob compiled into this binary, as lowercase hex.
///
/// Computed from the text rather than read from the recorded constant, so a failing integrity check
/// can name both halves of the mismatch.
[[nodiscard]] std::string embeddedUavcanCatalogComputedSha256();

/// @brief Repository-relative path of the generated file carrying the catalog text and its hash.
///
/// Named in the integrity-failure diagnostic: both values live in this one generated file, so a
/// mismatch is always a defect in it rather than in the two sources disagreeing.
inline constexpr const char* kEmbeddedUavcanCatalogSourceFile = "lib/CodeGen/UavcanEmbeddedMlir.inc";

/// @brief Loads and parses embedded UAVCAN catalog artifacts.
///
/// Fails (without parsing) when the embedded MLIR blob does not match its recorded SHA-256.
llvm::Expected<UavcanEmbeddedCatalog> loadUavcanEmbeddedCatalog(mlir::MLIRContext& context,
                                                                DiagnosticEngine&  diagnostics);

/// @brief Returns true when `filePath` is synthetic embedded UAVCAN metadata.
bool isEmbeddedUavcanSyntheticPath(const std::string& filePath);

/// @brief Outcome of expanding one embedded-catalog target selector.
struct EmbeddedSelectorExpansion final
{
    /// @brief Matching catalog type keys (`full_name:major:minor`), sorted. Empty means no match.
    std::vector<std::string> typeKeys;

    /// @brief Near-miss candidates in selector spelling, offered when `typeKeys` is empty.
    std::vector<std::string> suggestions;

    /// @brief True when the selector named one exact version (`uavcan.node.Heartbeat.1.0`).
    ///
    /// The other two spellings sweep -- every version of a type, or every type under a namespace --
    /// and a swept version is one a default may drop. A named one is not. False when nothing matched.
    bool namesExactVersion{false};
};

/// @brief Expands one embedded-catalog target selector into catalog type keys.
///
/// @details
/// `selector` is a `+`-sigil CLI target with the sigil already stripped. Three spellings resolve:
/// - `uavcan.node.Heartbeat.1.0` -- one exact version;
/// - `uavcan.node.Heartbeat` -- every version of one type;
/// - `uavcan` / `uavcan.node` -- every type under a namespace.
///
/// Namespace matching is anchored at a dot boundary, so `uavcan.n` selects nothing rather than
/// silently standing in for `uavcan.node`.
///
/// When a selector matches nothing the result carries `suggestions`, so the caller can turn a typo
/// into a diagnostic. A well-formed type name with an unavailable version suggests the versions the
/// catalog does carry.
EmbeddedSelectorExpansion expandEmbeddedCatalogSelector(const UavcanEmbeddedCatalog& catalog, llvm::StringRef selector);

/// @brief Appends embedded schemas for selected keys into destination module.
llvm::Error appendEmbeddedUavcanSchemasForKeys(const UavcanEmbeddedCatalog&           catalog,
                                               mlir::ModuleOp                         destination,
                                               const std::unordered_set<std::string>& selectedTypeKeys);

}  // namespace llvmdsdl

#endif  // LLVMDSDL_CODEGEN_UAVCAN_EMBEDDED_CATALOG_H
