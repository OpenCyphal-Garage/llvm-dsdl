//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Embedded UAVCAN catalog loader implementation.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/UavcanEmbeddedCatalog.h"

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/SHA256.h>
#include <mlir/IR/BuiltinAttributes.h>
#include <mlir/IR/Operation.h>
#include <mlir/IR/OperationSupport.h>
#include <mlir/Parser/Parser.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/Rational.h"

#include "UavcanEmbeddedMlir.inc"

namespace llvmdsdl
{

namespace
{

struct SectionBundle final
{
    SemanticSection section;
    bool            seenPlan{false};
};

std::string trim(std::string value)
{
    const auto isSpace = [](const unsigned char c) { return std::isspace(c) != 0; };

    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
    {
        value.erase(value.begin());
    }
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
    {
        value.pop_back();
    }
    return value;
}

std::string typeKey(llvm::StringRef fullName, std::uint32_t major, std::uint32_t minor)
{
    return fullName.str() + ":" + std::to_string(major) + ":" + std::to_string(minor);
}

std::vector<std::string> splitTypeName(llvm::StringRef fullName)
{
    std::vector<std::string>               out;
    llvm::SmallVector<llvm::StringRef, 16> parts;
    fullName.split(parts, '.', -1, false);
    out.reserve(parts.size());
    for (const auto part : parts)
    {
        out.emplace_back(part.str());
    }
    return out;
}

std::string syntheticFilePath(llvm::StringRef fullName, const std::uint32_t major, const std::uint32_t minor)
{
    return std::string(kEmbeddedUavcanSyntheticPathPrefix) + fullName.str() + "." + std::to_string(major) + "." +
           std::to_string(minor) + ".dsdl";
}

std::optional<std::int64_t> parseSignedInteger(llvm::StringRef text)
{
    std::int64_t parsed{};
    if (text.getAsInteger(10, parsed))
    {
        return std::nullopt;
    }
    return parsed;
}

std::optional<Value> parseConstantValue(const llvm::StringRef text)
{
    const std::string normalized = trim(text.str());
    if (normalized.empty())
    {
        return std::nullopt;
    }

    if (normalized == "true")
    {
        return Value{true};
    }
    if (normalized == "false")
    {
        return Value{false};
    }

    if (normalized.size() >= 2U && normalized.front() == '\'' && normalized.back() == '\'')
    {
        return Value{normalized.substr(1, normalized.size() - 2U)};
    }

    const auto slash = normalized.find('/');
    if (slash == std::string::npos)
    {
        if (const auto numerator = parseSignedInteger(normalized))
        {
            return Value{Rational(*numerator, 1)};
        }
        return std::nullopt;
    }

    const auto lhs = parseSignedInteger(llvm::StringRef(normalized).take_front(slash));
    const auto rhs = parseSignedInteger(llvm::StringRef(normalized).drop_front(slash + 1U));
    if (!lhs || !rhs)
    {
        return std::nullopt;
    }
    return Value{Rational(*lhs, *rhs)};
}

AttachedDoc parseDocAttr(const mlir::StringAttr docAttr)
{
    AttachedDoc out;
    if (!docAttr)
    {
        return out;
    }

    const std::string text  = docAttr.getValue().str();
    std::size_t       start = 0U;
    while (start <= text.size())
    {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end     = newline == std::string::npos ? text.size() : newline;
        out.lines.push_back(AttachedDocLine{{"<embedded-uavcan>", 1, 1}, text.substr(start, end - start)});
        if (newline == std::string::npos)
        {
            break;
        }
        start = newline + 1U;
    }
    return out;
}

TypeExprAST parseConstantType(llvm::StringRef text)
{
    TypeExprAST out;
    out.location = {"<embedded-uavcan>", 1, 1};

    std::string normalized = text.str();
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    PrimitiveTypeExprAST prim;
    if (normalized.find("truncated") != std::string::npos)
    {
        prim.castMode = CastMode::Truncated;
    }
    else
    {
        prim.castMode = CastMode::Saturated;
    }

    const auto parseBitLength = [&](const char* prefix, const std::uint32_t fallback) {
        const auto pos = normalized.find(prefix);
        if (pos == std::string::npos)
        {
            return fallback;
        }
        const auto  digitsStart = pos + std::strlen(prefix);
        std::size_t digitsEnd   = digitsStart;
        while (digitsEnd < normalized.size() && std::isdigit(static_cast<unsigned char>(normalized[digitsEnd])) != 0)
        {
            ++digitsEnd;
        }
        if (digitsEnd == digitsStart)
        {
            return fallback;
        }
        const auto maybeBits = parseSignedInteger(llvm::StringRef(normalized).slice(digitsStart, digitsEnd));
        if (!maybeBits || *maybeBits < 0)
        {
            return fallback;
        }
        return static_cast<std::uint32_t>(*maybeBits);
    };

    if (normalized.find("bool") != std::string::npos)
    {
        prim.kind      = PrimitiveKind::Bool;
        prim.bitLength = 1U;
        out.scalar     = prim;
        return out;
    }
    if (normalized.find("byte") != std::string::npos)
    {
        prim.kind      = PrimitiveKind::Byte;
        prim.bitLength = 8U;
        out.scalar     = prim;
        return out;
    }
    if (normalized.find("utf8") != std::string::npos)
    {
        prim.kind      = PrimitiveKind::Utf8;
        prim.bitLength = 8U;
        out.scalar     = prim;
        return out;
    }
    if (normalized.find("float") != std::string::npos)
    {
        prim.kind      = PrimitiveKind::Float;
        prim.bitLength = parseBitLength("float", 64U);
        out.scalar     = prim;
        return out;
    }
    if (normalized.find("uint") != std::string::npos)
    {
        prim.kind      = PrimitiveKind::UnsignedInt;
        prim.bitLength = parseBitLength("uint", 64U);
        out.scalar     = prim;
        return out;
    }
    if (normalized.find("int") != std::string::npos)
    {
        prim.kind      = PrimitiveKind::SignedInt;
        prim.bitLength = parseBitLength("int", 64U);
        out.scalar     = prim;
        return out;
    }
    if (normalized.find("void") != std::string::npos)
    {
        VoidTypeExprAST padding;
        padding.bitLength = parseBitLength("void", 1U);
        out.scalar        = padding;
        return out;
    }

    out.scalar = prim;
    return out;
}

SemanticScalarCategory parseScalarCategory(const llvm::StringRef value)
{
    if (value == "bool")
    {
        return SemanticScalarCategory::Bool;
    }
    if (value == "byte")
    {
        return SemanticScalarCategory::Byte;
    }
    if (value == "utf8")
    {
        return SemanticScalarCategory::Utf8;
    }
    if (value == "unsigned")
    {
        return SemanticScalarCategory::UnsignedInt;
    }
    if (value == "signed")
    {
        return SemanticScalarCategory::SignedInt;
    }
    if (value == "float")
    {
        return SemanticScalarCategory::Float;
    }
    if (value == "composite")
    {
        return SemanticScalarCategory::Composite;
    }
    return SemanticScalarCategory::Void;
}

CastMode parseCastMode(const llvm::StringRef value)
{
    if (value == "truncated")
    {
        return CastMode::Truncated;
    }
    return CastMode::Saturated;
}

ArrayKind parseArrayKind(const llvm::StringRef value)
{
    if (value == "fixed")
    {
        return ArrayKind::Fixed;
    }
    if (value == "variable_inclusive")
    {
        return ArrayKind::VariableInclusive;
    }
    if (value == "variable_exclusive")
    {
        return ArrayKind::VariableExclusive;
    }
    return ArrayKind::None;
}

SemanticTypeRef parseTypeRef(const llvm::StringRef fullName, const std::uint32_t major, const std::uint32_t minor)
{
    SemanticTypeRef out;
    out.fullName            = fullName.str();
    out.namespaceComponents = splitTypeName(fullName);
    if (!out.namespaceComponents.empty())
    {
        out.shortName = out.namespaceComponents.back();
        out.namespaceComponents.pop_back();
    }
    out.majorVersion = major;
    out.minorVersion = minor;
    return out;
}

BitLengthSet makeBitLengthSet(const std::int64_t minBits, const std::int64_t maxBits)
{
    if (minBits == maxBits)
    {
        return BitLengthSet(minBits);
    }
    return BitLengthSet(std::set<std::int64_t>{minBits, maxBits});
}

bool parseSectionPlan(mlir::Operation&   plan,
                      const std::string& sectionName,
                      const bool         defaultSealed,
                      const std::int64_t defaultExtentBits,
                      SemanticSection&   section,
                      DiagnosticEngine&  diagnostics)
{
    if (const auto isUnion = plan.getAttrOfType<mlir::UnitAttr>("is_union"))
    {
        (void) isUnion;
        section.isUnion = true;
    }

    const auto minBits = plan.getAttrOfType<mlir::IntegerAttr>("min_bits");
    const auto maxBits = plan.getAttrOfType<mlir::IntegerAttr>("max_bits");
    if (!minBits || !maxBits)
    {
        diagnostics.error({"<embedded-uavcan>", 1, 1},
                          "embedded dsdl.serialization_plan missing min_bits/max_bits for section '" + sectionName +
                              "'");
        return false;
    }

    const auto minValue = static_cast<std::int64_t>(minBits.getInt());
    const auto maxValue = static_cast<std::int64_t>(maxBits.getInt());

    section.minBitLength                = minValue;
    section.maxBitLength                = maxValue;
    section.fixedSize                   = plan.hasAttr("fixed_size");
    section.offsetAtEnd                 = makeBitLengthSet(minValue, maxValue);
    section.serializationBufferSizeBits = maxValue;

    section.sealed = defaultSealed;
    if (plan.hasAttr("sealed"))
    {
        section.sealed = true;
    }

    if (const auto extentBits = plan.getAttrOfType<mlir::IntegerAttr>("extent_bits"))
    {
        section.extentBits = static_cast<std::int64_t>(extentBits.getInt());
    }
    else if (section.sealed)
    {
        section.extentBits = maxValue;
    }
    else if (defaultExtentBits >= 0)
    {
        section.extentBits = defaultExtentBits;
    }
    else
    {
        section.extentBits = maxValue;
    }

    if (plan.getNumRegions() == 0 || plan.getRegion(0).empty())
    {
        return true;
    }

    for (mlir::Operation& step : plan.getRegion(0).front())
    {
        if (step.getName().getStringRef() != "dsdl.io")
        {
            continue;
        }

        const auto nameAttr = step.getAttrOfType<mlir::StringAttr>("name");
        const auto kindAttr = step.getAttrOfType<mlir::StringAttr>("kind");
        if (!nameAttr || !kindAttr)
        {
            diagnostics.error({"<embedded-uavcan>", 1, 1},
                              "embedded dsdl.io op missing name/kind in section '" + sectionName + "'");
            return false;
        }

        SemanticField field;
        field.name        = nameAttr.getValue().str();
        field.isPadding   = kindAttr.getValue() == "padding";
        field.sectionName = sectionName;
        field.doc         = parseDocAttr(step.getAttrOfType<mlir::StringAttr>("doc"));

        const auto scalarCategoryAttr = step.getAttrOfType<mlir::StringAttr>("scalar_category");
        const auto castModeAttr       = step.getAttrOfType<mlir::StringAttr>("cast_mode");
        const auto arrayKindAttr      = step.getAttrOfType<mlir::StringAttr>("array_kind");
        const auto bitLengthAttr      = step.getAttrOfType<mlir::IntegerAttr>("bit_length");
        const auto capacityAttr       = step.getAttrOfType<mlir::IntegerAttr>("array_capacity");
        const auto prefixAttr         = step.getAttrOfType<mlir::IntegerAttr>("array_length_prefix_bits");
        const auto alignAttr          = step.getAttrOfType<mlir::IntegerAttr>("alignment_bits");
        const auto minBitsAttr        = step.getAttrOfType<mlir::IntegerAttr>("min_bits");
        const auto maxBitsAttr        = step.getAttrOfType<mlir::IntegerAttr>("max_bits");

        field.resolvedType.scalarCategory =
            scalarCategoryAttr ? parseScalarCategory(scalarCategoryAttr.getValue()) : SemanticScalarCategory::Void;
        field.resolvedType.castMode      = castModeAttr ? parseCastMode(castModeAttr.getValue()) : CastMode::Saturated;
        field.resolvedType.arrayKind     = arrayKindAttr ? parseArrayKind(arrayKindAttr.getValue()) : ArrayKind::None;
        field.resolvedType.bitLength     = bitLengthAttr ? static_cast<std::uint32_t>(bitLengthAttr.getInt()) : 0U;
        field.resolvedType.arrayCapacity = capacityAttr ? static_cast<std::int64_t>(capacityAttr.getInt()) : 0;
        field.resolvedType.arrayLengthPrefixBits = prefixAttr ? static_cast<std::int64_t>(prefixAttr.getInt()) : 0;
        field.resolvedType.alignmentBits         = alignAttr ? static_cast<std::int64_t>(alignAttr.getInt()) : 1;

        const auto stepMinBits          = minBitsAttr ? static_cast<std::int64_t>(minBitsAttr.getInt()) : 0;
        const auto stepMaxBits          = maxBitsAttr ? static_cast<std::int64_t>(maxBitsAttr.getInt()) : stepMinBits;
        field.resolvedType.bitLengthSet = makeBitLengthSet(stepMinBits, stepMaxBits);

        if (const auto unionIndex = step.getAttrOfType<mlir::IntegerAttr>("union_option_index"))
        {
            field.unionOptionIndex = static_cast<std::uint32_t>(std::max<std::int64_t>(0, unionIndex.getInt()));
        }
        if (const auto unionTagBits = step.getAttrOfType<mlir::IntegerAttr>("union_tag_bits"))
        {
            field.unionTagBits = static_cast<std::uint32_t>(std::max<std::int64_t>(0, unionTagBits.getInt()));
        }

        if (const auto compositeName = step.getAttrOfType<mlir::StringAttr>("composite_full_name"))
        {
            const auto    majorAttr = step.getAttrOfType<mlir::IntegerAttr>("composite_major");
            const auto    minorAttr = step.getAttrOfType<mlir::IntegerAttr>("composite_minor");
            std::uint32_t major     = 0U;
            std::uint32_t minor     = 0U;
            if (majorAttr)
            {
                major = static_cast<std::uint32_t>(std::max<std::int64_t>(0, majorAttr.getInt()));
            }
            if (minorAttr)
            {
                minor = static_cast<std::uint32_t>(std::max<std::int64_t>(0, minorAttr.getInt()));
            }
            field.resolvedType.compositeType   = parseTypeRef(compositeName.getValue(), major, minor);
            field.resolvedType.compositeSealed = step.getAttrOfType<mlir::BoolAttr>("composite_sealed")
                                                     ? step.getAttrOfType<mlir::BoolAttr>("composite_sealed").getValue()
                                                     : true;
            if (const auto extentBits = step.getAttrOfType<mlir::IntegerAttr>("composite_extent_bits"))
            {
                field.resolvedType.compositeExtentBits = static_cast<std::int64_t>(extentBits.getInt());
            }
        }

        if (const auto typeNameAttr = step.getAttrOfType<mlir::StringAttr>("type_name"))
        {
            field.type = parseConstantType(typeNameAttr.getValue());
        }

        section.fields.push_back(std::move(field));
    }

    return true;
}

bool parseSemanticDefinition(mlir::Operation& schema, SemanticDefinition& out, DiagnosticEngine& diagnostics)
{
    const auto fullNameAttr = schema.getAttrOfType<mlir::StringAttr>("full_name");
    const auto majorAttr    = schema.getAttrOfType<mlir::IntegerAttr>("major");
    const auto minorAttr    = schema.getAttrOfType<mlir::IntegerAttr>("minor");
    if (!fullNameAttr || !majorAttr || !minorAttr)
    {
        diagnostics.error({"<embedded-uavcan>", 1, 1}, "embedded dsdl.schema missing identity attributes");
        return false;
    }

    const auto fullName = fullNameAttr.getValue();
    const auto major    = static_cast<std::uint32_t>(std::max<std::int64_t>(0, majorAttr.getInt()));
    const auto minor    = static_cast<std::uint32_t>(std::max<std::int64_t>(0, minorAttr.getInt()));

    out.info.fullName            = fullName.str();
    out.info.namespaceComponents = splitTypeName(fullName);
    if (out.info.namespaceComponents.empty())
    {
        diagnostics.error({"<embedded-uavcan>", 1, 1}, "embedded dsdl.schema has empty full_name");
        return false;
    }
    out.info.shortName = out.info.namespaceComponents.back();
    out.info.namespaceComponents.pop_back();

    out.info.majorVersion      = major;
    out.info.minorVersion      = minor;
    out.info.filePath          = syntheticFilePath(fullName, major, minor);
    out.info.rootNamespacePath = "<embedded-uavcan>";
    out.info.text              = "";
    out.doc                    = parseDocAttr(schema.getAttrOfType<mlir::StringAttr>("doc"));

    if (const auto fixedPortId = schema.getAttrOfType<mlir::IntegerAttr>("fixed_port_id"))
    {
        out.info.fixedPortId = static_cast<std::uint32_t>(std::max<std::int64_t>(0, fixedPortId.getInt()));
    }

    out.isService = schema.hasAttr("service");

    const bool   requestSealed     = schema.hasAttr("sealed");
    std::int64_t requestExtentBits = -1;
    if (const auto extentBits = schema.getAttrOfType<mlir::IntegerAttr>("extent_bits"))
    {
        requestExtentBits = static_cast<std::int64_t>(extentBits.getInt());
    }

    std::unordered_map<std::string, SectionBundle> sections;
    sections.emplace("", SectionBundle{});
    sections.emplace("request", SectionBundle{});
    sections.emplace("response", SectionBundle{});

    if (schema.getNumRegions() == 0 || schema.getRegion(0).empty())
    {
        diagnostics.error({"<embedded-uavcan>", 1, 1}, "embedded dsdl.schema has empty body region");
        return false;
    }

    for (mlir::Operation& child : schema.getRegion(0).front())
    {
        const auto opName = child.getName().getStringRef();
        if (opName == "dsdl.serialization_plan")
        {
            std::string sectionName;
            if (const auto sectionAttr = child.getAttrOfType<mlir::StringAttr>("section"))
            {
                sectionName = sectionAttr.getValue().str();
            }

            auto it = sections.find(sectionName);
            if (it == sections.end())
            {
                it = sections.emplace(sectionName, SectionBundle{}).first;
            }
            if (it->second.seenPlan)
            {
                diagnostics.error({"<embedded-uavcan>", 1, 1},
                                  "duplicate embedded serialization plan for section '" + sectionName + "'");
                return false;
            }

            const bool         sectionSealed = sectionName == "request" || sectionName.empty() ? requestSealed : false;
            const std::int64_t sectionExtent = sectionName == "request" || sectionName.empty() ? requestExtentBits : -1;
            if (!parseSectionPlan(child, sectionName, sectionSealed, sectionExtent, it->second.section, diagnostics))
            {
                return false;
            }

            it->second.section.deprecated =
                schema.hasAttr("deprecated") &&
                (sectionName.empty() || sectionName == "request" || sectionName == "response");
            it->second.seenPlan = true;
            continue;
        }

        if (opName == "dsdl.constant")
        {
            std::string sectionName;
            if (const auto sectionAttr = child.getAttrOfType<mlir::StringAttr>("section"))
            {
                sectionName = sectionAttr.getValue().str();
            }

            auto it = sections.find(sectionName);
            if (it == sections.end())
            {
                it = sections.emplace(sectionName, SectionBundle{}).first;
            }

            const auto nameAttr  = child.getAttrOfType<mlir::StringAttr>("name");
            const auto typeAttr  = child.getAttrOfType<mlir::StringAttr>("type_name");
            const auto valueAttr = child.getAttrOfType<mlir::StringAttr>("value_text");
            if (!nameAttr || !typeAttr || !valueAttr)
            {
                diagnostics.error({"<embedded-uavcan>", 1, 1},
                                  "embedded dsdl.constant missing name/type_name/value_text");
                return false;
            }

            const auto value = parseConstantValue(valueAttr.getValue());
            if (!value)
            {
                diagnostics.error({"<embedded-uavcan>", 1, 1},
                                  "failed to parse embedded constant value: " + valueAttr.getValue().str());
                return false;
            }

            it->second.section.constants.push_back(
                SemanticConstant{nameAttr.getValue().str(),
                                 parseDocAttr(child.getAttrOfType<mlir::StringAttr>("doc")),
                                 parseConstantType(typeAttr.getValue()),
                                 *value});
            continue;
        }
    }

    if (out.isService)
    {
        if (!sections["request"].seenPlan || !sections["response"].seenPlan)
        {
            diagnostics.error({"<embedded-uavcan>", 1, 1},
                              "embedded service schema missing request/response serialization plans for " +
                                  out.info.fullName);
            return false;
        }
        out.request  = sections["request"].section;
        out.response = sections["response"].section;
        if (out.response)
        {
            out.response->deprecated = out.request.deprecated;
        }
    }
    else
    {
        if (!sections[""].seenPlan)
        {
            diagnostics.error({"<embedded-uavcan>", 1, 1},
                              "embedded message schema missing primary serialization plan for " + out.info.fullName);
            return false;
        }
        out.request = sections[""].section;
    }

    return true;
}

}  // namespace

bool verifyEmbeddedCatalogIntegrity(llvm::StringRef mlirText, llvm::StringRef expectedSha256Hex)
{
    const std::array<uint8_t, 32> digest = llvm::SHA256::hash(
        llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(mlirText.data()), mlirText.size()));
    const std::string actual = llvm::toHex(digest, /*LowerCase=*/true);
    return actual == expectedSha256Hex;
}

bool embeddedUavcanCatalogIntegrityOk()
{
    return verifyEmbeddedCatalogIntegrity(uavcan_embedded_mlir::kEmbeddedUavcanMlirText,
                                          uavcan_embedded_mlir::kEmbeddedUavcanMlirSha256);
}

llvm::StringRef embeddedUavcanCatalogRecordedSha256()
{
    return uavcan_embedded_mlir::kEmbeddedUavcanMlirSha256;
}

std::string embeddedUavcanCatalogComputedSha256()
{
    const llvm::StringRef        text = uavcan_embedded_mlir::kEmbeddedUavcanMlirText;
    const std::array<uint8_t, 32> digest =
        llvm::SHA256::hash(llvm::ArrayRef<uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
    return llvm::toHex(digest, /*LowerCase=*/true);
}

llvm::Expected<UavcanEmbeddedCatalog> loadUavcanEmbeddedCatalog(mlir::MLIRContext& context,
                                                                DiagnosticEngine&  diagnostics)
{
    // Integrity gate: refuse to parse a blob that does not match its recorded SHA-256 (corruption,
    // tampering, or a text/hash mismatch introduced during the build).
    if (!embeddedUavcanCatalogIntegrityOk())
    {
        const SourceLocation here{"<embedded-uavcan>", 1, 1};
        diagnostics.error(here, "embedded UAVCAN catalog failed SHA-256 integrity verification");
        diagnostics.note(here,
                         "recorded " + embeddedUavcanCatalogRecordedSha256().str() + ", computed " +
                             embeddedUavcanCatalogComputedSha256());
        diagnostics.note(here,
                         std::string("both values are generated into ") + kEmbeddedUavcanCatalogSourceFile +
                             ", so they disagree only when that file was hand-edited, partially regenerated, "
                             "or merged badly");
        diagnostics.note(here,
                         std::string("restore it with 'git checkout -- ") + kEmbeddedUavcanCatalogSourceFile +
                             "', or regenerate it with 'python3 tools/dsdlc/generate_embedded_uavcan_mlir.py "
                             "--dsdlc <path-to-dsdlc>', then rebuild");
        diagnostics.note(here,
                         "to build without the compiled-in catalog, pass --no-embedded-uavcan and add "
                         "--lookup-dir <checkout>/public_regulated_data_types/uavcan");
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "embedded UAVCAN catalog failed SHA-256 integrity verification");
    }

    auto module = mlir::parseSourceString<mlir::ModuleOp>(uavcan_embedded_mlir::kEmbeddedUavcanMlirText, &context);
    if (!module)
    {
        diagnostics.error({"<embedded-uavcan>", 1, 1}, "failed to parse embedded UAVCAN MLIR module");
        return llvm::createStringError(llvm::inconvertibleErrorCode(), "failed to parse embedded UAVCAN MLIR module");
    }

    UavcanEmbeddedCatalog catalog;
    catalog.module = std::move(module);

    for (mlir::Operation& op : catalog.module->getBodyRegion().front())
    {
        if (op.getName().getStringRef() != "dsdl.schema")
        {
            continue;
        }

        const auto fullNameAttr = op.getAttrOfType<mlir::StringAttr>("full_name");
        const auto majorAttr    = op.getAttrOfType<mlir::IntegerAttr>("major");
        const auto minorAttr    = op.getAttrOfType<mlir::IntegerAttr>("minor");
        if (!fullNameAttr || !majorAttr || !minorAttr)
        {
            diagnostics.error({"<embedded-uavcan>", 1, 1}, "embedded dsdl.schema missing key attributes");
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "embedded schema missing key attributes");
        }

        const auto key = typeKey(fullNameAttr.getValue(),
                                 static_cast<std::uint32_t>(std::max<std::int64_t>(0, majorAttr.getInt())),
                                 static_cast<std::uint32_t>(std::max<std::int64_t>(0, minorAttr.getInt())));

        if (!catalog.typeKeys.insert(key).second)
        {
            diagnostics.error({"<embedded-uavcan>", 1, 1}, "duplicate embedded schema key: " + key);
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "duplicate embedded schema key");
        }

        catalog.schemaByKey.emplace(key, &op);

        SemanticDefinition sem;
        if (!parseSemanticDefinition(op, sem, diagnostics))
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "failed to parse embedded schema");
        }
        catalog.semantic.definitions.push_back(std::move(sem));
    }

    std::sort(catalog.semantic.definitions.begin(),
              catalog.semantic.definitions.end(),
              [](const SemanticDefinition& lhs, const SemanticDefinition& rhs) {
                  if (lhs.info.fullName != rhs.info.fullName)
                  {
                      return lhs.info.fullName < rhs.info.fullName;
                  }
                  if (lhs.info.majorVersion != rhs.info.majorVersion)
                  {
                      return lhs.info.majorVersion < rhs.info.majorVersion;
                  }
                  return lhs.info.minorVersion < rhs.info.minorVersion;
              });

    return catalog;
}

bool isEmbeddedUavcanSyntheticPath(const std::string& filePath)
{
    return filePath.rfind(kEmbeddedUavcanSyntheticPathPrefix, 0U) == 0U;
}

namespace
{

/// Full name portion of a `full_name:major:minor` key. Full names never contain a colon.
llvm::StringRef fullNameOfKey(llvm::StringRef key)
{
    return key.take_until([](char c) { return c == ':'; });
}

/// Selector spelling for a key, i.e. the inverse of the key format: `uavcan.node.Heartbeat.1.0`.
std::string selectorSpellingOfKey(llvm::StringRef key)
{
    std::string out = key.str();
    std::replace(out.begin(), out.end(), ':', '.');
    return out;
}

/// Reads a trailing `.<major>.<minor>` off a selector, if both components are numeric.
/// Versions are re-rendered from the parsed integers so `01.0` and `1.0` agree with the key format.
std::optional<std::string> keyFromVersionedSelector(llvm::StringRef selector)
{
    const auto lastDot = selector.rfind('.');
    if (lastDot == llvm::StringRef::npos || lastDot == 0U)
    {
        return std::nullopt;
    }
    const auto priorDot = selector.substr(0, lastDot).rfind('.');
    if (priorDot == llvm::StringRef::npos || priorDot == 0U)
    {
        return std::nullopt;
    }

    const llvm::StringRef fullName = selector.substr(0, priorDot);
    const llvm::StringRef majorRef = selector.substr(priorDot + 1U, lastDot - priorDot - 1U);
    const llvm::StringRef minorRef = selector.substr(lastDot + 1U);

    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    if (majorRef.empty() || minorRef.empty() || majorRef.getAsInteger(10, major) || minorRef.getAsInteger(10, minor))
    {
        return std::nullopt;
    }

    return typeKey(fullName, major, minor);
}

/// True when `fullName` sits at or under `prefix`, anchored at a dot so `uavcan.n` never stands in
/// for `uavcan.node`.
bool isAtOrUnderNamespace(llvm::StringRef fullName, llvm::StringRef prefix)
{
    if (fullName == prefix)
    {
        return true;
    }
    return fullName.starts_with(prefix) && fullName.size() > prefix.size() && fullName[prefix.size()] == '.';
}

std::vector<std::string> suggestionsForSelector(const UavcanEmbeddedCatalog& catalog, llvm::StringRef selector)
{
    // A well-formed type name carrying a version the catalog lacks is the common near-miss, and the
    // useful answer is the versions that do exist rather than a spelling guess.
    if (const auto versioned = keyFromVersionedSelector(selector); versioned.has_value())
    {
        const llvm::StringRef requestedName = fullNameOfKey(*versioned);
        std::set<std::string> versions;
        for (const auto& key : catalog.typeKeys)
        {
            if (fullNameOfKey(key) == requestedName)
            {
                versions.insert(selectorSpellingOfKey(key));
            }
        }
        if (!versions.empty())
        {
            return {versions.begin(), versions.end()};
        }
    }

    // Otherwise rank every type name and namespace by edit distance and offer the closest few.
    std::set<std::string> candidates;
    for (const auto& key : catalog.typeKeys)
    {
        const llvm::StringRef fullName = fullNameOfKey(key);
        candidates.insert(fullName.str());
        for (std::size_t i = 0; i < fullName.size(); ++i)
        {
            if (fullName[i] == '.')
            {
                candidates.insert(fullName.substr(0, i).str());
            }
        }
    }

    std::vector<std::pair<unsigned, std::string>> ranked;
    ranked.reserve(candidates.size());
    for (const auto& candidate : candidates)
    {
        ranked.emplace_back(llvm::StringRef(candidate).edit_distance(selector), candidate);
    }
    std::sort(ranked.begin(), ranked.end());

    // Past roughly a third of the selector's length the "suggestion" is noise, not a correction.
    const unsigned tolerance = std::max<unsigned>(2U, static_cast<unsigned>(selector.size()) / 3U);

    std::vector<std::string> out;
    for (const auto& [distance, candidate] : ranked)
    {
        if (distance > tolerance || out.size() >= 3U)
        {
            break;
        }
        out.push_back(candidate);
    }
    return out;
}

}  // namespace

EmbeddedSelectorExpansion expandEmbeddedCatalogSelector(const UavcanEmbeddedCatalog& catalog,
                                                        const llvm::StringRef        selector)
{
    EmbeddedSelectorExpansion expansion;
    if (selector.empty())
    {
        return expansion;
    }

    // Exact version first: no catalog full name ever equals `<name>.<major>.<minor>`, so this can
    // never race the namespace interpretation below.
    if (const auto versioned = keyFromVersionedSelector(selector); versioned.has_value())
    {
        if (catalog.typeKeys.contains(*versioned))
        {
            expansion.typeKeys.push_back(*versioned);
            return expansion;
        }
    }

    for (const auto& key : catalog.typeKeys)
    {
        if (isAtOrUnderNamespace(fullNameOfKey(key), selector))
        {
            expansion.typeKeys.push_back(key);
        }
    }
    std::sort(expansion.typeKeys.begin(), expansion.typeKeys.end());

    if (expansion.typeKeys.empty())
    {
        expansion.suggestions = suggestionsForSelector(catalog, selector);
    }
    return expansion;
}

llvm::Error appendEmbeddedUavcanSchemasForKeys(const UavcanEmbeddedCatalog&           catalog,
                                               mlir::ModuleOp                         destination,
                                               const std::unordered_set<std::string>& selectedTypeKeys,
                                               DiagnosticEngine&                      diagnostics)
{
    std::unordered_set<std::string> existingKeys;
    for (mlir::Operation& op : destination.getBodyRegion().front())
    {
        if (op.getName().getStringRef() != "dsdl.schema")
        {
            continue;
        }

        const auto fullNameAttr = op.getAttrOfType<mlir::StringAttr>("full_name");
        const auto majorAttr    = op.getAttrOfType<mlir::IntegerAttr>("major");
        const auto minorAttr    = op.getAttrOfType<mlir::IntegerAttr>("minor");
        if (!fullNameAttr || !majorAttr || !minorAttr)
        {
            diagnostics
                .error({"<embedded-uavcan>", 1, 1},
                       "destination module schema op missing key attributes while composing embedded UAVCAN schemas");
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "destination schema missing key attributes");
        }

        existingKeys.insert(typeKey(fullNameAttr.getValue(),
                                    static_cast<std::uint32_t>(std::max<std::int64_t>(0, majorAttr.getInt())),
                                    static_cast<std::uint32_t>(std::max<std::int64_t>(0, minorAttr.getInt()))));
    }

    std::vector<std::string> orderedKeys(selectedTypeKeys.begin(), selectedTypeKeys.end());
    std::sort(orderedKeys.begin(), orderedKeys.end());

    for (const auto& key : orderedKeys)
    {
        if (existingKeys.contains(key))
        {
            continue;
        }

        const auto it = catalog.schemaByKey.find(key);
        if (it == catalog.schemaByKey.end())
        {
            continue;
        }

        destination.getBodyRegion().front().push_back(it->second->clone());
        existingKeys.insert(key);
    }

    return llvm::Error::success();
}

}  // namespace llvmdsdl
