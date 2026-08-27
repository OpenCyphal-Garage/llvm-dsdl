//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements server configuration updates from LSP notifications.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/ServerConfig.h"

#include <algorithm>
#include <cctype>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/JSON.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace llvmdsdl::lsp
{
namespace
{

std::optional<std::vector<std::string>> parseStringArray(const llvm::json::Object& object, llvm::StringRef key)
{
    const auto* value = object.get(key);
    if (!value)
    {
        return std::nullopt;
    }

    const auto* array = value->getAsArray();
    if (!array)
    {
        return std::nullopt;
    }

    std::vector<std::string> out;
    out.reserve(array->size());
    for (const llvm::json::Value& item : *array)
    {
        const auto text = item.getAsString();
        if (!text.has_value())
        {
            return std::nullopt;
        }
        out.emplace_back(text->str());
    }
    return out;
}

std::optional<std::vector<std::string>> parseStringArrayValue(const llvm::json::Value& value)
{
    const auto* array = value.getAsArray();
    if (!array)
    {
        return std::nullopt;
    }

    std::vector<std::string> out;
    out.reserve(array->size());
    for (const llvm::json::Value& item : *array)
    {
        const auto text = item.getAsString();
        if (!text.has_value())
        {
            return std::nullopt;
        }
        out.emplace_back(text->str());
    }
    return out;
}

void applyTraceLevel(const llvm::json::Object& settings, ServerConfig& config)
{
    if (const auto rawTrace = settings.getString("trace"))
    {
        if (*rawTrace == "off")
        {
            config.traceLevel = TraceLevel::Off;
        }
        else if (*rawTrace == "verbose")
        {
            config.traceLevel = TraceLevel::Verbose;
        }
        else
        {
            config.traceLevel = TraceLevel::Basic;
        }
    }
}

void applyNestedBoolean(const llvm::json::Object& settings, llvm::StringRef key, llvm::StringRef field, bool& outValue)
{
    const auto* nestedValue = settings.get(key);
    if (!nestedValue)
    {
        return;
    }

    const auto* nested = nestedValue->getAsObject();
    if (!nested)
    {
        return;
    }

    if (const std::optional<bool> parsed = nested->getBoolean(field))
    {
        outValue = *parsed;
    }
}

std::optional<AiMode> parseAiModeString(llvm::StringRef rawMode)
{
    std::string normalized(rawMode.str());
    std::ranges::transform(normalized, normalized.begin(), [](const unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });

    if (normalized == "off")
    {
        return AiMode::Off;
    }
    if (normalized == "suggest")
    {
        return AiMode::Suggest;
    }
    if (normalized == "assist")
    {
        return AiMode::Assist;
    }
    if (normalized == "apply_with_confirmation")
    {
        return AiMode::ApplyWithConfirmation;
    }
    return std::nullopt;
}

void applyAiConfig(const llvm::json::Object& settings, ServerConfig& config)
{
    const auto* aiValue = settings.get("ai");
    if (!aiValue)
    {
        return;
    }
    const auto* ai = aiValue->getAsObject();
    if (!ai)
    {
        return;
    }

    if (const auto rawMode = ai->getString("mode"))
    {
        if (const std::optional<AiMode> parsedMode = parseAiModeString(*rawMode))
        {
            config.aiMode = *parsedMode;
        }
    }
}

void applyLintConfig(const llvm::json::Object& settings, ServerConfig& config)
{
    const auto* lintValue = settings.get("lint");
    if (!lintValue)
    {
        return;
    }
    const auto* lint = lintValue->getAsObject();
    if (!lint)
    {
        return;
    }

    if (const std::optional<bool> enabled = lint->getBoolean("enabled"))
    {
        config.lintEnabled = *enabled;
    }

    if (const auto* disabledRulesValue = lint->get("disabledRules"))
    {
        if (const auto disabledRules = parseStringArrayValue(*disabledRulesValue))
        {
            config.lintDisabledRules.clear();
            for (const std::string& ruleId : *disabledRules)
            {
                config.lintDisabledRules.insert(ruleId);
            }
        }
    }

    if (const auto* fileSuppressionsValue = lint->get("fileSuppressions"))
    {
        if (const auto* fileSuppressions = fileSuppressionsValue->getAsObject())
        {
            config.lintFileDisabledRules.clear();
            for (const auto& [fileKey, rulesValue] : *fileSuppressions)
            {
                if (const auto rules = parseStringArrayValue(rulesValue))
                {
                    std::unordered_set<std::string> set;
                    for (const std::string& ruleId : *rules)
                    {
                        set.insert(ruleId);
                    }
                    config.lintFileDisabledRules.insert_or_assign(fileKey.str(), std::move(set));
                }
            }
        }
    }

    if (const auto* pluginLibrariesValue = lint->get("pluginLibraries"))
    {
        if (const auto pluginLibraries = parseStringArrayValue(*pluginLibrariesValue))
        {
            config.lintPluginLibraries = *pluginLibraries;
        }
    }
}

}  // namespace

bool applyDidChangeConfiguration(const llvm::json::Value& params, ServerConfig& config)
{
    const auto* paramsObject = params.getAsObject();
    if (!paramsObject)
    {
        return false;
    }

    const auto* settingsValue = paramsObject->get("settings");
    if (!settingsValue)
    {
        return false;
    }

    const auto* settings = settingsValue->getAsObject();
    if (!settings)
    {
        return false;
    }

    if (const auto roots = parseStringArray(*settings, "roots"))
    {
        config.rootNamespaceDirs = *roots;
    }

    if (const auto lookupDirs = parseStringArray(*settings, "lookupDirs"))
    {
        config.lookupDirs = *lookupDirs;
    }

    if (const auto indexCacheDir = settings->getString("indexCacheDir"))
    {
        config.indexCacheDir = indexCacheDir->str();
    }

    applyLintConfig(*settings, config);
    applyAiConfig(*settings, config);
    applyNestedBoolean(*settings, "advanced", "enableMlirSnapshot", config.enableMlirSnapshot);
    applyTraceLevel(*settings, config);
    return true;
}

}  // namespace llvmdsdl::lsp
