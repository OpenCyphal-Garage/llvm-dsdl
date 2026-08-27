//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "llvmdsdl/LSP/DocumentStore.h"

#include "UnitTests.h"

bool runLspDocumentStoreTests()
{
    llvmdsdl::lsp::DocumentStore store;
    store.open("file:///tmp/demo.dsdl", "uint8 value\n", 1);

    if (store.size() != 1)
    {
        std::cerr << "expected one open document after didOpen\n";
        return false;
    }

    const auto firstSnapshot = store.lookup("file:///tmp/demo.dsdl");
    if (!firstSnapshot || firstSnapshot->text != "uint8 value\n" || firstSnapshot->version != 1)
    {
        std::cerr << "unexpected snapshot after didOpen\n";
        return false;
    }

    if (!store.applyFullTextChange("file:///tmp/demo.dsdl", "uint16 value\n", 2))
    {
        std::cerr << "expected didChange to update existing document\n";
        return false;
    }

    const auto updatedSnapshot = store.lookup("file:///tmp/demo.dsdl");
    if (!updatedSnapshot || updatedSnapshot->text != "uint16 value\n" || updatedSnapshot->version != 2)
    {
        std::cerr << "unexpected snapshot after didChange\n";
        return false;
    }

    if (store.applyFullTextChange("file:///tmp/missing.dsdl", "bad\n", 1))
    {
        std::cerr << "didChange on missing document should fail\n";
        return false;
    }

    if (!store.close("file:///tmp/demo.dsdl"))
    {
        std::cerr << "expected didClose to remove existing document\n";
        return false;
    }

    if (store.lookup("file:///tmp/demo.dsdl").has_value() || store.size() != 0)
    {
        std::cerr << "document should be removed after didClose\n";
        return false;
    }

    // Concurrency: hammer the store from multiple threads. With the internal mutex this must not
    // race or crash. Because lookup()/snapshots() return value copies, a reader can never hold a
    // pointer into the map while another thread erases/rehashes it (a use-after-free the old
    // raw-pointer API allowed). Exercised for real under the ASan/UBSan sanitizer lanes.
    {
        llvmdsdl::lsp::DocumentStore shared;
        constexpr int                kThreads      = 8;
        constexpr int                kIterations   = 4000;
        constexpr int                kDistinctDocs = 4;
        std::atomic<bool>            start{false};
        std::vector<std::thread>     threads;
        threads.reserve(kThreads);
        for (int t = 0; t < kThreads; ++t)
        {
            threads.emplace_back([&shared, &start, t]() {
                while (!start.load(std::memory_order_acquire))
                {
                }
                const std::string uri = "file:///doc" + std::to_string(t % kDistinctDocs) + ".dsdl";
                for (int i = 0; i < kIterations; ++i)
                {
                    switch (i % 5)
                    {
                    case 0:
                        shared.open(uri, "uint8 v\n", i);
                        break;
                    case 1:
                        (void) shared.applyFullTextChange(uri, "uint16 value\n", i);
                        break;
                    case 2: {
                        const auto snap = shared.lookup(uri);
                        (void) (snap && !snap->text.empty());
                        break;
                    }
                    case 3:
                        (void) shared.snapshots().size();
                        break;
                    default:
                        (void) shared.close(uri);
                        break;
                    }
                }
            });
        }
        start.store(true, std::memory_order_release);
        for (auto& thread : threads)
        {
            thread.join();
        }
        // At most one entry per distinct URI can remain; the store must be internally consistent.
        if (shared.size() > static_cast<std::size_t>(kDistinctDocs))
        {
            std::cerr << "document store size inconsistent after concurrent access\n";
            return false;
        }
    }

    return true;
}
