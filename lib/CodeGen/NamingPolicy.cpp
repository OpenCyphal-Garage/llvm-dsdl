//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// Implements shared naming-policy helpers for backend code generation.
///
/// The implementation provides language keyword tables, identifier sanitation,
/// and common case projections reused across emitters.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/NamingPolicy.h"

#include <cctype>
#include <string>
#include <cstddef>

#include "llvm/ADT/StringSet.h"

namespace llvmdsdl
{
CodegenIdentifierAllocator::CodegenIdentifierAllocator(
    llvm::ArrayRef<std::string>                       sourceNames,
    llvm::function_ref<std::string(llvm::StringRef)> transform)
{
    // `used` holds every identifier already handed out, so a disambiguating suffix cannot itself
    // collide with another field's base identifier (e.g. sources `foo_bar`, `fooBar`, `foo_bar_2`).
    llvm::StringSet<> used;
    for (const auto& source : sourceNames)
    {
        const std::string base      = transform(source);
        std::string       candidate = base;
        unsigned          suffix    = 2;
        while (!used.insert(candidate).second)
        {
            candidate = base + "_" + std::to_string(suffix);
            ++suffix;
        }
        assigned_[source] = candidate;
    }
}

std::string CodegenIdentifierAllocator::get(llvm::StringRef sourceName) const
{
    const auto it = assigned_.find(sourceName);
    if (it == assigned_.end())
    {
        return sourceName.str();
    }
    return it->second;
}

CodegenIdentifierAllocator codegenUpperSnakeAllocator(const CodegenNamingLanguage language,
                                                      llvm::ArrayRef<std::string> names)
{
    return CodegenIdentifierAllocator(
        names, [language](llvm::StringRef name) { return codegenToUpperSnakeCaseIdentifier(language, name); });
}
}  // namespace llvmdsdl

namespace llvmdsdl
{
namespace
{

const llvm::StringSet<>& keywordSet(const CodegenNamingLanguage language)
{
    static const llvm::StringSet<> cKeywords =
        {"auto",       "break",     "case",           "char",          "const",    "continue", "default",  "do",
         "double",     "else",      "enum",           "extern",        "float",    "for",      "goto",     "if",
         "inline",     "int",       "long",           "register",      "restrict", "return",   "short",    "signed",
         "sizeof",     "static",    "struct",         "switch",        "typedef",  "union",    "unsigned", "void",
         "volatile",   "while",     "_Alignas",       "_Alignof",      "_Atomic",  "_Bool",    "_Complex", "_Generic",
         "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local", "true",     "false"};

    static const llvm::StringSet<> cppKeywords = {"alignas",
                                                  "alignof",
                                                  "and",
                                                  "and_eq",
                                                  "asm",
                                                  "atomic_cancel",
                                                  "atomic_commit",
                                                  "atomic_noexcept",
                                                  "auto",
                                                  "bitand",
                                                  "bitor",
                                                  "bool",
                                                  "break",
                                                  "case",
                                                  "catch",
                                                  "char",
                                                  "char8_t",
                                                  "char16_t",
                                                  "char32_t",
                                                  "class",
                                                  "compl",
                                                  "concept",
                                                  "const",
                                                  "consteval",
                                                  "constexpr",
                                                  "constinit",
                                                  "const_cast",
                                                  "continue",
                                                  "co_await",
                                                  "co_return",
                                                  "co_yield",
                                                  "decltype",
                                                  "default",
                                                  "delete",
                                                  "do",
                                                  "double",
                                                  "dynamic_cast",
                                                  "else",
                                                  "enum",
                                                  "explicit",
                                                  "export",
                                                  "extern",
                                                  "false",
                                                  "float",
                                                  "for",
                                                  "friend",
                                                  "goto",
                                                  "if",
                                                  "inline",
                                                  "int",
                                                  "long",
                                                  "mutable",
                                                  "namespace",
                                                  "new",
                                                  "noexcept",
                                                  "not",
                                                  "not_eq",
                                                  "nullptr",
                                                  "operator",
                                                  "or",
                                                  "or_eq",
                                                  "private",
                                                  "protected",
                                                  "public",
                                                  "register",
                                                  "reinterpret_cast",
                                                  "requires",
                                                  "return",
                                                  "short",
                                                  "signed",
                                                  "sizeof",
                                                  "static",
                                                  "static_assert",
                                                  "static_cast",
                                                  "struct",
                                                  "switch",
                                                  "template",
                                                  "this",
                                                  "thread_local",
                                                  "throw",
                                                  "true",
                                                  "try",
                                                  "typedef",
                                                  "typeid",
                                                  "typename",
                                                  "union",
                                                  "unsigned",
                                                  "using",
                                                  "virtual",
                                                  "void",
                                                  "volatile",
                                                  "wchar_t",
                                                  "while",
                                                  "xor",
                                                  "xor_eq"};

    static const llvm::StringSet<> rustKeywords = {"as",      "break",   "const",    "continue", "crate",  "else",
                                                   "enum",    "extern",  "false",    "fn",       "for",    "if",
                                                   "impl",    "in",      "let",      "loop",     "match",  "mod",
                                                   "move",    "mut",     "pub",      "ref",      "return", "self",
                                                   "Self",    "static",  "struct",   "super",    "trait",  "true",
                                                   "type",    "unsafe",  "use",      "where",    "while",  "async",
                                                   "await",   "dyn",     "abstract", "become",   "box",    "do",
                                                   "final",   "macro",   "override", "priv",     "try",    "typeof",
                                                   "unsized", "virtual", "yield"};

    static const llvm::StringSet<> goKeywords = {"break",    "default",     "func",   "interface", "select",
                                                 "case",     "defer",       "go",     "map",       "struct",
                                                 "chan",     "else",        "goto",   "package",   "switch",
                                                 "const",    "fallthrough", "if",     "range",     "type",
                                                 "continue", "for",         "import", "return",    "var"};

    static const llvm::StringSet<> tsKeywords =
        {"break", "case",       "catch",     "class",      "const",   "continue", "debugger",  "default", "delete",
         "do",    "else",       "enum",      "export",     "extends", "false",    "finally",   "for",     "function",
         "if",    "import",     "in",        "instanceof", "new",     "null",     "return",    "super",   "switch",
         "this",  "throw",      "true",      "try",        "typeof",  "var",      "void",      "while",   "with",
         "as",    "implements", "interface", "let",        "package", "private",  "protected", "public",  "static",
         "yield", "any",        "boolean",   "number",     "string",  "symbol",   "type",      "from",    "of"};

    static const llvm::StringSet<> pyKeywords = {"False",  "None",     "True",  "and",    "as",       "assert",
                                                 "async",  "await",    "break", "class",  "continue", "def",
                                                 "del",    "elif",     "else",  "except", "finally",  "for",
                                                 "from",   "global",   "if",    "import", "in",       "is",
                                                 "lambda", "nonlocal", "not",   "or",     "pass",     "raise",
                                                 "return", "try",      "while", "with",   "yield",    "match",
                                                 "case"};

    switch (language)
    {
    case CodegenNamingLanguage::C:
        return cKeywords;
    case CodegenNamingLanguage::Cpp:
        return cppKeywords;
    case CodegenNamingLanguage::Rust:
        return rustKeywords;
    case CodegenNamingLanguage::Go:
        return goKeywords;
    case CodegenNamingLanguage::TypeScript:
        return tsKeywords;
    case CodegenNamingLanguage::Python:
        return pyKeywords;
    }
    return tsKeywords;
}

std::string normalizeSnakeCase(llvm::StringRef name)
{
    std::string out;
    out.reserve(name.size() + 8);

    bool prevUnderscore = false;
    for (std::size_t i = 0; i < name.size(); ++i)
    {
        const char c    = name[i];
        const char prev = (i > 0) ? name[i - 1] : '\0';
        const char next = (i + 1 < name.size()) ? name[i + 1] : '\0';
        if (!std::isalnum(static_cast<unsigned char>(c)))
        {
            if (!out.empty() && !prevUnderscore)
            {
                out.push_back('_');
                prevUnderscore = true;
            }
            continue;
        }

        if (std::isupper(static_cast<unsigned char>(c)))
        {
            const bool boundary =
                std::islower(static_cast<unsigned char>(prev)) ||
                (std::isupper(static_cast<unsigned char>(prev)) && std::islower(static_cast<unsigned char>(next)));
            if (!out.empty() && !prevUnderscore && boundary)
            {
                out.push_back('_');
            }
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            prevUnderscore = false;
        }
        else
        {
            out.push_back(c);
            prevUnderscore = (c == '_');
        }
    }
    return out;
}

std::string normalizePascalCase(llvm::StringRef name)
{
    std::string out;
    out.reserve(name.size() + 8);

    bool upperNext = true;
    for (const char c : name)
    {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
        {
            upperNext = true;
            continue;
        }
        if (c == '_')
        {
            upperNext = true;
            continue;
        }
        if (upperNext)
        {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            upperNext = false;
        }
        else
        {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace

bool codegenIsKeyword(const CodegenNamingLanguage language, const llvm::StringRef name)
{
    return keywordSet(language).contains(name);
}

std::string codegenSanitizeIdentifier(const CodegenNamingLanguage language, llvm::StringRef name)
{
    std::string out = name.str();
    if (out.empty())
    {
        return "_";
    }
    for (char& c : out)
    {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
        {
            c = '_';
        }
    }
    if (std::isdigit(static_cast<unsigned char>(out.front())))
    {
        out.insert(out.begin(), '_');
    }
    if (codegenIsKeyword(language, out))
    {
        out += "_";
    }
    return out;
}

std::string codegenToSnakeCaseIdentifier(const CodegenNamingLanguage language, const llvm::StringRef name)
{
    auto out = normalizeSnakeCase(name);
    if (out.empty())
    {
        out = "_";
    }
    if (std::isdigit(static_cast<unsigned char>(out.front())))
    {
        out.insert(out.begin(), '_');
    }
    return codegenSanitizeIdentifier(language, out);
}

std::string codegenToPascalCaseIdentifier(const CodegenNamingLanguage language, const llvm::StringRef name)
{
    auto out = normalizePascalCase(name);
    if (out.empty())
    {
        out = "X";
    }
    return codegenSanitizeIdentifier(language, out);
}

std::string codegenToUpperSnakeCaseIdentifier(const CodegenNamingLanguage language, const llvm::StringRef name)
{
    auto out = codegenToSnakeCaseIdentifier(language, name);
    for (char& c : out)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace llvmdsdl
