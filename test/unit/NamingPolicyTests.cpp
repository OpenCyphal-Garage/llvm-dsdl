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

#include "llvmdsdl/CodeGen/NamingPolicy.h"

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

/// @brief Reproduces the private `sanitizeMacroToken` in CEmitter.cpp and CppEmitter.cpp.
///
/// Phase 2 deletes those copies in favour of the MacroName role. Until then this is the oracle the
/// role has to match, so that the migration is a deletion rather than a behavior change.
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

/// @brief Ties every role in the table to the call site it was taken from.
///
/// The role table in NamingPolicy.cpp is a claim about what the emitters do today. Phase 2 will make
/// that claim structural by routing the emitters through it; until then these assertions are what
/// keep the table honest, and they are what will fail loudly if it was recorded wrong.
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

/// @brief Records the one input where the role table and the emitter call sites disagree.
///
/// On the empty name the shared pipeline substitutes `_`, because no target accepts an empty
/// identifier; the emitter-private `sanitizeMacroToken` and the raw C/C++ header stem both hand the
/// empty string straight back. The empty name cannot come from DSDL, so this is not reachable from
/// the compiler -- but phase 2 replaces those call sites, and when it does the pipeline's answer is
/// the one that survives. This test exists so that change reads as intended rather than as a
/// regression against the equivalence assertions above.
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

    // A name that would take a reserved identifier is escaped rather than shadowing it.
    static constexpr std::array<llvm::StringRef, 2> kReserved = {"Serialize", "Deserialize"};
    NamingScope                                     reservedScope(CodegenNamingLanguage::Go, kReserved);
    if (reservedScope.declare(IdentifierRole::FieldName, "serialize") != "Serialize_2")
    {
        std::cerr << "scope did not escape a field colliding with a generated method\n";
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

    return runNamingRoleTests() && runEmptyNameDivergenceTest() && runNamingTableInvariantTests() &&
           runNamingScopeTests();
}
