//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Cached dependency planner for per-output depfile input resolution.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Frontend/DepfilePlanner.h"

#include <algorithm>
#include <filesystem>
#include <queue>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace llvmdsdl
{

namespace
{

inline constexpr std::string_view kEmbeddedUavcanSyntheticPathPrefix = "<embedded-uavcan>:";

std::string makeTypeKey(const DiscoveredDefinition& info)
{
    return info.fullName + ":" + std::to_string(info.majorVersion) + ":" + std::to_string(info.minorVersion);
}

std::string makeTypeKey(const SemanticTypeRef& ref)
{
    return ref.fullName + ":" + std::to_string(ref.majorVersion) + ":" + std::to_string(ref.minorVersion);
}

bool isEmbeddedUavcanSyntheticPath(const std::string& filePath)
{
    return filePath.starts_with(kEmbeddedUavcanSyntheticPathPrefix);
}

std::string normalizePathForDepfile(const std::string& path)
{
    std::error_code             ec;
    const std::filesystem::path p(path);
    auto                        n = std::filesystem::weakly_canonical(p, ec);
    if (ec)
    {
        ec.clear();
        n = std::filesystem::absolute(p, ec);
        if (ec)
        {
            return p.lexically_normal().string();
        }
    }
    return n.lexically_normal().string();
}

void appendSectionDependencyIndexes(const SemanticSection&                              section,
                                    const std::unordered_map<std::string, std::size_t>& nodeIndexByTypeKey,
                                    std::vector<std::size_t>&                           destination)
{
    for (const auto& field : section.fields)
    {
        if (!field.resolvedType.compositeType)
        {
            continue;
        }

        const auto it = nodeIndexByTypeKey.find(makeTypeKey(*field.resolvedType.compositeType));
        if (it == nodeIndexByTypeKey.end())
        {
            continue;
        }

        destination.push_back(it->second);
    }
}

}  // namespace

DepfilePlanner::DepfilePlanner(const SemanticModule& semantic, const std::string& toolchainStampPath)
{
    if (!toolchainStampPath.empty())
    {
        toolchainStampPath_ = normalizePathForDepfile(toolchainStampPath);
        compilerOnlyDeps_.push_back(toolchainStampPath_);
    }

    nodes_.reserve(semantic.definitions.size());
    nodeIndexByTypeKey_.reserve(semantic.definitions.size());
    visitEpochByNode_.reserve(semantic.definitions.size());

    std::vector<const SemanticDefinition*> definitionsByIndex;
    definitionsByIndex.reserve(semantic.definitions.size());

    for (const auto& def : semantic.definitions)
    {
        const std::string typeKey = makeTypeKey(def.info);
        if (nodeIndexByTypeKey_.contains(typeKey))
        {
            continue;
        }

        const std::size_t nextIndex = nodes_.size();
        nodeIndexByTypeKey_.emplace(typeKey, nextIndex);

        Node node;
        node.normalizedInputPath = normalizePathForDepfile(def.info.filePath);
        node.fromEmbeddedCatalog = isEmbeddedUavcanSyntheticPath(def.info.filePath);
        nodes_.push_back(std::move(node));
        definitionsByIndex.push_back(&def);
    }

    visitEpochByNode_.assign(nodes_.size(), 0U);

    for (std::size_t i = 0; i < definitionsByIndex.size(); ++i)
    {
        const auto* def = definitionsByIndex[i];
        appendSectionDependencyIndexes(def->request, nodeIndexByTypeKey_, nodes_[i].dependencyIndexes);
        if (def->response)
        {
            appendSectionDependencyIndexes(*def->response, nodeIndexByTypeKey_, nodes_[i].dependencyIndexes);
        }

        auto& deps = nodes_[i].dependencyIndexes;
        std::ranges::sort(deps);
        deps.erase(std::ranges::unique(deps).begin(), deps.end());
    }
}

const std::vector<std::string>& DepfilePlanner::depsForCompilerOnlyOutput()
{
    return compilerOnlyDeps_;
}

const std::vector<std::string>& DepfilePlanner::depsForTypeKey(const std::string& typeKey)
{
    if (const auto it = depsByTypeKey_.find(typeKey); it != depsByTypeKey_.end())
    {
        return it->second;
    }

    const auto rootIt = nodeIndexByTypeKey_.find(typeKey);
    if (rootIt == nodeIndexByTypeKey_.end())
    {
        return emptyDeps_;
    }

    if (nextVisitEpoch_ == 0U)
    {
        std::ranges::fill(visitEpochByNode_, 0U);
        nextVisitEpoch_ = 1U;
    }
    const std::uint32_t visitEpoch = nextVisitEpoch_++;

    std::vector<std::size_t> queue;
    queue.push_back(rootIt->second);

    std::vector<std::string> deps;
    deps.reserve(nodes_.size());

    bool drawsOnEmbeddedCatalog = false;

    for (std::size_t cursor = 0; cursor < queue.size(); ++cursor)
    {
        const auto nodeIndex = queue[cursor];
        if (visitEpochByNode_[nodeIndex] == visitEpoch)
        {
            continue;
        }
        visitEpochByNode_[nodeIndex] = visitEpoch;

        const auto& node = nodes_[nodeIndex];
        if (node.fromEmbeddedCatalog)
        {
            drawsOnEmbeddedCatalog = true;
        }
        else
        {
            deps.push_back(node.normalizedInputPath);
        }

        queue.insert(queue.end(), node.dependencyIndexes.begin(), node.dependencyIndexes.end());
    }

    // The catalog has no source file to name, so the binary carrying it stands in. Without this the
    // rule would have no prerequisites at all and never rebuild across a compiler upgrade.
    if (drawsOnEmbeddedCatalog && !toolchainStampPath_.empty())
    {
        deps.push_back(toolchainStampPath_);
    }

    std::ranges::sort(deps);
    deps.erase(std::ranges::unique(deps).begin(), deps.end());

    const auto [inserted, _] = depsByTypeKey_.emplace(typeKey, std::move(deps));
    return inserted->second;
}

const std::vector<std::string>& DepfilePlanner::depsForRequiredTypeKeys(
    const std::vector<std::string>& requiredTypeKeys)
{
    if (requiredTypeKeys.empty())
    {
        return emptyDeps_;
    }

    std::vector<std::string> canonicalKeys = requiredTypeKeys;
    std::ranges::sort(canonicalKeys);
    canonicalKeys.erase(std::ranges::unique(canonicalKeys).begin(), canonicalKeys.end());

    if (canonicalKeys.empty())
    {
        return emptyDeps_;
    }

    std::string signature;
    for (const auto& key : canonicalKeys)
    {
        signature.append(key);
        signature.push_back('\n');
    }

    if (const auto it = depsByRequiredSetSignature_.find(signature); it != depsByRequiredSetSignature_.end())
    {
        return it->second;
    }

    std::vector<std::string> merged;
    for (const auto& key : canonicalKeys)
    {
        const auto& deps = depsForTypeKey(key);
        merged.insert(merged.end(), deps.begin(), deps.end());
    }

    std::ranges::sort(merged);
    merged.erase(std::ranges::unique(merged).begin(), merged.end());

    const auto [inserted, _] = depsByRequiredSetSignature_.emplace(signature, std::move(merged));
    return inserted->second;
}

}  // namespace llvmdsdl
