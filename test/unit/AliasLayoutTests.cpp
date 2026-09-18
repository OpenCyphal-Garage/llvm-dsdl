//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// One case per reason the two layout verdicts can give.
///
/// The catalogue census measures the feature's size; these measure what decides it. Each case names
/// the property under test and the field it is about, because that pairing is what `@aliasable`
/// will put in front of an author.
///
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <llvm/Support/Error.h>

#include "UnitTests.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Frontend/Discovery.h"
#include "llvmdsdl/Frontend/Lexer.h"
#include "llvmdsdl/Frontend/Parser.h"
#include "llvmdsdl/Semantics/AliasLayout.h"
#include "llvmdsdl/Semantics/Analyzer.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/Diagnostics.h"

namespace
{

/// @brief One definition to put into a module: its short name and its text.
struct Source final
{
    std::string shortName;
    std::string text;
};

/// @brief Analyses `sources` as one module under `vendor`, so a later one may name an earlier one.
bool analyseModule(const std::vector<Source>& sources, llvmdsdl::SemanticModule& out)
{
    llvmdsdl::ASTModule module;
    for (const Source& source : sources)
    {
        const std::string fileName = "vendor." + source.shortName + ".1.0.dsdl";

        llvmdsdl::DiagnosticEngine parseDiag;
        llvmdsdl::Lexer            lexer(fileName, source.text);
        llvmdsdl::Parser           parser(fileName, lexer.lex(), parseDiag);
        auto                       parsed = parser.parseDefinition();
        if (!parsed || parseDiag.hasErrors())
        {
            std::cerr << "alias layout: failed to parse " << fileName << "\n";
            return false;
        }

        llvmdsdl::DiscoveredDefinition discovered;
        discovered.fullName            = "vendor." + source.shortName;
        discovered.shortName           = source.shortName;
        discovered.namespaceComponents = {"vendor"};
        discovered.majorVersion        = 1;
        discovered.minorVersion        = 0;
        discovered.text                = source.text;
        module.definitions.push_back(llvmdsdl::ParsedDefinition{discovered, *parsed});
    }

    llvmdsdl::DiagnosticEngine semDiag;
    auto                       semantic = llvmdsdl::analyze(module, semDiag);
    if (!semantic || semDiag.hasErrors())
    {
        if (!semantic)
        {
            llvm::consumeError(semantic.takeError());
        }
        std::cerr << "alias layout: analysis failed\n";
        return false;
    }
    out = std::move(*semantic);
    return true;
}

const llvmdsdl::SemanticSection* sectionOf(const llvmdsdl::SemanticModule& module, const std::string& shortName)
{
    for (const llvmdsdl::SemanticDefinition& def : module.definitions)
    {
        if (def.info.shortName == shortName)
        {
            return &def.request;
        }
    }
    return nullptr;
}

/// @brief Checks one section's verdicts, and the field a refusal names.
bool expect(const llvmdsdl::SemanticModule&   module,
            const std::string&                shortName,
            const bool                        wireFlat,
            const llvmdsdl::AliasLayoutReason wireFlatReason,
            const bool                        hostImage,
            const llvmdsdl::AliasLayoutReason hostImageReason,
            const std::string&                blamedField = {})
{
    const llvmdsdl::SemanticSection* section = sectionOf(module, shortName);
    if (section == nullptr)
    {
        std::cerr << "alias layout: no section for " << shortName << "\n";
        return false;
    }
    bool ok = true;
    if ((section->wireFlat.holds != wireFlat) || (section->wireFlat.reason != wireFlatReason))
    {
        std::cerr << "alias layout: " << shortName << " wire_flat is " << section->wireFlat.holds << " '"
                  << std::string(llvmdsdl::aliasLayoutReasonToken(section->wireFlat.reason)) << "', expected "
                  << wireFlat << " '" << std::string(llvmdsdl::aliasLayoutReasonToken(wireFlatReason)) << "'\n";
        ok = false;
    }
    if ((section->hostImage.holds != hostImage) || (section->hostImage.reason != hostImageReason))
    {
        std::cerr << "alias layout: " << shortName << " host_image is " << section->hostImage.holds << " '"
                  << std::string(llvmdsdl::aliasLayoutReasonToken(section->hostImage.reason)) << "', expected "
                  << hostImage << " '" << std::string(llvmdsdl::aliasLayoutReasonToken(hostImageReason)) << "'\n";
        ok = false;
    }
    // A reason about a field names it; a reason about the section names none.
    if (!blamedField.empty())
    {
        const std::string named = hostImage ? section->wireFlat.fieldName : section->hostImage.fieldName;
        if (named != blamedField)
        {
            std::cerr << "alias layout: " << shortName << " blames '" << named << "', expected '" << blamedField
                      << "'\n";
            ok = false;
        }
    }
    return ok;
}

}  // namespace

bool runAliasLayoutTests()
{
    using llvmdsdl::AliasLayoutReason;
    bool ok = true;

    // Flat on the wire and a byte image in memory: every field is a whole number of bytes and the
    // host stores each in exactly the width the wire carries it.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Flat", "uint32 a\nuint32 b\n@sealed\n"}}, module))
        {
            return false;
        }
        ok = expect(module, "Flat", true, AliasLayoutReason::None, true, AliasLayoutReason::None) && ok;
    }

    // The two verdicts are independent. `uint8` then `uint16` is three contiguous bytes on the wire,
    // and four in a structure that aligns the second field to two.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"HostPadding", "uint8 a\nuint16 b\n@sealed\n"}}, module))
        {
            return false;
        }
        ok = expect(module, "HostPadding", true, AliasLayoutReason::None, false, AliasLayoutReason::HostPadding, "b") &&
             ok;
    }

    // A width the wire carries in five bytes and the host holds in eight.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"StorageWidth", "uint40 a\n@sealed\n"}}, module))
        {
            return false;
        }
        ok = expect(module,
                    "StorageWidth",
                    true,
                    AliasLayoutReason::None,
                    false,
                    AliasLayoutReason::StorageWidth,
                    "a") &&
             ok;
    }

    // A `float16` is held in single precision, so it is two bytes on the wire and four in memory.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"NarrowFloat", "float16 a\n@sealed\n"}}, module))
        {
            return false;
        }
        ok =
            expect(module, "NarrowFloat", true, AliasLayoutReason::None, false, AliasLayoutReason::StorageWidth, "a") &&
            ok;
    }

    // The wire reserves padding bytes and the structure holds no member for them.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"WirePadding", "uint8 a\nvoid8\nuint8 b\n@sealed\n"}}, module))
        {
            return false;
        }
        ok = expect(module, "WirePadding", true, AliasLayoutReason::None, false, AliasLayoutReason::WirePadding) && ok;
    }

    // Recursion is what makes the feature compose: a record of two flat records is itself flat, and
    // is a byte image when the nesting adds no alignment of its own.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Vec3", "float32 x\nfloat32 y\nfloat32 z\n@sealed\n"},
                            {"Pose", "vendor.Vec3.1.0 position\nvendor.Vec3.1.0 orientation\n@sealed\n"}},
                           module))
        {
            return false;
        }
        ok = expect(module, "Vec3", true, AliasLayoutReason::None, true, AliasLayoutReason::None) && ok;
        ok = expect(module, "Pose", true, AliasLayoutReason::None, true, AliasLayoutReason::None) && ok;
    }

    // A nested type that is not flat makes its holder not flat either, and the holder names the
    // field rather than repeating the nested type's own reason.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Health", "uint2 value\n@sealed\n"},
                            {"Holder", "vendor.Health.1.0 health\nuint8 code\n@sealed\n"}},
                           module))
        {
            return false;
        }
        ok = expect(module,
                    "Health",
                    false,
                    AliasLayoutReason::SubByteField,
                    false,
                    AliasLayoutReason::SubByteField,
                    "value") &&
             ok;
        ok = expect(module,
                    "Holder",
                    false,
                    AliasLayoutReason::NestedNotFlat,
                    false,
                    AliasLayoutReason::NestedNotFlat,
                    "health") &&
             ok;
    }

    // DSDL aligns a nested composite to a byte and C aligns it to its strictest member, so a
    // seven-byte composite before a `float32` is eleven bytes on the wire and twelve in memory.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Seven", "uint8[7] blob\n@sealed\n"},
                            {"Mixed", "vendor.Seven.1.0 head\nfloat32 value\n@sealed\n"}},
                           module))
        {
            return false;
        }
        ok = expect(module, "Seven", true, AliasLayoutReason::None, true, AliasLayoutReason::None) && ok;
        ok = expect(module, "Mixed", true, AliasLayoutReason::None, false, AliasLayoutReason::HostPadding, "value") &&
             ok;
    }

    // A fixed array advances the cursor by its whole length, so the field after it is measured where
    // an encoder would put it.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"FixedArray", "uint8[3] head\nuint8 tail\n@sealed\n"}}, module))
        {
            return false;
        }
        ok = expect(module, "FixedArray", true, AliasLayoutReason::None, true, AliasLayoutReason::None) && ok;
    }

    // A varying length is a consequence, and the field walk names the field it comes from.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Variable", "uint8 head\nuint8[<=8] tail\n@sealed\n"}}, module))
        {
            return false;
        }
        ok = expect(module,
                    "Variable",
                    false,
                    AliasLayoutReason::VariableArray,
                    false,
                    AliasLayoutReason::VariableArray,
                    "tail") &&
             ok;
    }

    // Sub-byte padding leaves the field after it off a byte boundary.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Unaligned", "void4\nuint8 value\n@sealed\n"}}, module))
        {
            return false;
        }
        ok = expect(module,
                    "Unaligned",
                    false,
                    AliasLayoutReason::UnalignedField,
                    false,
                    AliasLayoutReason::UnalignedField,
                    "value") &&
             ok;
    }

    // A union serialises a tag and one option, so its fields are not a sequence at all.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Choice", "@union\nuint32 a\nuint32 b\n@sealed\n"}}, module))
        {
            return false;
        }
        ok = expect(module, "Choice", false, AliasLayoutReason::UnionType, false, AliasLayoutReason::UnionType) && ok;
    }

    // A delimited type carries a length header when nested, and its payload may be longer or
    // shorter than this version's layout.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Delimited", "uint32 a\n@extent 64\n"}}, module))
        {
            return false;
        }
        ok =
            expect(module, "Delimited", false, AliasLayoutReason::NotSealed, false, AliasLayoutReason::NotSealed) && ok;
    }

    // A type with no fields has no layout to be an image of.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Empty", "@sealed\n"}}, module))
        {
            return false;
        }
        ok =
            expect(module, "Empty", false, AliasLayoutReason::EmptyLayout, false, AliasLayoutReason::EmptyLayout) && ok;
    }

    // A fixed array's run is what has to land on a byte boundary, not each element. `bool[8]` is one
    // byte on the wire; the host stores each element in eight bits, so it is not a byte image.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Mask", "bool[8] flags\nuint8 code\n@sealed\n"}}, module))
        {
            return false;
        }
        ok = expect(module, "Mask", true, AliasLayoutReason::None, false, AliasLayoutReason::StorageWidth, "flags") &&
             ok;
    }

    // Four bools are half a byte, so the run does not land on a boundary.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Odd", "bool[4] flags\n@sealed\n"}}, module))
        {
            return false;
        }
        ok = expect(module,
                    "Odd",
                    false,
                    AliasLayoutReason::SubByteField,
                    false,
                    AliasLayoutReason::SubByteField,
                    "flags") &&
             ok;
    }

    // A field blocker outranks sealing. An unsealed type with a sub-byte field reports the field,
    // which is the part that is hard to change; sealing is mentioned alongside it by the directive
    // check rather than standing in for it here.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"UnsealedAndNarrow", "uint2 health\nuint8 code\n@extent 64\n"}}, module))
        {
            return false;
        }
        ok = expect(module,
                    "UnsealedAndNarrow",
                    false,
                    AliasLayoutReason::SubByteField,
                    false,
                    AliasLayoutReason::SubByteField,
                    "health") &&
             ok;
    }

    // A delimited type whose fields are all clean is refused for the sealing alone.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"UnsealedButClean", "uint32 a\n@extent 64\n"}}, module))
        {
            return false;
        }
        ok = expect(module,
                    "UnsealedButClean",
                    false,
                    AliasLayoutReason::NotSealed,
                    false,
                    AliasLayoutReason::NotSealed) &&
             ok;
    }

    // A nested refusal carries the nested type's own verdict, so a diagnostic can name the cause
    // rather than sending the author to another file to find it.
    {
        llvmdsdl::SemanticModule module;
        if (!analyseModule({{"Narrow", "uint2 value\n@sealed\n"},
                            {"Outer", "vendor.Narrow.1.0 inner\nuint8 code\n@sealed\n"}},
                           module))
        {
            return false;
        }
        const llvmdsdl::SemanticSection* outer = sectionOf(module, "Outer");
        if ((outer == nullptr) || (outer->wireFlat.nestedTypeName != "vendor.Narrow.1.0") ||
            (outer->wireFlat.nestedReason != AliasLayoutReason::SubByteField) ||
            (outer->wireFlat.nestedFieldName != "value"))
        {
            std::cerr << "alias layout: Outer does not carry Narrow's own verdict\n";
            ok = false;
        }
    }

    return ok;
}
