//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "llvmdsdl/CodeGen/ImportSet.h"

#include "UnitTests.h"

bool runImportSetTests()
{
    using llvmdsdl::ImportOrigin;
    llvmdsdl::ImportSet imports;

    // Named in the order a file happens to write them, some more than once.
    const std::string health  = imports.member(ImportOrigin::Definition, "uavcan.node.health_1_0", "Health");
    const std::string runtime = imports.member(ImportOrigin::Runtime, "pkg._runtime_loader", "runtime", "dsdl_runtime");
    const std::string field   = imports.member(ImportOrigin::Standard, "dataclasses", "field");
    const std::string sys     = imports.module(ImportOrigin::Standard, "sys", "sys");
    (void) imports.member(ImportOrigin::Standard, "dataclasses", "dataclass");
    (void) imports.member(ImportOrigin::Standard, "dataclasses", "field");
    (void) imports.member(ImportOrigin::Runtime, "pkg._runtime_loader", "error_message");
    (void) imports.member(ImportOrigin::Definition, "uavcan.node.health_1_0", "Health");

    if ((health != "Health") || (runtime != "dsdl_runtime") || (field != "field") || (sys != "sys"))
    {
        std::cerr << "import set answered a spelling other than the one it recorded\n";
        return false;
    }

    // By origin, then by path; members once each, in the order of their names.
    const auto                                              modules   = imports.modules();
    const std::vector<std::pair<ImportOrigin, std::string>> wantOrder = {
        {ImportOrigin::Standard, "dataclasses"},
        {ImportOrigin::Standard, "sys"},
        {ImportOrigin::Runtime, "pkg._runtime_loader"},
        {ImportOrigin::Definition, "uavcan.node.health_1_0"},
    };
    if (modules.size() != wantOrder.size())
    {
        std::cerr << "import set recorded " << modules.size() << " modules, want " << wantOrder.size() << "\n";
        return false;
    }
    for (std::size_t i = 0; i < modules.size(); ++i)
    {
        if ((modules[i].origin != wantOrder[i].first) || (modules[i].path != wantOrder[i].second))
        {
            std::cerr << "import set ordered " << modules[i].path << " at " << i << "\n";
            return false;
        }
    }
    const std::vector<std::pair<std::string, std::string>> wantDataclasses = {{"dataclass", "dataclass"},
                                                                              {"field", "field"}};
    const std::vector<std::pair<std::string, std::string>> wantRuntime     = {{"error_message", "error_message"},
                                                                              {"runtime", "dsdl_runtime"}};
    if ((modules[0].members != wantDataclasses) || !modules[0].binding.empty())
    {
        std::cerr << "import set recorded dataclasses' members wrongly\n";
        return false;
    }
    if ((modules[1].binding != "sys") || !modules[1].members.empty())
    {
        std::cerr << "import set recorded the module sys wrongly\n";
        return false;
    }
    if (modules[2].members != wantRuntime)
    {
        std::cerr << "import set recorded the runtime's members wrongly\n";
        return false;
    }

    // A file that names nothing from outside imports nothing.
    if (!llvmdsdl::ImportSet{}.modules().empty())
    {
        std::cerr << "an empty import set answered modules\n";
        return false;
    }
    return true;
}
