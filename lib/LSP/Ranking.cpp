//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements ranking heuristics and persistent adaptive signal storage.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/Ranking.h"

#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <llvm/Support/Error.h>
#include <llvm/Support/raw_ostream.h>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace llvmdsdl::lsp
{
namespace
{

constexpr std::uint32_t RankingSignalSchemaVersion = 1;

std::string toLower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

bool startsWith(const std::string& text, const std::string& prefix)
{
    return text.size() >= prefix.size() && text.starts_with(prefix);
}

bool contains(const std::string& text, const std::string& needle)
{
    return needle.empty() || text.contains(needle);
}

double subsequenceScore(const std::string& text, const std::string& pattern)
{
    if (pattern.empty())
    {
        return 0.0;
    }

    std::size_t matchCount = 0;
    std::size_t index      = 0;
    for (const char patternChar : pattern)
    {
        while (index < text.size() && text[index] != patternChar)
        {
            ++index;
        }
        if (index == text.size())
        {
            break;
        }
        ++matchCount;
        ++index;
    }

    if (matchCount == 0)
    {
        return 0.0;
    }

    const double coverage = static_cast<double>(matchCount) / static_cast<double>(pattern.size());
    return std::clamp(coverage * 12.0, 0.0, 12.0);
}

double kindBoostCompletion(const std::int64_t kind)
{
    switch (kind)
    {
    case 7:  // Class/Type
        return 2.0;
    case 14:  // Keyword/directive
        return 1.0;
    default:
        return 0.5;
    }
}

double kindBoostSymbol(const std::int64_t kind)
{
    switch (kind)
    {
    case 23:  // Struct
        return 2.0;
    case 8:   // Field
    case 14:  // Constant
        return 1.0;
    default:
        return 0.5;
    }
}

RankingBreakdown scoreCommon(const std::string&                  query,
                             const std::string&                  primary,
                             const std::string&                  secondary,
                             const double                        lexicalBase,
                             const double                        kindBoost,
                             const std::optional<RankingSignal>& signal,
                             const std::uint64_t                 nowTick)
{
    const std::string queryLower     = toLower(query);
    const std::string primaryLower   = toLower(primary);
    const std::string secondaryLower = toLower(secondary);

    RankingBreakdown out;
    out.lexicalBase = lexicalBase;
    out.kindBoost   = kindBoost;

    if (!queryLower.empty())
    {
        if (primaryLower == queryLower)
        {
            out.matchQuality += 30.0;
        }
        else if (secondaryLower == queryLower)
        {
            out.matchQuality += 24.0;
        }

        if (startsWith(primaryLower, queryLower))
        {
            out.matchQuality += 16.0;
        }
        else if (startsWith(secondaryLower, queryLower))
        {
            out.matchQuality += 12.0;
        }

        if (contains(primaryLower, queryLower))
        {
            out.matchQuality += 6.0;
        }
        else if (contains(secondaryLower, queryLower))
        {
            out.matchQuality += 3.0;
        }

        out.fuzzyBoost += subsequenceScore(primaryLower, queryLower);
        out.fuzzyBoost += 0.5 * subsequenceScore(secondaryLower, queryLower);
    }

    if (signal.has_value())
    {
        const auto exposure = static_cast<double>(signal->exposureCount);
        const auto selected = static_cast<double>(signal->selectionCount);

        out.frequencyBoost = std::log1p((exposure * 0.25) + (selected * 2.0)) * 2.5;
        out.frequencyBoost = std::min(10.0, out.frequencyBoost);

        const std::uint64_t age   = nowTick > signal->lastTick ? (nowTick - signal->lastTick) : 0U;
        const double        decay = std::exp(-static_cast<double>(age) / 256.0);
        out.recencyBoost          = std::clamp(decay * 6.0, 0.0, 6.0);
    }

    out.lengthPenalty = -std::min(4.0, static_cast<double>(primary.size()) / 64.0);
    out.totalScore    = out.lexicalBase + out.matchQuality + out.fuzzyBoost + out.frequencyBoost + out.recencyBoost +
                        out.kindBoost + out.lengthPenalty;
    return out;
}

}  // namespace

RankingBreakdown RankingModel::scoreCompletion(const std::string&                  query,
                                               const CompletionRankingInput&       input,
                                               const std::optional<RankingSignal>& signal,
                                               const std::uint64_t                 nowTick)
{
    return scoreCommon(query,
                       input.label,
                       input.detail,
                       input.baseScore,
                       kindBoostCompletion(input.kind),
                       signal,
                       nowTick);
}

RankingBreakdown RankingModel::scoreSymbol(const std::string&                  query,
                                           const SymbolRankingInput&           input,
                                           const std::optional<RankingSignal>& signal,
                                           const std::uint64_t                 nowTick)
{
    return scoreCommon(query,
                       input.qualifiedName,
                       input.name,
                       input.baseScore,
                       kindBoostSymbol(input.kind),
                       signal,
                       nowTick);
}

AdaptiveSignalStore::AdaptiveSignalStore(std::string persistencePath, const std::size_t maxEntries)
    : persistencePath_(std::move(persistencePath))
    , maxEntries_(std::max<std::size_t>(1, maxEntries))
{
    std::scoped_lock<std::mutex> const lock(mutex_);
    loadLocked();
}

std::uint64_t AdaptiveSignalStore::currentTick() const
{
    std::scoped_lock<std::mutex> const lock(mutex_);
    return nextTick_;
}

std::optional<RankingSignal> AdaptiveSignalStore::signalFor(const std::string& key) const
{
    std::scoped_lock<std::mutex> const lock(mutex_);
    const auto                         it = signals_.find(key);
    if (it == signals_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

void AdaptiveSignalStore::noteExposure(const std::string& key)
{
    noteEvent(key, false);
}

void AdaptiveSignalStore::noteSelection(const std::string& key)
{
    noteEvent(key, true);
}

void AdaptiveSignalStore::noteTopExposures(const std::vector<std::string>& keys, const std::size_t maxCount)
{
    const std::size_t count = std::min(maxCount, keys.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        noteEvent(keys[i], false);
    }
}

bool AdaptiveSignalStore::flush()
{
    std::scoped_lock<std::mutex> const lock(mutex_);
    if (!dirty_)
    {
        return true;
    }
    if (persistencePath_.empty())
    {
        dirty_ = false;
        return true;
    }

    std::error_code             ec;
    const std::filesystem::path persistencePath(persistencePath_);
    std::filesystem::create_directories(persistencePath.parent_path(), ec);
    if (ec)
    {
        return false;
    }

    std::vector<std::pair<std::string, RankingSignal>> ordered(signals_.begin(), signals_.end());
    std::ranges::sort(ordered, [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.second.lastTick, lhs.first) < std::tie(rhs.second.lastTick, rhs.first);
    });

    llvm::json::Array entries;
    for (const auto& [key, signal] : ordered)
    {
        entries.push_back(llvm::json::Object{{"key", key},
                                             {"exposure", static_cast<std::int64_t>(signal.exposureCount)},
                                             {"selection", static_cast<std::int64_t>(signal.selectionCount)},
                                             {"last_tick", static_cast<std::int64_t>(signal.lastTick)}});
    }

    llvm::json::Object root;
    root["schema_version"] = static_cast<std::int64_t>(RankingSignalSchemaVersion);
    root["next_tick"]      = static_cast<std::int64_t>(nextTick_);
    root["entries"]        = std::move(entries);

    std::string              rendered;
    llvm::raw_string_ostream stream(rendered);
    stream << llvm::formatv("{0:2}", llvm::json::Value(std::move(root)));
    stream << '\n';
    stream.flush();

    const std::filesystem::path tmpPath = persistencePath.string() + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.good())
        {
            return false;
        }
        out.write(rendered.data(), static_cast<std::streamsize>(rendered.size()));
        out.flush();
        if (!out.good())
        {
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }

    std::filesystem::rename(tmpPath, persistencePath, ec);
    if (ec)
    {
        std::filesystem::remove(persistencePath, ec);
        ec.clear();
        std::filesystem::rename(tmpPath, persistencePath, ec);
        if (ec)
        {
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }

    dirty_ = false;
    return true;
}

std::size_t AdaptiveSignalStore::size() const
{
    std::scoped_lock<std::mutex> const lock(mutex_);
    return signals_.size();
}

void AdaptiveSignalStore::noteEvent(const std::string& key, const bool selection)
{
    if (key.empty())
    {
        return;
    }

    std::scoped_lock<std::mutex> const lock(mutex_);
    RankingSignal&                     signal = signals_[key];
    signal.exposureCount =
        selection ? signal.exposureCount : std::min<std::uint32_t>(signal.exposureCount + 1U, 100000U);
    signal.selectionCount =
        selection ? std::min<std::uint32_t>(signal.selectionCount + 1U, 100000U) : signal.selectionCount;
    signal.lastTick = nextTick_++;

    dirty_ = true;
    pruneLocked();
}

void AdaptiveSignalStore::pruneLocked()
{
    if (signals_.size() <= maxEntries_)
    {
        return;
    }

    std::vector<std::pair<std::string, RankingSignal>> ordered(signals_.begin(), signals_.end());
    std::ranges::sort(ordered, [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.second.lastTick, lhs.first) < std::tie(rhs.second.lastTick, rhs.first);
    });

    const std::size_t removeCount = ordered.size() - maxEntries_;
    for (std::size_t i = 0; i < removeCount; ++i)
    {
        signals_.erase(ordered[i].first);
    }
}

void AdaptiveSignalStore::loadLocked()
{
    if (persistencePath_.empty())
    {
        return;
    }

    std::error_code ec;
    if (!std::filesystem::exists(persistencePath_, ec))
    {
        return;
    }

    std::ifstream in(persistencePath_, std::ios::binary);
    if (!in.good())
    {
        return;
    }

    const std::string                 text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    llvm::Expected<llvm::json::Value> parsed = llvm::json::parse(text);
    if (!parsed)
    {
        return;
    }

    const auto* object = parsed->getAsObject();
    if (!object)
    {
        return;
    }

    const auto schema = object->getInteger("schema_version");
    if (!schema.has_value() || std::cmp_not_equal(*schema, RankingSignalSchemaVersion))
    {
        return;
    }

    if (const auto loadedTick = object->getInteger("next_tick"); loadedTick.has_value() && *loadedTick > 0)
    {
        nextTick_ = static_cast<std::uint64_t>(*loadedTick);
    }

    const auto* entries = object->getArray("entries");
    if (!entries)
    {
        return;
    }

    for (const llvm::json::Value& entryValue : *entries)
    {
        const auto* entry = entryValue.getAsObject();
        if (!entry)
        {
            continue;
        }

        const auto key       = entry->getString("key");
        const auto exposure  = entry->getInteger("exposure");
        const auto selection = entry->getInteger("selection");
        const auto lastTick  = entry->getInteger("last_tick");
        if (!key.has_value() || !exposure.has_value() || !selection.has_value() || !lastTick.has_value())
        {
            continue;
        }

        signals_.insert_or_assign(key->str(),
                                  RankingSignal{
                                      static_cast<std::uint32_t>(std::max<std::int64_t>(0, *exposure)),
                                      static_cast<std::uint32_t>(std::max<std::int64_t>(0, *selection)),
                                      static_cast<std::uint64_t>(std::max<std::int64_t>(0, *lastTick)),
                                  });
    }

    pruneLocked();
    dirty_ = false;
}

}  // namespace llvmdsdl::lsp
