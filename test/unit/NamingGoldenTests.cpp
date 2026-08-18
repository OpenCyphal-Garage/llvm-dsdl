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
/// Phase 0 of the identifier stropping design (docs/development/identifier-stropping.md). The point
/// is not to assert that any particular mapping is *correct* -- several rows record behavior the
/// design intends to change -- but to make "this refactor changed no generated identifier" a
/// checkable claim instead of a hope. Generated identifiers are the ABI between generated code and
/// hand-written code, so a diff in the golden map is an ABI change and has to be deliberate.
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
#include "llvmdsdl/CodeGen/NamingPolicy.h"
#include "llvmdsdl/Support/NameCanonicalization.h"
#include "llvmdsdl/Support/ReservedIdentifiers.h"

namespace
{

using llvmdsdl::CodegenIdentifierAllocator;
using llvmdsdl::canonicalSnakeCase;
using llvmdsdl::CodegenNamingLanguage;
using llvmdsdl::codegenSanitizeIdentifier;
using llvmdsdl::codegenToPascalCaseIdentifier;
using llvmdsdl::codegenToSnakeCaseIdentifier;
using llvmdsdl::codegenToUpperSnakeCaseIdentifier;
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
        // Keywords reachable through a snake_case projection, spread across all six targets.
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
    if (!(std::isalpha(first) || name.front() == '_'))
    {
        return false;
    }
    for (const char c : name)
    {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
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
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
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
/// so no two distinct type names can share a header. Phase 2 of the design routes all six through
/// one engine; until then the column would be a claim about code that does not run.
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

/// @brief One allocator scenario: a scope's worth of source names and the projection they claim.
struct AllocatorScope
{
    const char*                  name;
    const char*                  projection;
    std::vector<std::string>     sources;
    std::vector<llvm::StringRef> reserved;
};

const std::vector<AllocatorScope>& allocatorScopes()
{
    static const std::vector<AllocatorScope> scopes = {
        {"struct_body_snake", "snake", {"fooBar", "foo_bar", "FooBar", "break", "break_"}, {}},
        {"struct_body_pascal", "pascal", {"fooBar", "foo_bar", "serialize", "Serialize"}, {"Serialize", "Deserialize"}},
        {"constants_upper", "upper_snake", {"fooBar", "foo_bar", "FOO_BAR"}, {}},
    };
    return scopes;
}

void appendHeader(std::ostringstream& out)
{
    out << "# llvm-dsdl identifier naming map (golden)\n"
        << "#\n"
        << "# Generated by test/unit/NamingGoldenTests.cpp. Do not edit by hand.\n"
        << "# Regenerate:  LLVMDSDL_UPDATE_NAMING_GOLDEN=1 <build>/test/unit/llvmdsdl-unit-tests\n"
        << "#\n"
        << "# This is the phase 0 freeze described in docs/development/identifier-stropping.md. It\n"
        << "# records what the naming helpers produce today, including the parts the design intends\n"
        << "# to change, so that a refactor claiming to preserve behavior can be checked rather than\n"
        << "# trusted. A diff in this file changes identifiers in generated code and is therefore an\n"
        << "# ABI change: it belongs in a commit that says so.\n"
        << "#\n"
        << "# reachable  whether the source name can appear in a conformant DSDL definition (charset\n"
        << "#            plus DSDL v1.0 section 3.2.5). Rows marked 'no' pin defensive behavior that\n"
        << "#            the compiler never reaches, but the language server and library callers do.\n"
        << "# file_stem  and type_name come from DefinitionPathProjection and are shown for version\n"
        << "#            1.0. They read '-' for a backend that does not derive that name from the\n"
        << "#            shared helper today -- C and C++ name headers after the raw DSDL short\n"
        << "#            name, and C, C++ and Rust build namespace-qualified type symbols. Phase 2\n"
        << "#            routes all six through one engine and those cells become real.\n";
}

void appendProjections(std::ostringstream& out)
{
    out << "\n== projections ==\n\n";
    std::vector<std::pair<std::string, std::size_t>> header = {{"source", 18}, {"reach", 7}, {"language", 9}};
    for (const auto& projection : kProjections)
    {
        header.emplace_back(projection.name, 20);
    }
    appendRow(out, header);
    out << std::string(18 + 7 + 9 + (20 * (kProjections.size() - 1)), '-') << "\n";

    for (const auto& source : corpus())
    {
        const char* reach = isDsdlReachable(source) ? "yes" : "no";
        for (const auto& entry : kLanguages)
        {
            std::vector<std::pair<std::string, std::size_t>> cells = {{quoted(source), 18},
                                                                      {reach, 7},
                                                                      {entry.name, 9}};
            for (const auto& projection : kProjections)
            {
                cells.emplace_back(projectionApplies(projection.name, entry.language)
                                       ? quoted(projection.apply(entry.language, source))
                                       : "-",
                                   20);
            }
            appendRow(out, cells);
        }
    }
}

/// @brief Names what currently prevents a collision group from reaching generated code.
///
/// Output-path names (file_stem, type_name) are guarded upstream by the frontend's output-name
/// check, which keys on `canonicalSnakeCase` -- that is, on the projection *without* the keyword
/// escape. A group whose members share one such key is rejected there; a group that spans several
/// keys passes the check and collides in the backend anyway.
std::string guardFor(const std::string& projection, const std::vector<std::string>& sources)
{
    if (projection != "file_stem" && projection != "type_name")
    {
        return "allocator";
    }
    const std::string first = canonicalSnakeCase(sources.front());
    for (const auto& source : sources)
    {
        if (canonicalSnakeCase(source) != first)
        {
            return "MISSED";
        }
    }
    return "discovery";
}

void appendCollisions(std::ostringstream& out)
{
    out << "\n== collisions ==\n"
        << "#\n"
        << "# Distinct DSDL-reachable sources that project onto one identifier. Unreachable sources\n"
        << "# are excluded: a clash they cause is not a hazard the compiler can meet.\n"
        << "#\n"
        << "# guard: what stops the collision from reaching generated code today.\n"
        << "#   allocator  an in-scope name; CodegenIdentifierAllocator appends _2 (next section).\n"
        << "#   discovery  every source folds to one Discovery key, so the frontend rejects the pair\n"
        << "#              before codegen runs.\n"
        << "#   MISSED     the sources fold to different Discovery keys but the same output name, so\n"
        << "#              both reach the backend. For file_stem that silently overwrites a type's\n"
        << "#              output; for type_name it is a duplicate symbol. This is the gap phase 3\n"
        << "#              closes -- grep MISSED for the worklist.\n\n";
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

void appendAllocator(std::ostringstream& out)
{
    out << "\n== allocator ==\n"
        << "#\n"
        << "# CodegenIdentifierAllocator over one scope, in declaration order, showing the _2/_3\n"
        << "# repair that keeps the map injective within that scope.\n\n";
    appendRow(out, {{"scope", 20}, {"language", 9}, {"source", 14}, {"identifier", 0}});
    out << std::string(20 + 9 + 14 + 20, '-') << "\n";

    for (const auto& scope : allocatorScopes())
    {
        const Projection* projection = nullptr;
        for (const auto& candidate : kProjections)
        {
            if (std::string(candidate.name) == scope.projection)
            {
                projection = &candidate;
            }
        }
        if (projection == nullptr)
        {
            out << "!! unknown projection " << scope.projection << "\n";
            continue;
        }
        for (const auto& entry : kLanguages)
        {
            const CodegenNamingLanguage      language = entry.language;
            const CodegenIdentifierAllocator allocator(
                scope.sources,
                [projection, language](const llvm::StringRef name) { return projection->apply(language, name.str()); },
                scope.reserved);
            for (const auto& source : scope.sources)
            {
                appendRow(out,
                          {{scope.name, 20},
                           {entry.name, 9},
                           {quoted(source), 14},
                           {quoted(allocator.get(source)), 0}});
            }
        }
    }
}

std::string renderGolden()
{
    std::ostringstream out;
    appendHeader(out);
    appendProjections(out);
    appendCollisions(out);
    appendAllocator(out);
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

bool runNamingGoldenTests()
{
    const std::string produced = renderGolden();
    const std::string path     = LLVMDSDL_NAMING_GOLDEN_PATH;

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

    std::ifstream in(path, std::ios::binary);
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
