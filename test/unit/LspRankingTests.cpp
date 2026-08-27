//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <string>
#include <vector>

#include "llvmdsdl/LSP/Ranking.h"

namespace
{

std::filesystem::path makeUniqueTempDir()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() / ("llvmdsdl-lsp-ranking-" + std::to_string(now));
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.good())
    {
        return false;
    }
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    return out.good();
}

}  // namespace

bool runLspRankingTests()
{
    const std::filesystem::path tmpRoot = makeUniqueTempDir();
    std::error_code             ec;
    std::filesystem::create_directories(tmpRoot, ec);
    if (ec)
    {
        std::cerr << "failed to create temp ranking dir: " << ec.message() << "\n";
        return false;
    }

    auto fail = [&](std::string_view message) {
        std::cerr << message << "\n";
        std::filesystem::remove_all(tmpRoot, ec);
        return false;
    };

    const std::filesystem::path signalPath = tmpRoot / "signals.json";

    llvmdsdl::lsp::AdaptiveSignalStore store(signalPath.string(), 4);
    if (store.persistencePath() != signalPath.string())
    {
        return fail("store persistence path accessor returned unexpected value");
    }
    if (!store.flush())
    {
        return fail("flush should succeed for clean store");
    }
    const auto tickBeforeEmptyKey = store.currentTick();
    store.noteExposure("");
    if (store.currentTick() != tickBeforeEmptyKey || store.size() != 0)
    {
        return fail("empty-key exposure should not mutate store");
    }
    store.noteExposure("completion:demo.TypeA.1.0");
    store.noteSelection("completion:demo.TypeA.1.0");
    store.noteExposure("completion:demo.TypeB.1.0");
    store.noteExposure("completion:demo.TypeC.1.0");
    store.noteExposure("completion:demo.TypeD.1.0");
    store.noteExposure("completion:demo.TypeE.1.0");
    store.noteSelection("completion:demo.TypeA.1.0");
    if (!store.flush())
    {
        return fail("failed to flush signal store");
    }

    llvmdsdl::lsp::AdaptiveSignalStore reloaded(signalPath.string(), 4);
    if (reloaded.size() > 4)
    {
        return fail("signal store did not respect bounded size");
    }
    if (reloaded.signalFor("completion:demo.TypeB.1.0").has_value())
    {
        return fail("oldest store entry should have been pruned");
    }

    const auto signalA = reloaded.signalFor("completion:demo.TypeA.1.0");
    if (!signalA.has_value() || signalA->selectionCount == 0)
    {
        return fail("signal store did not persist selection count");
    }

    llvmdsdl::lsp::AdaptiveSignalStore exposureStore((tmpRoot / "top_exposures.json").string(), 8);
    exposureStore.noteTopExposures({"k1", "k2", "k3"}, 2);
    if (exposureStore.size() != 2 || !exposureStore.signalFor("k1").has_value() ||
        !exposureStore.signalFor("k2").has_value() || exposureStore.signalFor("k3").has_value())
    {
        return fail("top exposure tracking did not honor maxCount limit");
    }

    llvmdsdl::lsp::AdaptiveSignalStore inMemoryStore("", 2);
    inMemoryStore.noteExposure("volatile");
    if (!inMemoryStore.flush() || !inMemoryStore.flush())
    {
        return fail("in-memory store flush should always succeed");
    }

    const std::filesystem::path blockingPath = tmpRoot / "blocking_parent";
    if (!writeTextFile(blockingPath, "not a directory"))
    {
        return fail("failed to create blocking persistence parent file");
    }
    llvmdsdl::lsp::AdaptiveSignalStore blockedStore((blockingPath / "signals.json").string(), 2);
    blockedStore.noteExposure("x");
    if (blockedStore.flush())
    {
        return fail("flush should fail when parent path cannot be created");
    }

    const std::filesystem::path invalidJsonPath = tmpRoot / "invalid_signals.json";
    if (!writeTextFile(invalidJsonPath, "{this is not json"))
    {
        return fail("failed to write invalid json fixture");
    }
    llvmdsdl::lsp::AdaptiveSignalStore invalidJsonStore(invalidJsonPath.string(), 4);
    if (invalidJsonStore.size() != 0)
    {
        return fail("invalid json should be ignored during load");
    }

    const std::filesystem::path wrongSchemaPath = tmpRoot / "wrong_schema.json";
    if (!writeTextFile(
            wrongSchemaPath,
            R"({"schema_version":999,"next_tick":99,"entries":[{"key":"k","exposure":9,"selection":4,"last_tick":7}]})"))
    {
        return fail("failed to write wrong-schema fixture");
    }
    llvmdsdl::lsp::AdaptiveSignalStore wrongSchemaStore(wrongSchemaPath.string(), 4);
    if (wrongSchemaStore.size() != 0)
    {
        return fail("wrong schema version should be ignored");
    }

    const std::filesystem::path negativeFixturePath = tmpRoot / "negative_entries.json";
    if (!writeTextFile(
            negativeFixturePath,
            R"({"schema_version":1,"next_tick":42,"entries":[{"key":"a","exposure":-4,"selection":-2,"last_tick":-1},{"key":"b","exposure":2,"selection":1,"last_tick":5},{"key":"c","exposure":3,"selection":2,"last_tick":6}]})"))
    {
        return fail("failed to write clamping/pruning fixture");
    }
    llvmdsdl::lsp::AdaptiveSignalStore clampedStore(negativeFixturePath.string(), 2);
    if (clampedStore.size() != 2 || clampedStore.currentTick() != 42)
    {
        return fail("clamped/pruned store did not load expected shape");
    }
    if (clampedStore.signalFor("a").has_value())
    {
        return fail("oldest loaded entry should have been pruned");
    }
    const auto signalB = clampedStore.signalFor("b");
    if (!signalB.has_value() || signalB->exposureCount != 2 || signalB->selectionCount != 1 || signalB->lastTick != 5)
    {
        return fail("loaded signal values were not preserved as expected");
    }

    const auto exact = llvmdsdl::lsp::RankingModel::scoreCompletion("demo.TypeA.1.0",
                                                                    llvmdsdl::lsp::CompletionRankingInput{
                                                                        "completion:demo.TypeA.1.0",
                                                                        "demo.TypeA.1.0",
                                                                        "composite",
                                                                        7,
                                                                        70.0,
                                                                    },
                                                                    signalA,
                                                                    reloaded.currentTick());

    const auto fuzzy = llvmdsdl::lsp::RankingModel::scoreCompletion("tpA",
                                                                    llvmdsdl::lsp::CompletionRankingInput{
                                                                        "completion:demo.TypeA.1.0",
                                                                        "demo.TypeA.1.0",
                                                                        "composite",
                                                                        7,
                                                                        35.0,
                                                                    },
                                                                    signalA,
                                                                    reloaded.currentTick());

    if (exact.totalScore <= fuzzy.totalScore)
    {
        return fail("expected exact completion match to outrank fuzzy match");
    }

    const auto completionWithoutSignal =
        llvmdsdl::lsp::RankingModel::scoreCompletion("",
                                                     llvmdsdl::lsp::CompletionRankingInput{
                                                         "completion:demo.KindDefault.1.0",
                                                         "KindDefault",
                                                         "detail",
                                                         1,
                                                         10.0,
                                                     },
                                                     std::nullopt,
                                                     5);

    const auto completionWithSignal =
        llvmdsdl::lsp::RankingModel::scoreCompletion("",
                                                     llvmdsdl::lsp::CompletionRankingInput{
                                                         "completion:demo.KindDefault.1.0",
                                                         "KindDefault",
                                                         "detail",
                                                         1,
                                                         10.0,
                                                     },
                                                     llvmdsdl::lsp::RankingSignal{12, 3, 10},
                                                     5);

    if (completionWithSignal.matchQuality != 0.0 || completionWithSignal.fuzzyBoost != 0.0)
    {
        return fail("empty query should not add lexical/fuzzy boosts");
    }
    if (completionWithSignal.frequencyBoost <= completionWithoutSignal.frequencyBoost ||
        completionWithSignal.recencyBoost <= completionWithoutSignal.recencyBoost)
    {
        return fail("adaptive signal should increase frequency/recency boosts");
    }

    const auto completionTypeKind = llvmdsdl::lsp::RankingModel::scoreCompletion("",
                                                                                 llvmdsdl::lsp::CompletionRankingInput{
                                                                                     "completion:demo.TypeX.1.0",
                                                                                     "TypeX",
                                                                                     "",
                                                                                     7,
                                                                                     0.0,
                                                                                 },
                                                                                 std::nullopt,
                                                                                 1);
    const auto completionDefaultKind =
        llvmdsdl::lsp::RankingModel::scoreCompletion("",
                                                     llvmdsdl::lsp::CompletionRankingInput{
                                                         "completion:demo.UnknownKind.1.0",
                                                         "UnknownKind",
                                                         "",
                                                         99,
                                                         0.0,
                                                     },
                                                     std::nullopt,
                                                     1);
    if (completionTypeKind.kindBoost <= completionDefaultKind.kindBoost)
    {
        return fail("completion kind boost for type should exceed default boost");
    }

    const auto detailMatched    = llvmdsdl::lsp::RankingModel::scoreCompletion("meta",
                                                                               llvmdsdl::lsp::CompletionRankingInput{
                                                                                   "completion:demo.MatchInDetail.1.0",
                                                                                   "symbol",
                                                                                   "meta_serializable",
                                                                                   7,
                                                                                   0.0,
                                                                               },
                                                                               std::nullopt,
                                                                               1);
    const auto detailMismatched = llvmdsdl::lsp::RankingModel::scoreCompletion("meta",
                                                                               llvmdsdl::lsp::CompletionRankingInput{
                                                                                   "completion:demo.NoDetailMatch.1.0",
                                                                                   "symbol",
                                                                                   "transport",
                                                                                   7,
                                                                                   0.0,
                                                                               },
                                                                               std::nullopt,
                                                                               1);
    if (detailMatched.totalScore <= detailMismatched.totalScore)
    {
        return fail("secondary-detail lexical match should improve completion score");
    }

    const std::string longLabel(512, 'x');
    const auto shortLabelScore = llvmdsdl::lsp::RankingModel::scoreCompletion("",
                                                                              llvmdsdl::lsp::CompletionRankingInput{
                                                                                  "completion:demo.Short.1.0",
                                                                                  "short",
                                                                                  "",
                                                                                  7,
                                                                                  4.0,
                                                                              },
                                                                              std::nullopt,
                                                                              1);
    const auto longLabelScore  = llvmdsdl::lsp::RankingModel::scoreCompletion("",
                                                                              llvmdsdl::lsp::CompletionRankingInput{
                                                                                  "completion:demo.Long.1.0",
                                                                                  longLabel,
                                                                                  "",
                                                                                  7,
                                                                                  4.0,
                                                                              },
                                                                              std::nullopt,
                                                                              1);
    if (longLabelScore.lengthPenalty >= shortLabelScore.lengthPenalty ||
        longLabelScore.totalScore >= shortLabelScore.totalScore)
    {
        return fail("longer labels should incur a stronger length penalty");
    }

    const auto symbolExact = llvmdsdl::lsp::RankingModel::scoreSymbol("TypeA",
                                                                      llvmdsdl::lsp::SymbolRankingInput{
                                                                          "symbol:type:demo.TypeA.1.0",
                                                                          "TypeA",
                                                                          "demo.TypeA.1.0",
                                                                          "demo",
                                                                          "message",
                                                                          23,
                                                                          60.0,
                                                                      },
                                                                      std::nullopt,
                                                                      reloaded.currentTick());

    const auto symbolMismatch = llvmdsdl::lsp::RankingModel::scoreSymbol("Altitude",
                                                                         llvmdsdl::lsp::SymbolRankingInput{
                                                                             "symbol:type:demo.TypeA.1.0",
                                                                             "TypeA",
                                                                             "demo.TypeA.1.0",
                                                                             "demo",
                                                                             "message",
                                                                             23,
                                                                             60.0,
                                                                         },
                                                                         std::nullopt,
                                                                         reloaded.currentTick());

    if (symbolExact.totalScore <= symbolMismatch.totalScore)
    {
        return fail("expected matching workspace symbol query to outrank mismatch");
    }

    const auto symbolStructKind  = llvmdsdl::lsp::RankingModel::scoreSymbol("",
                                                                            llvmdsdl::lsp::SymbolRankingInput{
                                                                                "symbol:type:demo.StructType.1.0",
                                                                                "StructType",
                                                                                "demo.StructType.1.0",
                                                                                "demo",
                                                                                "message",
                                                                                23,
                                                                                0.0,
                                                                            },
                                                                            std::nullopt,
                                                                            1);
    const auto symbolDefaultKind = llvmdsdl::lsp::RankingModel::scoreSymbol("",
                                                                            llvmdsdl::lsp::SymbolRankingInput{
                                                                                "symbol:type:demo.UnknownKind.1.0",
                                                                                "UnknownKind",
                                                                                "demo.UnknownKind.1.0",
                                                                                "demo",
                                                                                "message",
                                                                                99,
                                                                                0.0,
                                                                            },
                                                                            std::nullopt,
                                                                            1);
    if (symbolStructKind.kindBoost <= symbolDefaultKind.kindBoost)
    {
        return fail("symbol kind boost for struct should exceed default boost");
    }

    std::filesystem::remove_all(tmpRoot, ec);
    return true;
}
