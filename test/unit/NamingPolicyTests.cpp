//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <array>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/Support/NamingPolicy.h"

namespace
{

using llvmdsdl::CodegenNamingLanguage;
using llvmdsdl::codegenIsKeyword;
using llvmdsdl::codegenProjectIdentifier;
using llvmdsdl::codegenSanitizeIdentifier;
using llvmdsdl::codegenToPascalCaseIdentifier;
using llvmdsdl::codegenToSnakeCaseIdentifier;
using llvmdsdl::codegenToUpperSnakeCaseIdentifier;
using llvmdsdl::IdentifierRole;
using llvmdsdl::NamingScope;

/// @brief A spread of names that reaches every stage of the pipeline.
///
/// Names the generated code claims are deliberately absent too: the case-explicit helpers the oracles
/// below are built from do not apply that escape, so including one would compare a role against a
/// function that was never meant to agree with it. @ref runNamingClaimedNameTests covers those.
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
                                                   "_foo",
                                                   "foo_",
                                                   "__bar",
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
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
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
/// DSDL constant the same prefix as their metadata constants. C is absent on purpose -- it suffixes
/// its metadata macros with `_` -- and so are TypeScript and Python for constants, which prefix
/// theirs with `DSDL_` while DSDL constants take a type prefix.
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
    static const std::array<Case, 12> kCases = {{
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
        // Not claimed: C metadata macros carry a trailing underscore, so nothing needs escaping.
        {CodegenNamingLanguage::C, IdentifierRole::ConstantName, "FULL_NAME", "FULL_NAME"},
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

/// @brief Checks the table properties the one-pass strop depends on.
///
/// Section 4.3 of docs/development/identifier-stropping.md argues that appending `_` terminates in
/// one step because no keyword ends in `_`. That is a property of the tables, not of the algorithm,
/// so it is asserted here rather than assumed there.
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

    // Every keyword, escaped once, must not be a keyword again -- otherwise the single-pass strop in
    // runPipeline would leave an illegal identifier behind.
    static const std::vector<std::string> probes = {"break",
                                                    "class",
                                                    "map",
                                                    "def",
                                                    "func",
                                                    "impl",
                                                    "let",
                                                    "match",
                                                    "type",
                                                    "typedef",
                                                    "yield",
                                                    "namespace",
                                                    "self",
                                                    "export",
                                                    "range",
                                                    "chan",
                                                    "lambda",
                                                    "operator"};
    for (const auto language : kAllLanguages)
    {
        for (const auto& probe : probes)
        {
            if (codegenIsKeyword(language, probe) && codegenIsKeyword(language, probe + "_"))
            {
                std::cerr << "keyword \"" << probe << "\" escapes to another keyword\n";
                ok = false;
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

    return runNamingRoleTests() && runEmptyNameDivergenceTest() && runNamingClaimedNameTests() &&
           runNamingTableInvariantTests() && runNamingScopeTests();
}
