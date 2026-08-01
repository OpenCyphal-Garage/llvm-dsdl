//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>
#include <unordered_set>

#include "llvmdsdl/CodeGen/EmitCommon.h"

namespace
{

llvmdsdl::DiscoveredDefinition makeInfo(const std::string& fullName)
{
    llvmdsdl::DiscoveredDefinition info;
    info.fullName     = fullName;
    info.majorVersion = 1U;
    info.minorVersion = 0U;
    return info;
}

bool expectSupport(const llvmdsdl::SupportGeneration mode,
                   const bool                        anyTypeEmitted,
                   const bool                        expected,
                   const char* const                 label)
{
    if (llvmdsdl::shouldEmitSupport(mode, anyTypeEmitted) != expected)
    {
        std::cerr << "shouldEmitSupport(" << label << ", anyTypeEmitted=" << (anyTypeEmitted ? "true" : "false")
                  << ") should be " << (expected ? "true" : "false") << "\n";
        return false;
    }
    return true;
}

}  // namespace

bool runSupportGenerationTests()
{
    using llvmdsdl::SupportGeneration;

    // `as-needed` is the only mode that consults whether any type is being emitted; the other three
    // are unconditional, which is what lets `only` and `always` run with no targets at all.
    if (!expectSupport(SupportGeneration::AsNeeded, true, true, "as-needed") ||
        !expectSupport(SupportGeneration::AsNeeded, false, false, "as-needed") ||
        !expectSupport(SupportGeneration::Always, false, true, "always") ||
        !expectSupport(SupportGeneration::Always, true, true, "always") ||
        !expectSupport(SupportGeneration::Never, true, false, "never") ||
        !expectSupport(SupportGeneration::Never, false, false, "never") ||
        !expectSupport(SupportGeneration::Only, false, true, "only") ||
        !expectSupport(SupportGeneration::Only, true, true, "only"))
    {
        return false;
    }

    // `only` is the sole mode that suppresses type emission.
    if (!llvmdsdl::shouldEmitTypes(SupportGeneration::AsNeeded) ||
        !llvmdsdl::shouldEmitTypes(SupportGeneration::Always) || !llvmdsdl::shouldEmitTypes(SupportGeneration::Never) ||
        llvmdsdl::shouldEmitTypes(SupportGeneration::Only))
    {
        std::cerr << "shouldEmitTypes should be false for 'only' and true for every other mode\n";
        return false;
    }

    // The mode overrides selection: under `only` even an explicitly selected type is not emitted.
    const auto                            info = makeInfo("ns.A");
    const std::unordered_set<std::string> selected{"ns.A:1:0"};
    const std::unordered_set<std::string> empty;

    if (!llvmdsdl::shouldEmitDefinition(info, selected, SupportGeneration::AsNeeded) ||
        !llvmdsdl::shouldEmitDefinition(info, empty, SupportGeneration::AsNeeded) ||
        !llvmdsdl::shouldEmitDefinition(info, selected, SupportGeneration::Never))
    {
        std::cerr << "a selected definition should be emitted under every mode except 'only'\n";
        return false;
    }
    if (llvmdsdl::shouldEmitDefinition(info, selected, SupportGeneration::Only) ||
        llvmdsdl::shouldEmitDefinition(info, empty, SupportGeneration::Only))
    {
        std::cerr << "'only' must suppress definition emission regardless of selection\n";
        return false;
    }
    if (llvmdsdl::shouldEmitDefinition(makeInfo("ns.B"), selected, SupportGeneration::AsNeeded))
    {
        std::cerr << "an unselected definition should not be emitted\n";
        return false;
    }

    // The default argument preserves the pre-option call form.
    if (!llvmdsdl::shouldEmitDefinition(info, selected))
    {
        std::cerr << "the defaulted mode should behave as 'as-needed'\n";
        return false;
    }

    return true;
}
