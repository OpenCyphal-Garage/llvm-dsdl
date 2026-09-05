//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Ranking model and adaptive usage signals for LSP completion/symbol queries.
///
/// This module provides explainable score breakdowns and a bounded persistent
/// recency/frequency signal store used to rerank candidates over time.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_LSP_RANKING_H
#define LLVMDSDL_LSP_RANKING_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace llvmdsdl::lsp
{

/// @brief Persisted adaptive signal for one ranking key.
struct RankingSignal final
{
    /// @brief Number of result impressions observed.
    std::uint32_t exposureCount{0};

    /// @brief Number of explicit selection events observed.
    std::uint32_t selectionCount{0};

    /// @brief Monotonic tick of last update.
    std::uint64_t lastTick{0};
};

/// @brief Input payload for completion reranking.
struct CompletionRankingInput final
{
    /// @brief Stable ranking key.
    std::string key;

    /// @brief Candidate label.
    std::string label;

    /// @brief Candidate detail.
    std::string detail;

    /// @brief LSP CompletionItemKind numeric value.
    std::int64_t kind{1};

    /// @brief Upstream lexical/base score.
    double baseScore{0.0};
};

/// @brief Input payload for workspace-symbol reranking.
struct SymbolRankingInput final
{
    /// @brief Stable ranking key (typically symbol USR).
    std::string key;

    /// @brief Symbol name.
    std::string name;

    /// @brief Fully qualified symbol name.
    std::string qualifiedName;

    /// @brief Symbol container.
    std::string containerName;

    /// @brief Symbol detail string.
    std::string detail;

    /// @brief LSP SymbolKind numeric value.
    std::int64_t kind{13};

    /// @brief Upstream lexical/base score.
    double baseScore{0.0};
};

/// @brief Explainable score components emitted by ranking model.
struct RankingBreakdown final
{
    /// @brief Base lexical score provided by retrieval stage.
    double lexicalBase{0.0};

    /// @brief Boost for exact/prefix containment quality.
    double matchQuality{0.0};

    /// @brief Boost for fuzzy subsequence matches.
    double fuzzyBoost{0.0};

    /// @brief Adaptive boost derived from selection/exposure frequency.
    double frequencyBoost{0.0};

    /// @brief Adaptive boost derived from recency.
    double recencyBoost{0.0};

    /// @brief Bias by candidate kind.
    double kindBoost{0.0};

    /// @brief Small penalty for long candidate names.
    double lengthPenalty{0.0};

    /// @brief Final aggregate score.
    double totalScore{0.0};
};

/// @brief Shared deterministic ranking/scoring model.
class RankingModel final
{
public:
    /// @brief Scores a completion candidate for query text.
    /// @param[in] query Query string.
    /// @param[in] input Completion input row.
    /// @param[in] signal Optional adaptive signal.
    /// @param[in] nowTick Current signal tick.
    /// @return Score component breakdown.
    [[nodiscard]] static RankingBreakdown scoreCompletion(const std::string&                  query,
                                                          const CompletionRankingInput&       input,
                                                          const std::optional<RankingSignal>& signal,
                                                          std::uint64_t                       nowTick);

    /// @brief Scores a workspace-symbol candidate for query text.
    /// @param[in] query Query string.
    /// @param[in] input Symbol input row.
    /// @param[in] signal Optional adaptive signal.
    /// @param[in] nowTick Current signal tick.
    /// @return Score component breakdown.
    [[nodiscard]] static RankingBreakdown scoreSymbol(const std::string&                  query,
                                                      const SymbolRankingInput&           input,
                                                      const std::optional<RankingSignal>& signal,
                                                      std::uint64_t                       nowTick);
};

/// @brief Bounded persistent store of adaptive ranking signals.
class AdaptiveSignalStore final
{
public:
    /// @brief Constructs store bound to a persistence file.
    /// @param[in] persistencePath JSON persistence file path.
    /// @param[in] maxEntries Maximum number of keys retained.
    explicit AdaptiveSignalStore(std::string persistencePath, std::size_t maxEntries = 4096);

    /// @brief Returns current persistence file path.
    /// @return Persistence file path.
    [[nodiscard]] const std::string& persistencePath() const
    {
        return persistencePath_;
    }

    /// @brief Returns current monotonic tick.
    /// @return Current tick value.
    [[nodiscard]] std::uint64_t currentTick() const;

    /// @brief Reads one key signal.
    /// @param[in] key Ranking key.
    /// @return Signal when present.
    [[nodiscard]] std::optional<RankingSignal> signalFor(const std::string& key) const;

    /// @brief Records one exposure event for a key.
    /// @param[in] key Ranking key.
    void noteExposure(const std::string& key);

    /// @brief Records one selection event for a key.
    /// @param[in] key Ranking key.
    void noteSelection(const std::string& key);

    /// @brief Records exposure events for top results.
    /// @param[in] keys Ranking keys in ranked order.
    /// @param[in] maxCount Max number of keys to record.
    void noteTopExposures(const std::vector<std::string>& keys, std::size_t maxCount);

    /// @brief Writes store to disk if dirty.
    /// @return `true` when write succeeds or no write needed.
    [[nodiscard]] bool flush();

    /// @brief Returns number of tracked keys.
    /// @return Entry count.
    [[nodiscard]] std::size_t size() const;

private:
    void noteEvent(const std::string& key, bool selection);
    void pruneLocked();
    void loadLocked();

    std::string                                    persistencePath_;
    std::size_t                                    maxEntries_{4096};
    mutable std::mutex                             mutex_;
    std::unordered_map<std::string, RankingSignal> signals_;
    std::uint64_t                                  nextTick_{1};
    bool                                           dirty_{false};
};

}  // namespace llvmdsdl::lsp

#endif  // LLVMDSDL_LSP_RANKING_H
