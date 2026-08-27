//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Freezes the identifier naming map produced by the current backend naming helpers.
///
/// Generated identifiers are the ABI between generated code and hand-written code, so a diff in the
/// golden map is an ABI change. The map makes "this change moved no identifier" checkable; it does
/// not assert that any particular mapping is the right one.
///
/// Regenerate after an intended change:
/// @code
///   LLVMDSDL_UPDATE_NAMING_GOLDEN=1 <build>/test/unit/llvmdsdl-unit-tests
/// @endcode
///
//===----------------------------------------------------------------------===//

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/CodeGen/DefinitionPathProjection.h"
#include "llvmdsdl/Support/NamingPolicy.h"
#include "llvmdsdl/Support/ReservedIdentifiers.h"

#include "UnitTests.h"

namespace
{

using llvmdsdl::NamingScope;
using llvmdsdl::CodegenNamingLanguage;
using llvmdsdl::codegenNamingPolicy;
using llvmdsdl::codegenProjectIdentifier;
using llvmdsdl::codegenSanitizeIdentifier;
using llvmdsdl::codegenToPascalCaseIdentifier;
using llvmdsdl::codegenToSnakeCaseIdentifier;
using llvmdsdl::codegenToUpperSnakeCaseIdentifier;
using llvmdsdl::CaseStyle;
using llvmdsdl::IdentifierRole;
using llvmdsdl::isReservedIdentifier;
using llvmdsdl::renderVersionedFileStem;
using llvmdsdl::renderVersionedTypeName;

struct LanguageEntry
{
    CodegenNamingLanguage language;
    const char*           name;
};

/// @brief Every target language, in a fixed order so the golden map is stable.
constexpr std::array<LanguageEntry, 6> kLanguages = {{
    {CodegenNamingLanguage::C, "c"},
    {CodegenNamingLanguage::Cpp, "cpp"},
    {CodegenNamingLanguage::Rust, "rust"},
    {CodegenNamingLanguage::Go, "go"},
    {CodegenNamingLanguage::TypeScript, "ts"},
    {CodegenNamingLanguage::Python, "python"},
}};

/// @brief The adversarial name corpus.
///
/// Drawn from the reachability analysis in the design note: names that are legal DSDL but collide
/// with a keyword in at least one target, names whose case projections collide with each other,
/// names that land in a language's reserved identifier namespace, and a short tail of names that
/// DSDL itself rejects -- those pin the defensive paths that the compiler never reaches but the
/// language server and library callers can.
const std::vector<std::string>& corpus()
{
    static const std::vector<std::string> names = {
        // Keywords reachable through a snake_case projection, spread across the targets.
        "break",
        "Break",
        "Break_",
        "map",
        "Map_",
        "class",
        "Class_",
        "match",
        "Match_",
        "def",
        "func",
        "interface",
        "let",
        "mut",
        "pub",
        "use",
        "string",
        "null",
        "None",
        "range",
        "defer",
        "chan",
        "impl",
        "loop",
        "yield",
        "lambda",
        "except",
        "typedef",
        "register",
        "operator",
        "namespace",
        "typename",
        "delete",
        "export",
        "package",
        "import",
        // Case-fold family: four distinct DSDL names, one snake form and one Pascal form.
        "fooBar",
        "foo_bar",
        "FooBar",
        "FOO_BAR",
        // Underscore shapes and the C/C++ reserved identifier namespace.
        "_foo",
        "__foo",
        "_Foo",
        "foo_",
        "Foo_",
        "foo_t",
        // The names generated code claims, in the spellings that reach them. C spells its metadata as
        // macros with a trailing `_`, which is a name a DSDL constant can carry.
        "FULL_NAME",
        "FULL_NAME_",
        "zoh_alias_eligible_",
        "UNION_OPTION_COUNT",
        "Efoo",
        "EFOO",
        // Acronym folding.
        "OpticalFlowRate",
        "VSLAMPoseUpdate",
        "IMUData",
        "HTTPServer2Foo",
        // Degenerate but legal.
        "a",
        "A",
        "x9",
        // Rejected by DSDL section 3.2.5 -- present to show the reachability column works.
        "self",
        "type",
        // Rejected by the DSDL charset -- these pin defensive behavior only.
        "9AxisIMU",
        "foo bar",
        "I\xe2\x9d\xa4"
        "c",
    };
    return names;
}

/// @brief True when @p name can appear in a conformant DSDL definition.
///
/// The charset rule mirrors `isValidNameComponent` in lib/Frontend/Discovery.cpp; the reserved
/// identifier rule is the shared implementation of DSDL v1.0 section 3.2.5.
bool isDsdlReachable(const std::string& name)
{
    if (name.empty())
    {
        return false;
    }
    const auto first = static_cast<unsigned char>(name.front());
    if (!std::isalpha(first) && name.front() != '_')
    {
        return false;
    }
    for (const char c : name)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
        {
            return false;
        }
    }
    return !isReservedIdentifier(name);
}

/// @brief Renders @p text for a fixed-width column: bare when it is identifier-shaped, otherwise a
///        quoted literal with non-printable and non-ASCII bytes escaped.
std::string quoted(const std::string& text)
{
    bool bare = !text.empty();
    for (const char c : text)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
        {
            bare = false;
            break;
        }
    }
    if (bare)
    {
        return text;
    }

    std::string out = "\"";
    for (const char c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20U || byte > 0x7EU)
        {
            static const char* const kHex = "0123456789abcdef";
            out += "\\x";
            out += kHex[byte >> 4U];
            out += kHex[byte & 0x0FU];
        }
        else if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else
        {
            out += c;
        }
    }
    out += '"';
    return out;
}

std::string pad(const std::string& text, const std::size_t width)
{
    std::string out = text;
    while (out.size() < width)
    {
        out.push_back(' ');
    }
    return out;
}

/// @brief Writes one table row, padding every cell but the last so no line carries trailing
///        whitespace (which a formatter would strip, breaking the comparison).
void appendRow(std::ostringstream& out, const std::vector<std::pair<std::string, std::size_t>>& cells)
{
    for (std::size_t i = 0; i < cells.size(); ++i)
    {
        out << ((i + 1 == cells.size()) ? cells[i].first : pad(cells[i].first, cells[i].second));
    }
    out << "\n";
}

/// @brief One named projection under test.
struct Projection
{
    const char* name;
    std::string (*apply)(CodegenNamingLanguage, const std::string&);
};

std::string applySanitize(const CodegenNamingLanguage language, const std::string& name)
{
    return codegenSanitizeIdentifier(language, name);
}
std::string applySnake(const CodegenNamingLanguage language, const std::string& name)
{
    return codegenToSnakeCaseIdentifier(language, name);
}
std::string applyPascal(const CodegenNamingLanguage language, const std::string& name)
{
    return codegenToPascalCaseIdentifier(language, name);
}
std::string applyUpperSnake(const CodegenNamingLanguage language, const std::string& name)
{
    return codegenToUpperSnakeCaseIdentifier(language, name);
}
std::string applyFileStem(const CodegenNamingLanguage language, const std::string& name)
{
    return renderVersionedFileStem(language, name, 1, 0);
}
std::string applyTypeName(const CodegenNamingLanguage language, const std::string& name)
{
    return renderVersionedTypeName(language, name, 1, 0);
}

/// @brief True when @p language's backend derives output file names from the shared projection.
///
/// Rust (RustEmitter.cpp `rustModuleName`), Go (GoEmitter.cpp `goFileName`), TypeScript and Python
/// (both via `renderVersionedFileStem`) all fold the short name to snake_case first. C and C++ use
/// the DSDL short name verbatim (CEmitter.cpp `headerFileName`, CppEmitter.cpp `headerFileName`),
/// so no two distinct type names can share a header and the column would say nothing about them.
bool usesSharedFileStem(const CodegenNamingLanguage language)
{
    return language == CodegenNamingLanguage::Rust || language == CodegenNamingLanguage::Go ||
           language == CodegenNamingLanguage::TypeScript || language == CodegenNamingLanguage::Python;
}

/// @brief True when @p language's backend derives the generated type name from the shared
///        projection: PascalCase short name plus version.
///
/// Go (`goTypeName`), TypeScript and Python (`renderVersionedTypeName`). C, C++ and Rust build a
/// namespace-qualified symbol instead (CEmitter.cpp `mangleSymbol`, CppEmitter.cpp `cppTypeName`,
/// RustEmitter.cpp `rustTypeName`), which cannot collide across namespaces.
bool usesSharedTypeName(const CodegenNamingLanguage language)
{
    return language == CodegenNamingLanguage::Go || language == CodegenNamingLanguage::TypeScript ||
           language == CodegenNamingLanguage::Python;
}

/// @brief Whether @p projection describes what @p language's backend actually emits.
bool projectionApplies(const std::string& projection, const CodegenNamingLanguage language)
{
    if (projection == "file_stem")
    {
        return usesSharedFileStem(language);
    }
    if (projection == "type_name")
    {
        return usesSharedTypeName(language);
    }
    return true;
}

constexpr std::array<Projection, 6> kProjections = {{
    {"sanitize", &applySanitize},
    {"snake", &applySnake},
    {"pascal", &applyPascal},
    {"upper_snake", &applyUpperSnake},
    {"file_stem", &applyFileStem},
    {"type_name", &applyTypeName},
}};

void appendHeader(std::ostringstream& out)
{
    out << "# llvm-dsdl identifier naming map (golden)\n"
        << "#\n"
        << "# Generated by test/unit/NamingGoldenTests.cpp. Do not edit by hand.\n"
        << "# Regenerate:  LLVMDSDL_UPDATE_NAMING_GOLDEN=1 <build>/test/unit/llvmdsdl-unit-tests\n"
        << "#\n"
        << "# A diff in this file changes identifiers in generated code and is therefore an ABI\n"
        << "# change: it belongs in a commit that says so.\n"
        << "#\n"
        << "# reachable  whether the source name can appear in a conformant DSDL definition (charset\n"
        << "#            plus DSDL v1.0 section 3.2.5). Rows marked 'no' pin defensive behavior that\n"
        << "#            the compiler never reaches, but the language server and library callers do.\n"
        << "# file_stem  and type_name come from DefinitionPathProjection and are shown for version\n"
        << "#            1.0. They read '-' for a backend that derives that name elsewhere: C and C++\n"
        << "#            name headers after the raw DSDL short name, and C, C++ and Rust build\n"
        << "#            namespace-qualified type symbols.\n";
}

void appendProjections(std::ostringstream& out)
{
    out << "\n== projections ==\n\n";
    std::vector<std::pair<std::string, std::size_t>> header = {{"source", 22}, {"reach", 7}, {"language", 9}};
    for (const auto& projection : kProjections)
    {
        header.emplace_back(projection.name, 23);
    }
    appendRow(out, header);
    out << std::string(22 + 7 + 9 + (23 * (kProjections.size() - 1)), '-') << "\n";

    for (const auto& source : corpus())
    {
        const char* reach = isDsdlReachable(source) ? "yes" : "no";
        for (const auto& entry : kLanguages)
        {
            std::vector<std::pair<std::string, std::size_t>> cells = {{quoted(source), 22},
                                                                      {reach, 7},
                                                                      {entry.name, 9}};
            for (const auto& projection : kProjections)
            {
                cells.emplace_back(projectionApplies(projection.name, entry.language)
                                       ? quoted(projection.apply(entry.language, source))
                                       : "-",
                                   23);
            }
            appendRow(out, cells);
        }
    }
}

/// @brief Names what keeps a collision group out of generated code.
///
/// Output-path names cannot be repaired: choosing which of two types to rename would depend on
/// discovery order, so the frontend rejects the pair. Names inside a scope are repaired in place.
std::string guardFor(const std::string& projection, const std::vector<std::string>& /*sources*/)
{
    return (projection == "file_stem" || projection == "type_name") ? "rejected" : "allocator";
}

void appendCollisions(std::ostringstream& out)
{
    out << "\n== collisions ==\n"
        << "#\n"
        << "# Distinct DSDL-reachable sources that project onto one identifier. Unreachable sources\n"
        << "# are excluded: a clash they cause is not a hazard the compiler can meet.\n"
        << "#\n"
        << "# guard: what stops the collision from reaching generated code.\n"
        << "#   allocator  an in-scope name; the scope appends _2 (see naming-roles.txt).\n"
        << "#   rejected   an output file name or type name; the frontend rejects the pair, because\n"
        << "#              choosing which type to rename would depend on discovery order.\n\n";
    appendRow(out, {{"language", 9}, {"projection", 13}, {"guard", 11}, {"identifier", 20}, {"sources", 0}});
    out << std::string(9 + 13 + 11 + 20 + 40, '-') << "\n";

    for (const auto& entry : kLanguages)
    {
        for (const auto& projection : kProjections)
        {
            if (!projectionApplies(projection.name, entry.language))
            {
                continue;
            }
            std::map<std::string, std::vector<std::string>> groups;
            for (const auto& source : corpus())
            {
                if (isDsdlReachable(source))
                {
                    groups[projection.apply(entry.language, source)].push_back(source);
                }
            }
            for (const auto& [identifier, sources] : groups)
            {
                if (sources.size() < 2U)
                {
                    continue;
                }
                std::string joined;
                for (std::size_t i = 0; i < sources.size(); ++i)
                {
                    joined += (i > 0 ? ", " : "") + quoted(sources[i]);
                }
                appendRow(out,
                          {{entry.name, 9},
                           {projection.name, 13},
                           {guardFor(projection.name, sources), 11},
                           {quoted(identifier), 20},
                           {joined, 0}});
            }
        }
    }
}

struct RoleEntry
{
    IdentifierRole role;
    const char*    name;
};

/// @brief Every role, in enum order.
constexpr std::array<RoleEntry, 8> kRoles = {{
    {IdentifierRole::TypeName, "TypeName"},
    {IdentifierRole::FieldName, "FieldName"},
    {IdentifierRole::ConstantName, "ConstantName"},
    {IdentifierRole::FunctionName, "FunctionName"},
    {IdentifierRole::LocalName, "LocalName"},
    {IdentifierRole::NamespaceName, "NamespaceName"},
    {IdentifierRole::FileStem, "FileStem"},
    {IdentifierRole::MacroName, "MacroName"},
}};

/// @brief Roles whose per-name projection is worth tabulating.
///
/// FunctionName and LocalName carry the same policy as FieldName and no call site feeds them a DSDL
/// name yet, so their columns would be duplicates. The policy section above still lists them.
constexpr std::array<RoleEntry, 6> kTabulatedRoles = {{
    {IdentifierRole::TypeName, "TypeName"},
    {IdentifierRole::FieldName, "FieldName"},
    {IdentifierRole::ConstantName, "ConstantName"},
    {IdentifierRole::NamespaceName, "NamespaceName"},
    {IdentifierRole::FileStem, "FileStem"},
    {IdentifierRole::MacroName, "MacroName"},
}};

const char* caseStyleName(const CaseStyle style)
{
    switch (style)
    {
    case CaseStyle::Preserve:
        return "preserve";
    case CaseStyle::Snake:
        return "snake";
    case CaseStyle::Pascal:
        return "pascal";
    }
    return "?";
}

/// @brief One scope's worth of names and the role they claim an identifier in.
struct ScopeScenario
{
    const char*                  name;
    IdentifierRole               role;
    std::vector<std::string>     sources;
    std::vector<llvm::StringRef> reserved;
};

const std::vector<ScopeScenario>& scopeScenarios()
{
    static const std::vector<ScopeScenario> scenarios = {
        {"struct_body", IdentifierRole::FieldName, {"fooBar", "foo_bar", "FooBar", "break", "break_"}, {}},
        {"struct_body_reserved",
         IdentifierRole::FieldName,
         {"fooBar", "serialize", "Serialize"},
         {"Serialize", "Deserialize"}},
        {"constants", IdentifierRole::ConstantName, {"fooBar", "foo_bar", "FOO_BAR"}, {}},
        // Claimed names as a scope sees them: the escape happens in the projection, so what the scope
        // has to keep apart is the escaped spelling against a DSDL name that is already spelled that
        // way. `FULL_NAME_` is C's metadata macro and `FULL_NAME` is everyone else's constant.
        {"constants_claimed",
         IdentifierRole::ConstantName,
         {"FULL_NAME", "FULL_NAME_", "full_name_", "zoh_alias_eligible_"},
         {}},
    };
    return scenarios;
}

void appendScopes(std::ostringstream& out)
{
    out << "\n== scopes ==\n"
        << "#\n"
        << "# NamingScope over one region of generated code, in declaration order. Where a role's\n"
        << "# projection is many-to-one the scope appends _2, _3, ... so the map stays injective; the\n"
        << "# reserved scenario shows a field escaping a name the generated methods already own.\n"
        << "# Every backend needs the scope. Case-folding makes the projection many-to-one in Go,\n"
        << "# TypeScript and Python; in C, C++ and Rust the case survives but the keyword and\n"
        << "# claimed-name escapes do the same thing, which is what the break/break_ rows show.\n\n";
    appendRow(out, {{"scope", 22}, {"language", 9}, {"source", 22}, {"identifier", 0}});
    out << std::string(22 + 9 + 22 + 20, '-') << "\n";

    for (const auto& scenario : scopeScenarios())
    {
        for (const auto& entry : kLanguages)
        {
            NamingScope scope(entry.language, scenario.reserved);
            for (const auto& source : scenario.sources)
            {
                (void) scope.declare(scenario.role, source);
            }
            for (const auto& source : scenario.sources)
            {
                appendRow(out,
                          {{scenario.name, 22},
                           {entry.name, 9},
                           {quoted(source), 22},
                           {quoted(scope.get(scenario.role, source)), 0}});
            }
        }
    }
}

std::string renderRoleGolden()
{
    std::ostringstream out;
    out << "# llvm-dsdl identifier role policy (golden)\n"
        << "#\n"
        << "# Generated by test/unit/NamingGoldenTests.cpp. Do not edit by hand.\n"
        << "# Regenerate:  LLVMDSDL_UPDATE_NAMING_GOLDEN=1 <build>/test/unit/llvmdsdl-unit-tests\n"
        << "#\n"
        << "# The table below is what the emitters do, not an aspiration: every cell was taken\n"
        << "# from the call site that produces that name, and NamingPolicyTests.cpp asserts each one\n"
        << "# against the projection that call site uses.\n"
        << "#\n"
        << "# escape  replaces characters outside [A-Za-z0-9_] and prefixes a leading digit.\n"
        << "# strop   escapes a result that collides with a language keyword.\n"
        << "# upper   upper-cases the finished identifier.\n"
        << "#\n"
        << "# The 'no' cells are deliberate, not unfinished. A C or C++ header stem is a file name\n"
        << "# rather than an identifier, and a C or C++ macro token is not in the language's\n"
        << "# namespace and always carries its type name as a prefix, so neither needs a keyword\n"
        << "# escape. Names the generated code has already claimed are a separate question and are\n"
        << "# still checked -- see the claimed-name cases in NamingPolicyTests.cpp.\n";

    out << "\n== role policy ==\n\n";
    appendRow(out, {{"language", 9}, {"role", 15}, {"case", 10}, {"escape", 8}, {"strop", 7}, {"upper", 0}});
    out << std::string(9 + 15 + 10 + 8 + 7 + 5, '-') << "\n";
    for (const auto& entry : kLanguages)
    {
        for (const auto& role : kRoles)
        {
            const auto& policy = codegenNamingPolicy(entry.language).roleFor(role.role);
            appendRow(out,
                      {{entry.name, 9},
                       {role.name, 15},
                       {caseStyleName(policy.caseStyle), 10},
                       {policy.escape ? "yes" : "no", 8},
                       {policy.strop ? "yes" : "no", 7},
                       {policy.upper ? "yes" : "no", 0}});
        }
    }

    out << "\n== projections by role ==\n\n";
    std::vector<std::pair<std::string, std::size_t>> header = {{"source", 22}, {"language", 9}};
    for (const auto& role : kTabulatedRoles)
    {
        header.emplace_back(role.name, 23);
    }
    appendRow(out, header);
    out << std::string(22 + 9 + (23 * (kTabulatedRoles.size() - 1)), '-') << "\n";

    for (const auto& source : corpus())
    {
        for (const auto& entry : kLanguages)
        {
            std::vector<std::pair<std::string, std::size_t>> cells = {{quoted(source), 22}, {entry.name, 9}};
            for (const auto& role : kTabulatedRoles)
            {
                cells.emplace_back(quoted(codegenProjectIdentifier(entry.language, role.role, source)), 23);
            }
            appendRow(out, cells);
        }
    }
    appendScopes(out);
    return out.str();
}

std::string renderGolden()
{
    std::ostringstream out;
    appendHeader(out);
    appendProjections(out);
    appendCollisions(out);
    return out.str();
}

bool wantsUpdate()
{
    const char* const value = std::getenv("LLVMDSDL_UPDATE_NAMING_GOLDEN");
    return value != nullptr && value[0] != '\0' && std::string(value) != "0";
}

/// @brief Reports the first line where produced and expected diverge, so a failure names the
///        identifier that moved instead of dumping two files.
void reportFirstDifference(const std::string& expected, const std::string& produced)
{
    std::istringstream expectedLines(expected);
    std::istringstream producedLines(produced);
    std::string        expectedLine;
    std::string        producedLine;
    std::size_t        line = 0;
    while (true)
    {
        const bool haveExpected = static_cast<bool>(std::getline(expectedLines, expectedLine));
        const bool haveProduced = static_cast<bool>(std::getline(producedLines, producedLine));
        ++line;
        if (!haveExpected && !haveProduced)
        {
            return;
        }
        if (!haveExpected)
        {
            std::cerr << "  line " << line << ": golden ends, generated has: " << producedLine << "\n";
            return;
        }
        if (!haveProduced)
        {
            std::cerr << "  line " << line << ": generated ends, golden has: " << expectedLine << "\n";
            return;
        }
        if (expectedLine != producedLine)
        {
            std::cerr << "  line " << line << ":\n"
                      << "    golden:    " << expectedLine << "\n"
                      << "    generated: " << producedLine << "\n";
            return;
        }
    }
}

}  // namespace

namespace
{

/// @brief Compares @p produced against the golden file at @p path, or rewrites it when asked.
bool checkGolden(const std::string& path, const std::string& produced)
{
    if (wantsUpdate())
    {
        std::ofstream out(path, std::ios::binary);
        if (!out.good())
        {
            std::cerr << "naming golden map: cannot write " << path << "\n";
            return false;
        }
        out << produced;
        if (!out.good())
        {
            std::cerr << "naming golden map: write failed for " << path << "\n";
            return false;
        }
        std::cerr << "naming golden map rewritten: " << path << "\n";
        return true;
    }

    std::ifstream const in(path, std::ios::binary);
    if (!in.good())
    {
        std::cerr << "naming golden map: cannot read " << path << "\n";
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string expected = buffer.str();

    if (expected == produced)
    {
        return true;
    }

    std::cerr << "naming golden map mismatch (" << path << ")\n";
    reportFirstDifference(expected, produced);
    std::cerr << "  a generated identifier changed; if that was intended, regenerate with\n"
              << "  LLVMDSDL_UPDATE_NAMING_GOLDEN=1 <build>/test/unit/llvmdsdl-unit-tests\n";
    return false;
}

}  // namespace

bool runNamingGoldenTests()
{
    const bool mapOk   = checkGolden(LLVMDSDL_NAMING_GOLDEN_PATH, renderGolden());
    const bool rolesOk = checkGolden(LLVMDSDL_NAMING_ROLES_GOLDEN_PATH, renderRoleGolden());
    return mapOk && rolesOk;
}
