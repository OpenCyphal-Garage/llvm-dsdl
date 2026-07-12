//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements overlay document storage for open editor buffers.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/DocumentStore.h"

#include <mutex>
#include <utility>

namespace llvmdsdl::lsp
{

void DocumentStore::open(std::string uri, std::string text, const std::int64_t version)
{
    DocumentSnapshot            snapshot{uri, std::move(text), version};
    std::lock_guard<std::mutex> lock(mutex_);
    documents_.insert_or_assign(std::move(uri), std::move(snapshot));
}

bool DocumentStore::applyFullTextChange(const std::string& uri, std::string text, const std::int64_t version)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto                  it = documents_.find(uri);
    if (it == documents_.end())
    {
        return false;
    }
    it->second.text    = std::move(text);
    it->second.version = version;
    return true;
}

bool DocumentStore::close(const std::string& uri)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return documents_.erase(uri) > 0U;
}

std::optional<DocumentSnapshot> DocumentStore::lookup(const std::string& uri) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto                  it = documents_.find(uri);
    if (it == documents_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::vector<DocumentSnapshot> DocumentStore::snapshots() const
{
    std::lock_guard<std::mutex>   lock(mutex_);
    std::vector<DocumentSnapshot> out;
    out.reserve(documents_.size());
    for (const auto& [_, snapshot] : documents_)
    {
        out.push_back(snapshot);
    }
    return out;
}

std::size_t DocumentStore::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return documents_.size();
}

}  // namespace llvmdsdl::lsp
