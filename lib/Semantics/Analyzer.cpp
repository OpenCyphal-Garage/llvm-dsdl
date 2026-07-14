//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements semantic analysis for parsed DSDL definitions.
///
/// The analyzer resolves symbols, evaluates type rules, and computes layout and extent facts used by lowering.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Semantics/Analyzer.h"

#include <llvm/ADT/StringRef.h>
#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "llvmdsdl/Semantics/Evaluator.h"
#include "llvm/Support/Error.h"
#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Frontend/SourceLocation.h"
#include "llvmdsdl/Semantics/BitLengthSet.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/Rational.h"
#include "llvmdsdl/Support/ReservedIdentifiers.h"

namespace llvmdsdl
{
namespace
{

struct TypeLayout final
{
    BitLengthSet      bls;
    std::int64_t      alignment{1};
    SemanticFieldType resolved;
};

std::string typeKey(const std::string& name, std::uint32_t major, std::uint32_t minor)
{
    return name + ":" + std::to_string(major) + ":" + std::to_string(minor);
}

std::int64_t ceilLog2(std::int64_t x)
{
    if (x <= 1)
    {
        return 0;
    }
    std::int64_t v   = x - 1;
    std::int64_t out = 0;
    while (v > 0)
    {
        v >>= 1;
        ++out;
    }
    return out;
}

std::int64_t pow2ceil(std::int64_t x)
{
    if (x <= 1)
    {
        return 1;
    }
    std::int64_t out = 1;
    while (out < x && out < (1LL << 62))
    {
        out <<= 1;
    }
    return out;
}

bool isStandardRootNamespace(llvm::StringRef root)
{
    return root == "uavcan" || root == "cyphal";
}

bool isValidRegulatedPortId(const std::uint32_t portId, const bool isService, llvm::StringRef rootNamespace)
{
    const bool standard = isStandardRootNamespace(rootNamespace.trim());
    if (isService)
    {
        const std::uint32_t lo = standard ? 384U : 256U;
        const std::uint32_t hi = standard ? 511U : 383U;
        return portId >= lo && portId <= hi;
    }

    const std::uint32_t lo = standard ? 7168U : 6144U;
    const std::uint32_t hi = standard ? 8191U : 7167U;
    return portId >= lo && portId <= hi;
}

bool exprContainsOffset(const std::shared_ptr<ExprAST>& expr)
{
    if (!expr)
    {
        return false;
    }

    if (auto id = std::get_if<ExprAST::Identifier>(&expr->value))
    {
        return id->name == "_offset_";
    }
    if (auto u = std::get_if<ExprAST::Unary>(&expr->value))
    {
        return exprContainsOffset(u->operand);
    }
    if (auto b = std::get_if<ExprAST::Binary>(&expr->value))
    {
        return exprContainsOffset(b->lhs) || exprContainsOffset(b->rhs);
    }
    if (auto s = std::get_if<ExprAST::SetLiteral>(&expr->value))
    {
        for (const auto& e : s->elements)
        {
            if (exprContainsOffset(e))
            {
                return true;
            }
        }
    }
    return false;
}

std::uint32_t expressionSpanLengthFromSource(const std::string&    sourceText,
                                             const SourceLocation& location,
                                             const std::size_t     fallbackLength)
{
    const std::uint32_t fallback = static_cast<std::uint32_t>(std::max<std::size_t>(1, fallbackLength));
    if (location.line == 0 || location.column == 0)
    {
        return fallback;
    }

    std::size_t lineStart = 0;
    std::size_t lineIndex = 1;
    while (lineIndex < location.line && lineStart < sourceText.size())
    {
        const std::size_t nextLine = sourceText.find('\n', lineStart);
        if (nextLine == std::string::npos)
        {
            return fallback;
        }
        lineStart = nextLine + 1;
        ++lineIndex;
    }

    const std::size_t lineEnd = sourceText.find('\n', lineStart);
    const std::size_t stop    = lineEnd == std::string::npos ? sourceText.size() : lineEnd;
    if (location.column < 1)
    {
        return fallback;
    }

    const std::size_t start = lineStart + static_cast<std::size_t>(location.column - 1);
    if (start >= stop)
    {
        return fallback;
    }

    std::size_t       end     = stop;
    const std::size_t comment = sourceText.find('#', start);
    if (comment != std::string::npos && comment < end)
    {
        end = comment;
    }

    while (end > start)
    {
        const char c = sourceText[end - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v')
        {
            --end;
            continue;
        }
        break;
    }

    if (end <= start)
    {
        return fallback;
    }
    return static_cast<std::uint32_t>(end - start);
}

// Expansion ceiling for materializing `_offset_` as a concrete value set. A type whose set of
// possible offsets exceeds this is expanded incompletely; `exact` reports that so the caller can
// warn rather than silently evaluate assertions against a truncated set (BLS-D2).
constexpr std::size_t kOffsetExpansionLimit = 4096;

Value::Set bitLengthSetToValueSet(const BitLengthSet& bls, bool& exact)
{
    const auto expansion = bls.expandChecked(kOffsetExpansionLimit);
    exact                = expansion.exact;
    Value::Set out;
    for (const auto v : expansion.values)
    {
        out.insert(Rational(v, 1));
    }
    return out;
}

class AnalyzerImpl final
{
public:
    AnalyzerImpl(const ASTModule& module, DiagnosticEngine& diagnostics, AnalyzeOptions options)
        : module_(module)
        , diagnostics_(diagnostics)
        , options_(options)
    {
        state_.resize(module_.definitions.size(), State::Unvisited);
        results_.resize(module_.definitions.size());
        for (std::size_t i = 0; i < module_.definitions.size(); ++i)
        {
            const auto& d = module_.definitions[i];
            indexByKey_.emplace(typeKey(d.info.fullName, d.info.majorVersion, d.info.minorVersion), i);
        }
        if (options_.externalSemanticCatalog != nullptr)
        {
            for (const auto& def : options_.externalSemanticCatalog->definitions)
            {
                externalByKey_.emplace(typeKey(def.info.fullName, def.info.majorVersion, def.info.minorVersion), &def);
            }
        }
    }

    llvm::Expected<SemanticModule> run()
    {
        for (std::size_t i = 0; i < module_.definitions.size(); ++i)
        {
            (void) analyzeOne(i);
        }

        checkVersionRules();

        if (diagnostics_.hasErrors())
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(), "semantic analysis failed");
        }

        SemanticModule out;
        out.definitions.reserve(results_.size());
        for (auto& r : results_)
        {
            if (r)
            {
                out.definitions.push_back(*r);
            }
        }
        return out;
    }

private:
    enum class State
    {
        Unvisited,
        Visiting,
        Done,
    };

    const ASTModule&                                           module_;
    DiagnosticEngine&                                          diagnostics_;
    AnalyzeOptions                                             options_;
    std::vector<State>                                         state_;
    std::vector<std::optional<SemanticDefinition>>             results_;
    std::unordered_map<std::string, std::size_t>               indexByKey_;
    std::unordered_map<std::string, const SemanticDefinition*> externalByKey_;

    struct ResolvedCompositeDefinition final
    {
        std::string                candidateName;
        const SemanticDefinition*  definition{nullptr};
        std::optional<std::size_t> localIndex;
    };

    std::optional<std::size_t> resolveDefinitionIndex(const std::string& fullName,
                                                      std::uint32_t      major,
                                                      std::uint32_t      minor)
    {
        const auto it = indexByKey_.find(typeKey(fullName, major, minor));
        if (it == indexByKey_.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    static std::string joinComponents(const std::vector<std::string>& components)
    {
        std::ostringstream out;
        for (std::size_t i = 0; i < components.size(); ++i)
        {
            if (i > 0)
            {
                out << '.';
            }
            out << components[i];
        }
        return out.str();
    }

    std::optional<ResolvedCompositeDefinition> resolveCompositeType(const DiscoveredDefinition& owner,
                                                                    const VersionedTypeExprAST& type,
                                                                    const SourceLocation&       location,
                                                                    bool                        reportError)
    {
        std::vector<std::string> candidates;

        const std::string explicitName = joinComponents(type.nameComponents);
        if (type.nameComponents.size() == 1)
        {
            if (!owner.namespaceComponents.empty())
            {
                candidates.push_back(joinComponents(owner.namespaceComponents) + "." + type.nameComponents.front());
            }
            else
            {
                candidates.push_back(type.nameComponents.front());
            }
        }
        else
        {
            candidates.push_back(explicitName);

            if (!owner.namespaceComponents.empty())
            {
                candidates.push_back(owner.namespaceComponents.front() + "." + explicitName);
                candidates.push_back(joinComponents(owner.namespaceComponents) + "." + explicitName);
            }
        }

        std::set<std::string> seen;
        for (const auto& candidate : candidates)
        {
            if (!seen.insert(candidate).second)
            {
                continue;
            }
            auto idx = resolveDefinitionIndex(candidate, type.major, type.minor);
            if (idx)
            {
                return ResolvedCompositeDefinition{candidate, nullptr, *idx};
            }

            const auto extIt = externalByKey_.find(typeKey(candidate, type.major, type.minor));
            if (extIt != externalByKey_.end())
            {
                return ResolvedCompositeDefinition{candidate, extIt->second, std::nullopt};
            }
        }

        if (reportError)
        {
            diagnostics_.error(location,
                               "unresolved composite type: " + explicitName + "." + std::to_string(type.major) + "." +
                                   std::to_string(type.minor));
        }
        return std::nullopt;
    }

    std::optional<SemanticDefinition>* analyzeOne(std::size_t idx)
    {
        if (state_[idx] == State::Done)
        {
            return &results_[idx];
        }
        if (state_[idx] == State::Visiting)
        {
            diagnostics_.error(module_.definitions[idx].ast.location, "circular dependency detected");
            return &results_[idx];
        }

        state_[idx]        = State::Visiting;
        const auto& parsed = module_.definitions[idx];

        SemanticDefinition sem;
        sem.info      = parsed.info;
        sem.doc       = parsed.ast.doc;
        sem.isService = parsed.ast.isService();

        std::vector<StatementAST> req;
        std::vector<StatementAST> resp;
        bool                      inResponse  = false;
        int                       markerCount = 0;

        for (const auto& stmt : parsed.ast.statements)
        {
            if (std::holds_alternative<ServiceResponseMarkerAST>(stmt))
            {
                ++markerCount;
                inResponse = true;
                continue;
            }
            if (inResponse)
            {
                resp.push_back(stmt);
            }
            else
            {
                req.push_back(stmt);
            }
        }

        if (!sem.isService && markerCount > 0)
        {
            diagnostics_.error(parsed.ast.location, "service response marker is only valid in service types");
        }
        if (markerCount > 1)
        {
            diagnostics_.error(parsed.ast.location, "duplicated service response marker");
        }

        sem.request = analyzeSection(parsed.info, req, /*isResponse=*/false);

        if (sem.isService)
        {
            auto response = analyzeSection(parsed.info, resp, /*isResponse=*/true);
            if (response.deprecated)
            {
                diagnostics_.error(parsed.ast.location, "@deprecated is only allowed in service request section");
            }
            response.deprecated = sem.request.deprecated;
            sem.response        = std::move(response);
        }

        results_[idx] = sem;
        state_[idx]   = State::Done;
        return &results_[idx];
    }

    TypeLayout analyzeType(const DiscoveredDefinition&  owner,
                           const TypeExprAST&           type,
                           ValueEnv&                    env,
                           const TypeAttributeResolver* resolver)
    {
        TypeLayout out;
        out.bls                   = BitLengthSet(0);
        out.alignment             = 1;
        out.resolved.bitLengthSet = out.bls;

        auto baseFromScalar = [&]() -> TypeLayout {
            if (auto p = std::get_if<PrimitiveTypeExprAST>(&type.scalar))
            {
                TypeLayout layout;
                layout.bls                    = BitLengthSet(static_cast<std::int64_t>(p->bitLength));
                layout.alignment              = 1;
                layout.resolved.bitLengthSet  = layout.bls;
                layout.resolved.bitLength     = p->bitLength;
                layout.resolved.castMode      = p->castMode;
                layout.resolved.alignmentBits = 1;
                switch (p->kind)
                {
                case PrimitiveKind::Bool:
                    layout.resolved.scalarCategory = SemanticScalarCategory::Bool;
                    break;
                case PrimitiveKind::Byte:
                    layout.resolved.scalarCategory = SemanticScalarCategory::Byte;
                    break;
                case PrimitiveKind::Utf8:
                    layout.resolved.scalarCategory = SemanticScalarCategory::Utf8;
                    break;
                case PrimitiveKind::UnsignedInt:
                    layout.resolved.scalarCategory = SemanticScalarCategory::UnsignedInt;
                    break;
                case PrimitiveKind::SignedInt:
                    layout.resolved.scalarCategory = SemanticScalarCategory::SignedInt;
                    break;
                case PrimitiveKind::Float:
                    layout.resolved.scalarCategory = SemanticScalarCategory::Float;
                    break;
                }
                return layout;
            }
            if (auto p = std::get_if<VoidTypeExprAST>(&type.scalar))
            {
                TypeLayout layout;
                layout.bls                     = BitLengthSet(static_cast<std::int64_t>(p->bitLength));
                layout.alignment               = 1;
                layout.resolved.scalarCategory = SemanticScalarCategory::Void;
                layout.resolved.bitLength      = p->bitLength;
                layout.resolved.alignmentBits  = 1;
                layout.resolved.bitLengthSet   = layout.bls;
                return layout;
            }

            const auto* v = std::get_if<VersionedTypeExprAST>(&type.scalar);
            if (!v)
            {
                return out;
            }

            auto resolved = resolveCompositeType(owner, *v, type.location, true);
            if (!resolved)
            {
                return out;
            }

            const SemanticDefinition* def = nullptr;
            if (resolved->localIndex)
            {
                auto* resolvedDef = analyzeOne(*resolved->localIndex);
                if (!resolvedDef || !*resolvedDef)
                {
                    return out;
                }
                def = &(**resolvedDef);
            }
            else
            {
                def = resolved->definition;
            }

            if (def == nullptr)
            {
                return out;
            }
            if (def->isService)
            {
                diagnostics_.error(type.location, "service types are not valid field types");
                return out;
            }

            TypeLayout layout;
            layout.alignment               = 8;
            layout.resolved.scalarCategory = SemanticScalarCategory::Composite;
            layout.resolved.alignmentBits  = 8;
            layout.resolved.compositeType  = SemanticTypeRef{def->info.fullName,
                                                            def->info.namespaceComponents,
                                                            def->info.shortName,
                                                            def->info.majorVersion,
                                                            def->info.minorVersion};

            const auto& sec                     = def->request;
            layout.resolved.compositeSealed     = sec.sealed;
            layout.resolved.compositeExtentBits = sec.extentBits.value_or(sec.offsetAtEnd.max());
            if (sec.sealed)
            {
                layout.bls                   = sec.offsetAtEnd;
                layout.resolved.bitLengthSet = layout.bls;
                return layout;
            }

            const std::int64_t extent = sec.extentBits.value_or(0);
            BitLengthSet       bls(32);
            bls                          = bls + BitLengthSet(8).repeatRange(extent / 8);
            layout.bls                   = bls;
            layout.resolved.bitLengthSet = layout.bls;
            return layout;
        };

        auto scalarLayout               = baseFromScalar();
        scalarLayout.resolved.arrayKind = type.arrayKind;

        if (type.arrayKind == ArrayKind::None)
        {
            return scalarLayout;
        }

        if (std::holds_alternative<VoidTypeExprAST>(type.scalar))
        {
            diagnostics_.error(type.location, "void type is not allowed as array element type");
            return out;
        }

        std::int64_t capacity = 0;
        if (!type.arrayCapacity)
        {
            diagnostics_.error(type.location, "array type missing capacity expression");
            return out;
        }

        auto capValue = evaluateExpression(*type.arrayCapacity, env, diagnostics_, type.location, resolver);
        if (!capValue || !std::holds_alternative<Rational>(capValue->data) ||
            !std::get<Rational>(capValue->data).isInteger())
        {
            diagnostics_.error(type.location, "array capacity expression must yield integer rational");
            return out;
        }
        else if (const auto fitted = std::get<Rational>(capValue->data).asInteger())
        {
            capacity = *fitted;
        }
        else
        {
            // Integral but outside 64-bit range: no array can have that many elements.
            diagnostics_.error(type.location, "array capacity is too large");
            return out;
        }

        if (type.arrayKind == ArrayKind::Fixed)
        {
            if (capacity < 1)
            {
                diagnostics_.error(type.location, "fixed-length array capacity must be positive");
                return out;
            }
            TypeLayout layout;
            layout.bls                            = scalarLayout.bls.repeat(capacity);
            layout.alignment                      = scalarLayout.alignment;
            layout.resolved                       = scalarLayout.resolved;
            layout.resolved.arrayKind             = type.arrayKind;
            layout.resolved.arrayCapacity         = capacity;
            layout.resolved.arrayLengthPrefixBits = 0;
            layout.resolved.alignmentBits         = layout.alignment;
            layout.resolved.bitLengthSet          = layout.bls;
            return layout;
        }

        if (type.arrayKind == ArrayKind::VariableExclusive)
        {
            if (capacity <= 1)
            {
                diagnostics_.error(type.location, "exclusive variable-length array bound must be > 1");
                return out;
            }
            capacity -= 1;
        }
        else if (type.arrayKind == ArrayKind::VariableInclusive)
        {
            if (capacity < 1)
            {
                diagnostics_.error(type.location, "inclusive variable-length array bound must be >= 1");
                return out;
            }
        }

        // Length-prefix width: bits needed to represent a count in [0, capacity]. This equals
        // ceilLog2(capacity + 1), but computed as the bit-width of `capacity` (which is >= 1 here)
        // so an adversarial capacity of INT64_MAX does not overflow the `+ 1` (signed-overflow UB).
        std::int64_t b = 0;
        for (std::int64_t v = capacity; v > 0; v >>= 1)
        {
            ++b;
        }
        const auto prefixBits = pow2ceil(std::max<std::int64_t>(8, b));

        BitLengthSet bls(prefixBits);
        bls = bls + scalarLayout.bls.repeatRange(capacity);
        TypeLayout layout;
        layout.bls                            = bls;
        layout.alignment                      = scalarLayout.alignment;
        layout.resolved                       = scalarLayout.resolved;
        layout.resolved.arrayKind             = type.arrayKind;
        layout.resolved.arrayCapacity         = capacity;
        layout.resolved.arrayLengthPrefixBits = prefixBits;
        layout.resolved.alignmentBits         = layout.alignment;
        layout.resolved.bitLengthSet          = layout.bls;
        return layout;
    }

    bool checkConstantCompatibility(const ConstantDeclAST& decl, const Value& value)
    {
        auto prim = std::get_if<PrimitiveTypeExprAST>(&decl.type.scalar);
        if (!prim)
        {
            diagnostics_.error(decl.location, "constant attributes must be primitive-typed");
            return false;
        }

        if (prim->kind == PrimitiveKind::Bool)
        {
            if (!std::holds_alternative<bool>(value.data))
            {
                diagnostics_.error(decl.location, "boolean constants must be initialized from bool");
                return false;
            }
            return true;
        }

        if (prim->kind == PrimitiveKind::UnsignedInt || prim->kind == PrimitiveKind::SignedInt)
        {
            if (auto r = std::get_if<Rational>(&value.data))
            {
                if (!r->isInteger())
                {
                    diagnostics_.error(decl.location, "integer constants require integer rational values");
                    return false;
                }
                const auto wide = r->asWideInteger();
                if (!wide)
                {
                    diagnostics_.error(decl.location, "integer constant value is not representable");
                    return false;
                }
                // A constant must fit the [min, max] range of its declared width and signedness
                // (unsigned `[0, 2^n-1]`, signed two's-complement `[-2^(n-1), 2^(n-1)-1]`); `n <= 64`
                // so every bound fits the 128-bit constant value type. Enforced here because the
                // Rational core deliberately no longer poisons at 64-bit range.
                const std::uint32_t bits = prim->bitLength;
                const bool          isSigned = prim->kind == PrimitiveKind::SignedInt;
                const __int128      minValue = isSigned ? -(static_cast<__int128>(1) << (bits - 1U)) : __int128{0};
                const __int128      maxValue = isSigned ? (static_cast<__int128>(1) << (bits - 1U)) - 1
                                                        : (static_cast<__int128>(1) << bits) - 1;
                if (*wide < minValue || *wide > maxValue)
                {
                    diagnostics_.error(decl.location,
                                       "constant value " + wideToString(*wide) + " is out of range for " +
                                           (isSigned ? "int" : "uint") + std::to_string(bits));
                    return false;
                }
                return true;
            }
            if (auto s = std::get_if<std::string>(&value.data))
            {
                if (prim->kind == PrimitiveKind::UnsignedInt && prim->bitLength == 8 && s->size() == 1 &&
                    static_cast<unsigned char>((*s)[0]) <= 127)
                {
                    return true;
                }
            }
            diagnostics_.error(decl.location, "integer constants require rational or ASCII single-char string");
            return false;
        }

        if (prim->kind == PrimitiveKind::Float)
        {
            if (!std::holds_alternative<Rational>(value.data))
            {
                diagnostics_.error(decl.location, "floating constants require rational values");
                return false;
            }
            return true;
        }

        diagnostics_.error(decl.location, "byte/utf8 are not valid constant attribute types");
        return false;
    }

    SemanticSection analyzeSection(const DiscoveredDefinition&      owner,
                                   const std::vector<StatementAST>& statements,
                                   bool                             isResponse)
    {
        SemanticSection section;

        ValueEnv                      env;
        BitLengthSet                  structureOffset(0);
        std::vector<TypeLayout>       unionFields;
        bool                          attributesStarted    = false;
        bool                          serializationModeSet = false;
        bool                          unionOffsetUsed      = false;
        std::uint32_t                 unionOptionCounter   = 0;
        std::optional<SourceLocation> firstStatementLocation;
        std::optional<SourceLocation> unionDirectiveLocation;
        std::optional<SourceLocation> serializationDirectiveLocation;
        std::optional<SourceLocation> extentDirectiveLocation;
        std::optional<SourceLocation> extentExpressionLocation;
        std::optional<std::uint32_t>  extentExpressionLength;

        const auto ownerFallbackLocation = [&owner]() {
            return owner.filePath.empty() ? SourceLocation{"<unknown>", 1, 1} : SourceLocation{owner.filePath, 1, 1};
        };

        const auto sectionAnchorLocation = [&]() {
            if (serializationDirectiveLocation.has_value())
            {
                return *serializationDirectiveLocation;
            }
            if (firstStatementLocation.has_value())
            {
                return *firstStatementLocation;
            }
            return ownerFallbackLocation();
        };

        const auto statementLocation = [](const StatementAST& statement) -> SourceLocation {
            return std::visit([](const auto& node) -> SourceLocation { return node.location; }, statement);
        };

        TypeAttributeResolver typeAttrResolver = [&](const TypeExprAST&    typeExpr,
                                                     const std::string&    attributeName,
                                                     const SourceLocation& location) -> std::optional<Value> {
            const auto unsupported = [&](const std::string& message) -> std::optional<Value> {
                diagnostics_.error(location, message);
                return std::nullopt;
            };

            if (typeExpr.arrayKind != ArrayKind::None)
            {
                return unsupported("unsupported metaserializable attribute: " + attributeName);
            }

            const auto* versioned = std::get_if<VersionedTypeExprAST>(&typeExpr.scalar);
            if (!versioned)
            {
                return unsupported("unsupported metaserializable attribute: " + attributeName);
            }

            auto resolved = resolveCompositeType(owner, *versioned, location, true);
            if (!resolved)
            {
                return std::nullopt;
            }

            const SemanticDefinition* def = nullptr;
            if (resolved->localIndex)
            {
                auto* resolvedDef = analyzeOne(*resolved->localIndex);
                if (!resolvedDef || !*resolvedDef)
                {
                    diagnostics_.error(location, "failed to analyze dependent type: " + resolved->candidateName);
                    return std::nullopt;
                }
                def = &(**resolvedDef);
            }
            else
            {
                def = resolved->definition;
            }

            if (def == nullptr)
            {
                diagnostics_.error(location, "failed to analyze dependent type: " + resolved->candidateName);
                return std::nullopt;
            }

            if (def->isService)
            {
                return unsupported("service types do not expose value attributes: " + resolved->candidateName);
            }

            const auto& sec = def->request;
            if (attributeName == "_extent_")
            {
                return Value{Rational(sec.extentBits.value_or(sec.offsetAtEnd.max()), 1)};
            }

            for (const auto& constant : sec.constants)
            {
                if (constant.name == attributeName)
                {
                    return constant.value;
                }
            }

            return unsupported("unsupported metaserializable attribute: " + attributeName);
        };

        auto computeUnionOffsetFromSeenFields = [&]() -> BitLengthSet {
            if (unionFields.empty())
            {
                return BitLengthSet(0);
            }

            const auto b       = ceilLog2(static_cast<std::int64_t>(unionFields.size()));
            const auto tagBits = pow2ceil(std::max<std::int64_t>(8, b));

            BitLengthSet payloadSet(0);
            bool         first = true;
            for (const auto& field : unionFields)
            {
                if (first)
                {
                    payloadSet = field.bls;
                    first      = false;
                }
                else
                {
                    payloadSet = payloadSet | field.bls;
                }
            }
            return (BitLengthSet(tagBits) + payloadSet).padToAlignment(8);
        };

        bool offsetInexactWarned = false;
        auto updateOffsetEnv     = [&](const SourceLocation& location) {
            bool exact = true;
            if (section.isUnion)
            {
                env["_offset_"] = Value{bitLengthSetToValueSet(computeUnionOffsetFromSeenFields(), exact)};
            }
            else
            {
                env["_offset_"] = Value{bitLengthSetToValueSet(structureOffset, exact)};
            }
            // The offset value set was truncated: assertions over `_offset_` would be evaluated
            // against an incomplete set and could be unsound. Surface it once per section rather
            // than silently proceeding (BLS-D2). A warning, not an error, because pydsdl evaluates
            // such offsets exactly — we only lack the capacity, we do not reject the definition.
            if (!exact && !offsetInexactWarned)
            {
                diagnostics_.warning(location,
                                     "the set of possible values of '_offset_' exceeds the analyzer's expansion limit "
                                         "of " +
                                         std::to_string(kOffsetExpansionLimit) +
                                         "; assertions referencing '_offset_' may be evaluated against an incomplete "
                                             "set and could be unsound");
                offsetInexactWarned = true;
            }
        };

        auto stmtNeedsOffset = [&](const StatementAST& statement) -> bool {
            if (auto c = std::get_if<ConstantDeclAST>(&statement))
            {
                return exprContainsOffset(c->value);
            }
            if (auto f = std::get_if<FieldDeclAST>(&statement))
            {
                return exprContainsOffset(f->type.arrayCapacity);
            }
            if (auto d = std::get_if<DirectiveAST>(&statement))
            {
                return exprContainsOffset(d->expression);
            }
            return false;
        };

        std::set<std::string> names;

        for (const auto& stmt : statements)
        {
            if (!firstStatementLocation.has_value())
            {
                firstStatementLocation = statementLocation(stmt);
            }

            // Materialize _offset_ lazily only when the current statement can reference it.
            if (stmtNeedsOffset(stmt))
            {
                updateOffsetEnv(statementLocation(stmt));
            }

            if (std::holds_alternative<DirectiveAST>(stmt))
            {
                const auto& d = std::get<DirectiveAST>(stmt);

                if (d.kind == DirectiveKind::Unknown)
                {
                    diagnostics_.error(d.location, "unknown directive @" + d.rawName);
                    continue;
                }

                if (d.kind == DirectiveKind::Union)
                {
                    if (d.expression)
                    {
                        diagnostics_.error(d.location, "@union does not accept an expression");
                    }
                    if (section.isUnion)
                    {
                        diagnostics_.error(d.location, "duplicated @union directive");
                    }
                    if (attributesStarted)
                    {
                        diagnostics_.error(d.location, "@union must appear before first attribute");
                    }
                    section.isUnion        = true;
                    unionDirectiveLocation = d.location;
                    continue;
                }

                if (d.kind == DirectiveKind::Deprecated)
                {
                    if (d.expression)
                    {
                        diagnostics_.error(d.location, "@deprecated does not accept an expression");
                    }
                    if (isResponse)
                    {
                        diagnostics_.error(d.location, "@deprecated is only valid in service request section");
                    }
                    if (section.deprecated)
                    {
                        diagnostics_.error(d.location, "duplicated @deprecated directive");
                    }
                    if (attributesStarted)
                    {
                        diagnostics_.error(d.location, "@deprecated must be placed before first attribute");
                    }
                    section.deprecated = true;
                    continue;
                }

                if (d.kind == DirectiveKind::Extent)
                {
                    if (serializationModeSet)
                    {
                        diagnostics_.error(d.location, "serialization mode already set before @extent");
                        continue;
                    }
                    if (!d.expression)
                    {
                        diagnostics_.error(d.location, "@extent requires an expression");
                        continue;
                    }
                    const SourceLocation expressionLocation = d.expression->location;
                    const std::uint32_t  expressionLength =
                        expressionSpanLengthFromSource(owner.text, expressionLocation, d.expression->str().size());
                    if (exprContainsOffset(d.expression) && section.isUnion)
                    {
                        unionOffsetUsed = true;
                    }
                    auto v = evaluateExpression(*d.expression, env, diagnostics_, d.location, &typeAttrResolver);
                    if (!v || !std::holds_alternative<Rational>(v->data) || !std::get<Rational>(v->data).isInteger())
                    {
                        diagnostics_.error(expressionLocation,
                                           "@extent expression must evaluate to integer rational",
                                           expressionLength);
                        continue;
                    }
                    const auto fittedExtent = std::get<Rational>(v->data).asInteger();
                    if (!fittedExtent)
                    {
                        diagnostics_.error(expressionLocation, "@extent is too large", expressionLength);
                        continue;
                    }
                    const auto extent = *fittedExtent;
                    if (extent < 0)
                    {
                        diagnostics_.error(expressionLocation, "@extent cannot be negative", expressionLength);
                    }
                    section.extentBits             = extent;
                    section.sealed                 = false;
                    serializationModeSet           = true;
                    serializationDirectiveLocation = d.location;
                    extentDirectiveLocation        = d.location;
                    extentExpressionLocation       = expressionLocation;
                    extentExpressionLength         = expressionLength;
                    continue;
                }

                if (d.kind == DirectiveKind::Sealed)
                {
                    if (serializationModeSet)
                    {
                        diagnostics_.error(d.location, "serialization mode already set before @sealed");
                        continue;
                    }
                    if (d.expression)
                    {
                        diagnostics_.error(d.location, "@sealed does not accept an expression");
                    }
                    section.sealed                 = true;
                    serializationModeSet           = true;
                    serializationDirectiveLocation = d.location;
                    continue;
                }

                if (d.kind == DirectiveKind::Assert || d.kind == DirectiveKind::Print)
                {
                    if (d.expression && exprContainsOffset(d.expression) && section.isUnion)
                    {
                        unionOffsetUsed = true;
                    }
                    if (!d.expression && d.kind == DirectiveKind::Assert)
                    {
                        diagnostics_.error(d.location, "@assert requires an expression");
                        continue;
                    }
                    if (d.expression)
                    {
                        auto value =
                            evaluateExpression(*d.expression, env, diagnostics_, d.location, &typeAttrResolver);
                        if (d.kind == DirectiveKind::Assert)
                        {
                            if (!value || !std::holds_alternative<bool>(value->data))
                            {
                                diagnostics_.error(d.location, "@assert expression must evaluate to bool");
                            }
                            else if (!std::get<bool>(value->data))
                            {
                                diagnostics_.error(d.location, "assertion failed");
                            }
                        }
                    }
                    continue;
                }
            }

            if (std::holds_alternative<ConstantDeclAST>(stmt))
            {
                const auto& c     = std::get<ConstantDeclAST>(stmt);
                attributesStarted = true;
                if (serializationModeSet)
                {
                    diagnostics_.error(c.location, "attributes cannot follow @extent/@sealed");
                }
                if (isReservedIdentifier(c.name))
                {
                    diagnostics_.error(c.location, "attribute name is a reserved identifier: " + c.name);
                    continue;
                }
                if (names.contains(c.name))
                {
                    diagnostics_.error(c.location, "duplicate attribute name: " + c.name);
                    continue;
                }

                auto value = evaluateExpression(*c.value, env, diagnostics_, c.location, &typeAttrResolver);
                if (!value)
                {
                    continue;
                }
                if (!checkConstantCompatibility(c, *value))
                {
                    continue;
                }

                names.insert(c.name);
                section.constants.push_back(SemanticConstant{c.name, c.doc, c.type, *value});
                env[c.name] = *value;
                continue;
            }

            if (std::holds_alternative<FieldDeclAST>(stmt))
            {
                const auto& f     = std::get<FieldDeclAST>(stmt);
                attributesStarted = true;
                if (serializationModeSet)
                {
                    diagnostics_.error(f.location, "attributes cannot follow @extent/@sealed");
                }

                if (!f.isPadding && isReservedIdentifier(f.name))
                {
                    diagnostics_.error(f.location, "attribute name is a reserved identifier: " + f.name);
                    continue;
                }
                if (!f.isPadding && names.contains(f.name))
                {
                    diagnostics_.error(f.location, "duplicate attribute name: " + f.name);
                    continue;
                }
                if (!f.isPadding)
                {
                    names.insert(f.name);
                }

                if (section.isUnion && f.isPadding)
                {
                    diagnostics_.error(f.location, "tagged unions cannot contain padding fields");
                    continue;
                }
                if (section.isUnion && unionOffsetUsed)
                {
                    diagnostics_.error(f.location, "field after _offset_ usage in union is not allowed by spec");
                }

                auto          layout = analyzeType(owner, f.type, env, &typeAttrResolver);
                SemanticField semanticField;
                semanticField.name         = f.name;
                semanticField.doc          = f.doc;
                semanticField.type         = f.type;
                semanticField.isPadding    = f.isPadding;
                semanticField.resolvedType = layout.resolved;
                semanticField.sectionName  = isResponse ? "response" : "request";
                if (section.isUnion && !f.isPadding)
                {
                    semanticField.unionOptionIndex = unionOptionCounter++;
                }
                section.fields.push_back(std::move(semanticField));

                if (section.isUnion)
                {
                    unionFields.push_back(layout);
                }
                else
                {
                    structureOffset = structureOffset.padToAlignment(layout.alignment);
                    structureOffset = structureOffset + layout.bls;
                }

                continue;
            }
        }

        if (section.isUnion)
        {
            if (unionFields.size() < 2)
            {
                diagnostics_.error(unionDirectiveLocation.value_or(sectionAnchorLocation()),
                                   "tagged unions must contain at least two fields");
            }

            section.offsetAtEnd     = computeUnionOffsetFromSeenFields();
            const auto b            = ceilLog2(static_cast<std::int64_t>(unionFields.size()));
            const auto unionTagBits = static_cast<std::uint32_t>(pow2ceil(std::max<std::int64_t>(8, b)));
            for (auto& field : section.fields)
            {
                if (!field.isPadding)
                {
                    field.unionTagBits = unionTagBits;
                }
            }
        }
        else
        {
            section.offsetAtEnd = structureOffset.padToAlignment(8);
        }

        if (!serializationModeSet)
        {
            diagnostics_.error(sectionAnchorLocation(), "either @sealed or @extent are required");
        }

        if (!section.sealed)
        {
            const auto           extent              = section.extentBits.value_or(-1);
            const SourceLocation extentLocation      = extentDirectiveLocation.value_or(sectionAnchorLocation());
            const SourceLocation extentValueLocation = extentExpressionLocation.value_or(extentLocation);
            const std::uint32_t  extentValueLength   = extentExpressionLength.value_or(1);
            std::int64_t         suggestedExtent     = std::max<std::int64_t>(section.offsetAtEnd.max(), extent);
            suggestedExtent                          = std::max<std::int64_t>(0, suggestedExtent);
            if (suggestedExtent % 8 != 0)
            {
                suggestedExtent += 8 - (suggestedExtent % 8);
            }
            if (extent < 0)
            {
                diagnostics_.error(extentValueLocation,
                                   "@extent must define non-negative extent bits",
                                   extentValueLength,
                                   suggestedExtent);
            }
            if (extent % 8 != 0)
            {
                diagnostics_.error(extentValueLocation,
                                   "extent must be a multiple of 8 bits",
                                   extentValueLength,
                                   suggestedExtent);
            }
            if (extent < section.offsetAtEnd.max())
            {
                diagnostics_.error(extentValueLocation,
                                   "extent smaller than maximal serialized length",
                                   extentValueLength,
                                   suggestedExtent);
            }
        }
        else
        {
            section.extentBits = section.offsetAtEnd.max();
        }

        section.minBitLength                = section.offsetAtEnd.min();
        section.maxBitLength                = section.offsetAtEnd.max();
        section.fixedSize                   = section.offsetAtEnd.fixed();
        section.serializationBufferSizeBits = section.maxBitLength;

        return section;
    }

    void checkVersionRules()
    {
        struct GroupKey
        {
            std::string   name;
            std::uint32_t major{0};

            auto operator<=>(const GroupKey&) const = default;
        };

        std::map<GroupKey, std::vector<const SemanticDefinition*>> groups;
        for (const auto& r : results_)
        {
            if (!r)
            {
                continue;
            }
            groups[{r->info.fullName, r->info.majorVersion}].push_back(&*r);
        }

        for (auto& [_, defs] : groups)
        {
            std::sort(defs.begin(), defs.end(), [](const auto* a, const auto* b) {
                return a->info.minorVersion < b->info.minorVersion;
            });

            if (defs.empty())
            {
                continue;
            }
            if (defs.front()->info.majorVersion > 0)
            {
                const bool sealed = defs.front()->request.sealed;
                const auto extent = defs.front()->request.extentBits.value_or(0);

                for (const auto* d : defs)
                {
                    if (d->request.sealed != sealed)
                    {
                        diagnostics_.error({d->info.filePath, 1, 1},
                                           "all minor versions under same major must share sealing status");
                    }
                    if (d->request.extentBits.value_or(0) != extent)
                    {
                        diagnostics_.error({d->info.filePath, 1, 1},
                                           "all minor versions under same major must share extent");
                    }
                }
            }

            std::optional<std::uint32_t> lastFixed;
            for (const auto* d : defs)
            {
                if (!d->info.fixedPortId && lastFixed)
                {
                    diagnostics_.error({d->info.filePath, 1, 1},
                                       "fixed port-ID cannot be removed under same major version");
                }
                if (d->info.fixedPortId)
                {
                    if (lastFixed && *lastFixed != *d->info.fixedPortId)
                    {
                        diagnostics_.error({d->info.filePath, 1, 1}, "fixed port-ID mismatch under same major version");
                    }
                    lastFixed = d->info.fixedPortId;
                }
            }
        }

        // Fixed port-ID collisions across unrelated types of the same kind.
        for (std::size_t i = 0; i < results_.size(); ++i)
        {
            if (!results_[i] || !results_[i]->info.fixedPortId)
            {
                continue;
            }
            for (std::size_t j = i + 1; j < results_.size(); ++j)
            {
                if (!results_[j] || !results_[j]->info.fixedPortId)
                {
                    continue;
                }
                if (results_[i]->isService != results_[j]->isService)
                {
                    continue;
                }
                if (results_[i]->info.fullName == results_[j]->info.fullName &&
                    results_[i]->info.majorVersion == results_[j]->info.majorVersion)
                {
                    continue;
                }
                if (*results_[i]->info.fixedPortId == *results_[j]->info.fixedPortId &&
                    results_[i]->info.majorVersion > 0 && results_[j]->info.majorVersion > 0)
                {
                    diagnostics_.error({results_[j]->info.filePath, 1, 1},
                                       "fixed port-ID collision with " + results_[i]->info.filePath);
                }
            }
        }

        if (!options_.allowUnregulatedFixedPortId)
        {
            for (const auto& result : results_)
            {
                if (!result || !result->info.fixedPortId)
                {
                    continue;
                }
                const auto&     sem = *result;
                llvm::StringRef rootNamespace;
                if (!sem.info.namespaceComponents.empty())
                {
                    rootNamespace = sem.info.namespaceComponents.front();
                }
                else
                {
                    const auto pos = sem.info.fullName.find('.');
                    rootNamespace  = pos == std::string::npos ? llvm::StringRef(sem.info.fullName)
                                                              : llvm::StringRef(sem.info.fullName.data(), pos);
                }

                if (!isValidRegulatedPortId(*sem.info.fixedPortId, sem.isService, rootNamespace))
                {
                    diagnostics_.error({sem.info.filePath, 1, 1},
                                       "regulated port-ID " + std::to_string(*sem.info.fixedPortId) + " for " +
                                           (sem.isService ? "service" : "message") + " type " + sem.info.fullName +
                                           " is not valid; use --allow-unregulated-fixed-port-id to override");
                }
            }
        }
    }
};

}  // namespace

llvm::Expected<SemanticModule> analyze(const ASTModule& module, DiagnosticEngine& diagnostics)
{
    return analyze(module, diagnostics, AnalyzeOptions{});
}

llvm::Expected<SemanticModule> analyze(const ASTModule&      module,
                                       DiagnosticEngine&     diagnostics,
                                       const AnalyzeOptions& options)
{
    AnalyzerImpl impl(module, diagnostics, options);
    return impl.run();
}

}  // namespace llvmdsdl
