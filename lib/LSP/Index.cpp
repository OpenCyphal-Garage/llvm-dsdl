//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shard persistence, workspace-symbol querying, and background
/// index maintenance for the DSDL language server.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/Index.h"

#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace llvmdsdl::lsp
{
namespace
{

struct ParseShardResult final
{
    std::optional<IndexFileShard> shard;
    std::string                   error;
};

std::string normalizePath(const std::string& pathText)
{
    if (pathText.empty())
    {
        return {};
    }

    std::error_code       ec;
    std::filesystem::path absolutePath = std::filesystem::absolute(std::filesystem::path(pathText), ec);
    if (ec)
    {
        absolutePath = std::filesystem::path(pathText);
    }

    std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(absolutePath, ec);
    if (ec)
    {
        canonicalPath = absolutePath.lexically_normal();
    }
    return canonicalPath.string();
}

std::string toLower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool containsInsensitive(const std::string& haystackLower, const std::string& needleLower)
{
    return needleLower.empty() || haystackLower.contains(needleLower);
}

std::string encodeShardKey(const std::string& normalizedPath)
{
    static constexpr char Hex[] = "0123456789abcdef";

    std::string encoded;
    encoded.reserve((normalizedPath.size() * 3U) + 1U);
    for (const unsigned char c : normalizedPath)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.')
        {
            encoded.push_back(static_cast<char>(c));
            continue;
        }
        encoded.push_back('_');
        encoded.push_back(Hex[(c >> 4U) & 0xFU]);
        encoded.push_back(Hex[c & 0xFU]);
    }

    if (encoded.empty())
    {
        return "empty";
    }
    return encoded;
}

std::filesystem::path shardPathFor(const std::string& cacheDirectory, const std::string& normalizedPath)
{
    return std::filesystem::path(cacheDirectory) / (encodeShardKey(normalizedPath) + ".index.json");
}

llvm::json::Object locationToJson(const IndexLocation& location)
{
    return llvm::json::Object{{"uri", location.uri},
                              {"line", static_cast<std::int64_t>(location.line)},
                              {"character", static_cast<std::int64_t>(location.character)},
                              {"length", static_cast<std::int64_t>(location.length)}};
}

llvm::json::Object symbolToJson(const IndexSymbolRecord& symbol)
{
    return llvm::json::Object{{"usr", symbol.usr},
                              {"name", symbol.name},
                              {"qualified_name", symbol.qualifiedName},
                              {"container_name", symbol.containerName},
                              {"detail", symbol.detail},
                              {"kind", symbol.kind},
                              {"file_path", symbol.filePath},
                              {"location", locationToJson(symbol.location)}};
}

llvm::json::Object referenceToJson(const IndexReferenceRecord& reference)
{
    return llvm::json::Object{{"target_usr", reference.targetUsr},
                              {"file_path", reference.filePath},
                              {"is_declaration", reference.isDeclaration},
                              {"location", locationToJson(reference.location)}};
}

std::optional<std::string> getRequiredString(const llvm::json::Object& object, llvm::StringRef key, std::string& error)
{
    const auto value = object.getString(key);
    if (!value.has_value())
    {
        error = "missing required string field: " + key.str();
        return std::nullopt;
    }
    return value->str();
}

std::optional<std::uint32_t> getRequiredUnsigned32(const llvm::json::Object& object,
                                                   llvm::StringRef           key,
                                                   std::string&              error)
{
    const auto value = object.getInteger(key);
    if (!value.has_value() || *value < 0)
    {
        error = "missing required unsigned integer field: " + key.str();
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*value);
}

std::optional<std::uint64_t> getRequiredUnsigned64(const llvm::json::Object& object,
                                                   llvm::StringRef           key,
                                                   std::string&              error)
{
    const auto value = object.getInteger(key);
    if (!value.has_value() || *value < 0)
    {
        error = "missing required unsigned integer field: " + key.str();
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(*value);
}

std::optional<std::size_t> getRequiredSize(const llvm::json::Object& object, llvm::StringRef key, std::string& error)
{
    // Encoded as a decimal string: a full 64-bit std::size_t hash does not fit
    // JSON's signed integer domain, so an integer encoding would round-trip a
    // high-bit-set value back as a negative and be rejected.
    const auto value = object.getString(key);
    if (!value.has_value())
    {
        error = "missing required size field: " + key.str();
        return std::nullopt;
    }
    std::size_t parsed = 0;
    if (value->getAsInteger(10, parsed))
    {
        error = "invalid size field: " + key.str();
        return std::nullopt;
    }
    return parsed;
}

std::optional<IndexLocation> parseLocation(const llvm::json::Object& object, std::string& error)
{
    IndexLocation location;

    const auto uri = getRequiredString(object, "uri", error);
    if (!uri.has_value())
    {
        return std::nullopt;
    }
    location.uri = *uri;

    const auto line = getRequiredUnsigned32(object, "line", error);
    if (!line.has_value())
    {
        return std::nullopt;
    }
    location.line = *line;

    const auto character = getRequiredUnsigned32(object, "character", error);
    if (!character.has_value())
    {
        return std::nullopt;
    }
    location.character = *character;

    const auto length = getRequiredUnsigned32(object, "length", error);
    if (!length.has_value())
    {
        return std::nullopt;
    }
    location.length = *length;

    return location;
}

ParseShardResult parseShardFromFile(const std::filesystem::path& shardPath)
{
    std::ifstream in(shardPath, std::ios::binary);
    if (!in.good())
    {
        return ParseShardResult{std::nullopt, "failed to read shard file"};
    }

    std::string                       text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(text);
    if (!parsed)
    {
        return ParseShardResult{std::nullopt, "invalid JSON"};
    }

    const auto* root = parsed->getAsObject();
    if (!root)
    {
        return ParseShardResult{std::nullopt, "top-level shard payload must be an object"};
    }

    IndexFileShard shard;

    const auto schemaVersion = getRequiredUnsigned32(*root, "schema_version", text);
    if (!schemaVersion.has_value())
    {
        return ParseShardResult{std::nullopt, text};
    }
    shard.metadata.schemaVersion = *schemaVersion;
    if (shard.metadata.schemaVersion != LspIndexSchemaVersion)
    {
        return ParseShardResult{std::nullopt,
                                "unsupported schema version: " + std::to_string(shard.metadata.schemaVersion)};
    }

    const auto* metadataValue = root->getObject("metadata");
    if (!metadataValue)
    {
        return ParseShardResult{std::nullopt, "missing metadata object"};
    }

    const auto filePath = getRequiredString(*metadataValue, "file_path", text);
    if (!filePath.has_value())
    {
        return ParseShardResult{std::nullopt, text};
    }
    shard.metadata.filePath = *filePath;

    const auto sourceUri = getRequiredString(*metadataValue, "source_uri", text);
    if (!sourceUri.has_value())
    {
        return ParseShardResult{std::nullopt, text};
    }
    shard.metadata.sourceUri = *sourceUri;

    const auto textHash = getRequiredSize(*metadataValue, "text_hash", text);
    if (!textHash.has_value())
    {
        return ParseShardResult{std::nullopt, text};
    }
    shard.metadata.textHash = *textHash;

    const auto snapshotVersion = getRequiredUnsigned64(*metadataValue, "snapshot_version", text);
    if (!snapshotVersion.has_value())
    {
        return ParseShardResult{std::nullopt, text};
    }
    shard.metadata.snapshotVersion = *snapshotVersion;

    const auto* symbolsValue = root->getArray("symbols");
    if (!symbolsValue)
    {
        return ParseShardResult{std::nullopt, "missing symbols array"};
    }
    shard.symbols.reserve(symbolsValue->size());
    for (const llvm::json::Value& symbolValue : *symbolsValue)
    {
        const auto* symbolObject = symbolValue.getAsObject();
        if (!symbolObject)
        {
            return ParseShardResult{std::nullopt, "symbol row must be an object"};
        }

        IndexSymbolRecord symbol;

        const auto usr = getRequiredString(*symbolObject, "usr", text);
        if (!usr.has_value())
        {
            return ParseShardResult{std::nullopt, text};
        }
        symbol.usr = *usr;

        const auto name = getRequiredString(*symbolObject, "name", text);
        if (!name.has_value())
        {
            return ParseShardResult{std::nullopt, text};
        }
        symbol.name = *name;

        const auto qualifiedName = getRequiredString(*symbolObject, "qualified_name", text);
        if (!qualifiedName.has_value())
        {
            return ParseShardResult{std::nullopt, text};
        }
        symbol.qualifiedName = *qualifiedName;

        const auto containerName = getRequiredString(*symbolObject, "container_name", text);
        if (!containerName.has_value())
        {
            return ParseShardResult{std::nullopt, text};
        }
        symbol.containerName = *containerName;

        const auto detail = getRequiredString(*symbolObject, "detail", text);
        if (!detail.has_value())
        {
            return ParseShardResult{std::nullopt, text};
        }
        symbol.detail = *detail;

        const auto kind = symbolObject->getInteger("kind");
        if (!kind.has_value())
        {
            return ParseShardResult{std::nullopt, "missing symbol kind"};
        }
        symbol.kind = *kind;

        const auto sourcePath = getRequiredString(*symbolObject, "file_path", text);
        if (!sourcePath.has_value())
        {
            return ParseShardResult{std::nullopt, text};
        }
        symbol.filePath = *sourcePath;

        const auto* locationValue = symbolObject->getObject("location");
        if (!locationValue)
        {
            return ParseShardResult{std::nullopt, "missing symbol location"};
        }

        const auto location = parseLocation(*locationValue, text);
        if (!location.has_value())
        {
            return ParseShardResult{std::nullopt, text};
        }
        symbol.location = *location;

        shard.symbols.push_back(std::move(symbol));
    }

    const auto* referencesValue = root->getArray("references");
    if (!referencesValue)
    {
        return ParseShardResult{std::nullopt, "missing references array"};
    }
    shard.references.reserve(referencesValue->size());
    for (const llvm::json::Value& referenceValue : *referencesValue)
    {
        const auto* referenceObject = referenceValue.getAsObject();
        if (!referenceObject)
        {
            return ParseShardResult{std::nullopt, "reference row must be an object"};
        }

        IndexReferenceRecord reference;

        const auto targetUsr = getRequiredString(*referenceObject, "target_usr", text);
        if (!targetUsr.has_value())
        {
            return ParseShardResult{std::nullopt, text};
        }
        reference.targetUsr = *targetUsr;

        const auto sourcePath = getRequiredString(*referenceObject, "file_path", text);
        if (!sourcePath.has_value())
        {
            return ParseShardResult{std::nullopt, text};
        }
        reference.filePath = *sourcePath;

        const auto declaration = referenceObject->getBoolean("is_declaration");
        if (!declaration.has_value())
        {
            return ParseShardResult{std::nullopt, "missing is_declaration flag"};
        }
        reference.isDeclaration = *declaration;

        const auto* locationValue = referenceObject->getObject("location");
        if (!locationValue)
        {
            return ParseShardResult{std::nullopt, "missing reference location"};
        }

        const auto location = parseLocation(*locationValue, text);
        if (!location.has_value())
        {
            return ParseShardResult{std::nullopt, text};
        }
        reference.location = *location;

        shard.references.push_back(std::move(reference));
    }

    return ParseShardResult{std::move(shard), {}};
}

}  // namespace

IndexStorage::IndexStorage(const std::string& cacheDirectory)
    : cacheDirectory_(normalizePath(cacheDirectory))
{
}

bool IndexStorage::writeShard(const IndexFileShard& shard, std::string* errorMessage) const
{
    if (cacheDirectory_.empty())
    {
        if (errorMessage)
        {
            *errorMessage = "cache directory is not configured";
        }
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(cacheDirectory_, ec);
    if (ec)
    {
        if (errorMessage)
        {
            *errorMessage = "failed to create cache directory: " + ec.message();
        }
        return false;
    }

    llvm::json::Object metadata;
    metadata["file_path"]        = shard.metadata.filePath;
    metadata["source_uri"]       = shard.metadata.sourceUri;
    metadata["text_hash"]        = std::to_string(shard.metadata.textHash);
    metadata["snapshot_version"] = static_cast<std::int64_t>(shard.metadata.snapshotVersion);

    std::vector<IndexSymbolRecord> sortedSymbols = shard.symbols;
    std::ranges::sort(sortedSymbols,

                      [](const IndexSymbolRecord& lhs, const IndexSymbolRecord& rhs) {
                          return std::tie(lhs.usr,
                                          lhs.filePath,
                                          lhs.location.line,
                                          lhs.location.character,
                                          lhs.name,
                                          lhs.qualifiedName,
                                          lhs.kind,
                                          lhs.detail) < std::tie(rhs.usr,
                                                                 rhs.filePath,
                                                                 rhs.location.line,
                                                                 rhs.location.character,
                                                                 rhs.name,
                                                                 rhs.qualifiedName,
                                                                 rhs.kind,
                                                                 rhs.detail);
                      });

    llvm::json::Array symbols;
    for (const IndexSymbolRecord& symbol : sortedSymbols)
    {
        symbols.push_back(symbolToJson(symbol));
    }

    std::vector<IndexReferenceRecord> sortedReferences = shard.references;
    std::ranges::sort(sortedReferences,

                      [](const IndexReferenceRecord& lhs, const IndexReferenceRecord& rhs) {
                          return std::tie(lhs.targetUsr,
                                          lhs.filePath,
                                          lhs.location.line,
                                          lhs.location.character,
                                          lhs.location.length,
                                          lhs.isDeclaration) < std::tie(rhs.targetUsr,
                                                                        rhs.filePath,
                                                                        rhs.location.line,
                                                                        rhs.location.character,
                                                                        rhs.location.length,
                                                                        rhs.isDeclaration);
                      });

    llvm::json::Array references;
    for (const IndexReferenceRecord& reference : sortedReferences)
    {
        references.push_back(referenceToJson(reference));
    }

    llvm::json::Object root;
    root["schema_version"] = static_cast<std::int64_t>(LspIndexSchemaVersion);
    root["metadata"]       = std::move(metadata);
    root["symbols"]        = std::move(symbols);
    root["references"]     = std::move(references);

    std::string              rendered;
    llvm::raw_string_ostream stream(rendered);
    stream << llvm::formatv("{0:2}", llvm::json::Value(std::move(root)));
    stream << '\n';
    stream.flush();

    const std::filesystem::path targetPath = shardPathFor(cacheDirectory_, shard.metadata.filePath);
    const std::filesystem::path tempPath   = targetPath.string() + ".tmp";

    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out.good())
        {
            if (errorMessage)
            {
                *errorMessage = "failed to open temporary shard file for write";
            }
            return false;
        }
        out.write(rendered.data(), static_cast<std::streamsize>(rendered.size()));
        out.flush();
        if (!out.good())
        {
            if (errorMessage)
            {
                *errorMessage = "failed to write temporary shard file";
            }
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

    std::filesystem::rename(tempPath, targetPath, ec);
    if (ec)
    {
        std::filesystem::remove(targetPath, ec);
        ec.clear();
        std::filesystem::rename(tempPath, targetPath, ec);
        if (ec)
        {
            if (errorMessage)
            {
                *errorMessage = "failed to finalize shard write: " + ec.message();
            }
            std::filesystem::remove(tempPath, ec);
            return false;
        }
    }

    return true;
}

std::optional<IndexFileShard> IndexStorage::readShardForPath(const std::string& normalizedPath,
                                                             std::string*       errorMessage) const
{
    if (cacheDirectory_.empty())
    {
        if (errorMessage)
        {
            *errorMessage = "cache directory is not configured";
        }
        return std::nullopt;
    }

    const std::filesystem::path path = shardPathFor(cacheDirectory_, normalizedPath);
    std::error_code             ec;
    if (!std::filesystem::exists(path, ec))
    {
        return std::nullopt;
    }

    ParseShardResult const parsed = parseShardFromFile(path);
    if (!parsed.shard.has_value())
    {
        if (errorMessage)
        {
            *errorMessage = parsed.error;
        }
        return std::nullopt;
    }

    return parsed.shard;
}

std::vector<IndexFileShard> IndexStorage::loadAllShards(std::vector<std::string>* invalidShardPaths) const
{
    std::vector<IndexFileShard> shards;
    if (cacheDirectory_.empty())
    {
        return shards;
    }

    std::error_code ec;
    if (!std::filesystem::exists(cacheDirectory_, ec))
    {
        return shards;
    }

    std::vector<std::filesystem::path> shardPaths;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(cacheDirectory_, ec))
    {
        if (ec)
        {
            break;
        }
        if (!entry.is_regular_file())
        {
            continue;
        }
        const std::filesystem::path& path = entry.path();
        if (path.extension() != ".json")
        {
            continue;
        }
        if (!path.filename().string().contains(".index.json"))
        {
            continue;
        }
        shardPaths.push_back(path);
    }

    std::ranges::sort(shardPaths);

    for (const std::filesystem::path& path : shardPaths)
    {
        ParseShardResult parsed = parseShardFromFile(path);
        if (!parsed.shard.has_value())
        {
            if (invalidShardPaths)
            {
                invalidShardPaths->push_back(path.string());
            }
            continue;
        }
        shards.push_back(std::move(*parsed.shard));
    }

    std::ranges::sort(shards, [](const IndexFileShard& lhs, const IndexFileShard& rhs) {
        return lhs.metadata.filePath < rhs.metadata.filePath;
    });
    return shards;
}

bool IndexStorage::removeShardForPath(const std::string& normalizedPath) const
{
    if (cacheDirectory_.empty())
    {
        return false;
    }

    std::error_code ec;
    return std::filesystem::remove(shardPathFor(cacheDirectory_, normalizedPath), ec);
}

IndexRepairReport IndexStorage::verifyAndRepair(const bool removeInvalidShards) const
{
    IndexRepairReport report;
    if (cacheDirectory_.empty())
    {
        return report;
    }

    std::error_code ec;
    if (!std::filesystem::exists(cacheDirectory_, ec))
    {
        return report;
    }

    std::vector<std::filesystem::path> shardPaths;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(cacheDirectory_, ec))
    {
        if (ec)
        {
            break;
        }
        if (!entry.is_regular_file())
        {
            continue;
        }
        const std::filesystem::path& path = entry.path();
        if (!path.filename().string().contains(".index.json"))
        {
            continue;
        }
        shardPaths.push_back(path);
    }
    std::ranges::sort(shardPaths);

    std::unordered_set<std::string> seenLogicalPaths;
    for (const std::filesystem::path& path : shardPaths)
    {
        ++report.inspectedFiles;
        ParseShardResult parsed = parseShardFromFile(path);
        if (!parsed.shard.has_value())
        {
            ++report.invalidFiles;
            if (removeInvalidShards)
            {
                std::filesystem::remove(path, ec);
                if (!ec)
                {
                    ++report.removedFiles;
                    report.removedShardPaths.push_back(path.string());
                }
                ec.clear();
            }
            continue;
        }

        const std::string normalizedLogicalPath = normalizePath(parsed.shard->metadata.filePath);
        if (!seenLogicalPaths.insert(normalizedLogicalPath).second)
        {
            ++report.duplicateFiles;
            if (removeInvalidShards)
            {
                std::filesystem::remove(path, ec);
                if (!ec)
                {
                    ++report.removedFiles;
                    report.removedShardPaths.push_back(path.string());
                }
                ec.clear();
            }
            continue;
        }
    }

    return report;
}

void WorkspaceIndex::replaceAll(std::vector<IndexFileShard> shards)
{
    std::ranges::sort(shards, [](const IndexFileShard& lhs, const IndexFileShard& rhs) {
        return lhs.metadata.filePath < rhs.metadata.filePath;
    });

    shards_ = std::move(shards);
    shardsByPath_.clear();
    for (std::size_t i = 0; i < shards_.size(); ++i)
    {
        shardsByPath_.insert_or_assign(shards_[i].metadata.filePath, i);
    }

    rebuildFlattenedSymbols();
}

std::vector<WorkspaceSymbolResult> WorkspaceIndex::querySymbols(const std::string& query, const std::size_t limit) const
{
    std::vector<WorkspaceSymbolResult> out;
    if (flattenedSymbols_.empty() || limit == 0)
    {
        return out;
    }

    const std::string queryLower = toLower(query);
    out.reserve(std::min<std::size_t>(flattenedSymbols_.size(), limit));

    const auto rankSymbol = [&queryLower](const SearchableSymbol& symbol) {
        if (queryLower.empty())
        {
            return 1.0;
        }

        if (symbol.qualifiedLower == queryLower)
        {
            return 100.0;
        }
        if (symbol.nameLower == queryLower)
        {
            return 95.0;
        }

        if (symbol.qualifiedLower.starts_with(queryLower))
        {
            return 90.0;
        }
        if (symbol.nameLower.starts_with(queryLower))
        {
            return 85.0;
        }

        if (containsInsensitive(symbol.qualifiedLower, queryLower))
        {
            return 70.0;
        }
        if (containsInsensitive(symbol.nameLower, queryLower))
        {
            return 65.0;
        }
        if (containsInsensitive(symbol.containerLower, queryLower))
        {
            return 50.0;
        }
        if (containsInsensitive(symbol.detailLower, queryLower))
        {
            return 25.0;
        }

        return 0.0;
    };

    for (const SearchableSymbol& symbol : flattenedSymbols_)
    {
        const double score = rankSymbol(symbol);
        if (score <= 0.0)
        {
            continue;
        }

        out.push_back(WorkspaceSymbolResult{
            symbol.record.usr,
            symbol.record.name,
            symbol.record.qualifiedName,
            symbol.record.containerName,
            symbol.record.detail,
            symbol.record.kind,
            symbol.record.location.uri,
            symbol.record.location.line,
            symbol.record.location.character,
            symbol.record.location.length,
            score,
        });
    }

    std::ranges::sort(out, [](const WorkspaceSymbolResult& lhs, const WorkspaceSymbolResult& rhs) {
        if (lhs.score != rhs.score)
        {
            return lhs.score > rhs.score;
        }
        return std::tie(lhs.qualifiedName, lhs.uri, lhs.line, lhs.character, lhs.name) <
               std::tie(rhs.qualifiedName, rhs.uri, rhs.line, rhs.character, rhs.name);
    });

    if (out.size() > limit)
    {
        out.resize(limit);
    }
    return out;
}

std::vector<std::string> WorkspaceIndex::indexedPaths() const
{
    std::vector<std::string> out;
    out.reserve(shards_.size());
    for (const IndexFileShard& shard : shards_)
    {
        out.push_back(shard.metadata.filePath);
    }
    return out;
}

void WorkspaceIndex::rebuildFlattenedSymbols()
{
    flattenedSymbols_.clear();

    std::size_t symbolCount = 0;
    for (const IndexFileShard& shard : shards_)
    {
        symbolCount += shard.symbols.size();
    }
    flattenedSymbols_.reserve(symbolCount);

    for (const IndexFileShard& shard : shards_)
    {
        for (const IndexSymbolRecord& symbol : shard.symbols)
        {
            flattenedSymbols_.push_back(SearchableSymbol{
                symbol,
                toLower(symbol.name),
                toLower(symbol.qualifiedName),
                toLower(symbol.containerName),
                toLower(symbol.detail),
            });
        }
    }

    std::ranges::sort(flattenedSymbols_,

                      [](const SearchableSymbol& lhs, const SearchableSymbol& rhs) {
                          return std::tie(lhs.record.qualifiedName,
                                          lhs.record.location.uri,
                                          lhs.record.location.line,
                                          lhs.record.location.character,
                                          lhs.record.usr) < std::tie(rhs.record.qualifiedName,
                                                                     rhs.record.location.uri,
                                                                     rhs.record.location.line,
                                                                     rhs.record.location.character,
                                                                     rhs.record.usr);
                      });
}

class IndexManager::Impl final
{
public:
    explicit Impl(const std::string& cacheDirectory)
        : storage_(normalizePath(cacheDirectory))
    {
        warmStartLoad();
        worker_ = std::thread([this]() { workerLoop(); });
    }

    Impl(const Impl&)            = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&)                 = delete;
    Impl& operator=(Impl&&)      = delete;

    ~Impl()
    {
        shutdown();
    }

    void scheduleRebuild(const std::uint64_t snapshotVersion, std::vector<IndexFileShard> shards)
    {
        std::scoped_lock const lock(mutex_);

        if (pendingJob_.has_value())
        {
            pendingJob_->cancelFlag->store(true);
            ++stats_.cancelledJobs;
        }

        if (activeJobCancelFlag_)
        {
            activeJobCancelFlag_->store(true);
        }

        Job job;
        job.snapshotVersion = snapshotVersion;
        job.shards          = std::move(shards);
        job.cancelFlag      = std::make_shared<std::atomic_bool>(false);
        pendingJob_         = std::move(job);

        ++stats_.scheduledJobs;
        cv_.notify_all();
    }

    bool waitForSnapshot(const std::uint64_t snapshotVersion, const std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [this, snapshotVersion]() {
            return stopping_ || stats_.lastCompletedSnapshotVersion >= snapshotVersion;
        });
    }

    std::vector<WorkspaceSymbolResult> workspaceSymbols(const std::string& query, const std::size_t limit) const
    {
        std::scoped_lock const lock(mutex_);
        return index_.querySymbols(query, limit);
    }

    IndexRepairReport verifyAndRepair(const bool removeInvalidShards)
    {
        waitForIdle();

        std::scoped_lock const      lock(mutex_);
        IndexRepairReport           report = storage_.verifyAndRepair(removeInvalidShards);
        std::vector<std::string>    invalidShardPaths;
        std::vector<IndexFileShard> loadedShards = storage_.loadAllShards(&invalidShardPaths);
        index_.replaceAll(std::move(loadedShards));
        indexedPaths_.clear();
        for (const std::string& path : index_.indexedPaths())
        {
            indexedPaths_.insert(path);
        }
        stats_.indexedFileCount   = index_.fileCount();
        stats_.indexedSymbolCount = index_.symbolCount();
        return report;
    }

    IndexManagerStats stats() const
    {
        std::scoped_lock const lock(mutex_);
        return stats_;
    }

    const std::string& cacheDirectory() const
    {
        return storage_.cacheDirectory();
    }

    void shutdown()
    {
        {
            std::scoped_lock const lock(mutex_);
            if (stopping_)
            {
                return;
            }
            stopping_ = true;
            if (pendingJob_.has_value())
            {
                pendingJob_->cancelFlag->store(true);
            }
            if (activeJobCancelFlag_)
            {
                activeJobCancelFlag_->store(true);
            }
        }
        cv_.notify_all();

        if (worker_.joinable())
        {
            worker_.join();
        }
    }

private:
    struct Job final
    {
        std::uint64_t                     snapshotVersion{0};
        std::vector<IndexFileShard>       shards;
        std::shared_ptr<std::atomic_bool> cancelFlag;
    };

    void warmStartLoad()
    {
        std::vector<std::string>    invalidShardPaths;
        std::vector<IndexFileShard> loaded = storage_.loadAllShards(&invalidShardPaths);
        index_.replaceAll(std::move(loaded));
        indexedPaths_.clear();
        for (const std::string& path : index_.indexedPaths())
        {
            indexedPaths_.insert(path);
        }

        stats_.indexedFileCount   = index_.fileCount();
        stats_.indexedSymbolCount = index_.symbolCount();
        stats_.warmStartLoaded    = stats_.indexedFileCount > 0;
    }

    void waitForIdle()
    {
        std::unique_lock<std::mutex> lock(mutex_);

        if (pendingJob_.has_value())
        {
            pendingJob_->cancelFlag->store(true);
            ++stats_.cancelledJobs;
            pendingJob_.reset();
        }

        if (activeJobCancelFlag_)
        {
            activeJobCancelFlag_->store(true);
        }

        cv_.wait(lock, [this]() { return !activeJobCancelFlag_ && !pendingJob_.has_value(); });
    }

    void workerLoop()
    {
        while (true)
        {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() { return stopping_ || pendingJob_.has_value(); });
                if (stopping_)
                {
                    break;
                }

                job = std::move(*pendingJob_);
                pendingJob_.reset();
                activeJobCancelFlag_ = job.cancelFlag;
            }

            const bool completed = applyJob(job);

            {
                std::scoped_lock const lock(mutex_);
                if (!completed)
                {
                    ++stats_.cancelledJobs;
                }
                else
                {
                    ++stats_.completedJobs;
                    stats_.lastCompletedSnapshotVersion = job.snapshotVersion;
                    stats_.indexedFileCount             = index_.fileCount();
                    stats_.indexedSymbolCount           = index_.symbolCount();
                }
                activeJobCancelFlag_.reset();
            }
            cv_.notify_all();
        }
    }

    bool applyJob(const Job& job)
    {
        auto cancelled = [&job]() { return job.cancelFlag && job.cancelFlag->load(); };

        if (cancelled())
        {
            return false;
        }

        std::vector<IndexFileShard> sortedShards = job.shards;
        std::ranges::sort(sortedShards, [](const IndexFileShard& lhs, const IndexFileShard& rhs) {
            return lhs.metadata.filePath < rhs.metadata.filePath;
        });

        std::unordered_set<std::string> desiredPaths;
        desiredPaths.reserve(sortedShards.size());

        for (const IndexFileShard& shard : sortedShards)
        {
            if (cancelled())
            {
                return false;
            }

            desiredPaths.insert(shard.metadata.filePath);

            std::string errorMessage;
            const bool  wrote = storage_.writeShard(shard, &errorMessage);
            (void) wrote;
            (void) errorMessage;

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if (cancelled())
        {
            return false;
        }

        std::vector<std::string> stalePaths;
        {
            std::scoped_lock const lock(mutex_);
            for (const std::string& indexedPath : indexedPaths_)
            {
                if (!desiredPaths.contains(indexedPath))
                {
                    stalePaths.push_back(indexedPath);
                }
            }
        }

        for (const std::string& stalePath : stalePaths)
        {
            if (cancelled())
            {
                return false;
            }
            const bool removed = storage_.removeShardForPath(stalePath);
            (void) removed;
        }

        if (cancelled())
        {
            return false;
        }

        {
            std::scoped_lock const lock(mutex_);
            index_.replaceAll(std::move(sortedShards));
            indexedPaths_ = std::move(desiredPaths);
        }

        return true;
    }

    mutable std::mutex                mutex_;
    std::condition_variable           cv_;
    IndexStorage                      storage_;
    WorkspaceIndex                    index_;
    std::unordered_set<std::string>   indexedPaths_;
    std::optional<Job>                pendingJob_;
    std::shared_ptr<std::atomic_bool> activeJobCancelFlag_;
    IndexManagerStats                 stats_;
    bool                              stopping_{false};
    std::thread                       worker_;
};

IndexManager::IndexManager(const std::string& cacheDirectory)
    : impl_(std::make_unique<Impl>(cacheDirectory))
{
}

IndexManager::~IndexManager() = default;

const std::string& IndexManager::cacheDirectory() const
{
    return impl_->cacheDirectory();
}

void IndexManager::scheduleRebuild(const std::uint64_t snapshotVersion, std::vector<IndexFileShard> shards)
{
    impl_->scheduleRebuild(snapshotVersion, std::move(shards));
}

bool IndexManager::waitForSnapshot(const std::uint64_t snapshotVersion, const std::chrono::milliseconds timeout)
{
    return impl_->waitForSnapshot(snapshotVersion, timeout);
}

std::vector<WorkspaceSymbolResult> IndexManager::workspaceSymbols(const std::string& query,
                                                                  const std::size_t  limit) const
{
    return impl_->workspaceSymbols(query, limit);
}

IndexRepairReport IndexManager::verifyAndRepair(const bool removeInvalidShards)
{
    return impl_->verifyAndRepair(removeInvalidShards);
}

IndexManagerStats IndexManager::stats() const
{
    return impl_->stats();
}

void IndexManager::shutdown()
{
    impl_->shutdown();
}

}  // namespace llvmdsdl::lsp
