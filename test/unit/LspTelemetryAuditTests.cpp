//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Regression tests for the `dsdld` telemetry + AI-audit memory bounds.
///
/// Both surfaces ingest attacker-controlled strings from JSON-RPC (any method
/// name reaches Telemetry; serialized tool arguments reach the audit log). These
/// tests pin the two DoS bounds found in the LSP data-flow audit: the telemetry
/// distinct-key cap and the audit per-record detail cap.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/LSP/AI.h"
#include "llvmdsdl/LSP/Telemetry.h"

#include <iostream>
#include <string>

namespace
{

bool fail(const char* message)
{
    std::cerr << "LspTelemetryAuditTests: " << message << '\n';
    return false;
}

// Telemetry must not grow its distinct-method map without bound: unknown method
// names past the cap aggregate under a single "<other>" key.
bool telemetryDistinctMethodCapTest()
{
    llvmdsdl::lsp::Telemetry telemetry;

    // A real method recorded before the flood must survive and keep counting.
    telemetry.record("initialize", 10, false);
    telemetry.record("initialize", 10, false);

    // Flood with far more distinct (garbage) method names than the internal cap.
    for (int i = 0; i < 5000; ++i)
    {
        telemetry.record("garbage_" + std::to_string(i), 1, false);
    }

    if (telemetry.requestCount("initialize") != 2U)
    {
        return fail("real method telemetry was displaced by the garbage flood");
    }
    // The vast majority of distinct garbage keys cannot have been retained; overflow
    // must have been bucketed. If every garbage key were retained, "<other>" would be 0.
    if (telemetry.requestCount("<other>") == 0U)
    {
        return fail("telemetry did not bucket overflow methods into '<other>' (map is unbounded)");
    }
    // A late garbage key (well past the cap) must NOT have its own retained counter.
    if (telemetry.requestCount("garbage_4999") != 0U)
    {
        return fail("a post-cap distinct method retained its own key (map is unbounded)");
    }
    return true;
}

// The AI audit log must cap per-record detail so a large request cannot amplify
// into unbounded retained memory, while still redacting secrets.
bool auditDetailCapAndRedactionTest()
{
    llvmdsdl::lsp::AiAuditLogger logger;

    // Oversized detail with an embedded secret.
    std::string huge = "token=supersecret ";
    huge += std::string(1'000'000, 'A');
    logger.record("tool_use", huge);

    const std::vector<llvmdsdl::lsp::AiAuditRecord> records = logger.snapshot();
    if (records.size() != 1U)
    {
        return fail("expected exactly one audit record");
    }
    const std::string& detail = records.front().detail;
    if (detail.size() > 4096U + 32U)  // MaxDetailBytes + truncation marker slack
    {
        return fail("audit detail was not capped (unbounded audit memory)");
    }
    if (detail.find("supersecret") != std::string::npos)
    {
        return fail("audit detail leaked an unredacted secret");
    }
    if (detail.find("[truncated]") == std::string::npos)
    {
        return fail("oversized audit detail was not marked truncated");
    }
    return true;
}

}  // namespace

bool runLspTelemetryAuditTests()
{
    return telemetryDistinctMethodCapTest() && auditDetailCapAndRedactionTest();
}
