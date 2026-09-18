//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements the two layout verdicts a section carries.
///
/// `wireFlat` asks whether the serialised form is a contiguous byte image. It reads the schema and
/// nothing else, so it is the same answer on every target and is what `@aliasable` asserts.
///
/// `hostImage` asks whether the generated structure is that same image. It reads the schema and the
/// storage widths every backend uses, under natural alignment. Natural alignment is the strictest
/// model a mainstream ABI applies, and a weaker one only removes padding, so a layout that needs
/// none here needs none anywhere; the target's own compiler confirms the answer.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Semantics/AliasLayout.h"

#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/ScalarStorage.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace llvmdsdl
{
namespace
{

/// @brief Identity of a section: a definition's version, plus which of a service's two it is.
using SectionKey = std::tuple<std::string, std::uint32_t, std::uint32_t, bool>;

SectionKey keyOf(const SemanticTypeRef& ref, const bool response)
{
    return {ref.fullName, ref.majorVersion, ref.minorVersion, response};
}

/// @brief What one field contributes to a structure, once it is known to be flat on the wire.
struct HostExtent final
{
    std::int64_t sizeBytes{0};
    std::int64_t alignBytes{1};
};

AliasLayoutVerdict blocked(const AliasLayoutReason reason, std::string fieldName = {})
{
    return AliasLayoutVerdict{/*holds=*/false, reason, std::move(fieldName)};
}

/// @brief The wire width of a field's element, in bits, or zero when it has none of its own.
std::int64_t elementBits(const SemanticFieldType& type)
{
    return static_cast<std::int64_t>(type.bitLength);
}

std::int64_t elementCount(const SemanticFieldType& type)
{
    if (type.arrayKind == ArrayKind::None)
    {
        return 1;
    }
    return std::max<std::int64_t>(type.arrayCapacity, 0);
}

bool isVariableLength(const ArrayKind kind)
{
    return kind == ArrayKind::VariableInclusive || kind == ArrayKind::VariableExclusive;
}

/// @brief Decides both verdicts for every section, resolving composites through an index.
class Evaluator final
{
public:
    Evaluator(SemanticModule& module, const SemanticModule* externalCatalog)
        : module_(module)
    {
        // A local definition wins a key collision, matching how composites resolve during analysis.
        if (externalCatalog != nullptr)
        {
            for (const SemanticDefinition& def : externalCatalog->definitions)
            {
                index(def, /*local=*/false);
            }
        }
        for (const SemanticDefinition& def : module_.definitions)
        {
            index(def, /*local=*/true);
        }
    }

    void run()
    {
        for (SemanticDefinition& def : module_.definitions)
        {
            annotate(def, def.request, /*response=*/false);
            if (def.response)
            {
                annotate(def, *def.response, /*response=*/true);
            }
        }
    }

private:
    struct Entry final
    {
        const SemanticDefinition* def{nullptr};
        bool                      local{false};
    };

    void index(const SemanticDefinition& def, const bool local)
    {
        const SectionKey request{def.info.fullName, def.info.majorVersion, def.info.minorVersion, false};
        if (local || !entries_.contains(request))
        {
            entries_[request] = Entry{&def, local};
        }
        if (def.response)
        {
            const SectionKey response{def.info.fullName, def.info.majorVersion, def.info.minorVersion, true};
            if (local || !entries_.contains(response))
            {
                entries_[response] = Entry{&def, local};
            }
        }
    }

    const SemanticSection* find(const SectionKey& key) const
    {
        const auto it = entries_.find(key);
        if (it == entries_.end())
        {
            return nullptr;
        }
        const SemanticDefinition& def = *it->second.def;
        if (std::get<3>(key))
        {
            return def.response ? &*def.response : nullptr;
        }
        return &def.request;
    }

    void annotate(const SemanticDefinition& def, SemanticSection& section, const bool response)
    {
        const SectionKey key{def.info.fullName, def.info.majorVersion, def.info.minorVersion, response};
        section.wireFlat  = wireFlat(key, section);
        section.hostImage = hostImage(key, section);
    }

    /// @brief Is the serialised form a contiguous byte image?
    AliasLayoutVerdict wireFlat(const SectionKey& key, const SemanticSection& section)
    {
        if (const auto cached = wireFlatCache_.find(key); cached != wireFlatCache_.end())
        {
            return cached->second;
        }
        if (!visiting_.insert(key).second)
        {
            return blocked(AliasLayoutReason::NestedUnresolved);
        }
        AliasLayoutVerdict verdict = computeWireFlat(section);
        visiting_.erase(key);
        wireFlatCache_[key] = verdict;
        return verdict;
    }

    AliasLayoutVerdict computeWireFlat(const SemanticSection& section)
    {
        if (!section.sealed)
        {
            return blocked(AliasLayoutReason::NotSealed);
        }
        // A union's fields are its options, so they are not a sequence and have no shared offsets.
        if (section.isUnion)
        {
            return blocked(AliasLayoutReason::UnionType);
        }
        // A varying length is a consequence; the field walk below names the field it comes from,
        // and `NotFixedSize` is the answer only when no field accounts for it.

        std::int64_t offsetBits = 0;
        bool         hasPayload = false;
        for (const SemanticField& field : section.fields)
        {
            const SemanticFieldType& type = field.resolvedType;
            // Padding carries no name, so a refusal here could name nothing. Let it move the
            // cursor instead: the field it displaces is named below, which is the one to change.
            if (field.isPadding)
            {
                offsetBits += elementBits(type) * elementCount(type);
                continue;
            }
            hasPayload = true;
            if ((offsetBits % 8) != 0)
            {
                return blocked(AliasLayoutReason::UnalignedField, field.name);
            }
            if (isVariableLength(type.arrayKind))
            {
                return blocked(AliasLayoutReason::VariableArray, field.name);
            }
            if (type.scalarCategory == SemanticScalarCategory::Composite)
            {
                if (!type.compositeType)
                {
                    return blocked(AliasLayoutReason::NestedUnresolved, field.name);
                }
                const SectionKey       nestedKey = keyOf(*type.compositeType, /*response=*/false);
                const SemanticSection* nested    = find(nestedKey);
                if (nested == nullptr)
                {
                    return blocked(AliasLayoutReason::NestedUnresolved, field.name);
                }
                const AliasLayoutVerdict nestedVerdict = wireFlat(nestedKey, *nested);
                if (!nestedVerdict.holds)
                {
                    return blocked(AliasLayoutReason::NestedNotFlat, field.name);
                }
                offsetBits += nested->maxBitLength * elementCount(type);
                continue;
            }
            const std::int64_t bits = elementBits(type);
            if (bits <= 0 || (bits % 8) != 0)
            {
                return blocked(AliasLayoutReason::SubByteField, field.name);
            }
            offsetBits += bits * elementCount(type);
        }
        if (!section.fixedSize)
        {
            return blocked(AliasLayoutReason::NotFixedSize);
        }
        if (!hasPayload)
        {
            return blocked(AliasLayoutReason::EmptyLayout);
        }
        // Trailing padding that does not reach a byte boundary: the section's own length is not a
        // whole number of bytes, which is about the section rather than any one field.
        if ((offsetBits % 8) != 0)
        {
            return blocked(AliasLayoutReason::SubByteField);
        }
        return AliasLayoutVerdict{/*holds=*/true, AliasLayoutReason::None, {}};
    }

    /// @brief Is the generated structure that same byte image?
    AliasLayoutVerdict hostImage(const SectionKey& key, const SemanticSection& section)
    {
        if (const auto cached = hostImageCache_.find(key); cached != hostImageCache_.end())
        {
            return cached->second.verdict;
        }
        if (!visiting_.insert(key).second)
        {
            return blocked(AliasLayoutReason::NestedUnresolved);
        }
        const Resolved resolved = computeHostImage(section);
        visiting_.erase(key);
        hostImageCache_[key] = resolved;
        return resolved.verdict;
    }

    struct Resolved final
    {
        AliasLayoutVerdict verdict;
        HostExtent         extent;
    };

    Resolved computeHostImage(const SemanticSection& section)
    {
        // Every way a wire form fails to be flat is also a way the structure fails to mirror it.
        const AliasLayoutVerdict flat = computeWireFlat(section);
        if (!flat.holds)
        {
            return Resolved{flat, {}};
        }

        std::int64_t offsetBytes = 0;
        std::int64_t alignBytes  = 1;
        for (const SemanticField& field : section.fields)
        {
            const SemanticFieldType& type = field.resolvedType;
            // The wire reserves these bytes and the structure holds no member for them.
            if (field.isPadding)
            {
                return Resolved{blocked(AliasLayoutReason::WirePadding, field.name), {}};
            }

            HostExtent element;
            if (type.scalarCategory == SemanticScalarCategory::Composite)
            {
                const SectionKey       nestedKey = keyOf(*type.compositeType, /*response=*/false);
                const SemanticSection* nested    = find(nestedKey);
                if (nested == nullptr)
                {
                    return Resolved{blocked(AliasLayoutReason::NestedUnresolved, field.name), {}};
                }
                if (const auto cached = hostImageCache_.find(nestedKey); cached != hostImageCache_.end())
                {
                    if (!cached->second.verdict.holds)
                    {
                        return Resolved{blocked(AliasLayoutReason::NestedNotFlat, field.name), {}};
                    }
                    element = cached->second.extent;
                }
                else
                {
                    if (!visiting_.insert(nestedKey).second)
                    {
                        return Resolved{blocked(AliasLayoutReason::NestedUnresolved, field.name), {}};
                    }
                    const Resolved nestedResolved = computeHostImage(*nested);
                    visiting_.erase(nestedKey);
                    hostImageCache_[nestedKey] = nestedResolved;
                    if (!nestedResolved.verdict.holds)
                    {
                        return Resolved{blocked(AliasLayoutReason::NestedNotFlat, field.name), {}};
                    }
                    element = nestedResolved.extent;
                }
            }
            else
            {
                const auto bits    = static_cast<std::uint32_t>(elementBits(type));
                const auto storage = (type.scalarCategory == SemanticScalarCategory::Float) ? floatStorageBits(bits)
                                                                                            : scalarStorageBits(bits);
                if (storage != bits)
                {
                    return Resolved{blocked(AliasLayoutReason::StorageWidth, field.name), {}};
                }
                element.sizeBytes  = static_cast<std::int64_t>(bits / 8U);
                element.alignBytes = element.sizeBytes;
            }

            if ((offsetBytes % element.alignBytes) != 0)
            {
                return Resolved{blocked(AliasLayoutReason::HostPadding, field.name), {}};
            }
            offsetBytes += element.sizeBytes * elementCount(type);
            alignBytes = std::max(alignBytes, element.alignBytes);
        }
        // Trailing padding to the structure's own alignment.
        if ((offsetBytes % alignBytes) != 0)
        {
            return Resolved{blocked(AliasLayoutReason::HostPadding), {}};
        }
        return Resolved{AliasLayoutVerdict{/*holds=*/true, AliasLayoutReason::None, {}},
                        HostExtent{offsetBytes, alignBytes}};
    }

    SemanticModule&                          module_;
    std::map<SectionKey, Entry>              entries_;
    std::map<SectionKey, AliasLayoutVerdict> wireFlatCache_;
    std::map<SectionKey, Resolved>           hostImageCache_;
    std::set<SectionKey>                     visiting_;
};

}  // namespace

void annotateAliasLayout(SemanticModule& module, const SemanticModule* const externalCatalog)
{
    Evaluator evaluator(module, externalCatalog);
    evaluator.run();
}

llvm::StringRef aliasLayoutReasonToken(const AliasLayoutReason reason)
{
    switch (reason)
    {
    case AliasLayoutReason::None:
        return "flat";
    case AliasLayoutReason::NotSealed:
        return "not-sealed";
    case AliasLayoutReason::NotFixedSize:
        return "not-fixed-size";
    case AliasLayoutReason::UnionType:
        return "union-type";
    case AliasLayoutReason::EmptyLayout:
        return "empty-layout";
    case AliasLayoutReason::VariableArray:
        return "variable-array";
    case AliasLayoutReason::SubByteField:
        return "sub-byte-field";
    case AliasLayoutReason::UnalignedField:
        return "unaligned-field";
    case AliasLayoutReason::NestedNotFlat:
        return "nested-not-flat";
    case AliasLayoutReason::NestedUnresolved:
        return "nested-unresolved";
    case AliasLayoutReason::WirePadding:
        return "wire-padding";
    case AliasLayoutReason::StorageWidth:
        return "storage-width";
    case AliasLayoutReason::HostPadding:
        return "host-padding";
    }
    return "flat";
}

std::string describeAliasLayoutVerdict(const AliasLayoutVerdict& verdict)
{
    const std::string field = verdict.fieldName.empty() ? std::string{} : ("field '" + verdict.fieldName + "' ");
    switch (verdict.reason)
    {
    case AliasLayoutReason::None:
        return "the layout is a flat byte image";
    case AliasLayoutReason::NotSealed:
        return "the type is delimited, so a length header precedes its payload; add @sealed";
    case AliasLayoutReason::NotFixedSize:
        return "the serialised length varies with the value";
    case AliasLayoutReason::UnionType:
        return "a union serialises a tag and one option, so it has no single layout";
    case AliasLayoutReason::EmptyLayout:
        return "the type has no fields";
    case AliasLayoutReason::VariableArray:
        return field + "is a variable-length array, so the fields after it move";
    case AliasLayoutReason::SubByteField:
        return field + "is not a whole number of bytes wide";
    case AliasLayoutReason::UnalignedField:
        return field + "does not begin on a byte boundary";
    case AliasLayoutReason::NestedNotFlat:
        return field + "has a type whose own layout is not a flat byte image";
    case AliasLayoutReason::NestedUnresolved:
        return field + "has a type that could not be resolved";
    case AliasLayoutReason::WirePadding:
        return field + "is padding, which the wire reserves and the generated type does not hold";
    case AliasLayoutReason::StorageWidth:
        return field + "is stored wider than the wire carries it";
    case AliasLayoutReason::HostPadding:
        return verdict.fieldName.empty() ? std::string{"the generated type would carry padding after its last field"}
                                         : (field + "would follow alignment padding in the generated type");
    }
    return "the layout is a flat byte image";
}

}  // namespace llvmdsdl
