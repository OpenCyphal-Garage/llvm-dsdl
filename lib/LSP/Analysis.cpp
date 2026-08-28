//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements overlay-aware incremental analysis and semantic snapshot caching.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/Analysis.h"

#include "llvmdsdl/Frontend/AST.h"
#include "llvmdsdl/Frontend/Discovery.h"
#include "llvmdsdl/Frontend/Lexer.h"
#include "llvmdsdl/Frontend/Parser.h"
#include "llvmdsdl/IR/DSDLDialect.h"
#include "llvmdsdl/LSP/DocumentStore.h"
#include "llvmdsdl/LSP/Index.h"
#include "llvmdsdl/LSP/Lint.h"
#include "llvmdsdl/LSP/ServerConfig.h"
#include "llvmdsdl/Lowering/LowerToMLIR.h"
#include "llvmdsdl/Semantics/Analyzer.h"
#include "llvmdsdl/Semantics/Model.h"
#include "llvmdsdl/Support/Diagnostics.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <ios>
#include <llvm/Support/Error.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/OwningOpRef.h>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace llvmdsdl::lsp
{
namespace
{

struct HighlightToken final
{
    std::uint32_t line{0};
    std::uint32_t character{0};
    std::uint32_t length{0};
    std::uint32_t type{0};
    std::uint32_t modifiers{0};
};

constexpr std::uint32_t SemanticTypeKeyword  = 0;
constexpr std::uint32_t SemanticTypeType     = 1;
constexpr std::uint32_t SemanticTypeProperty = 2;
constexpr std::uint32_t SemanticTypeComment  = 3;
constexpr std::uint32_t SemanticTypeOperator = 4;

std::string normalizePath(const std::string_view pathText)
{
    if (pathText.empty())
    {
        return {};
    }

    const std::filesystem::path inputPath(pathText);
    std::error_code             ec;
    std::filesystem::path       absolutePath = std::filesystem::absolute(inputPath, ec);
    if (ec)
    {
        absolutePath = inputPath;
    }

    std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(absolutePath, ec);
    if (ec)
    {
        canonicalPath = absolutePath.lexically_normal();
    }

    return canonicalPath.string();
}

bool startsWithInsensitive(const std::string_view text, const std::string_view prefix)
{
    if (text.size() < prefix.size())
    {
        return false;
    }
    for (std::size_t i = 0; i < prefix.size(); ++i)
    {
        if (std::tolower(static_cast<unsigned char>(text[i])) != std::tolower(static_cast<unsigned char>(prefix[i])))
        {
            return false;
        }
    }
    return true;
}

int hexDigitValue(const char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F')
    {
        return 10 + (c - 'A');
    }
    return -1;
}

std::string decodeUriPath(const std::string_view encoded)
{
    std::string out;
    out.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i)
    {
        if (encoded[i] == '%' && i + 2 < encoded.size())
        {
            const int hi = hexDigitValue(encoded[i + 1]);
            const int lo = hexDigitValue(encoded[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(encoded[i]);
    }
    return out;
}

std::string makeTypeKey(const std::string& fullName, const std::uint32_t major, const std::uint32_t minor)
{
    return fullName + "." + std::to_string(major) + "." + std::to_string(minor);
}

std::string makeTypeKey(const VersionedTypeExprAST& ref)
{
    std::string fullName;
    for (std::size_t i = 0; i < ref.nameComponents.size(); ++i)
    {
        if (i != 0)
        {
            fullName.append(".");
        }
        fullName.append(ref.nameComponents[i]);
    }
    return makeTypeKey(fullName, ref.major, ref.minor);
}

std::string containerFromQualifiedName(const std::string& qualifiedName)
{
    const std::size_t dot = qualifiedName.rfind('.');
    if (dot == std::string::npos)
    {
        return {};
    }
    return qualifiedName.substr(0, dot);
}

std::string makeOverlayShortName(const std::string& normalizedPath)
{
    const std::size_t  value = std::hash<std::string>{}(normalizedPath);
    std::ostringstream stream;
    stream << "Overlay_" << std::hex << value;
    return stream.str();
}

DiscoveredDefinition makeOverlayDiscoveredDefinition(const std::string& normalizedPath, const std::string& text)
{
    DiscoveredDefinition definition;
    definition.filePath          = normalizedPath;
    definition.rootNamespacePath = std::filesystem::path(normalizedPath).parent_path().string();
    definition.shortName         = makeOverlayShortName(normalizedPath);
    definition.namespaceComponents.emplace_back("__overlay");
    definition.fullName     = "__overlay." + definition.shortName;
    definition.majorVersion = 1;
    definition.minorVersion = 0;
    definition.text         = text;
    return definition;
}

std::uint32_t zeroBasedLine(const SourceLocation& location)
{
    return location.line == 0 ? 0U : location.line - 1U;
}

std::uint32_t zeroBasedColumn(const SourceLocation& location)
{
    return location.column == 0 ? 0U : location.column - 1U;
}

bool containsCharacter(const std::uint32_t start, const std::uint32_t length, const std::uint32_t character)
{
    return character >= start && character < start + length;
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

std::int64_t roundUpMultiple(const std::int64_t value, const std::int64_t factor)
{
    if (factor <= 0 || value <= 0)
    {
        return value;
    }
    const std::int64_t remainder = value % factor;
    if (remainder == 0)
    {
        return value;
    }
    return value + (factor - remainder);
}

bool isValidIdentifier(const std::string& text)
{
    if (text.empty())
    {
        return false;
    }
    const auto isHead = [](const unsigned char c) { return std::isalpha(c) || c == '_'; };
    const auto isTail = [](const unsigned char c) { return std::isalnum(c) || c == '_'; };
    if (!isHead(static_cast<unsigned char>(text.front())))
    {
        return false;
    }
    for (std::size_t i = 1; i < text.size(); ++i)
    {
        if (!isTail(static_cast<unsigned char>(text[i])))
        {
            return false;
        }
    }
    return true;
}

DiagnosticLevel lintSeverityToDiagnosticLevel(const LintSeverity severity)
{
    switch (severity)
    {
    case LintSeverity::Info:
        return DiagnosticLevel::Note;
    case LintSeverity::Warning:
        return DiagnosticLevel::Warning;
    case LintSeverity::Error:
        return DiagnosticLevel::Error;
    }
    return DiagnosticLevel::Warning;
}

std::string versionSuffix(const std::uint32_t major, const std::uint32_t minor)
{
    return "." + std::to_string(major) + "." + std::to_string(minor);
}

std::string shortNameFromTypeKey(const std::string& typeKey)
{
    const std::size_t finalDot = typeKey.rfind('.');
    if (finalDot == std::string::npos)
    {
        return {};
    }
    const std::size_t middleDot = typeKey.rfind('.', finalDot - 1U);
    if (middleDot == std::string::npos)
    {
        return {};
    }
    const std::size_t nameDot = typeKey.rfind('.', middleDot - 1U);
    if (nameDot == std::string::npos)
    {
        return typeKey.substr(0, middleDot);
    }
    return typeKey.substr(nameDot + 1U, middleDot - nameDot - 1U);
}

bool isCompletionPrefixTokenKind(const TokenKind kind)
{
    return kind == TokenKind::Identifier || kind == TokenKind::Integer || kind == TokenKind::Dot ||
           kind == TokenKind::At;
}

std::optional<std::string> completionPrefixFromTokens(const std::vector<Token>& tokens,
                                                      const std::uint32_t       line,
                                                      const std::uint32_t       character)
{
    struct CandidateToken final
    {
        std::size_t   index{0};
        std::uint32_t start{0};
        std::uint32_t end{0};
    };
    std::optional<CandidateToken> candidate;

    for (std::size_t index = 0; index < tokens.size(); ++index)
    {
        const Token& token = tokens[index];
        if (!isCompletionPrefixTokenKind(token.kind))
        {
            continue;
        }
        if (zeroBasedLine(token.location) != line)
        {
            continue;
        }

        const std::uint32_t start = zeroBasedColumn(token.location);
        const std::uint32_t end   = start + static_cast<std::uint32_t>(token.text.size());
        if (character <= start || character > end)
        {
            continue;
        }

        if (!candidate.has_value() || start > candidate->start)
        {
            candidate = CandidateToken{index, start, end};
        }
    }
    if (!candidate.has_value())
    {
        return std::nullopt;
    }

    const Token&        first = tokens[candidate->index];
    const std::uint32_t prefixLength =
        std::min<std::uint32_t>(character - candidate->start, static_cast<std::uint32_t>(first.text.size()));
    if (prefixLength == 0)
    {
        return std::nullopt;
    }

    std::string   prefix       = first.text.substr(0, prefixLength);
    std::uint32_t currentStart = candidate->start;
    std::size_t   index        = candidate->index;
    while (index > 0)
    {
        const Token& previous = tokens[index - 1U];
        if (!isCompletionPrefixTokenKind(previous.kind))
        {
            break;
        }
        if (zeroBasedLine(previous.location) != line)
        {
            break;
        }

        const std::uint32_t previousStart = zeroBasedColumn(previous.location);
        const std::uint32_t previousEnd   = previousStart + static_cast<std::uint32_t>(previous.text.size());
        if (previousEnd != currentStart)
        {
            break;
        }

        prefix.insert(0, previous.text);
        currentStart = previousStart;
        --index;
    }
    return prefix;
}

std::size_t hashText(const std::string& text)
{
    return std::hash<std::string>{}(text);
}

std::vector<std::string> collectTypeDependencies(const DefinitionAST& definition)
{
    std::vector<std::string> out;
    for (const StatementAST& statement : definition.statements)
    {
        if (const auto* field = std::get_if<FieldDeclAST>(&statement))
        {
            if (const auto* versioned = std::get_if<VersionedTypeExprAST>(&field->type.scalar))
            {
                out.push_back(makeTypeKey(*versioned));
            }
            continue;
        }
        if (const auto* constant = std::get_if<ConstantDeclAST>(&statement))
        {
            if (const auto* versioned = std::get_if<VersionedTypeExprAST>(&constant->type.scalar))
            {
                out.push_back(makeTypeKey(*versioned));
            }
        }
    }
    std::ranges::sort(out);
    out.erase(std::ranges::unique(out).begin(), out.end());
    return out;
}

}  // namespace

std::string uriToNormalizedPath(const std::string& uri)
{
    if (!startsWithInsensitive(uri, "file://"))
    {
        return normalizePath(uri);
    }

    std::string_view path = std::string_view(uri).substr(7);
    if (!path.empty() && path.front() != '/')
    {
        const std::size_t slash = path.find('/');
        if (slash == std::string_view::npos)
        {
            return {};
        }
        path = path.substr(slash);
    }

    const std::string decoded = decodeUriPath(path);
    return normalizePath(decoded);
}

std::string normalizedPathToFileUri(const std::string& filePath)
{
    const std::string normalized = normalizePath(filePath);
    if (normalized.empty())
    {
        return "file:///";
    }
    if (normalized.front() == '/')
    {
        return "file://" + normalized;
    }
    return "file:///" + normalized;
}

void AnalysisPipeline::populateCachedDefinitionMetadata(CachedDefinition& cached)
{
    cached.typeReferences.clear();
    cached.fieldSymbols.clear();
    cached.constantSymbols.clear();
    cached.directiveTokens.clear();

    for (const StatementAST& statement : cached.ast.statements)
    {
        if (const auto* field = std::get_if<FieldDeclAST>(&statement))
        {
            const std::uint32_t line        = zeroBasedLine(field->type.location);
            const std::uint32_t col         = zeroBasedColumn(field->type.location);
            const std::string   typeDisplay = field->type.str();
            const auto          typeLength  = static_cast<std::uint32_t>(typeDisplay.size());

            if (const auto* versioned = std::get_if<VersionedTypeExprAST>(&field->type.scalar))
            {
                cached.typeReferences.push_back(CachedDefinition::TypeReference{
                    makeTypeKey(*versioned),
                    typeDisplay,
                    line,
                    col,
                    typeLength,
                });
            }

            cached.fieldSymbols.push_back(CachedDefinition::FieldSymbol{
                field->name,
                typeDisplay,
                zeroBasedLine(field->nameLocation),
                zeroBasedColumn(field->nameLocation),
                static_cast<std::uint32_t>(field->name.size()),
            });
            continue;
        }

        if (const auto* constant = std::get_if<ConstantDeclAST>(&statement))
        {
            const std::uint32_t line        = zeroBasedLine(constant->type.location);
            const std::uint32_t col         = zeroBasedColumn(constant->type.location);
            const std::string   typeDisplay = constant->type.str();
            const auto          typeLength  = static_cast<std::uint32_t>(typeDisplay.size());

            if (const auto* versioned = std::get_if<VersionedTypeExprAST>(&constant->type.scalar))
            {
                cached.typeReferences.push_back(CachedDefinition::TypeReference{
                    makeTypeKey(*versioned),
                    typeDisplay,
                    line,
                    col,
                    typeLength,
                });
            }

            cached.constantSymbols.push_back(CachedDefinition::ConstantSymbol{
                constant->name,
                typeDisplay,
                zeroBasedLine(constant->nameLocation),
                zeroBasedColumn(constant->nameLocation),
                static_cast<std::uint32_t>(constant->name.size()),
            });
            continue;
        }

        if (const auto* directive = std::get_if<DirectiveAST>(&statement))
        {
            const std::uint32_t line = zeroBasedLine(directive->location);
            const std::uint32_t col  = zeroBasedColumn(directive->location);
            const std::string   text = "@" + directive->rawName;
            cached.directiveTokens.push_back(CachedDefinition::DirectiveToken{
                text,
                line,
                col,
                static_cast<std::uint32_t>(text.size()),
            });
            continue;
        }

        if (const auto* marker = std::get_if<ServiceResponseMarkerAST>(&statement))
        {
            const std::uint32_t line = zeroBasedLine(marker->location);
            const std::uint32_t col  = zeroBasedColumn(marker->location);
            cached.directiveTokens.push_back(CachedDefinition::DirectiveToken{
                "---",
                line,
                col,
                3,
            });
        }
    }
}

void AnalysisPipeline::rebuildDependencyGraph()
{
    typeKeyToPath_.clear();
    reverseDependenciesByTypeKey_.clear();

    for (const auto& [path, cached] : cachedDefinitionsByPath_)
    {
        typeKeyToPath_[makeTypeKey(cached.info.fullName, cached.info.majorVersion, cached.info.minorVersion)] = path;
    }
    for (const auto& [path, cached] : cachedDefinitionsByPath_)
    {
        for (const std::string& dependencyKey : cached.dependencyTypeKeys)
        {
            reverseDependenciesByTypeKey_[dependencyKey].push_back(path);
        }
    }
}

AnalysisResult AnalysisPipeline::run(const ServerConfig& config, const DocumentStore& documents)
{
    AnalysisResult result;

    const bool rootsChanged =
        cachedRootNamespaceDirs_ != config.rootNamespaceDirs || cachedLookupDirs_ != config.lookupDirs;
    const bool fullRebuild = rootsChanged || cachedDefinitionsByPath_.empty();
    result.fullRebuild     = fullRebuild;
    if (fullRebuild)
    {
        ++stats_.fullRebuildCount;
    }
    else
    {
        ++stats_.incrementalRebuildCount;
    }

    DiagnosticEngine                        discoveryDiagnostics;
    std::vector<DiscoveredDefinition> const discovered =
        discoverDefinitions(config.rootNamespaceDirs,
                            config.lookupDirs,
                            discoveryDiagnostics,
                            // The editor emits nothing, so it is the analysis case: surface a name
                            // collision for any backend rather than none.
                            allOutputLanguages());

    std::unordered_map<std::string, DocumentSnapshot> overlaysByPath;
    std::unordered_map<std::string, std::string>      overlayUriByPath;
    for (const DocumentSnapshot& snapshot : documents.snapshots())
    {
        const std::string path = uriToNormalizedPath(snapshot.uri);
        if (!path.empty())
        {
            overlaysByPath.insert_or_assign(path, snapshot);
            overlayUriByPath.insert_or_assign(path, snapshot.uri);
        }
    }

    std::unordered_map<std::string, DiscoveredDefinition> discoveredByPath;
    discoveredByPath.reserve(discovered.size());
    for (DiscoveredDefinition def : discovered)
    {
        def.filePath = normalizePath(def.filePath);
        if (const auto it = overlaysByPath.find(def.filePath); it != overlaysByPath.end())
        {
            def.text = it->second.text;
        }
        discoveredByPath.insert_or_assign(def.filePath, std::move(def));
    }

    for (const auto& [path, snapshot] : overlaysByPath)
    {
        if (!discoveredByPath.contains(path))
        {
            discoveredByPath.insert_or_assign(path, makeOverlayDiscoveredDefinition(path, snapshot.text));
        }
    }

    std::unordered_set<std::string> dirtyPaths;
    if (fullRebuild)
    {
        for (const auto& [path, _] : discoveredByPath)
        {
            dirtyPaths.insert(path);
        }
        for (const auto& [path, _] : cachedDefinitionsByPath_)
        {
            dirtyPaths.insert(path);
        }
    }
    else
    {
        for (const auto& [cachedPath, _] : cachedDefinitionsByPath_)
        {
            if (!discoveredByPath.contains(cachedPath))
            {
                dirtyPaths.insert(cachedPath);
            }
        }
        for (const auto& [path, current] : discoveredByPath)
        {
            const auto cached = cachedDefinitionsByPath_.find(path);
            if (cached == cachedDefinitionsByPath_.end() || cached->second.textHash != hashText(current.text))
            {
                dirtyPaths.insert(path);
            }
        }
    }

    std::unordered_set<std::string> impactedPaths = dirtyPaths;
    if (!fullRebuild)
    {
        std::queue<std::string> queue;
        for (const std::string& path : dirtyPaths)
        {
            queue.push(path);
        }
        while (!queue.empty())
        {
            const std::string path = queue.front();
            queue.pop();

            std::set<std::string> candidateTypeKeys;
            if (const auto oldDef = cachedDefinitionsByPath_.find(path); oldDef != cachedDefinitionsByPath_.end())
            {
                candidateTypeKeys.insert(makeTypeKey(oldDef->second.info.fullName,
                                                     oldDef->second.info.majorVersion,
                                                     oldDef->second.info.minorVersion));
            }
            if (const auto newDef = discoveredByPath.find(path); newDef != discoveredByPath.end())
            {
                candidateTypeKeys.insert(
                    makeTypeKey(newDef->second.fullName, newDef->second.majorVersion, newDef->second.minorVersion));
            }

            for (const std::string& typeKey : candidateTypeKeys)
            {
                const auto reverseIt = reverseDependenciesByTypeKey_.find(typeKey);
                if (reverseIt == reverseDependenciesByTypeKey_.end())
                {
                    continue;
                }
                for (const std::string& dependentPath : reverseIt->second)
                {
                    if (impactedPaths.insert(dependentPath).second)
                    {
                        queue.push(dependentPath);
                    }
                }
            }
        }
    }

    result.dirtyDefinitionCount        = dirtyPaths.size();
    result.impactedDefinitionCount     = impactedPaths.size();
    stats_.lastDirtyDefinitionCount    = result.dirtyDefinitionCount;
    stats_.lastImpactedDefinitionCount = result.impactedDefinitionCount;

    for (const std::string& path : impactedPaths)
    {
        const auto current = discoveredByPath.find(path);
        if (current == discoveredByPath.end())
        {
            cachedDefinitionsByPath_.erase(path);
            parseDiagnosticsByPath_.erase(path);
            continue;
        }

        DiagnosticEngine   parseDiagnostics;
        Lexer              lexer(current->second.filePath, current->second.text);
        std::vector<Token> lexTokens = lexer.lex();
        for (const LexerError& lexError : lexer.errors())
        {
            parseDiagnostics.error(lexError.location, lexError.message);
        }
        std::vector<Token>            parserTokens = lexTokens;
        Parser                        parser(current->second.filePath, std::move(parserTokens), parseDiagnostics);
        llvm::Expected<DefinitionAST> parsed = parser.parseDefinition();
        if (!parsed)
        {
            llvm::consumeError(parsed.takeError());
            cachedDefinitionsByPath_.erase(path);
            parseDiagnosticsByPath_[path] = parseDiagnostics.diagnostics();
            continue;
        }

        CachedDefinition cached;
        cached.info          = current->second;
        cached.ast           = *parsed;
        cached.sourceText    = current->second.text;
        cached.lexTokens     = std::move(lexTokens);
        const auto overlayIt = overlayUriByPath.find(path);
        cached.sourceUri     = overlayIt == overlayUriByPath.end() ? normalizedPathToFileUri(path) : overlayIt->second;
        cached.textHash      = hashText(current->second.text);
        cached.dependencyTypeKeys = collectTypeDependencies(*parsed);
        cached.parseDiagnostics   = parseDiagnostics.diagnostics();
        populateCachedDefinitionMetadata(cached);
        cachedDefinitionsByPath_.insert_or_assign(path, std::move(cached));
        parseDiagnosticsByPath_.erase(path);
    }

    rebuildDependencyGraph();

    std::vector<std::string> sortedPaths;
    sortedPaths.reserve(cachedDefinitionsByPath_.size());
    for (const auto& [path, _] : cachedDefinitionsByPath_)
    {
        sortedPaths.push_back(path);
    }
    std::ranges::sort(sortedPaths);

    std::vector<ParsedDefinition> parsedDefinitions;
    parsedDefinitions.reserve(cachedDefinitionsByPath_.size());
    for (const std::string& path : sortedPaths)
    {
        const CachedDefinition& cached = cachedDefinitionsByPath_.at(path);
        parsedDefinitions.push_back(ParsedDefinition{cached.info, cached.ast});
    }
    std::ranges::sort(parsedDefinitions,

                      [](const ParsedDefinition& lhs, const ParsedDefinition& rhs) {
                          if (lhs.info.fullName != rhs.info.fullName)
                          {
                              return lhs.info.fullName < rhs.info.fullName;
                          }
                          if (lhs.info.majorVersion != rhs.info.majorVersion)
                          {
                              return lhs.info.majorVersion > rhs.info.majorVersion;
                          }
                          if (lhs.info.minorVersion != rhs.info.minorVersion)
                          {
                              return lhs.info.minorVersion > rhs.info.minorVersion;
                          }
                          return lhs.info.filePath < rhs.info.filePath;
                      });

    ASTModule module;
    module.definitions = std::move(parsedDefinitions);

    DiagnosticEngine              semanticDiagnostics;
    std::optional<SemanticModule> semanticModule;
    if (!module.definitions.empty())
    {
        llvm::Expected<SemanticModule> analyzed = analyze(module, semanticDiagnostics);
        if (analyzed)
        {
            semanticModule = std::move(*analyzed);
        }
        else
        {
            llvm::consumeError(analyzed.takeError());
        }
    }

    latestExtentInfoByPath_.clear();
    if (semanticModule.has_value())
    {
        for (const SemanticDefinition& definition : semanticModule->definitions)
        {
            DefinitionExtentInfo extentInfo;
            extentInfo.request.requiredBits = definition.request.offsetAtEnd.max();
            extentInfo.request.declaredBits = definition.request.extentBits;
            if (definition.response.has_value())
            {
                extentInfo.response = SectionExtentInfo{
                    definition.response->offsetAtEnd.max(),
                    definition.response->extentBits,
                };
            }
            latestExtentInfoByPath_.insert_or_assign(normalizePath(definition.info.filePath), extentInfo);
        }
    }

    std::vector<Diagnostic> allDiagnostics;
    allDiagnostics.reserve(discoveryDiagnostics.diagnostics().size() + semanticDiagnostics.diagnostics().size() +
                           parseDiagnosticsByPath_.size() + cachedDefinitionsByPath_.size());

    for (const Diagnostic& d : discoveryDiagnostics.diagnostics())
    {
        allDiagnostics.push_back(d);
    }
    for (const auto& [_, parseDiagnostics] : parseDiagnosticsByPath_)
    {
        allDiagnostics.insert(allDiagnostics.end(), parseDiagnostics.begin(), parseDiagnostics.end());
    }
    for (const auto& [_, cached] : cachedDefinitionsByPath_)
    {
        allDiagnostics.insert(allDiagnostics.end(), cached.parseDiagnostics.begin(), cached.parseDiagnostics.end());
    }
    for (const Diagnostic& d : semanticDiagnostics.diagnostics())
    {
        allDiagnostics.push_back(d);
    }

    latestMlirSnapshot_.reset();
    if (config.enableMlirSnapshot && semanticModule.has_value())
    {
        DiagnosticEngine      loweringDiagnostics;
        mlir::DialectRegistry registry;
        registry.insert<mlir::dsdl::DSDLDialect, mlir::func::FuncDialect>();
        mlir::MLIRContext context(registry);
        // Best-effort introspection snapshot: lower error-tolerant / partial semantic
        // models without the strict codegen-path verification (which would reject a
        // partial module and drop the snapshot entirely).
        mlir::OwningOpRef<mlir::ModuleOp> mlirModule =
            lowerToMLIR(*semanticModule, context, loweringDiagnostics, /*verifyModule=*/false);
        for (const Diagnostic& d : loweringDiagnostics.diagnostics())
        {
            allDiagnostics.push_back(d);
        }
        if (mlirModule)
        {
            std::string              rendered;
            llvm::raw_string_ostream os(rendered);
            mlirModule->print(os);
            os.flush();
            latestMlirSnapshot_ = std::move(rendered);
        }
    }

    latestLintFindingsByUri_.clear();
    if (config.lintEnabled)
    {
        LintExecutionConfig lintConfig;
        lintConfig.enabled           = config.lintEnabled;
        lintConfig.disabledRules     = config.lintDisabledRules;
        lintConfig.fileDisabledRules = config.lintFileDisabledRules;
        lintConfig.pluginLibraries   = config.lintPluginLibraries;

        std::vector<LintDocument> lintDocuments;
        lintDocuments.reserve(sortedPaths.size());
        for (const std::string& path : sortedPaths)
        {
            const CachedDefinition& cached = cachedDefinitionsByPath_.at(path);
            lintDocuments.push_back(LintDocument{
                path,
                cached.sourceUri,
                cached.info,
                cached.ast,
                cached.sourceText,
            });
        }

        LintEngine const    lintEngine(LintRegistry{}, std::move(lintConfig));
        LintRunResult const lintResult = lintEngine.run(lintDocuments);
        latestLintFindingsByUri_       = lintResult.findingsByUri;
        for (const LintFinding& finding : lintResult.findings)
        {
            allDiagnostics.push_back(Diagnostic{
                lintSeverityToDiagnosticLevel(finding.severity),
                finding.location,
                "[lint:" + finding.ruleId + "] " + finding.message,
                1,
                std::nullopt,
            });
        }
    }

    bool hasErrors = false;
    for (const Diagnostic& diagnostic : allDiagnostics)
    {
        hasErrors = hasErrors || diagnostic.level == DiagnosticLevel::Error;
        if (diagnostic.location.file.empty())
        {
            continue;
        }
        if (const auto overlayIt = overlayUriByPath.find(diagnostic.location.file); overlayIt != overlayUriByPath.end())
        {
            result.diagnosticsByUri[overlayIt->second].push_back(diagnostic);
            continue;
        }
        result.diagnosticsByUri[normalizedPathToFileUri(diagnostic.location.file)].push_back(diagnostic);
    }
    result.hasErrors        = hasErrors;
    latestDiagnosticsByUri_ = result.diagnosticsByUri;

    cachedRootNamespaceDirs_ = config.rootNamespaceDirs;
    cachedLookupDirs_        = config.lookupDirs;

    ++snapshotVersion_;
    stats_.lastSnapshotVersion = snapshotVersion_;
    result.snapshotVersion     = snapshotVersion_;
    result.mlirSnapshot        = latestMlirSnapshot_;
    return result;
}

bool AnalysisPipeline::documentTextMatches(const std::string& uri, const std::string& text) const
{
    const std::string path = uriToNormalizedPath(uri);
    const auto        it   = cachedDefinitionsByPath_.find(path);
    return it != cachedDefinitionsByPath_.end() && it->second.sourceText == text;
}

namespace
{

/// @brief Renders "emits as X (c, cpp) - Y (go)" for one attribute.
///
/// Languages are grouped by the identifier they produce, because listing six rows for a name that
/// is the same in five of them is noise. The scope is rebuilt from the attribute names in
/// declaration order, which is how the emitters build theirs, so a name that had to be
/// disambiguated reads the same here as it will in the generated source.
std::string renderGeneratedNames(const std::vector<std::string>& orderedNames,
                                 const std::string&              hovered,
                                 const llvmdsdl::IdentifierRole  role)
{
    std::vector<std::pair<std::string, std::vector<std::string>>> groups;
    for (const auto& [language, languageName] : llvmdsdl::allOutputLanguages())
    {
        llvmdsdl::NamingScope scope(language);
        for (const auto& name : orderedNames)
        {
            (void) scope.declare(role, name);
        }
        const std::string identifier = scope.get(role, hovered);

        auto group =
            std::ranges::find_if(groups, [&identifier](const auto& entry) { return entry.first == identifier; });
        if (group == groups.end())
        {
            groups.push_back({identifier, {languageName.str()}});
        }
        else
        {
            group->second.push_back(languageName.str());
        }
    }

    std::string out = "\n\nemits as ";
    for (std::size_t i = 0; i < groups.size(); ++i)
    {
        out += (i > 0 ? " · " : "");
        out += "`" + groups[i].first + "` (";
        for (std::size_t j = 0; j < groups[i].second.size(); ++j)
        {
            out += (j > 0 ? ", " : "") + groups[i].second[j];
        }
        out += ")";
    }
    return out;
}

}  // namespace

std::optional<HoverData> AnalysisPipeline::hover(const std::string&  uri,
                                                 const std::uint32_t line,
                                                 const std::uint32_t character) const
{
    const std::string path = uriToNormalizedPath(uri);
    const auto        it   = cachedDefinitionsByPath_.find(path);
    if (it == cachedDefinitionsByPath_.end())
    {
        return std::nullopt;
    }

    for (const CachedDefinition::TypeReference& reference : it->second.typeReferences)
    {
        if (reference.line != line || !containsCharacter(reference.character, reference.length, character))
        {
            continue;
        }

        const auto targetPath = typeKeyToPath_.find(reference.typeKey);
        if (targetPath == typeKeyToPath_.end())
        {
            return HoverData{"`" + reference.typeKey + "` (unresolved type)"};
        }
        const CachedDefinition& target = cachedDefinitionsByPath_.at(targetPath->second);
        return HoverData{
            "`" + reference.typeKey + "`\n\n" + (target.ast.isService() ? "Service type" : "Message type") +
                "\n\nDefined in `" + target.info.filePath + "`",
        };
    }

    for (const CachedDefinition::FieldSymbol& field : it->second.fieldSymbols)
    {
        if (field.line == line && containsCharacter(field.character, field.length, character))
        {
            std::vector<std::string> names;
            names.reserve(it->second.fieldSymbols.size());
            for (const auto& symbol : it->second.fieldSymbols)
            {
                names.push_back(symbol.name);
            }
            return HoverData{"`" + field.name + "`: `" + field.typeDisplay + "`" +
                             renderGeneratedNames(names, field.name, llvmdsdl::IdentifierRole::FieldName)};
        }
    }
    for (const CachedDefinition::ConstantSymbol& constant : it->second.constantSymbols)
    {
        if (constant.line == line && containsCharacter(constant.character, constant.length, character))
        {
            std::vector<std::string> names;
            names.reserve(it->second.constantSymbols.size());
            for (const auto& symbol : it->second.constantSymbols)
            {
                names.push_back(symbol.name);
            }
            return HoverData{"`" + constant.name + "`: `" + constant.typeDisplay + "`" +
                             renderGeneratedNames(names, constant.name, llvmdsdl::IdentifierRole::ConstantName)};
        }
    }

    return std::nullopt;
}

std::optional<AnalysisLocation> AnalysisPipeline::definition(const std::string&  uri,
                                                             const std::uint32_t line,
                                                             const std::uint32_t character) const
{
    const std::string path = uriToNormalizedPath(uri);
    const auto        it   = cachedDefinitionsByPath_.find(path);
    if (it == cachedDefinitionsByPath_.end())
    {
        return std::nullopt;
    }

    for (const CachedDefinition::TypeReference& reference : it->second.typeReferences)
    {
        if (reference.line != line || !containsCharacter(reference.character, reference.length, character))
        {
            continue;
        }

        const auto targetPath = typeKeyToPath_.find(reference.typeKey);
        if (targetPath == typeKeyToPath_.end())
        {
            return std::nullopt;
        }
        const CachedDefinition& target = cachedDefinitionsByPath_.at(targetPath->second);
        return AnalysisLocation{
            target.sourceUri,
            target.ast.location.line == 0 ? 0 : target.ast.location.line - 1U,
            target.ast.location.column == 0 ? 0 : target.ast.location.column - 1U,
            static_cast<std::uint32_t>(target.info.shortName.size()),
        };
    }
    return std::nullopt;
}

std::vector<AnalysisLocation> AnalysisPipeline::references(const std::string&  uri,
                                                           const std::uint32_t line,
                                                           const std::uint32_t character,
                                                           const bool          includeDeclaration) const
{
    std::vector<AnalysisLocation> out;
    const std::string             path = uriToNormalizedPath(uri);
    const auto                    it   = cachedDefinitionsByPath_.find(path);
    if (it == cachedDefinitionsByPath_.end())
    {
        return out;
    }

    std::optional<std::string> targetTypeKey;
    for (const CachedDefinition::TypeReference& reference : it->second.typeReferences)
    {
        if (reference.line == line && containsCharacter(reference.character, reference.length, character))
        {
            targetTypeKey = reference.typeKey;
            break;
        }
    }
    if (!targetTypeKey.has_value())
    {
        return out;
    }

    if (includeDeclaration)
    {
        if (const auto definitionLocation = definition(uri, line, character))
        {
            out.push_back(*definitionLocation);
        }
    }

    for (const auto& [_, cached] : cachedDefinitionsByPath_)
    {
        for (const CachedDefinition::TypeReference& reference : cached.typeReferences)
        {
            if (reference.typeKey == *targetTypeKey)
            {
                out.push_back(
                    AnalysisLocation{cached.sourceUri, reference.line, reference.character, reference.length});
            }
        }
    }

    std::ranges::sort(out, [](const AnalysisLocation& lhs, const AnalysisLocation& rhs) {
        return std::tie(lhs.uri, lhs.line, lhs.character, lhs.length) <
               std::tie(rhs.uri, rhs.line, rhs.character, rhs.length);
    });
    out.erase(std::ranges::unique(out,

                                  [](const AnalysisLocation& lhs, const AnalysisLocation& rhs) {
                                      return lhs.uri == rhs.uri && lhs.line == rhs.line &&
                                             lhs.character == rhs.character && lhs.length == rhs.length;
                                  })
                  .begin(),
              out.end());
    return out;
}

std::vector<DocumentSymbolData> AnalysisPipeline::documentSymbols(const std::string& uri) const
{
    std::vector<DocumentSymbolData> out;
    const std::string               path = uriToNormalizedPath(uri);
    const auto                      it   = cachedDefinitionsByPath_.find(path);
    if (it == cachedDefinitionsByPath_.end())
    {
        return out;
    }

    const CachedDefinition& cached = it->second;
    out.push_back(DocumentSymbolData{
        cached.info.shortName,
        cached.ast.isService() ? "service" : "message",
        23,
        AnalysisLocation{
            cached.sourceUri,
            cached.ast.location.line == 0 ? 0 : cached.ast.location.line - 1U,
            cached.ast.location.column == 0 ? 0 : cached.ast.location.column - 1U,
            static_cast<std::uint32_t>(cached.info.shortName.size()),
        },
    });

    for (const CachedDefinition::FieldSymbol& field : cached.fieldSymbols)
    {
        out.push_back(DocumentSymbolData{
            field.name,
            field.typeDisplay,
            8,
            AnalysisLocation{
                cached.sourceUri,
                field.line,
                field.character,
                field.length,
            },
        });
    }
    for (const CachedDefinition::ConstantSymbol& constant : cached.constantSymbols)
    {
        out.push_back(DocumentSymbolData{
            constant.name,
            constant.typeDisplay,
            14,
            AnalysisLocation{
                cached.sourceUri,
                constant.line,
                constant.character,
                constant.length,
            },
        });
    }

    std::ranges::sort(out, [](const DocumentSymbolData& lhs, const DocumentSymbolData& rhs) {
        return std::tie(lhs.location.line, lhs.location.character, lhs.name) <
               std::tie(rhs.location.line, rhs.location.character, rhs.name);
    });
    return out;
}

std::vector<CompletionData> AnalysisPipeline::completions(const std::string&  uri,
                                                          const std::uint32_t line,
                                                          const std::uint32_t character,
                                                          std::string*        queryPrefix) const
{
    std::vector<CompletionData> out;

    std::string       prefix;
    const std::string path = uriToNormalizedPath(uri);
    if (const auto it = cachedDefinitionsByPath_.find(path); it != cachedDefinitionsByPath_.end())
    {
        if (const auto tokenPrefix = completionPrefixFromTokens(it->second.lexTokens, line, character))
        {
            prefix = *tokenPrefix;
        }
    }
    if (queryPrefix)
    {
        *queryPrefix = prefix;
    }

    const bool        directiveMode = !prefix.empty() && prefix.front() == '@';
    const std::string loweredPrefix = [&prefix]() {
        std::string lower = prefix;
        std::ranges::transform(lower, lower.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return lower;
    }();
    const auto matchesPrefix = [&loweredPrefix](const std::string& value) {
        std::string lower = value;
        std::ranges::transform(lower, lower.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return loweredPrefix.empty() || lower.starts_with(loweredPrefix);
    };
    const auto lexicalScoreFor = [&loweredPrefix](const std::string& value) {
        if (loweredPrefix.empty())
        {
            return 10.0;
        }
        std::string lower = value;
        std::ranges::transform(lower, lower.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lower == loweredPrefix)
        {
            return 90.0;
        }
        if (lower.starts_with(loweredPrefix))
        {
            return 70.0 + std::min(15.0, static_cast<double>(loweredPrefix.size()));
        }
        if (lower.contains(loweredPrefix))
        {
            return 40.0;
        }
        return 0.0;
    };

    if (directiveMode || prefix.empty())
    {
        static const std::array<std::string, 6> directives =
            {"@union", "@extent", "@sealed", "@deprecated", "@assert", "@print"};
        for (const std::string& directive : directives)
        {
            if (matchesPrefix(directive))
            {
                out.push_back(CompletionData{
                    directive,
                    14,
                    "directive",
                    "completion:directive:" + directive,
                    lexicalScoreFor(directive),
                });
            }
        }
    }

    if (!directiveMode)
    {
        static const std::array<std::string, 7> primitiveTypes =
            {"bool", "uint8", "uint16", "uint32", "uint64", "int8", "float32"};
        for (const std::string& primitive : primitiveTypes)
        {
            if (matchesPrefix(primitive))
            {
                out.push_back(CompletionData{
                    primitive,
                    7,
                    "primitive",
                    "completion:primitive:" + primitive,
                    lexicalScoreFor(primitive),
                });
            }
        }
        for (const auto& [typeKey, _] : typeKeyToPath_)
        {
            if (matchesPrefix(typeKey))
            {
                out.push_back(CompletionData{
                    typeKey,
                    7,
                    "composite",
                    "completion:composite:" + typeKey,
                    lexicalScoreFor(typeKey) + 2.0,
                });
            }
        }
    }

    std::ranges::sort(out, [](const CompletionData& lhs, const CompletionData& rhs) {
        if (lhs.baseScore != rhs.baseScore)
        {
            return lhs.baseScore > rhs.baseScore;
        }
        return std::tie(lhs.label, lhs.kind) < std::tie(rhs.label, rhs.kind);
    });
    out.erase(std::ranges::unique(out,

                                  [](const CompletionData& lhs, const CompletionData& rhs) {
                                      return lhs.label == rhs.label && lhs.kind == rhs.kind && lhs.detail == rhs.detail;
                                  })
                  .begin(),
              out.end());
    return out;
}

std::vector<std::array<std::uint32_t, 5>> AnalysisPipeline::semanticTokens(const std::string& uri) const
{
    std::vector<std::array<std::uint32_t, 5>> encoded;
    const std::string                         path = uriToNormalizedPath(uri);
    const auto                                it   = cachedDefinitionsByPath_.find(path);
    if (it == cachedDefinitionsByPath_.end())
    {
        return encoded;
    }

    const CachedDefinition&     cached = it->second;
    std::vector<HighlightToken> tokens;
    tokens.reserve((cached.fieldSymbols.size() * 2U) + (cached.constantSymbols.size() * 2U) +
                   cached.directiveTokens.size() + 8U);

    for (const CachedDefinition::DirectiveToken& directive : cached.directiveTokens)
    {
        const std::uint32_t tokenType = directive.text == "---" ? SemanticTypeOperator : SemanticTypeKeyword;
        tokens.push_back(HighlightToken{directive.line, directive.character, directive.length, tokenType, 0});
    }

    for (const Token& token : cached.lexTokens)
    {
        if (token.kind != TokenKind::Comment)
        {
            continue;
        }
        tokens.push_back(HighlightToken{
            zeroBasedLine(token.location),
            zeroBasedColumn(token.location),
            static_cast<std::uint32_t>(token.text.size()),
            SemanticTypeComment,
            0,
        });
    }

    for (const StatementAST& statement : cached.ast.statements)
    {
        if (const auto* field = std::get_if<FieldDeclAST>(&statement))
        {
            const std::uint32_t line   = field->type.location.line == 0 ? 0 : field->type.location.line - 1U;
            const std::uint32_t column = field->type.location.column == 0 ? 0 : field->type.location.column - 1U;
            tokens.push_back(HighlightToken{
                line,
                column,
                static_cast<std::uint32_t>(field->type.str().size()),
                SemanticTypeType,
                0,
            });
        }
        if (const auto* constant = std::get_if<ConstantDeclAST>(&statement))
        {
            const std::uint32_t line   = constant->type.location.line == 0 ? 0 : constant->type.location.line - 1U;
            const std::uint32_t column = constant->type.location.column == 0 ? 0 : constant->type.location.column - 1U;
            tokens.push_back(HighlightToken{
                line,
                column,
                static_cast<std::uint32_t>(constant->type.str().size()),
                SemanticTypeType,
                0,
            });
        }
    }

    for (const CachedDefinition::FieldSymbol& field : cached.fieldSymbols)
    {
        tokens.push_back(HighlightToken{field.line, field.character, field.length, SemanticTypeProperty, 0});
    }
    for (const CachedDefinition::ConstantSymbol& constant : cached.constantSymbols)
    {
        tokens.push_back(HighlightToken{constant.line, constant.character, constant.length, SemanticTypeProperty, 0});
    }

    std::ranges::sort(tokens, [](const HighlightToken& lhs, const HighlightToken& rhs) {
        return std::tie(lhs.line, lhs.character, lhs.length, lhs.type) <
               std::tie(rhs.line, rhs.character, rhs.length, rhs.type);
    });
    tokens.erase(std::ranges::unique(tokens,

                                     [](const HighlightToken& lhs, const HighlightToken& rhs) {
                                         return lhs.line == rhs.line && lhs.character == rhs.character &&
                                                lhs.length == rhs.length && lhs.type == rhs.type &&
                                                lhs.modifiers == rhs.modifiers;
                                     })
                     .begin(),
                 tokens.end());

    std::uint32_t previousLine = 0U;
    std::uint32_t previousChar = 0U;
    bool          first        = true;
    encoded.reserve(tokens.size());
    for (const HighlightToken& token : tokens)
    {
        const std::uint32_t deltaLine = first ? token.line : token.line - previousLine;
        const std::uint32_t deltaChar =
            (first || token.line != previousLine) ? token.character : token.character - previousChar;
        encoded.push_back({deltaLine, deltaChar, token.length, token.type, token.modifiers});
        previousLine = token.line;
        previousChar = token.character;
        first        = false;
    }

    return encoded;
}

std::vector<IndexFileShard> AnalysisPipeline::buildIndexShards() const
{
    std::vector<IndexFileShard> shards;
    shards.reserve(cachedDefinitionsByPath_.size());

    std::vector<std::string> sortedPaths;
    sortedPaths.reserve(cachedDefinitionsByPath_.size());
    for (const auto& [path, _] : cachedDefinitionsByPath_)
    {
        sortedPaths.push_back(path);
    }
    std::ranges::sort(sortedPaths);

    for (const std::string& path : sortedPaths)
    {
        const CachedDefinition& cached = cachedDefinitionsByPath_.at(path);
        const std::string       typeKey =
            makeTypeKey(cached.info.fullName, cached.info.majorVersion, cached.info.minorVersion);
        const std::string typeUsr = "type:" + typeKey;

        IndexFileShard shard;
        shard.metadata.schemaVersion   = LspIndexSchemaVersion;
        shard.metadata.filePath        = path;
        shard.metadata.sourceUri       = cached.sourceUri;
        shard.metadata.textHash        = cached.textHash;
        shard.metadata.snapshotVersion = snapshotVersion_;

        const std::uint32_t typeLine      = cached.ast.location.line == 0 ? 0U : cached.ast.location.line - 1U;
        const std::uint32_t typeCharacter = cached.ast.location.column == 0 ? 0U : cached.ast.location.column - 1U;

        shard.symbols.push_back(IndexSymbolRecord{
            typeUsr,
            cached.info.shortName,
            typeKey,
            containerFromQualifiedName(typeKey),
            cached.ast.isService() ? "service" : "message",
            23,
            path,
            IndexLocation{
                cached.sourceUri,
                typeLine,
                typeCharacter,
                static_cast<std::uint32_t>(cached.info.shortName.size()),
            },
        });

        shard.references.push_back(IndexReferenceRecord{
            typeUsr,
            path,
            IndexLocation{
                cached.sourceUri,
                typeLine,
                typeCharacter,
                static_cast<std::uint32_t>(cached.info.shortName.size()),
            },
            true,
        });

        for (const CachedDefinition::FieldSymbol& field : cached.fieldSymbols)
        {
            const std::string usr = "field:" + typeKey + ":" + field.name + ":" + std::to_string(field.line) + ":" +
                                    std::to_string(field.character);
            shard.symbols.push_back(IndexSymbolRecord{
                usr,
                field.name,
                typeKey + "." + field.name,
                typeKey,
                field.typeDisplay,
                8,
                path,
                IndexLocation{
                    cached.sourceUri,
                    field.line,
                    field.character,
                    field.length,
                },
            });
        }

        for (const CachedDefinition::ConstantSymbol& constant : cached.constantSymbols)
        {
            const std::string usr = "constant:" + typeKey + ":" + constant.name + ":" + std::to_string(constant.line) +
                                    ":" + std::to_string(constant.character);
            shard.symbols.push_back(IndexSymbolRecord{
                usr,
                constant.name,
                typeKey + "." + constant.name,
                typeKey,
                constant.typeDisplay,
                14,
                path,
                IndexLocation{
                    cached.sourceUri,
                    constant.line,
                    constant.character,
                    constant.length,
                },
            });
        }

        for (const CachedDefinition::TypeReference& reference : cached.typeReferences)
        {
            shard.references.push_back(IndexReferenceRecord{
                "type:" + reference.typeKey,
                path,
                IndexLocation{
                    cached.sourceUri,
                    reference.line,
                    reference.character,
                    reference.length,
                },
                false,
            });
        }

        std::ranges::sort(shard.symbols,

                          [](const IndexSymbolRecord& lhs, const IndexSymbolRecord& rhs) {
                              return std::tie(lhs.usr,
                                              lhs.location.line,
                                              lhs.location.character,
                                              lhs.name,
                                              lhs.qualifiedName,
                                              lhs.kind) < std::tie(rhs.usr,
                                                                   rhs.location.line,
                                                                   rhs.location.character,
                                                                   rhs.name,
                                                                   rhs.qualifiedName,
                                                                   rhs.kind);
                          });

        std::ranges::sort(shard.references,

                          [](const IndexReferenceRecord& lhs, const IndexReferenceRecord& rhs) {
                              return std::tie(lhs.targetUsr,
                                              lhs.location.line,
                                              lhs.location.character,
                                              lhs.location.length,
                                              lhs.isDeclaration) < std::tie(rhs.targetUsr,
                                                                            rhs.location.line,
                                                                            rhs.location.character,
                                                                            rhs.location.length,
                                                                            rhs.isDeclaration);
                          });

        shards.push_back(std::move(shard));
    }

    return shards;
}

std::optional<PrepareRenameData> AnalysisPipeline::prepareRename(const std::string&  uri,
                                                                 const std::uint32_t line,
                                                                 const std::uint32_t character) const
{
    const std::string path = uriToNormalizedPath(uri);
    const auto        it   = cachedDefinitionsByPath_.find(path);
    if (it == cachedDefinitionsByPath_.end())
    {
        return std::nullopt;
    }

    const CachedDefinition& cached = it->second;
    for (const CachedDefinition::TypeReference& reference : cached.typeReferences)
    {
        if (reference.line == line && containsCharacter(reference.character, reference.length, character))
        {
            return PrepareRenameData{
                AnalysisLocation{cached.sourceUri, reference.line, reference.character, reference.length},
                shortNameFromTypeKey(reference.typeKey),
            };
        }
    }
    for (const CachedDefinition::FieldSymbol& field : cached.fieldSymbols)
    {
        if (field.line == line && containsCharacter(field.character, field.length, character))
        {
            return PrepareRenameData{
                AnalysisLocation{cached.sourceUri, field.line, field.character, field.length},
                field.name,
            };
        }
    }
    for (const CachedDefinition::ConstantSymbol& constant : cached.constantSymbols)
    {
        if (constant.line == line && containsCharacter(constant.character, constant.length, character))
        {
            return PrepareRenameData{
                AnalysisLocation{cached.sourceUri, constant.line, constant.character, constant.length},
                constant.name,
            };
        }
    }

    const std::uint32_t typeLine   = cached.ast.location.line == 0 ? 0U : cached.ast.location.line - 1U;
    const std::uint32_t typeColumn = cached.ast.location.column == 0 ? 0U : cached.ast.location.column - 1U;
    const auto          typeLength = static_cast<std::uint32_t>(cached.info.shortName.size());
    if (typeLine == line && containsCharacter(typeColumn, typeLength, character))
    {
        return PrepareRenameData{
            AnalysisLocation{cached.sourceUri, typeLine, typeColumn, typeLength},
            cached.info.shortName,
        };
    }
    return std::nullopt;
}

RenamePlanData AnalysisPipeline::planRename(const std::string&  uri,
                                            const std::uint32_t line,
                                            const std::uint32_t character,
                                            const std::string&  newName,
                                            const bool          includeFileRename) const
{
    RenamePlanData plan;

    if (!isValidIdentifier(newName))
    {
        plan.errorMessage = "newName must be a valid DSDL identifier";
        return plan;
    }

    const std::string sourcePath = uriToNormalizedPath(uri);
    const auto        sourceIt   = cachedDefinitionsByPath_.find(sourcePath);
    if (sourceIt == cachedDefinitionsByPath_.end())
    {
        plan.errorMessage = "document is not indexed in the current analysis snapshot";
        return plan;
    }

    enum class TargetKind
    {
        None,
        Type,
        Field,
        Constant,
    };
    TargetKind    targetKind = TargetKind::None;
    std::string   targetPath = sourcePath;
    std::string   targetTypeKey;
    std::string   oldName;
    std::uint32_t targetLine{0};
    std::uint32_t targetCharacter{0};
    std::uint32_t targetLength{0};

    const CachedDefinition& source = sourceIt->second;
    for (const CachedDefinition::TypeReference& reference : source.typeReferences)
    {
        if (reference.line != line || !containsCharacter(reference.character, reference.length, character))
        {
            continue;
        }
        const auto definitionPathIt = typeKeyToPath_.find(reference.typeKey);
        if (definitionPathIt == typeKeyToPath_.end())
        {
            plan.errorMessage = "cannot rename unresolved composite type reference";
            return plan;
        }

        targetKind      = TargetKind::Type;
        targetPath      = definitionPathIt->second;
        targetTypeKey   = reference.typeKey;
        oldName         = shortNameFromTypeKey(reference.typeKey);
        targetLine      = reference.line;
        targetCharacter = reference.character;
        targetLength    = reference.length;
        break;
    }

    if (targetKind == TargetKind::None)
    {
        for (const CachedDefinition::FieldSymbol& field : source.fieldSymbols)
        {
            if (field.line == line && containsCharacter(field.character, field.length, character))
            {
                targetKind      = TargetKind::Field;
                oldName         = field.name;
                targetLine      = field.line;
                targetCharacter = field.character;
                targetLength    = field.length;
                break;
            }
        }
    }
    if (targetKind == TargetKind::None)
    {
        for (const CachedDefinition::ConstantSymbol& constant : source.constantSymbols)
        {
            if (constant.line == line && containsCharacter(constant.character, constant.length, character))
            {
                targetKind      = TargetKind::Constant;
                oldName         = constant.name;
                targetLine      = constant.line;
                targetCharacter = constant.character;
                targetLength    = constant.length;
                break;
            }
        }
    }
    if (targetKind == TargetKind::None)
    {
        const std::uint32_t typeLine      = source.ast.location.line == 0 ? 0U : source.ast.location.line - 1U;
        const std::uint32_t typeCharacter = source.ast.location.column == 0 ? 0U : source.ast.location.column - 1U;
        const auto          typeLength    = static_cast<std::uint32_t>(source.info.shortName.size());
        if (typeLine == line && containsCharacter(typeCharacter, typeLength, character))
        {
            targetKind      = TargetKind::Type;
            targetPath      = sourcePath;
            targetTypeKey   = makeTypeKey(source.info.fullName, source.info.majorVersion, source.info.minorVersion);
            oldName         = source.info.shortName;
            targetLine      = typeLine;
            targetCharacter = typeCharacter;
            targetLength    = typeLength;
        }
    }

    if (targetKind == TargetKind::None)
    {
        plan.errorMessage = "symbol at cursor is not renameable";
        return plan;
    }

    if (newName == oldName)
    {
        plan.ok = true;
        return plan;
    }

    if (targetKind == TargetKind::Type)
    {
        const CachedDefinition& target          = cachedDefinitionsByPath_.at(targetPath);
        const std::string       oldFullName     = target.info.fullName;
        const std::string       namespacePrefix = containerFromQualifiedName(oldFullName);
        const std::string       newFullName     = namespacePrefix.empty() ? newName : (namespacePrefix + "." + newName);
        const std::string       oldKey =
            makeTypeKey(target.info.fullName, target.info.majorVersion, target.info.minorVersion);
        const std::string newKey = makeTypeKey(newFullName, target.info.majorVersion, target.info.minorVersion);

        if (const auto existing = typeKeyToPath_.find(newKey);
            existing != typeKeyToPath_.end() && existing->second != targetPath)
        {
            plan.conflicts.push_back("target type already exists: " + newKey);
        }

        const std::string suffix           = versionSuffix(target.info.majorVersion, target.info.minorVersion);
        const std::string oldShortSpelling = target.info.shortName + suffix;
        const std::string oldFullSpelling  = target.info.fullName + suffix;
        const std::string newShortSpelling = newName + suffix;
        const std::string newFullSpelling  = newFullName + suffix;

        for (const auto& [_, cached] : cachedDefinitionsByPath_)
        {
            for (const CachedDefinition::TypeReference& reference : cached.typeReferences)
            {
                if (reference.typeKey != oldKey)
                {
                    continue;
                }
                std::string replacement = newFullSpelling;
                if (reference.display.starts_with(oldShortSpelling))
                {
                    replacement = newShortSpelling;
                }
                else if (reference.display.starts_with(oldFullSpelling))
                {
                    replacement = newFullSpelling;
                }

                plan.edit.textEdits.push_back(TextEditData{
                    cached.sourceUri,
                    reference.line,
                    reference.character,
                    reference.length,
                    replacement,
                });
            }
        }

        if (includeFileRename)
        {
            const std::filesystem::path oldFile(target.info.filePath);
            std::string                 newFileName;
            if (target.info.fixedPortId.has_value())
            {
                newFileName.append(std::to_string(*target.info.fixedPortId));
                newFileName.push_back('.');
            }
            newFileName.append(newName);
            newFileName.push_back('.');
            newFileName.append(std::to_string(target.info.majorVersion));
            newFileName.push_back('.');
            newFileName.append(std::to_string(target.info.minorVersion));
            newFileName.append(".dsdl");

            const std::string newPath = normalizePath((oldFile.parent_path() / newFileName).string());
            const std::string oldPath = normalizePath(oldFile.string());
            if (!newPath.empty() && newPath != oldPath)
            {
                std::error_code ec;
                if (std::filesystem::exists(newPath, ec))
                {
                    plan.conflicts.push_back("target declaration file already exists: " + newPath);
                }
                else
                {
                    plan.edit.fileRename = FileRenameData{
                        normalizedPathToFileUri(oldPath),
                        normalizedPathToFileUri(newPath),
                    };
                }
            }
        }
    }
    else
    {
        const auto hasConflict = [&source, &oldName, &newName, targetKind]() {
            if (targetKind == TargetKind::Field)
            {
                for (const auto& field : source.fieldSymbols)
                {
                    if (field.name == newName && field.name != oldName)
                    {
                        return true;
                    }
                }
            }
            if (targetKind == TargetKind::Constant)
            {
                for (const auto& constant : source.constantSymbols)
                {
                    if (constant.name == newName && constant.name != oldName)
                    {
                        return true;
                    }
                }
            }
            return false;
        }();
        if (hasConflict)
        {
            plan.conflicts.push_back("symbol already exists in this definition: " + newName);
        }

        plan.edit.textEdits.push_back(TextEditData{
            source.sourceUri,
            targetLine,
            targetCharacter,
            targetLength,
            newName,
        });
    }

    if (!plan.conflicts.empty())
    {
        plan.errorMessage = "rename plan has conflicts";
        return plan;
    }

    std::ranges::sort(plan.edit.textEdits,

                      [](const TextEditData& lhs, const TextEditData& rhs) {
                          return std::tie(lhs.uri, lhs.line, lhs.character, lhs.length, lhs.newText) <
                                 std::tie(rhs.uri, rhs.line, rhs.character, rhs.length, rhs.newText);
                      });
    plan.edit.textEdits.erase(std::ranges::unique(plan.edit.textEdits,

                                                  [](const TextEditData& lhs, const TextEditData& rhs) {
                                                      return lhs.uri == rhs.uri && lhs.line == rhs.line &&
                                                             lhs.character == rhs.character &&
                                                             lhs.length == rhs.length && lhs.newText == rhs.newText;
                                                  })
                                  .begin(),
                              plan.edit.textEdits.end());

    plan.ok = true;
    return plan;
}

std::vector<CodeActionData> AnalysisPipeline::codeActions(const std::string&              uri,
                                                          const std::uint32_t             startLine,
                                                          const std::uint32_t             startCharacter,
                                                          const std::uint32_t             endLine,
                                                          const std::uint32_t             endCharacter,
                                                          const std::vector<std::string>& diagnosticMessages) const
{
    std::vector<CodeActionData> actions;
    const std::string           path     = uriToNormalizedPath(uri);
    const auto                  cachedIt = cachedDefinitionsByPath_.find(path);
    if (cachedIt == cachedDefinitionsByPath_.end())
    {
        return actions;
    }
    const CachedDefinition& cached = cachedIt->second;

    const auto diagnosticFilterMatches = [&diagnosticMessages](const std::string& message) {
        if (diagnosticMessages.empty())
        {
            return true;
        }
        return std::ranges::find(diagnosticMessages, message) != diagnosticMessages.end();
    };
    const auto inRange = [startLine, startCharacter, endLine, endCharacter](const Diagnostic& diagnostic) {
        const std::uint32_t line      = diagnostic.location.line == 0 ? 0U : diagnostic.location.line - 1U;
        const std::uint32_t character = diagnostic.location.column == 0 ? 0U : diagnostic.location.column - 1U;
        if (line < startLine || line > endLine)
        {
            return false;
        }
        if (line == startLine && character < startCharacter)
        {
            return false;
        }
        if (line == endLine && character > endCharacter)
        {
            return false;
        }
        return true;
    };
    const auto lintInRange = [startLine, startCharacter, endLine, endCharacter](const LintFinding& finding) {
        const std::uint32_t line      = finding.location.line == 0 ? 0U : finding.location.line - 1U;
        const std::uint32_t character = finding.location.column == 0 ? 0U : finding.location.column - 1U;
        if (line < startLine || line > endLine)
        {
            return false;
        }
        if (line == startLine && character < startCharacter)
        {
            return false;
        }
        if (line == endLine && character > endCharacter)
        {
            return false;
        }
        return true;
    };

    struct ExtentDirectiveSite final
    {
        bool          responseSection{false};
        std::uint32_t line{0};
        std::uint32_t character{0};
        std::uint32_t length{1};
    };

    std::vector<ExtentDirectiveSite> extentSites;
    extentSites.reserve(2);
    bool inResponseSection = false;
    for (const StatementAST& statement : cached.ast.statements)
    {
        if (std::holds_alternative<ServiceResponseMarkerAST>(statement))
        {
            inResponseSection = true;
            continue;
        }
        const auto* directive = std::get_if<DirectiveAST>(&statement);
        if (!directive || directive->kind != DirectiveKind::Extent || !directive->expression)
        {
            continue;
        }
        const SourceLocation expressionLocation = directive->expression->location;
        extentSites.push_back(ExtentDirectiveSite{
            inResponseSection,
            zeroBasedLine(expressionLocation),
            zeroBasedColumn(expressionLocation),
            expressionSpanLengthFromSource(cached.sourceText, expressionLocation, directive->expression->str().size()),
        });
    }

    const auto                  extentInfoIt = latestExtentInfoByPath_.find(path);
    const DefinitionExtentInfo* definitionExtentInfo =
        extentInfoIt == latestExtentInfoByPath_.end() ? nullptr : &extentInfoIt->second;
    std::unordered_set<std::string> emittedExtentFixKeys;

    const auto diagnosticsIt = latestDiagnosticsByUri_.find(uri);
    if (diagnosticsIt != latestDiagnosticsByUri_.end())
    {
        for (const Diagnostic& diagnostic : diagnosticsIt->second)
        {
            if (!inRange(diagnostic) || !diagnosticFilterMatches(diagnostic.message))
            {
                continue;
            }

            if (diagnostic.message == "extent must be a multiple of 8 bits" ||
                diagnostic.message == "extent smaller than maximal serialized length" ||
                diagnostic.message == "@extent must define non-negative extent bits")
            {
                const std::uint32_t line      = diagnostic.location.line == 0 ? 0U : diagnostic.location.line - 1U;
                const std::uint32_t character = diagnostic.location.column == 0 ? 0U : diagnostic.location.column - 1U;

                const ExtentDirectiveSite* matchedSite = nullptr;
                for (const ExtentDirectiveSite& site : extentSites)
                {
                    if (site.line == line && containsCharacter(site.character, site.length, character))
                    {
                        matchedSite = &site;
                        break;
                    }
                }
                if (!matchedSite)
                {
                    continue;
                }

                std::optional<std::int64_t> suggestedExtent = diagnostic.suggestedInteger;
                if (!suggestedExtent.has_value() && definitionExtentInfo)
                {
                    const SectionExtentInfo* sectionExtent = &definitionExtentInfo->request;
                    if (matchedSite->responseSection)
                    {
                        if (!definitionExtentInfo->response.has_value())
                        {
                            continue;
                        }
                        sectionExtent = &*definitionExtentInfo->response;
                    }
                    suggestedExtent = std::max(sectionExtent->requiredBits,
                                               sectionExtent->declaredBits.value_or(sectionExtent->requiredBits));
                    suggestedExtent = std::max<std::int64_t>(0, *suggestedExtent);
                    suggestedExtent = roundUpMultiple(*suggestedExtent, 8);
                }
                if (!suggestedExtent.has_value())
                {
                    continue;
                }

                const std::string replacement = std::to_string(*suggestedExtent);
                const std::string key         = std::to_string(static_cast<int>(matchedSite->responseSection)) + ":" +
                                                std::to_string(matchedSite->line) + ":" +
                                                std::to_string(matchedSite->character) + ":" + replacement;
                if (!emittedExtentFixKeys.insert(key).second)
                {
                    continue;
                }

                actions.push_back(CodeActionData{
                    "Set extent to " + replacement + " bits",
                    "quickfix",
                    true,
                    diagnostic.message,
                    WorkspaceEditData{
                        std::vector<TextEditData>{TextEditData{
                            cached.sourceUri,
                            matchedSite->line,
                            matchedSite->character,
                            matchedSite->length,
                            replacement,
                        }},
                        std::nullopt,
                    },
                    true,
                });
                continue;
            }

            if (diagnostic.message.starts_with("unresolved composite type: "))
            {
                const std::uint32_t line      = diagnostic.location.line == 0 ? 0U : diagnostic.location.line - 1U;
                const std::uint32_t character = diagnostic.location.column == 0 ? 0U : diagnostic.location.column - 1U;

                const CachedDefinition::TypeReference* matchedRef = nullptr;
                for (const CachedDefinition::TypeReference& reference : cached.typeReferences)
                {
                    if (reference.line == line && containsCharacter(reference.character, reference.length, character))
                    {
                        matchedRef = &reference;
                        break;
                    }
                }
                if (!matchedRef)
                {
                    continue;
                }

                std::vector<std::string> candidateKeys;
                candidateKeys.reserve(typeKeyToPath_.size());
                for (const auto& [typeKey, _] : typeKeyToPath_)
                {
                    candidateKeys.push_back(typeKey);
                }
                std::ranges::sort(candidateKeys);

                const std::string unresolvedShort = shortNameFromTypeKey(diagnostic.message.substr(27));
                std::string       chosen;
                for (const std::string& candidate : candidateKeys)
                {
                    if (shortNameFromTypeKey(candidate) == unresolvedShort)
                    {
                        chosen = candidate;
                        break;
                    }
                }
                if (chosen.empty() && !candidateKeys.empty())
                {
                    chosen = candidateKeys.front();
                }
                if (chosen.empty())
                {
                    continue;
                }

                actions.push_back(CodeActionData{
                    "Replace with " + chosen,
                    "quickfix",
                    true,
                    diagnostic.message,
                    WorkspaceEditData{
                        std::vector<TextEditData>{TextEditData{
                            cached.sourceUri,
                            matchedRef->line,
                            matchedRef->character,
                            matchedRef->length,
                            chosen,
                        }},
                        std::nullopt,
                    },
                    true,
                });
                continue;
            }

            if (diagnostic.message == "service types are not valid field types")
            {
                const std::uint32_t line = diagnostic.location.line == 0 ? 0U : diagnostic.location.line - 1U;
                actions.push_back(CodeActionData{
                    "Insert TODO above invalid service field",
                    "quickfix",
                    false,
                    diagnostic.message,
                    WorkspaceEditData{
                        std::vector<TextEditData>{TextEditData{
                            cached.sourceUri,
                            line,
                            0,
                            0,
                            "# TODO: service types are not valid field types; choose a message type\n",
                        }},
                        std::nullopt,
                    },
                    true,
                });
                continue;
            }

            if (diagnostic.message.starts_with("namespace root does not exist: ") ||
                diagnostic.message.starts_with("invalid root namespace directory name: "))
            {
                actions.push_back(CodeActionData{
                    "Review workspace roots/lookupDirs configuration",
                    "quickfix",
                    false,
                    diagnostic.message,
                    WorkspaceEditData{},
                    false,
                });
            }
        }
    }

    const auto lintFindingsIt = latestLintFindingsByUri_.find(uri);
    if (lintFindingsIt != latestLintFindingsByUri_.end())
    {
        for (const LintFinding& finding : lintFindingsIt->second)
        {
            const std::string diagnosticText = "[lint:" + finding.ruleId + "] " + finding.message;
            if (!lintInRange(finding) ||
                !(diagnosticFilterMatches(diagnosticText) || diagnosticFilterMatches(finding.message)))
            {
                continue;
            }
            if (!finding.hasFix || finding.fixes.empty())
            {
                continue;
            }

            WorkspaceEditData edit;
            for (const LintFixEdit& fix : finding.fixes)
            {
                edit.textEdits.push_back(TextEditData{
                    finding.uri,
                    fix.line,
                    fix.character,
                    fix.length,
                    fix.newText,
                });
            }

            actions.push_back(CodeActionData{
                "Lint: " + finding.ruleId,
                "quickfix",
                finding.preferredFix,
                diagnosticText,
                std::move(edit),
                true,
            });
        }
    }

    std::vector<TextEditData> normalizeEdits;
    for (const CachedDefinition::TypeReference& reference : cached.typeReferences)
    {
        if (reference.display != reference.typeKey)
        {
            normalizeEdits.push_back(TextEditData{
                cached.sourceUri,
                reference.line,
                reference.character,
                reference.length,
                reference.typeKey,
            });
        }
    }
    if (!normalizeEdits.empty())
    {
        actions.push_back(CodeActionData{
            "Normalize versioned type references",
            "refactor.rewrite",
            false,
            {},
            WorkspaceEditData{
                std::move(normalizeEdits),
                std::nullopt,
            },
            true,
        });
    }

    bool hasFieldInSelection = false;
    for (const CachedDefinition::FieldSymbol& field : cached.fieldSymbols)
    {
        if (field.line < startLine || field.line > endLine)
        {
            continue;
        }
        if (field.line == startLine && field.character + field.length < startCharacter)
        {
            continue;
        }
        if (field.line == endLine && field.character > endCharacter)
        {
            continue;
        }
        hasFieldInSelection = true;
        break;
    }
    if (hasFieldInSelection)
    {
        actions.push_back(CodeActionData{
            "Extract selected field(s) into new type (preview)",
            "refactor.extract",
            false,
            {},
            WorkspaceEditData{},
            false,
        });
    }

    return actions;
}

}  // namespace llvmdsdl::lsp
