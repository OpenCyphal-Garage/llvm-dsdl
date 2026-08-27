//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <array>
#include <cctype>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/Support/NamingPolicy.h"

#include "UnitTests.h"

namespace
{

using llvmdsdl::CodegenNamingLanguage;
using llvmdsdl::codegenIsKeyword;
using llvmdsdl::codegenIsReservedNamespaceIdentifier;
using llvmdsdl::codegenProjectIdentifier;
using llvmdsdl::codegenSanitizeIdentifier;
using llvmdsdl::codegenToPascalCaseIdentifier;
using llvmdsdl::codegenToSnakeCaseIdentifier;
using llvmdsdl::codegenToUpperSnakeCaseIdentifier;
using llvmdsdl::IdentifierRole;
using llvmdsdl::NamingScope;

/// @brief A spread of names that reaches every stage of the pipeline.
///
/// Two kinds of name are deliberately absent: those the generated code claims, and those landing in a
/// namespace C or C++ reserves. The case-explicit helpers the oracles below are built from apply
/// neither escape, so including one would compare a role against a function never meant to agree with
/// it. @ref runNamingClaimedNameTests and @ref runNamingReservedNamespaceTests cover them.
///
/// The empty string is deliberately absent: it is not a DSDL name, and it is the one input where the
/// shared pipeline and the emitter-private paths disagree. That divergence is pinned separately by
/// @ref runEmptyNameDivergenceTest so it is recorded rather than assumed away.
const std::vector<std::string>& sampleNames()
{
    static const std::vector<std::string> names = {"break",
                                                   "Break",
                                                   "Break_",
                                                   "map",
                                                   "class",
                                                   "fooBar",
                                                   "foo_bar",
                                                   "FooBar",
                                                   "foo_",
                                                   "OpticalFlowRate",
                                                   "VSLAMPoseUpdate",
                                                   "a",
                                                   "x9",
                                                   "9AxisIMU",
                                                   "foo bar"};
    return names;
}

constexpr std::array<CodegenNamingLanguage, 6> kAllLanguages = {CodegenNamingLanguage::C,
                                                                CodegenNamingLanguage::Cpp,
                                                                CodegenNamingLanguage::Rust,
                                                                CodegenNamingLanguage::Go,
                                                                CodegenNamingLanguage::TypeScript,
                                                                CodegenNamingLanguage::Python};

/// @brief Reference implementation of the C and C++ macro token transform.
///
/// Upper-cases and escapes without stropping. The MacroName role has to match it.
std::string emitterMacroToken(std::string token)
{
    for (char& c : token)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
        {
            c = '_';
        }
        else
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    if (!token.empty() && std::isdigit(static_cast<unsigned char>(token.front())))
    {
        token.insert(token.begin(), '_');
    }
    return token;
}

bool expectRole(const CodegenNamingLanguage language,
                const IdentifierRole        role,
                const std::string&          name,
                const std::string&          expected,
                const char* const           oracle)
{
    const std::string actual = codegenProjectIdentifier(language, role, name);
    if (actual != expected)
    {
        std::cerr << "role policy disagrees with " << oracle << " for \"" << name << "\": role gives \"" << actual
                  << "\", call site gives \"" << expected << "\"\n";
        return false;
    }
    return true;
}

/// @brief Ties every role in the table to the projection its call sites use.
///
/// A role that is recorded wrong produces identifiers no backend intended, and the emitters cannot
/// catch it themselves now that they name through the roles. These assertions can.
bool runNamingRoleTests()
{
    bool ok = true;
    for (const auto& name : sampleNames())
    {
        for (const auto language : kAllLanguages)
        {
            const bool cLike    = language == CodegenNamingLanguage::C || language == CodegenNamingLanguage::Cpp;
            const bool rustLike = language == CodegenNamingLanguage::Rust;
            const bool goLike   = language == CodegenNamingLanguage::Go;

            // Fields: C/C++/Rust keep the DSDL spelling (CEmitter.cpp, CppEmitter.cpp, RustEmitter.cpp
            // all call codegenSanitizeIdentifier); Go exports PascalCase (GoEmitter.cpp
            // toExportedIdent); TypeScript and Python fold to snake_case.
            const std::string fieldOracle = (cLike || rustLike) ? codegenSanitizeIdentifier(language, name)
                                            : goLike            ? codegenToPascalCaseIdentifier(language, name)
                                                                : codegenToSnakeCaseIdentifier(language, name);
            ok = expectRole(language, IdentifierRole::FieldName, name, fieldOracle, "the field call site") && ok;

            // Constants: C and C++ build macro tokens, the other four use the shared UPPER_SNAKE
            // constant scope (NamingScope at the ConstantName role).
            const std::string constOracle =
                cLike ? emitterMacroToken(name) : codegenToUpperSnakeCaseIdentifier(language, name);
            ok = expectRole(language, IdentifierRole::ConstantName, name, constOracle, "the constant call site") && ok;
            ok = expectRole(language, IdentifierRole::MacroName, name, constOracle, "the macro call site") && ok;

            // Namespaces: C/C++/Rust sanitize each component, Go/TypeScript/Python snake_case it
            // (GoEmitter.cpp packagePathFromComponents, renderNamespaceRelativePath).
            const std::string nsOracle = (cLike || rustLike) ? codegenSanitizeIdentifier(language, name)
                                                             : codegenToSnakeCaseIdentifier(language, name);
            ok = expectRole(language, IdentifierRole::NamespaceName, name, nsOracle, "the namespace call site") && ok;

            // File stems: C and C++ use the DSDL short name untouched (headerFileName), the other
            // four fold to snake_case (rustModuleName, goFileName, renderVersionedFileStem).
            const std::string stemOracle = cLike ? name : codegenToSnakeCaseIdentifier(language, name);
            ok = expectRole(language, IdentifierRole::FileStem, name, stemOracle, "the file stem call site") && ok;
        }
    }
    return ok;
}

/// @brief Pins what an empty name projects to.
///
/// No target accepts an empty identifier, so the pipeline substitutes `_` rather than passing one
/// through. The empty name cannot come from DSDL; the language server and library callers reach it.
bool runEmptyNameDivergenceTest()
{
    bool ok = true;
    if (codegenProjectIdentifier(CodegenNamingLanguage::C, IdentifierRole::MacroName, "") != "_")
    {
        std::cerr << "empty name: expected the pipeline to substitute _ for a macro token\n";
        ok = false;
    }
    if (codegenProjectIdentifier(CodegenNamingLanguage::C, IdentifierRole::FileStem, "") != "_")
    {
        std::cerr << "empty name: expected the pipeline to substitute _ for a file stem\n";
        ok = false;
    }
    if (!emitterMacroToken("").empty())
    {
        std::cerr << "empty name: the emitter macro token oracle changed; re-check the divergence\n";
        ok = false;
    }
    return ok;
}

/// @brief Checks that a name the generated code already claims is escaped.
///
/// Every case below was a real duplicate declaration before this existed: a C++ struct holds its
/// fields, its DSDL constants and the generated statics and methods in one scope; Go and Rust give a
/// DSDL constant the same prefix as their metadata constants. C claims the same names with the
/// trailing `_` it spells them with, since a macro token passes a source name's own trailing
/// underscore through. TypeScript and Python are absent for constants, prefixing theirs with `DSDL_`
/// while DSDL constants take a type prefix.
bool runNamingClaimedNameTests()
{
    struct Case
    {
        CodegenNamingLanguage language;
        IdentifierRole        role;
        const char*           source;
        const char*           expected;
    };

    // The Go and Rust cases start from a lower-case source on purpose: the claimed-name check has to
    // run after the upper-casing, or `full_name` would be compared as `full_name` and never match.
    static const std::array<Case, 16> kCases = {{
        {CodegenNamingLanguage::Cpp, IdentifierRole::ConstantName, "FULL_NAME", "FULL_NAME_"},
        {CodegenNamingLanguage::Cpp, IdentifierRole::ConstantName, "extent_bytes", "EXTENT_BYTES_"},
        {CodegenNamingLanguage::Cpp, IdentifierRole::FieldName, "FULL_NAME", "FULL_NAME_"},
        {CodegenNamingLanguage::Cpp, IdentifierRole::FieldName, "serialize", "serialize_"},
        {CodegenNamingLanguage::Cpp, IdentifierRole::FieldName, "try_deserialize_view", "try_deserialize_view_"},
        {CodegenNamingLanguage::Go, IdentifierRole::ConstantName, "full_name", "FULL_NAME_"},
        {CodegenNamingLanguage::Go, IdentifierRole::FieldName, "serialize", "Serialize_"},
        {CodegenNamingLanguage::Rust, IdentifierRole::ConstantName, "zoh_alias_reason", "ZOH_ALIAS_REASON_"},
        {CodegenNamingLanguage::Python, IdentifierRole::FieldName, "serialize", "serialize_"},
        {CodegenNamingLanguage::TypeScript, IdentifierRole::FieldName, "constructor", "constructor_"},
        // C spells its metadata macros with a trailing underscore, so `FULL_NAME` is free and
        // `FULL_NAME_` is the one that has to move -- as does the lower-case spelling of it, since a
        // macro token is upper-cased before the claim is checked.
        {CodegenNamingLanguage::C, IdentifierRole::ConstantName, "FULL_NAME", "FULL_NAME"},
        {CodegenNamingLanguage::C, IdentifierRole::ConstantName, "FULL_NAME_", "FULL_NAME__"},
        {CodegenNamingLanguage::C, IdentifierRole::ConstantName, "full_name_", "FULL_NAME__"},
        // The generated `try_deserialize_view_` tests this one in an `#elif`, so a redefinition
        // changes what the generated code does rather than only what it reports.
        {CodegenNamingLanguage::C, IdentifierRole::ConstantName, "zoh_alias_eligible_", "ZOH_ALIAS_ELIGIBLE__"},
        // `MacroName` and `ConstantName` name the same thing in C and are claimed alike.
        {CodegenNamingLanguage::C, IdentifierRole::MacroName, "union_option_count_", "UNION_OPTION_COUNT__"},
        // A C macro token is not an identifier in the language namespace, so keywords are left alone.
        {CodegenNamingLanguage::C, IdentifierRole::ConstantName, "break", "BREAK"},
    }};

    bool ok = true;
    for (const auto& c : kCases)
    {
        const std::string actual = codegenProjectIdentifier(c.language, c.role, c.source);
        if (actual != c.expected)
        {
            std::cerr << "claimed-name escape mismatch for \"" << c.source << "\": got \"" << actual
                      << "\", expected \"" << c.expected << "\"\n";
            ok = false;
        }
    }
    return ok;
}

/// @brief Checks that a name landing in a language's reserved namespace is encoded.
///
/// C reserves identifiers beginning `__` or `_` plus a capital; C++ reserves those and any identifier
/// containing `__` anywhere. A trailing `_` repairs none of them, so the offending underscores are
/// encoded, which is injective and needs no scope to disambiguate afterwards. Whether a definition
/// needing this is *accepted* is the driver's decision, not the engine's.
bool runNamingReservedNamespaceTests()
{
    struct Case
    {
        CodegenNamingLanguage language;
        IdentifierRole        role;
        const char*           source;
        const char*           expected;
    };

    static const std::array<Case, 10> kCases = {{
        {CodegenNamingLanguage::C, IdentifierRole::FieldName, "_Foo", "zX005FFoo"},
        {CodegenNamingLanguage::C, IdentifierRole::FieldName, "__bar", "zX005FzX005Fbar"},
        // C reserves a leading underscore before a capital, so the upper-casing a constant gets is
        // what puts `_foo` in the reserved namespace; as a field it stays put.
        {CodegenNamingLanguage::C, IdentifierRole::FieldName, "_foo", "_foo"},
        {CodegenNamingLanguage::C, IdentifierRole::ConstantName, "_foo", "zX005FFOO"},
        // A double underscore inside an identifier is ordinary in C and reserved in C++.
        {CodegenNamingLanguage::C, IdentifierRole::FieldName, "foo__bar", "foo__bar"},
        {CodegenNamingLanguage::Cpp, IdentifierRole::FieldName, "foo__bar", "foozX005FzX005Fbar"},
        {CodegenNamingLanguage::Cpp, IdentifierRole::FieldName, "_Foo", "zX005FFoo"},
        // No other language reserves a namespace of this kind.
        {CodegenNamingLanguage::Go, IdentifierRole::FieldName, "__bar", "Bar"},
        {CodegenNamingLanguage::Rust, IdentifierRole::FieldName, "__bar", "__bar"},
        {CodegenNamingLanguage::Python, IdentifierRole::FieldName, "__bar", "bar"},
    }};

    bool ok = true;
    for (const auto& c : kCases)
    {
        const auto projected = codegenProjectIdentifierDetailed(c.language, c.role, c.source);
        if (projected.identifier != c.expected)
        {
            std::cerr << "reserved-namespace encoding mismatch for \"" << c.source << "\": got \""
                      << projected.identifier << "\", expected \"" << c.expected << "\"\n";
            ok = false;
        }
    }
    return ok;
}

/// @brief Checks the table properties the one-pass strop depends on.
///
/// Section 4.1 of docs/development/identifier-stropping.md argues that appending `_` terminates in
/// one step because the result is never something that has to be escaped again. That is a property
/// of the tables, not of the algorithm, so it is asserted here rather than assumed there.
bool runNamingTableInvariantTests()
{
    bool ok = true;
    for (const auto language : kAllLanguages)
    {
        for (const auto& name : sampleNames())
        {
            const std::string once  = codegenSanitizeIdentifier(language, name);
            const std::string twice = codegenSanitizeIdentifier(language, once);
            if (once != twice)
            {
                std::cerr << "stropping is not a fixpoint for \"" << name << "\": " << once << " -> " << twice << "\n";
                ok = false;
            }
        }
    }

    // The single-pass escape in runPipeline appends `_` once and asserts the result is clean. That is
    // sound only while no keyword ends in `_` -- otherwise escaping one could land on another. The
    // claim is about every keyword in every table, so it is asked of every keyword in every table
    // rather than of names a test author happened to think of.
    for (const auto language : kAllLanguages)
    {
        for (const auto& keyword : codegenNamingPolicy(language).keywords())
        {
            if (keyword.ends_with("_"))
            {
                std::cerr << "keyword \"" << keyword.str() << "\" ends in an underscore, which breaks the "
                          << "single-pass escape\n";
                ok = false;
            }
            if (codegenIsKeyword(language, keyword.str() + "_"))
            {
                std::cerr << "keyword \"" << keyword.str() << "\" escapes to another keyword\n";
                ok = false;
            }
        }
        // The same has to hold for the names the generated code claims, which take the same escape.
        // Ending in `_` is not itself the problem -- C's metadata macros are spelled that way and
        // have to be claimed as spelled. What must not happen is the escape landing somewhere it
        // then has to escape again, or somewhere the language reserves.
        for (const auto role : {IdentifierRole::FieldName, IdentifierRole::ConstantName, IdentifierRole::MacroName})
        {
            const auto owned_names = codegenNamingPolicy(language).runtimeOwned(role);
            for (const auto& owned : owned_names)
            {
                const std::string escaped = owned.str() + "_";
                if (llvm::is_contained(owned_names, llvm::StringRef(escaped)))
                {
                    std::cerr << "claimed name \"" << owned.str() << "\" escapes onto another claimed name\n";
                    ok = false;
                }
                if (codegenIsKeyword(language, escaped))
                {
                    std::cerr << "claimed name \"" << owned.str() << "\" escapes onto a keyword, which the "
                              << "keyword stage has already run past\n";
                    ok = false;
                }
                if (codegenIsReservedNamespaceIdentifier(language, escaped))
                {
                    std::cerr << "claimed name \"" << owned.str() << "\" escapes into a reserved namespace\n";
                    ok = false;
                }
            }
        }
    }
    return ok;
}

/// @brief Checks that a scope's assignment is injective over generated name sets.
///
/// The engine's contract is that distinct DSDL names in one scope get distinct identifiers: the case
/// projections are many-to-one, and the scope is what repairs them. A handful of hand-picked names
/// cannot establish that, so the names are generated -- from the pieces that actually collide, which
/// is case variation, underscore placement, and the escapes.
bool runNamingInjectivityTests()
{
    // A deterministic sequence: a fixed seed makes a failure reproducible, and nothing here depends
    // on the values being unpredictable.
    std::mt19937 rng(20260820U);

    static const std::array<llvm::StringRef, 12> kPieces =
        {"foo", "Foo", "FOO", "bar", "Bar", "_", "__", "break", "Break", "serialize", "full", "name"};

    bool ok = true;
    for (unsigned round = 0; round < 200U; ++round)
    {
        // Distinct sources: the contract says nothing about a scope handed the same name twice, and
        // the frontend rejects duplicate attribute names before codegen sees them.
        std::set<std::string> sources;
        const unsigned        count = 2U + (rng() % 7U);
        while (sources.size() < count)
        {
            std::string    name;
            const unsigned pieces = 1U + (rng() % 3U);
            for (unsigned i = 0; i < pieces; ++i)
            {
                name += kPieces[rng() % kPieces.size()].str();
            }
            // Only names DSDL would accept: it requires a leading letter or underscore, and rejects
            // anything both starting and ending with one.
            if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front())) != 0)
            {
                continue;
            }
            if (name.front() == '_' && name.back() == '_')
            {
                continue;
            }
            sources.insert(name);
        }
        const std::vector<std::string> ordered(sources.begin(), sources.end());

        for (const auto language : kAllLanguages)
        {
            for (const auto role : {IdentifierRole::FieldName, IdentifierRole::ConstantName})
            {
                NamingScope                        scope(language);
                std::map<std::string, std::string> assigned;
                for (const auto& source : ordered)
                {
                    const std::string identifier = scope.declare(role, source);
                    const auto [it, inserted]    = assigned.emplace(identifier, source);
                    if (!inserted)
                    {
                        std::cerr << "scope is not injective: \"" << source << "\" and \"" << it->second
                                  << "\" both became \"" << identifier << "\"\n";
                        ok = false;
                    }
                    // Distinctness is not enough. The repair suffix is appended after the pipeline
                    // has run, so it is the one part of an identifier the reserved-namespace stage
                    // never sees; a scope that emits a name the language reserves is as broken as
                    // one that emits a duplicate, and the engine rejects such a name on input.
                    if (codegenIsReservedNamespaceIdentifier(language, identifier))
                    {
                        std::cerr << "scope produced a reserved identifier: \"" << source << "\" became \""
                                  << identifier << "\"\n";
                        ok = false;
                    }
                }
            }
        }
    }
    return ok;
}

/// @brief Checks that a scope keeps its assignments injective and stable.
bool runNamingScopeTests()
{
    bool ok = true;

    // Three names that fold together in Go must come back as three identifiers, in declaration order.
    NamingScope       goScope(CodegenNamingLanguage::Go);
    const std::string first  = goScope.declare(IdentifierRole::FieldName, "fooBar");
    const std::string second = goScope.declare(IdentifierRole::FieldName, "foo_bar");
    const std::string third  = goScope.declare(IdentifierRole::FieldName, "FooBar");
    if (first != "FooBar" || second != "FooBar_2" || third != "FooBar_3")
    {
        std::cerr << "scope allocation mismatch: " << first << ", " << second << ", " << third << "\n";
        ok = false;
    }

    // Re-declaring is idempotent, and get() agrees with declare().
    if (goScope.declare(IdentifierRole::FieldName, "foo_bar") != second ||
        goScope.get(IdentifierRole::FieldName, "foo_bar") != second)
    {
        std::cerr << "scope re-declaration is not idempotent\n";
        ok = false;
    }

    // A name claimed by a particular scope is escaped rather than shadowing it. Names the backend
    // claims for *every* type are policy instead, and are covered by runNamingClaimedNameTests.
    static constexpr std::array<llvm::StringRef, 1> kReserved = {"Extra"};
    NamingScope                                     reservedScope(CodegenNamingLanguage::Go, kReserved);
    if (reservedScope.declare(IdentifierRole::FieldName, "extra") != "Extra_2")
    {
        std::cerr << "scope did not escape a field colliding with a scope-reserved name\n";
        ok = false;
    }

    // Two roles in one scope share the pool: a Go field and a Go method cannot both be `Value`.
    NamingScope sharedScope(CodegenNamingLanguage::Go);
    if (sharedScope.declare(IdentifierRole::FieldName, "value") != "Value" ||
        sharedScope.declare(IdentifierRole::FunctionName, "value") != "Value_2")
    {
        std::cerr << "roles in one scope did not share the identifier pool\n";
        ok = false;
    }
    return ok;
}

}  // namespace

bool runNamingPolicyTests()
{
    using llvmdsdl::CodegenNamingLanguage;
    using llvmdsdl::codegenSanitizeIdentifier;
    using llvmdsdl::codegenToPascalCaseIdentifier;
    using llvmdsdl::codegenToSnakeCaseIdentifier;
    using llvmdsdl::codegenToUpperSnakeCaseIdentifier;

    if (codegenSanitizeIdentifier(CodegenNamingLanguage::TypeScript, "class") != "class_")
    {
        std::cerr << "TypeScript keyword sanitization mismatch\n";
        return false;
    }
    if (codegenSanitizeIdentifier(CodegenNamingLanguage::Python, "def") != "def_")
    {
        std::cerr << "Python keyword sanitization mismatch\n";
        return false;
    }
    if (codegenSanitizeIdentifier(CodegenNamingLanguage::Rust, "self") != "self_")
    {
        std::cerr << "Rust keyword sanitization mismatch\n";
        return false;
    }
    if (codegenSanitizeIdentifier(CodegenNamingLanguage::Go, "map") != "map_")
    {
        std::cerr << "Go keyword sanitization mismatch\n";
        return false;
    }
    if (codegenSanitizeIdentifier(CodegenNamingLanguage::Cpp, "namespace") != "namespace_")
    {
        std::cerr << "C++ keyword sanitization mismatch\n";
        return false;
    }
    if (codegenSanitizeIdentifier(CodegenNamingLanguage::C, "int") != "int_")
    {
        std::cerr << "C keyword sanitization mismatch\n";
        return false;
    }

    if (codegenToSnakeCaseIdentifier(CodegenNamingLanguage::TypeScript, "FlightControlMode") != "flight_control_mode")
    {
        std::cerr << "snake_case projection mismatch\n";
        return false;
    }
    if (codegenToSnakeCaseIdentifier(CodegenNamingLanguage::Python, "9AxisIMU") != "_9axis_imu")
    {
        std::cerr << "snake_case digit-prefix projection mismatch\n";
        return false;
    }
    if (codegenToPascalCaseIdentifier(CodegenNamingLanguage::Python, "vslam_pose_update") != "VslamPoseUpdate")
    {
        std::cerr << "PascalCase projection mismatch\n";
        return false;
    }
    if (codegenToUpperSnakeCaseIdentifier(CodegenNamingLanguage::Go, "OpticalFlowRate") != "OPTICAL_FLOW_RATE")
    {
        std::cerr << "UPPER_SNAKE_CASE projection mismatch\n";
        return false;
    }

    // Each result is collected rather than short-circuited: `&&` would let the first failure hide
    // every later one, and a naming change usually breaks more than one property at a time.
    bool ok = true;
    ok      = runNamingRoleTests() && ok;
    ok      = runEmptyNameDivergenceTest() && ok;
    ok      = runNamingClaimedNameTests() && ok;
    ok      = runNamingReservedNamespaceTests() && ok;
    ok      = runNamingTableInvariantTests() && ok;
    ok      = runNamingScopeTests() && ok;
    ok      = runNamingInjectivityTests() && ok;
    return ok;
}
