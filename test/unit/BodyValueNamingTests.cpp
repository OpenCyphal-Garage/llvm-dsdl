//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <iostream>
#include <string>

#include <llvm/ADT/StringRef.h>

#include "llvmdsdl/CodeGen/BodyTranslator.h"

#include "UnitTests.h"

namespace
{

using llvmdsdl::camelValueName;
using llvmdsdl::snakeValueName;
using llvmdsdl::ValueRole;

bool expect(const std::string& actual, const llvm::StringRef expected, const llvm::StringRef what)
{
    if (actual == expected)
    {
        return true;
    }
    std::cerr << what.str() << ": expected '" << expected.str() << "', got '" << actual << "'\n";
    return false;
}

}  // namespace

bool runBodyValueNamingTests()
{
    bool ok = true;

    // A role with no member is the role's own word; a member carries it.
    ok = expect(snakeValueName(ValueRole::Error, {}, 0), "err", "snake error") && ok;
    ok = expect(snakeValueName(ValueRole::Error, "frequency", 0), "frequency_err", "snake member error") && ok;
    ok = expect(camelValueName(ValueRole::Size, "acoustic_power", 0), "acousticPowerSize", "camel member size") && ok;

    // An ordinal past the first distinguishes a repeat in the style of its language.
    ok = expect(snakeValueName(ValueRole::Error, {}, 1), "err_2", "snake repeat") && ok;
    ok = expect(camelValueName(ValueRole::Error, {}, 2), "err3", "camel repeat") && ok;

    // A member that already trails an underscore -- a target's escape of a reserved word, or a
    // DSDL name in its own right -- must not double it: C++ reserves an identifier containing
    // '__'. A leading one is dropped for the same reason.
    ok = expect(snakeValueName(ValueRole::Scalar, "break_", 0), "break_value", "snake trailing underscore") && ok;
    ok = expect(snakeValueName(ValueRole::Scalar, "_foo", 0), "foo_value", "snake leading underscore") && ok;
    ok = expect(camelValueName(ValueRole::Scalar, "_foo", 0), "fooValue", "camel leading underscore") && ok;

    // An array's count is spelled 'count' rather than 'len' because it reads better and stays
    // free: BodySpelling::reservedLocals claims the builtin a Go or Python body calls, so a role
    // landing on one is bumped rather than capturing the call.
    ok = expect(snakeValueName(ValueRole::Length, {}, 0), "count", "snake length") && ok;
    ok = expect(camelValueName(ValueRole::Length, "name", 0), "nameCount", "camel member length") && ok;

    // The offset a plan threads through its steps: a role no operation states, stamped on the
    // structured results by build-dsdl-plan-bodies.
    ok = expect(snakeValueName(ValueRole::Offset, {}, 0), "offset", "snake offset") && ok;
    ok = expect(snakeValueName(ValueRole::Offset, {}, 1), "offset_2", "snake offset repeat") && ok;
    ok = expect(camelValueName(ValueRole::Offset, {}, 1), "offset2", "camel offset repeat") && ok;

    // An operation that says nothing about its result leaves the value to the translator.
    ok = expect(snakeValueName(ValueRole::Anonymous, "frequency", 0), "", "snake anonymous") && ok;
    ok = expect(camelValueName(ValueRole::Anonymous, "frequency", 0), "", "camel anonymous") && ok;

    return ok;
}
