//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements verification and operation glue for DSDL MLIR ops.
///
/// Operation-specific semantic checks and folders are defined here alongside generated operation class inclusions.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/IR/DSDLOps.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/STLFunctionalExtras.h>
#include <llvm/ADT/StringMap.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Casting.h>
#include <cstddef>
#include <cstdint>

#include "llvmdsdl/IR/DSDLAttrs.h"
#include "llvmdsdl/Support/LanguageTraits.h"
#include "llvmdsdl/Support/PlanSymbol.h"
#include "llvmdsdl/Support/SurfacePlan.h"
#include "llvmdsdl/Transforms/LoweredSerDesContract.h"
#include "mlir/IR/Block.h"
#include "mlir/IR/Builders.h"           // IWYU pragma: keep
#include "mlir/IR/BuiltinAttributes.h"  // IWYU pragma: keep
#include "mlir/IR/Diagnostics.h"        // IWYU pragma: keep
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Support/LLVM.h"

using namespace mlir;
using namespace mlir::dsdl;

namespace
{

bool isSupportedScalarCategory(llvm::StringRef category)
{
    return category == "bool" || category == "byte" || category == "utf8" || category == "unsigned" ||
           category == "signed" || category == "float" || category == "void" || category == "composite";
}

bool isSupportedCastMode(llvm::StringRef castMode)
{
    return castMode == "saturated" || castMode == "truncated";
}

bool isVariableArrayKind(llvm::StringRef arrayKind)
{
    return arrayKind == "variable_inclusive" || arrayKind == "variable_exclusive";
}

bool isSupportedArrayKind(llvm::StringRef arrayKind)
{
    return arrayKind == "none" || arrayKind == "fixed" || isVariableArrayKind(arrayKind);
}

/// @brief The section an op belongs to; empty for a message, which has only one.
llvm::StringRef sectionOf(const std::optional<llvm::StringRef> section)
{
    return section.value_or(llvm::StringRef{});
}

/// @brief Checks one section's union option indices against its plan.
///
/// The tag value that selects an option is stamped twice -- on the `dsdl.field` that names it and on
/// the `dsdl.io` step that serialises it -- because the name and the wire layout live in two
/// different halves of the schema and each half needs the pair. They are produced by the same walk
/// over one section, so a disagreement here is a lowering bug rather than malformed input, which is
/// the reason to check it: a backend that reads one half and a serialiser that reads the other would
/// otherwise disagree silently about which option a tag means.
///
/// @param[in] plan The section's plan.
/// @param[in] fields The section's fields, in declaration order.
/// @param[in] emitError Sink for the diagnostic.
/// @return Success when the section's options agree.
LogicalResult verifyUnionOptionIndices(SerializationPlanOp                                 plan,
                                       llvm::ArrayRef<FieldOp>                             fields,
                                       const llvm::function_ref<InFlightDiagnostic(Twine)> emitError)
{
    if (!plan.getIsUnion())
    {
        for (FieldOp field : fields)
        {
            if (field.getUnionOptionIndex())
            {
                return emitError("'union_option_index' on a field of a section that is not a union");
            }
        }
        return success();
    }

    // Schema space is introspection: a plan written by hand carries no `dsdl.field` ops at all, and
    // that is a plan with nothing to cross-check rather than a plan missing something. What the pair
    // rules out is a section that names its options and disagrees with itself.
    if (fields.empty())
    {
        return success();
    }

    std::int64_t expected = 0;
    for (FieldOp field : fields)
    {
        const auto optionIndex = field.getUnionOptionIndex();
        if (!optionIndex)
        {
            return emitError("union option '" + field.getName() + "' requires a 'union_option_index'");
        }
        if (*optionIndex != expected)
        {
            return emitError("union option '" + field.getName() + "' has index " + Twine(*optionIndex) + ", expected " +
                             Twine(expected) + " from its declaration order");
        }
        ++expected;
    }

    if (const auto optionCount = plan.getUnionOptionCount())
    {
        if (*optionCount != expected)
        {
            return emitError("'union_option_count' is " + Twine(*optionCount) + ", but the section declares " +
                             Twine(expected) + " options");
        }
    }

    // The plan's own steps carry the same index. Pairing is positional: a union has one field step
    // per option, in option order.
    std::size_t stepPosition = 0;
    for (Operation& step : plan.getBody().front())
    {
        auto io = llvm::dyn_cast<IOOp>(step);
        if (!io || (io.getKind() != "field"))
        {
            continue;
        }
        if (stepPosition >= fields.size())
        {
            return emitError("the plan has more option steps than the section declares options");
        }
        FieldOp            option   = fields[stepPosition];
        const std::int64_t declared = *option.getUnionOptionIndex();
        // Pairing is by position, so the names are what confirm the two lists describe the same
        // options in the same order. Without this an index-for-index match would accept a schema
        // that names one option where its plan reads another, and the metadata a backend emits
        // would name an option the serialiser does not write.
        if (io.getName() != option.getName())
        {
            return emitError("option " + Twine(stepPosition) + " is '" + option.getName() + "' in schema space and '" +
                             io.getName() + "' in its plan step");
        }
        if (io.getUnionOptionIndex() != declared)
        {
            return emitError("option '" + option.getName() + "' is index " + Twine(declared) + " in schema space and " +
                             Twine(io.getUnionOptionIndex()) + " in its plan step");
        }
        ++stepPosition;
    }
    if (stepPosition != fields.size())
    {
        return emitError("the section declares " + Twine(fields.size()) + " options, but the plan has " +
                         Twine(stepPosition) + " option steps");
    }
    return success();
}

}  // namespace

LogicalResult SchemaOp::verify()
{
    if (!getSealed() && !getExtentBitsAttr())
    {
        return emitOpError("requires either sealed or extent");
    }
    if ((*this)->getNumRegions() == 0 || (*this)->getRegion(0).empty())
    {
        return emitOpError("must contain a non-empty body region");
    }

    // Fields and plans are siblings in the schema body, one group per section. A message has a
    // single unnamed section; a service has `request` and `response`.
    llvm::StringMap<llvm::SmallVector<FieldOp, 8>> fieldsBySection;
    for (Operation& op : (*this)->getRegion(0).front())
    {
        if (auto field = llvm::dyn_cast<FieldOp>(op); field && !field.getPadding())
        {
            fieldsBySection[sectionOf(field.getSection())].push_back(field);
        }
    }
    for (Operation& op : (*this)->getRegion(0).front())
    {
        auto plan = llvm::dyn_cast<SerializationPlanOp>(op);
        if (!plan || plan.getBody().empty())
        {
            continue;
        }
        const llvm::StringRef section = sectionOf(plan.getSection());
        if (failed(verifyUnionOptionIndices(plan, fieldsBySection.lookup(section), [&](const Twine message) {
                return emitOpError(message);
            })))
        {
            return failure();
        }
    }
    return success();
}

LogicalResult SerializationPlanOp::verify()
{
    if (getBody().empty())
    {
        return emitOpError("must contain a non-empty body region");
    }

    const std::int64_t minBits = getMinBits();
    const std::int64_t maxBits = getMaxBits();
    if (minBits < 0 || maxBits < 0 || maxBits < minBits)
    {
        return emitOpError("invalid min_bits/max_bits plan metadata");
    }

    if (getLowered())
    {
        const auto loweredContractVersion = (*this)->getAttrOfType<IntegerAttr>("llvmdsdl.lowered_contract_version");
        if (!loweredContractVersion ||
            !llvmdsdl::isSupportedLoweredSerDesContractVersion(loweredContractVersion.getInt()))
        {
            return emitOpError("lowered plan requires supported llvmdsdl.lowered_contract_version");
        }
        const auto loweredContractProducer = (*this)->getAttrOfType<StringAttr>("llvmdsdl.lowered_contract_producer");
        if (!loweredContractProducer || loweredContractProducer.getValue() != llvmdsdl::kLoweredSerDesContractProducer)
        {
            return emitOpError("lowered plan requires llvmdsdl.lowered_contract_producer=" +
                               std::string(llvmdsdl::kLoweredSerDesContractProducer));
        }

        // All six are read before any failure check so that a plan missing several of them reports
        // every missing attribute in one pass.
        auto require = [&](const std::optional<std::int64_t> value,
                           const llvm::StringRef             name) -> FailureOr<std::int64_t> {
            if (!value)
            {
                emitOpError("missing required '" + name.str() + "' plan attribute");
                return failure();
            }
            if (*value < 0)
            {
                emitOpError("invalid '" + name.str() + "' plan metadata");
                return failure();
            }
            return *value;
        };
        const auto lMinBits      = require(getLoweredMinBits(), getLoweredMinBitsAttrName());
        const auto lMaxBits      = require(getLoweredMaxBits(), getLoweredMaxBitsAttrName());
        const auto lStepCount    = require(getLoweredStepCount(), getLoweredStepCountAttrName());
        const auto lFieldCount   = require(getLoweredFieldCount(), getLoweredFieldCountAttrName());
        const auto lPaddingCount = require(getLoweredPaddingCount(), getLoweredPaddingCountAttrName());
        const auto lAlignCount   = require(getLoweredAlignCount(), getLoweredAlignCountAttrName());
        if (failed(lMinBits) || failed(lMaxBits) || failed(lStepCount) || failed(lFieldCount) ||
            failed(lPaddingCount) || failed(lAlignCount))
        {
            return failure();
        }
        if (*lMaxBits < *lMinBits)
        {
            return emitOpError("invalid lowered_min_bits/lowered_max_bits plan metadata");
        }
        if (*lMinBits != minBits || *lMaxBits != maxBits)
        {
            return emitOpError("lowered_min_bits/lowered_max_bits must match min_bits/max_bits");
        }
    }

    return success();
}

LogicalResult SerializationPlanOp::verifyRegions()
{
    const bool loweredPlan = getLowered();
    // Present when the plan is lowered: verify() has already required them.
    const std::int64_t loweredStepCount    = getLoweredStepCount().value_or(0);
    const std::int64_t loweredFieldCount   = getLoweredFieldCount().value_or(0);
    const std::int64_t loweredPaddingCount = getLoweredPaddingCount().value_or(0);
    const std::int64_t loweredAlignCount   = getLoweredAlignCount().value_or(0);

    std::set<std::int64_t> unionOptionIndexes;
    std::set<std::int64_t> seenStepIndexes;
    std::int64_t           observedStepCount    = 0;
    std::int64_t           observedFieldCount   = 0;
    std::int64_t           observedPaddingCount = 0;
    std::int64_t           observedAlignCount   = 0;
    auto                   recordStepIndex      = [&](Operation& op, const std::optional<std::int64_t> stepIndex) {
        if (!stepIndex)
        {
            return op.emitError("missing required 'step_index' attribute in lowered plan");
        }
        if (*stepIndex < 0)
        {
            return op.emitError("invalid negative step_index in lowered plan");
        }
        if (!seenStepIndexes.insert(*stepIndex).second)
        {
            return op.emitError("duplicate step_index in lowered plan");
        }
        return InFlightDiagnostic();
    };
    for (Operation& op : getBody().front())
    {
        if (auto align = dyn_cast<AlignOp>(op))
        {
            ++observedStepCount;
            ++observedAlignCount;
            if (loweredPlan)
            {
                if (failed(recordStepIndex(op, align.getStepIndex())))
                {
                    return failure();
                }
                if (align.getBits() <= 1)
                {
                    return op.emitError("lowered plan cannot contain no-op alignment");
                }
            }
            continue;
        }
        if (auto io = dyn_cast<IOOp>(op))
        {
            ++observedStepCount;
            if (io.isPadding())
            {
                ++observedPaddingCount;
            }
            else
            {
                ++observedFieldCount;
                if (getIsUnion())
                {
                    unionOptionIndexes.insert(io.getUnionOptionIndex());
                }
            }

            if (loweredPlan)
            {
                if (failed(recordStepIndex(op, io.getStepIndex())))
                {
                    return failure();
                }
                const auto loweredBits = io.getLoweredBits();
                if (!loweredBits)
                {
                    return op.emitError("missing required lowered_bits step metadata in lowered plan");
                }
                if (*loweredBits < 0 || *loweredBits != io.getMaxBits())
                {
                    return op.emitError("invalid min_bits/max_bits/lowered_bits step metadata in lowered plan");
                }
            }
            continue;
        }
        return op.emitError("unsupported operation in serialisation plan body");
    }

    if (getIsUnion())
    {
        const auto unionTagBits     = getUnionTagBits();
        const auto unionOptionCount = getUnionOptionCount();
        if (!unionTagBits || !unionOptionCount)
        {
            return emitOpError("union plan missing union_tag_bits/union_option_count metadata");
        }
        if (*unionTagBits <= 0 || *unionTagBits > 64)
        {
            return emitOpError("union plan has invalid union_tag_bits");
        }
        if (unionOptionIndexes.empty())
        {
            return emitOpError("union plan has no selectable options");
        }
        if (*unionOptionCount <= 0)
        {
            return emitOpError("union plan has invalid union_option_count");
        }
        if (loweredPlan && std::cmp_not_equal(*unionOptionCount, unionOptionIndexes.size()))
        {
            return emitOpError("lowered union_option_count does not match selectable options");
        }
    }

    if (loweredPlan)
    {
        if (observedStepCount != loweredStepCount || observedFieldCount != loweredFieldCount ||
            observedPaddingCount != loweredPaddingCount || observedAlignCount != loweredAlignCount)
        {
            return emitOpError("lowered step counters do not match serialisation plan body");
        }
        for (const auto stepIndex : seenStepIndexes)
        {
            if (stepIndex >= loweredStepCount)
            {
                return emitOpError("step_index out of lowered_step_count bounds");
            }
        }
    }

    return success();
}

LogicalResult AlignOp::verify()
{
    if (getBits() <= 0)
    {
        return emitOpError("requires positive 'bits' value");
    }
    return success();
}

LogicalResult ImageReadOp::verify()
{
    if (getBytes() <= 0)
    {
        return emitOpError("a payload of no bytes has nothing to move");
    }
    return success();
}

LogicalResult ImageWriteOp::verify()
{
    if (getBytes() <= 0)
    {
        return emitOpError("a payload of no bytes has nothing to move");
    }
    return success();
}

LogicalResult FieldOp::verify()
{
    // Padding fields (e.g. void8) are anonymous by construction; only named fields
    // must carry a name.
    if (!getPadding() && getName().empty())
    {
        return emitOpError("requires a non-empty 'name' for non-padding fields");
    }
    if (getTypeName().empty())
    {
        return emitOpError("requires a non-empty 'type_name'");
    }
    if (const auto optionIndex = getUnionOptionIndex())
    {
        if (*optionIndex < 0)
        {
            return emitOpError("invalid 'union_option_index'");
        }
        if (getPadding())
        {
            return emitOpError("a padding field is not a union option and carries no 'union_option_index'");
        }
    }
    return success();
}

LogicalResult ConstantOp::verify()
{
    if (getName().empty())
    {
        return emitOpError("requires a non-empty 'name'");
    }
    if (getTypeName().empty())
    {
        return emitOpError("requires a non-empty 'type_name'");
    }
    if (getValueText().empty())
    {
        return emitOpError("requires a non-empty 'value_text'");
    }
    return success();
}

LogicalResult IOOp::verify()
{
    const auto kind = getKind();
    if (kind != "field" && kind != "padding")
    {
        return emitOpError("unsupported 'kind' value");
    }
    const auto scalarCategory = getScalarCategory();
    if (!isSupportedScalarCategory(scalarCategory))
    {
        return emitOpError("unsupported 'scalar_category' value");
    }
    if (!isSupportedCastMode(getCastMode()))
    {
        return emitOpError("unsupported 'cast_mode' value");
    }
    if (!isSupportedArrayKind(getArrayKind()))
    {
        return emitOpError("unsupported 'array_kind' value");
    }
    if ((kind == "padding") != (scalarCategory == "void"))
    {
        return emitOpError("a padding step is a void category, and a void category is a padding step");
    }

    const std::int64_t minBits = getMinBits();
    const std::int64_t maxBits = getMaxBits();
    if (minBits < 0 || maxBits < 0 || maxBits < minBits)
    {
        return emitOpError("invalid min_bits/max_bits metadata");
    }

    const std::int64_t bitLength             = getBitLength();
    const std::int64_t arrayCapacity         = getArrayCapacity();
    const std::int64_t arrayLengthPrefixBits = getArrayLengthPrefixBits();
    if (bitLength < 0 || arrayCapacity < 0 || arrayLengthPrefixBits < 0)
    {
        return emitOpError("invalid bit_length/array_capacity/array_length_prefix_bits metadata");
    }

    // Defense-in-depth for the Cyphal Specification primitive bit-length ranges.
    // The frontend already rejects out-of-range widths, but enforcing it here
    // guarantees no downstream pass or hand-authored IR can smuggle a
    // non-conformant scalar (e.g. int1, uint100, float8) into codegen. Signed
    // and unsigned integers have different minimums per the spec: signed [2, 64],
    // unsigned [1, 64].
    if (scalarCategory == "signed")
    {
        if (bitLength < 2 || bitLength > 64)
        {
            return emitOpError("invalid scalar bit_length metadata; signed integer widths must be in [2, 64]");
        }
    }
    else if (scalarCategory == "unsigned")
    {
        if (bitLength < 1 || bitLength > 64)
        {
            return emitOpError("invalid scalar bit_length metadata; unsigned integer widths must be in [1, 64]");
        }
    }
    else if (scalarCategory == "float")
    {
        if (bitLength != 16 && bitLength != 32 && bitLength != 64)
        {
            return emitOpError("invalid scalar bit_length metadata; floating-point width must be 16, 32, or 64");
        }
    }
    else if (scalarCategory == "void")
    {
        // Void categories model alignment/spacer padding. The frontend enforces
        // the spec range [1, 64] for user-written `voidN` fields; the lowering
        // pipeline may additionally synthesize a degenerate 0-bit padding that is
        // dropped downstream, so 0 is admitted here.
        if (bitLength > 64)
        {
            return emitOpError("invalid scalar bit_length metadata; void widths must be in [0, 64]");
        }
    }
    if (getAlignmentBits() <= 0)
    {
        return emitOpError("invalid alignment_bits metadata");
    }
    if (getHeldAsView() && !isComposite())
    {
        return emitOpError("held_as_view on a step that is not a composite");
    }
    if (getUnionOptionIndex() < 0)
    {
        return emitOpError("invalid union_option_index metadata");
    }
    if (getUnionTagBits() < 0 || getUnionTagBits() > 64)
    {
        return emitOpError("invalid union_tag_bits metadata");
    }

    if (kind == "field" && isVariableArrayKind(getArrayKind()))
    {
        if (arrayLengthPrefixBits <= 0)
        {
            return emitOpError("variable array field requires positive prefix width");
        }
        if (arrayLengthPrefixBits > 64)
        {
            return emitOpError("variable array field prefix width exceeds 64 bits");
        }
    }

    const bool namesComposite = getCompositeFullNameAttr() || getCompositeMajor() || getCompositeMinor() ||
                                getCompositeSealed() || getCompositeExtentBits();
    if (scalarCategory == "composite")
    {
        if (!getCompositeFullNameAttr() || !getCompositeMajor() || !getCompositeMinor() || !getCompositeSealed() ||
            !getCompositeExtentBits())
        {
            return emitOpError("a composite step carries composite_full_name, composite_major, composite_minor, "
                               "composite_sealed and composite_extent_bits");
        }
    }
    else if (namesComposite)
    {
        return emitOpError("only a composite step carries composite_* attributes");
    }

    return success();
}

//===----------------------------------------------------------------------===//
// The surface tree
//===----------------------------------------------------------------------===//

namespace
{

using Partition = llvmdsdl::NamePartition;

/// @brief The partition @p nameClass falls in, or none where the language has no such class.
std::optional<Partition> partitionOf(const NameClass nameClass, const llvmdsdl::NameClasses& classes)
{
    switch (nameClass)
    {
    case NameClass::Value:
        return llvmdsdl::namePartition(classes, llvmdsdl::NameClass::Value);
    case NameClass::Type:
        return llvmdsdl::namePartition(classes, llvmdsdl::NameClass::Type);
    case NameClass::Module:
        return llvmdsdl::namePartition(classes, llvmdsdl::NameClass::Module);
    case NameClass::Tag:
        return llvmdsdl::namePartition(classes, llvmdsdl::NameClass::Tag);
    case NameClass::Macro:
        return llvmdsdl::namePartition(classes, llvmdsdl::NameClass::Macro);
    case NameClass::Field:
        return llvmdsdl::namePartition(classes, llvmdsdl::NameClass::Field);
    }
    return std::nullopt;
}

/// @brief Whether a scope of @p kind is named as a module in the scope that holds it.
bool namedAsModule(const ScopeKind kind)
{
    return (kind == ScopeKind::Namespace) || (kind == ScopeKind::Module) || (kind == ScopeKind::Package);
}

/// @brief The namespace a declaration made in @p scope is made in: the scope, or for a file scope the
///        nearest scope around it that is not a file.
ScopeOp namespaceOf(ScopeOp scope)
{
    while (scope.getKind() == ScopeKind::File)
    {
        scope = llvm::cast<ScopeOp>(scope->getParentOp());
    }
    return scope;
}

/// @brief Whether @p path names a file within the output directory.
bool isOutputPath(const llvm::StringRef path)
{
    if (path.empty() || path.starts_with("/"))
    {
        return false;
    }
    llvm::SmallVector<llvm::StringRef> components;
    path.split(components, '/');
    return llvm::none_of(components, [](const llvm::StringRef component) {
        return component.empty() || (component == ".") || (component == "..");
    });
}

/// @brief The definition @p op stands for, by its `of`; none where it names none.
std::optional<llvmdsdl::SchemaSymbol> definitionOf(Operation* op)
{
    FlatSymbolRefAttr of;
    if (auto scope = llvm::dyn_cast<ScopeOp>(op))
    {
        of = scope.getOfAttr();
    }
    else if (auto decl = llvm::dyn_cast<DeclOp>(op))
    {
        of = decl.getOfAttr();
    }
    if (!of)
    {
        return std::nullopt;
    }
    if (const std::optional<llvmdsdl::PlanSymbol> function = llvmdsdl::parsePlanSymbol(of.getValue()))
    {
        return function->schema;
    }
    return llvmdsdl::parseSchemaSymbol(of.getValue());
}

/// @brief Whether @p a and @p b stand for two versions of one definition.
bool versionsOfOneDefinition(Operation* a, Operation* b)
{
    const std::optional<llvmdsdl::SchemaSymbol> first  = definitionOf(a);
    const std::optional<llvmdsdl::SchemaSymbol> second = definitionOf(b);
    return first && second && (first->fullName == second->fullName) &&
           ((first->major != second->major) || (first->minor != second->minor));
}

/// @brief Whether one of @p a and @p b is a namespace's scope and the other a definition's module.
bool directoryBesideFile(Operation* a, Operation* b)
{
    const auto kindOf = [](Operation* op) {
        auto scope = llvm::dyn_cast<ScopeOp>(op);
        return scope ? std::optional<ScopeKind>(scope.getKind()) : std::nullopt;
    };
    const std::optional<ScopeKind> first       = kindOf(a);
    const std::optional<ScopeKind> second      = kindOf(b);
    const auto                     isDirectory = [](const std::optional<ScopeKind> kind) {
        return kind && ((*kind == ScopeKind::Namespace) || (*kind == ScopeKind::Package));
    };
    const auto isModule = [](const std::optional<ScopeKind> kind) { return kind && (*kind == ScopeKind::Module); };
    return (isDirectory(first) && isModule(second)) || (isModule(first) && isDirectory(second));
}

/// @brief One name a namespace holds, and what claimed it.
struct Claim final
{
    Operation* op{};
    NameClass  nameClass{};
};

/// @brief The names each namespace of one surface holds, claimed scope by scope.
class SurfaceNames final
{
public:
    SurfaceNames(const llvm::StringRef target, const llvmdsdl::LanguageTraits& row)
        : target_(target)
        , classes_(row.classification.nameClasses)
        , fileAndDirectoryAreOneModule_(row.composition.fileAndDirectoryAreOneModule)
    {
    }

    /// @brief Claims the path of @p root and every name in it.
    LogicalResult claimTree(ScopeOp root)
    {
        if (failed(claimPath(root)) || failed(claimWithin(root)))
        {
            return failure();
        }
        return checkFileImports();
    }

private:
    LogicalResult claimWithin(ScopeOp scope)
    {
        if (scope.getBodyRegion().empty())
        {
            return success();
        }
        const ScopeOp space = namespaceOf(scope);
        for (Operation& op : *scope.getBody())
        {
            if (auto child = llvm::dyn_cast<ScopeOp>(op))
            {
                if (failed(claimPath(child)) || failed(claimScopeName(space, child)) || failed(claimWithin(child)))
                {
                    return failure();
                }
                continue;
            }
            auto       decl       = llvm::cast<DeclOp>(op);
            const bool fileImport = (scope.getKind() == ScopeKind::File) && (decl.getKind() == DeclKind::Import);
            if (failed(claim(fileImport ? scope : space, decl, decl.getName(), decl.declaredClass())))
            {
                return failure();
            }
            if (fileImport)
            {
                fileImports_.emplace_back(scope, decl);
            }
        }
        return success();
    }

    LogicalResult claimScopeName(ScopeOp space, ScopeOp child)
    {
        if (child.getKind() == ScopeKind::Type)
        {
            return claim(space, child, child.getName(), NameClass::Type);
        }
        if (namedAsModule(child.getKind()))
        {
            return claim(space, child, child.getName(), NameClass::Module);
        }
        return success();
    }

    LogicalResult claim(ScopeOp space, Operation* op, const llvm::StringRef name, const NameClass nameClass)
    {
        const std::optional<Partition> partition = partitionOf(nameClass, classes_);
        if (!partition)
        {
            return op->emitOpError("declares '") << name << "' as a " << stringifyNameClass(nameClass)
                                                 << ", a class of name a " << target_ << " surface does not have";
        }
        const bool              macro = (*partition == Partition::Macros);
        llvm::StringMap<Claim>& names = macro ? macros_ : names_[{space.getOperation(), *partition}];
        const auto [found, inserted]  = names.try_emplace(name, Claim{op, nameClass});
        // Two versions of one definition are alternatives a consumer builds against one of, and the
        // output keeps them apart where it must, so their names may be one. A namespace's directory
        // and a definition's module of one name are two paths where the language keeps a file and a
        // directory apart.
        if (inserted || versionsOfOneDefinition(op, found->second.op) ||
            (!fileAndDirectoryAreOneModule_ && directoryBesideFile(op, found->second.op)))
        {
            return success();
        }
        const std::string where =
            macro ? std::string("the translation unit") : ("scope '" + space.getName() + "'").str();
        InFlightDiagnostic diag = op->emitOpError("declares '")
                                  << name << "' as a " << stringifyNameClass(nameClass) << " in " << where
                                  << ", which already declares it as a " << stringifyNameClass(found->second.nameClass);
        diag.attachNote(found->second.op->getLoc()) << "declared here";
        return diag;
    }

    LogicalResult claimPath(ScopeOp scope)
    {
        const std::optional<llvm::StringRef> path = scope.getPath();
        if (!path)
        {
            return success();
        }
        const auto [found, inserted] = paths_.try_emplace(*path, scope);
        if (inserted)
        {
            return success();
        }
        InFlightDiagnostic diag = scope.emitOpError("is written to '")
                                  << *path << "', as scope '" << found->second.getName() << "' is";
        diag.attachNote(found->second.getLoc()) << "written here";
        return diag;
    }

    /// @brief Holds each import a file scope makes apart from the names its namespace declares.
    LogicalResult checkFileImports()
    {
        for (auto& [file, decl] : fileImports_)
        {
            ScopeOp                        space     = namespaceOf(file);
            const std::optional<Partition> partition = partitionOf(decl.declaredClass(), classes_);
            const auto                     names     = names_.find({space.getOperation(), *partition});
            if (names == names_.end())
            {
                continue;
            }
            const auto found = names->second.find(decl.getName());
            if (found == names->second.end())
            {
                continue;
            }
            InFlightDiagnostic diag = decl.emitOpError("imports '")
                                      << decl.getName() << "' into file '" << file.getName() << "', where scope '"
                                      << space.getName() << "' declares it as a "
                                      << stringifyNameClass(found->second.nameClass);
            diag.attachNote(found->second.op->getLoc()) << "declared here";
            return diag;
        }
        return success();
    }

    llvm::StringRef                                                    target_;
    const llvmdsdl::NameClasses&                                       classes_;
    bool                                                               fileAndDirectoryAreOneModule_{};
    std::map<std::pair<Operation*, Partition>, llvm::StringMap<Claim>> names_;
    llvm::StringMap<Claim>                                             macros_;
    llvm::StringMap<ScopeOp>                                           paths_;
    std::vector<std::pair<ScopeOp, DeclOp>>                            fileImports_;
};

/// @brief Whether @p schema has a section named @p section.
bool hasSection(SchemaOp schema, const llvm::StringRef section)
{
    return llvm::any_of(schema.getBody().getOps<SerializationPlanOp>(),
                        [&](SerializationPlanOp plan) { return sectionOf(plan.getSection()) == section; });
}

/// @brief Whether @p schema declares a field or a constant named @p member in @p section.
bool hasMember(SchemaOp schema, const llvm::StringRef section, const llvm::StringRef member)
{
    for (Block& block : schema.getBody())
    {
        for (Operation& op : block)
        {
            if (auto field = llvm::dyn_cast<FieldOp>(op); field && !field.getPadding() && (field.getName() == member) &&
                                                          (sectionOf(field.getSection()) == section))
            {
                return true;
            }
            if (auto constant = llvm::dyn_cast<ConstantOp>(op);
                constant && (constant.getName() == member) && (sectionOf(constant.getSection()) == section))
            {
                return true;
            }
        }
    }
    return false;
}

/// @brief Resolves what @p op names by @p of, @p section and @p member against the module.
LogicalResult verifyEntity(Operation*                           op,
                           SymbolTableCollection&               symbolTable,
                           const FlatSymbolRefAttr              of,
                           const std::optional<llvm::StringRef> section,
                           const std::optional<llvm::StringRef> member,
                           const bool                           schemaOnly)
{
    if (!of)
    {
        return success();
    }
    Operation* const entity = symbolTable.lookupNearestSymbolFrom(op, of);
    if (entity == nullptr)
    {
        return op->emitOpError("names ") << of << ", which resolves to nothing";
    }
    auto schema = llvm::dyn_cast<SchemaOp>(entity);
    if ((schemaOnly || section || member) && !schema)
    {
        return op->emitOpError("names ") << of << ", which is not a schema";
    }
    if (section && !hasSection(schema, *section))
    {
        return op->emitOpError("names section '") << *section << "' of " << of << ", which has no such section";
    }
    if (member && !hasMember(schema, sectionOf(section), *member))
    {
        return op->emitOpError("names member '")
               << *member << "' of " << of << ", which declares no such field or constant";
    }
    return success();
}

}  // namespace

LogicalResult SurfaceOp::verify()
{
    if (llvmdsdl::languageTraitsNamed(getTarget()) == nullptr)
    {
        return emitOpError("names target '") << getTarget() << "', which is no language";
    }
    if (getProfile() && getProfile()->empty())
    {
        return emitOpError("names an empty profile");
    }
    Region& body = getBodyRegion();
    if (body.empty() || !llvm::hasSingleElement(body.front()) || !llvm::isa<ScopeOp>(body.front().front()))
    {
        return emitOpError("holds one root scope and nothing else");
    }
    for (SurfaceOp other : llvm::cast<ModuleOp>(getOperation()->getParentOp()).getOps<SurfaceOp>())
    {
        if (other == *this)
        {
            break;
        }
        if ((other.getTarget() == getTarget()) && (other.getProfile() == getProfile()))
        {
            InFlightDiagnostic diag = emitOpError("is a second surface of target '")
                                      << getTarget() << "' and profile '" << getProfile().value_or("") << "'";
            diag.attachNote(other.getLoc()) << "the first";
            return diag;
        }
    }
    return success();
}

LogicalResult SurfaceOp::verifyRegions()
{
    const llvmdsdl::LanguageTraits* const row = llvmdsdl::languageTraitsNamed(getTarget());
    if (row == nullptr)
    {
        return failure();
    }
    SurfaceNames names(getTarget(), *row);
    return names.claimTree(llvm::cast<ScopeOp>(getBody()->front()));
}

LogicalResult ScopeOp::verify()
{
    // A root scope is the output's root, which a language without a package declares no name for.
    if (getName().empty() && (getKind() != ScopeKind::Root))
    {
        return emitOpError("has no name");
    }
    const bool rootPlace = llvm::isa<SurfaceOp>(getOperation()->getParentOp());
    if ((getKind() == ScopeKind::Root) != rootPlace)
    {
        return emitOpError(rootPlace ? "is the surface's scope, which only a root scope is"
                                     : "is a root scope inside another scope");
    }
    if ((getOf() || getSection()) && (getKind() != ScopeKind::Type))
    {
        return emitOpError("names an entity, which only a type scope does");
    }
    if (getSection() && !getOf())
    {
        return emitOpError("names a section of no schema");
    }
    if ((getKind() == ScopeKind::File) && !getPath())
    {
        return emitOpError("is a file scope with no path");
    }
    if (getPath() && !isOutputPath(*getPath()))
    {
        return emitOpError("is written to '") << *getPath() << "', which is not a path within the output directory";
    }
    if (!getBodyRegion().empty())
    {
        for (Operation& op : *getBody())
        {
            if (!llvm::isa<ScopeOp, DeclOp>(op))
            {
                return op.emitOpError("is inside a surface scope, which holds only scopes and declarations");
            }
        }
    }
    return success();
}

LogicalResult ScopeOp::verifySymbolUses(SymbolTableCollection& symbolTable)
{
    return verifyEntity(getOperation(), symbolTable, getOfAttr(), getSection(), std::nullopt, true);
}

NameClass DeclOp::declaredClass()
{
    if (const std::optional<NameClass> given = getNameClass())
    {
        return *given;
    }
    switch (getKind())
    {
    case DeclKind::Alias:
        return NameClass::Type;
    case DeclKind::Tag:
        return NameClass::Tag;
    case DeclKind::Module:
        return NameClass::Module;
    case DeclKind::Guard:
        return NameClass::Macro;
    case DeclKind::Field:
        return NameClass::Field;
    case DeclKind::Constant:
    case DeclKind::Option:
    case DeclKind::Entry:
    case DeclKind::Accessor:
    case DeclKind::Helper:
    case DeclKind::Method:
    case DeclKind::Wrapper:
    case DeclKind::Import:
        return NameClass::Value;
    }
    return NameClass::Value;
}

LogicalResult DeclOp::verify()
{
    if (getName().empty())
    {
        return emitOpError("has no name");
    }
    if ((getSection() || getMember()) && !getOf())
    {
        return emitOpError("names a section or a member of no entity");
    }
    if (getFact() && (getOrigin() != Origin::Generated))
    {
        return emitOpError("states a fact, which only a generated declaration does");
    }
    if ((getKind() == DeclKind::Field) &&
        (llvm::cast<ScopeOp>(getOperation()->getParentOp()).getKind() != ScopeKind::Type))
    {
        return emitOpError("declares a field outside a type scope");
    }
    if ((getKind() == DeclKind::Import) && !getNameClass())
    {
        return emitOpError("is an import with no class");
    }
    return success();
}

LogicalResult DeclOp::verifySymbolUses(SymbolTableCollection& symbolTable)
{
    return verifyEntity(getOperation(), symbolTable, getOfAttr(), getSection(), getMember(), false);
}

OpFoldResult BufferAtOp::fold(FoldAdaptor adaptor)
{
    const auto offset = llvm::dyn_cast_if_present<IntegerAttr>(adaptor.getByteOffset());
    if (offset && offset.getValue().isZero() && (getBuffer().getType() == getType()))
    {
        return getBuffer();
    }
    return {};
}

#define GET_OP_CLASSES
#include "llvmdsdl/IR/DSDLOps.cpp.inc"  // IWYU pragma: keep
