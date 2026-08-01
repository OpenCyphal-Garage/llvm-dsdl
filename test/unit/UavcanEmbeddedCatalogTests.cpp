//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iostream>
#include <unordered_set>

#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/EmitC/IR/EmitC.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/DialectRegistry.h>
#include <llvm/Support/Error.h>

#include "llvmdsdl/CodeGen/UavcanEmbeddedCatalog.h"
#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/Support/Diagnostics.h"

namespace
{

const llvmdsdl::SemanticDefinition* findDefinition(const llvmdsdl::SemanticModule& module,
                                                   const std::string&              fullName,
                                                   const std::uint32_t             major,
                                                   const std::uint32_t             minor)
{
    for (const auto& def : module.definitions)
    {
        if (def.info.fullName == fullName && def.info.majorVersion == major && def.info.minorVersion == minor)
        {
            return &def;
        }
    }
    return nullptr;
}

}  // namespace

bool runUavcanEmbeddedCatalogTests()
{
    // Runtime SHA-256 integrity check (P1 #2).
    // (1) The blob compiled into this binary must match its recorded hash. This also confirms the
    //     C++ string-literal text decodes byte-identically to the text the generator hashed.
    if (!llvmdsdl::embeddedUavcanCatalogIntegrityOk())
    {
        std::cerr << "embedded UAVCAN catalog failed its own SHA-256 integrity check\n";
        return false;
    }
    // (2) The verifier must compute SHA-256 correctly: the empty string has a well-known digest.
    if (!llvmdsdl::verifyEmbeddedCatalogIntegrity(
            "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"))
    {
        std::cerr << "verifyEmbeddedCatalogIntegrity rejected the known empty-string digest\n";
        return false;
    }
    // (3) A mismatched hash must be rejected (the check is not a no-op).
    if (llvmdsdl::verifyEmbeddedCatalogIntegrity(
            "dsdl.schema @tampered {}", "0000000000000000000000000000000000000000000000000000000000000000"))
    {
        std::cerr << "verifyEmbeddedCatalogIntegrity accepted a mismatched hash\n";
        return false;
    }
    // (4) The two accessors the integrity-failure diagnostic quotes must agree on a healthy binary,
    //     and must be the same pair the check itself compares.
    if (llvmdsdl::embeddedUavcanCatalogComputedSha256() != llvmdsdl::embeddedUavcanCatalogRecordedSha256())
    {
        std::cerr << "embedded UAVCAN catalog hash accessors disagree: computed "
                  << llvmdsdl::embeddedUavcanCatalogComputedSha256() << ", recorded "
                  << llvmdsdl::embeddedUavcanCatalogRecordedSha256().str() << "\n";
        return false;
    }

    mlir::DialectRegistry registry;
    registry.insert<mlir::dsdl::DSDLDialect,
                    mlir::func::FuncDialect,
                    mlir::arith::ArithDialect,
                    mlir::scf::SCFDialect,
                    mlir::emitc::EmitCDialect>();

    mlir::MLIRContext context(registry);
    context.getOrLoadDialect<mlir::dsdl::DSDLDialect>();

    llvmdsdl::DiagnosticEngine diagnostics;
    auto                       loaded = llvmdsdl::loadUavcanEmbeddedCatalog(context, diagnostics);
    if (!loaded)
    {
        std::cerr << "failed to load embedded UAVCAN catalog\n";
        llvm::consumeError(loaded.takeError());
        return false;
    }
    if (diagnostics.hasErrors())
    {
        std::cerr << "embedded UAVCAN catalog load emitted diagnostics\n";
        return false;
    }

    const auto& catalog = *loaded;

    if (catalog.semantic.definitions.size() < 100U)
    {
        std::cerr << "embedded UAVCAN catalog has unexpectedly few semantic definitions\n";
        return false;
    }

    if (!catalog.typeKeys.contains("uavcan.node.Heartbeat:1:0"))
    {
        std::cerr << "embedded UAVCAN catalog missing sentinel key uavcan.node.Heartbeat:1:0\n";
        return false;
    }

    const auto* heartbeat = findDefinition(catalog.semantic, "uavcan.node.Heartbeat", 1U, 0U);
    if (heartbeat == nullptr)
    {
        std::cerr << "embedded UAVCAN catalog missing semantic definition uavcan.node.Heartbeat.1.0\n";
        return false;
    }
    if (!llvmdsdl::isEmbeddedUavcanSyntheticPath(heartbeat->info.filePath))
    {
        std::cerr << "embedded UAVCAN semantic definition did not use synthetic source path\n";
        return false;
    }
    if (heartbeat->request.constants.empty() || heartbeat->request.constants.front().doc.empty())
    {
        std::cerr << "embedded UAVCAN semantic constants missing attached docs\n";
        return false;
    }

    const auto* registerValue = findDefinition(catalog.semantic, "uavcan.register.Value", 1U, 0U);
    if (registerValue == nullptr || !registerValue->request.isUnion)
    {
        std::cerr << "embedded UAVCAN catalog failed to preserve union metadata for uavcan.register.Value.1.0\n";
        return false;
    }

    const auto* getInfo = findDefinition(catalog.semantic, "uavcan.node.GetInfo", 1U, 0U);
    if (getInfo == nullptr || !getInfo->isService || !getInfo->response)
    {
        std::cerr
            << "embedded UAVCAN catalog failed to preserve service request/response for uavcan.node.GetInfo.1.0\n";
        return false;
    }

    auto destination = mlir::ModuleOp::create(mlir::UnknownLoc::get(&context));

    std::unordered_set<std::string> selected;
    selected.insert("uavcan.node.Heartbeat:1:0");

    if (auto err = llvmdsdl::appendEmbeddedUavcanSchemasForKeys(catalog, destination, selected, diagnostics))
    {
        std::cerr << "failed to append embedded UAVCAN schema to destination module\n";
        llvm::consumeError(std::move(err));
        return false;
    }

    bool sawHeartbeatSchema = false;
    for (mlir::Operation& op : destination.getBodyRegion().front())
    {
        if (op.getName().getStringRef() != "dsdl.schema")
        {
            continue;
        }

        const auto fullName = op.getAttrOfType<mlir::StringAttr>("full_name");
        const auto major    = op.getAttrOfType<mlir::IntegerAttr>("major");
        const auto minor    = op.getAttrOfType<mlir::IntegerAttr>("minor");
        if (!fullName || !major || !minor)
        {
            continue;
        }

        if (fullName.getValue() == "uavcan.node.Heartbeat" && major.getInt() == 1 && minor.getInt() == 0)
        {
            sawHeartbeatSchema = true;
            break;
        }
    }

    if (!sawHeartbeatSchema)
    {
        std::cerr << "appended embedded UAVCAN module missing heartbeat schema\n";
        return false;
    }

    // '+' target selector expansion.
    const auto exact = llvmdsdl::expandEmbeddedCatalogSelector(catalog, "uavcan.node.Heartbeat.1.0");
    if (exact.typeKeys.size() != 1U || exact.typeKeys.front() != "uavcan.node.Heartbeat:1:0")
    {
        std::cerr << "versioned selector should expand to exactly its own key\n";
        return false;
    }

    // Leading zeros are re-rendered from the parsed integers, so they agree with the key format.
    const auto paddedVersion = llvmdsdl::expandEmbeddedCatalogSelector(catalog, "uavcan.node.Heartbeat.01.00");
    if (paddedVersion.typeKeys != exact.typeKeys)
    {
        std::cerr << "zero-padded versions should resolve identically to their canonical spelling\n";
        return false;
    }

    const auto byTypeName = llvmdsdl::expandEmbeddedCatalogSelector(catalog, "uavcan.node.Heartbeat");
    if (byTypeName.typeKeys.empty())
    {
        std::cerr << "unversioned type selector should expand to that type's versions\n";
        return false;
    }
    for (const auto& key : byTypeName.typeKeys)
    {
        if (key.rfind("uavcan.node.Heartbeat:", 0U) != 0U)
        {
            std::cerr << "unversioned type selector leaked an unrelated key: " << key << "\n";
            return false;
        }
    }

    const auto namespaceKeys = llvmdsdl::expandEmbeddedCatalogSelector(catalog, "uavcan.node");
    const auto rootKeys      = llvmdsdl::expandEmbeddedCatalogSelector(catalog, "uavcan");
    if (namespaceKeys.typeKeys.size() <= byTypeName.typeKeys.size() ||
        rootKeys.typeKeys.size() <= namespaceKeys.typeKeys.size())
    {
        std::cerr << "selector granularity should widen strictly: type < namespace < root\n";
        return false;
    }
    if (rootKeys.typeKeys.size() != catalog.typeKeys.size())
    {
        std::cerr << "root namespace selector should select the whole catalog\n";
        return false;
    }
    if (!std::is_sorted(rootKeys.typeKeys.begin(), rootKeys.typeKeys.end()))
    {
        std::cerr << "selector expansion should return sorted keys\n";
        return false;
    }

    // Namespace matching is anchored at a dot, so a truncated prefix selects nothing rather than
    // silently standing in for the namespace the caller meant.
    const auto partialPrefix = llvmdsdl::expandEmbeddedCatalogSelector(catalog, "uavcan.n");
    if (!partialPrefix.typeKeys.empty())
    {
        std::cerr << "a partial namespace prefix must not match at a non-dot boundary\n";
        return false;
    }
    if (partialPrefix.suggestions.empty())
    {
        std::cerr << "an unmatched selector should carry suggestions\n";
        return false;
    }

    // A known type with an unavailable version suggests the versions that exist.
    const auto badVersion = llvmdsdl::expandEmbeddedCatalogSelector(catalog, "uavcan.node.Heartbeat.9.9");
    if (!badVersion.typeKeys.empty())
    {
        std::cerr << "an unavailable version must not resolve\n";
        return false;
    }
    if (std::find(badVersion.suggestions.begin(), badVersion.suggestions.end(), "uavcan.node.Heartbeat.1.0") ==
        badVersion.suggestions.end())
    {
        std::cerr << "an unavailable version should suggest the versions the catalog carries\n";
        return false;
    }

    const auto typo = llvmdsdl::expandEmbeddedCatalogSelector(catalog, "uavcna");
    if (!typo.typeKeys.empty() ||
        std::find(typo.suggestions.begin(), typo.suggestions.end(), "uavcan") == typo.suggestions.end())
    {
        std::cerr << "a transposed namespace should suggest the namespace it was reaching for\n";
        return false;
    }

    if (!llvmdsdl::expandEmbeddedCatalogSelector(catalog, "").typeKeys.empty())
    {
        std::cerr << "an empty selector must select nothing\n";
        return false;
    }

    return true;
}
