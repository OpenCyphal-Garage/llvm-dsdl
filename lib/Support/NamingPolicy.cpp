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

#include "llvmdsdl/Support/NamingPolicy.h"

#include <cassert>
#include <cctype>
#include <string>
#include <vector>
#include <cstddef>
#include <optional>

#include "llvm/ADT/StringSet.h"

#include "llvmdsdl/Support/NameCanonicalization.h"

namespace llvmdsdl
{
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

    // C output is compiled as C++ more often than not, so it is escaped against both sets. The
    // object backend is the case that forced it: its C++ ABI lane includes the staged C headers from
    // C++ translation units, and the `c_shim` header it publishes is a dual-language surface that the
    // suite compiles both ways. `extern "C"` changes linkage, not tokenization, so a member named
    // `class` is a parse error there however it is linked.
    //
    // The cost is a trailing `_` on the handful of DSDL names that are C++ keywords and not C ones --
    // `class`, `new`, `operator`, `template`, `export` and their kin. Macro constants are unaffected:
    // `MacroName` in C is a macro token, which is never stropped, and always carries a type prefix.
    static const llvm::StringSet<> cKeywordsIncludingCpp = [] {
        llvm::StringSet<> merged = cKeywords;
        for (const auto& keyword : cppKeywords)
        {
            merged.insert(keyword.getKey());
        }
        return merged;
    }();

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
        return cKeywordsIncludingCpp;
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
    // Shared with the frontend's output-name collision check so identifiers and file stems fold
    // identically (see llvmdsdl::canonicalSnakeCase).
    return canonicalSnakeCase(name);
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

namespace
{

/// @brief The role policy table: how each language names each kind of identifier.
///
/// The `escape` and `strop` columns are not uniform. A role skips the keyword escape when its output
/// is not an identifier in the language's own namespace -- a file name, or a token that is always
/// emitted under a type-name prefix -- because there is nothing there for a keyword to collide with.
/// Names the generated code has already claimed are a separate question and are checked regardless;
/// see @ref runtimeOwnedNames.
///
/// FunctionName and LocalName carry the same policy as FieldName. No call site feeds them a
/// DSDL-derived name -- generated helpers and locals are built from mangled type names and
/// generator-internal spellings -- so nothing exercises them; they exist so that a backend which
/// starts naming one from DSDL has a defined answer rather than a new decision.
const RolePolicy& rolePolicy(const CodegenNamingLanguage language, const IdentifierRole role)
{
    static constexpr RolePolicy kPreserve{CaseStyle::Preserve, true, true, false};
    static constexpr RolePolicy kSnake{CaseStyle::Snake, true, true, false};
    static constexpr RolePolicy kPascal{CaseStyle::Pascal, true, true, false};
    static constexpr RolePolicy kUpperSnake{CaseStyle::Snake, true, true, true};

    // A preprocessor token: escaped and upper-cased, never stropped against keywords.
    static constexpr RolePolicy kMacroToken{CaseStyle::Preserve, true, false, true};
    // A file name taken from the DSDL short name, untouched.
    static constexpr RolePolicy kVerbatim{CaseStyle::Preserve, false, false, false};

    const bool cLike    = language == CodegenNamingLanguage::C || language == CodegenNamingLanguage::Cpp;
    const bool rustLike = language == CodegenNamingLanguage::Rust;
    const bool goLike   = language == CodegenNamingLanguage::Go;

    switch (role)
    {
    case IdentifierRole::TypeName:
        return (cLike || rustLike) ? kPreserve : kPascal;
    case IdentifierRole::FieldName:
    case IdentifierRole::FunctionName:
    case IdentifierRole::LocalName:
        if (cLike || rustLike)
        {
            return kPreserve;
        }
        return goLike ? kPascal : kSnake;
    case IdentifierRole::ConstantName:
    case IdentifierRole::MacroName:
        return cLike ? kMacroToken : kUpperSnake;
    case IdentifierRole::NamespaceName:
        return (cLike || rustLike) ? kPreserve : kSnake;
    case IdentifierRole::FileStem:
        return cLike ? kVerbatim : kSnake;
    }
    return kPreserve;
}

/// @brief The names generated code claims for a role, per language.
///
/// A backend contributes names for a role when what it emits for every type lands in the same scope,
/// and under the same prefix, as the DSDL-derived names for that role. Where the two are separated
/// -- by a prefix on one side and not the other, or by different scopes -- there is nothing to
/// escape, and the arm below says so.
///
/// These names are the emitters' to change, and this list is the only copy: a second one in the
/// emitting layer would be the duplication this engine exists to remove. What keeps the two in step
/// is a test rather than a cross-reference -- `test/lit/naming-stropping.txt` generates a type whose
/// DSDL constants are named after every entry here, so a name added to an emitter and forgotten here
/// surfaces as a duplicate declaration in a language that has to compile.
llvm::ArrayRef<llvm::StringRef> runtimeOwnedNames(const CodegenNamingLanguage language, const IdentifierRole role)
{
    // `UNION_OPTION_COUNT` is emitted only for a union and the memory-resource pair only under the
    // PMR profile, but both are claimed for every type: a member name that changed with
    // `--cpp-profile`, or with whether a later revision of a type became a union, would be an ABI
    // that depends on how the generator was invoked rather than on the DSDL.
    static constexpr std::array<llvm::StringRef, 8> kMetadata = {"FULL_NAME",
                                                                 "FULL_NAME_AND_VERSION",
                                                                 "IS_DEPRECATED",
                                                                 "EXTENT_BYTES",
                                                                 "SERIALIZATION_BUFFER_SIZE_BYTES",
                                                                 "ZOH_ALIAS_ELIGIBLE",
                                                                 "ZOH_ALIAS_REASON",
                                                                 "UNION_OPTION_COUNT"};

    static constexpr std::array<llvm::StringRef, 16> kCppMembers = {"FULL_NAME",
                                                                    "FULL_NAME_AND_VERSION",
                                                                    "IS_DEPRECATED",
                                                                    "EXTENT_BYTES",
                                                                    "SERIALIZATION_BUFFER_SIZE_BYTES",
                                                                    "ZOH_ALIAS_ELIGIBLE",
                                                                    "ZOH_ALIAS_REASON",
                                                                    "UNION_OPTION_COUNT",
                                                                    "serialize",
                                                                    "deserialize",
                                                                    "try_serialize_view",
                                                                    "try_deserialize_view",
                                                                    "set_memory_resource",
                                                                    "_memory_resource",
                                                                    "to_c",
                                                                    "from_c"};

    // `Tag` is the union discriminator; Go is the only backend that spells it as an exported field,
    // and an exported field is what a DSDL field named `tag` projects to.
    static constexpr std::array<llvm::StringRef, 3> kGoMethods = {"Serialize", "Deserialize", "Tag"};

    static constexpr std::array<llvm::StringRef, 4> kPyMethods = {"serialize",
                                                                  "deserialize",
                                                                  "_serialize_to",
                                                                  "_deserialize_from"};

    static constexpr std::array<llvm::StringRef, 2> kTsProperties = {"constructor", "prototype"};

    // C spells the same constants as macros carrying a trailing `_`, and that underscore is part of
    // the name to be missed rather than a reason there is nothing to miss: `ConstantName` in C is a
    // macro token -- preserve case, escape, upper-case, no strop -- which passes a source name's own
    // trailing underscore straight through, and DSDL reserves only names that both start and end
    // with one. `full_name_` is a conformant DSDL constant that reaches `FULL_NAME_`.
    static constexpr std::array<llvm::StringRef, 8> kCMacros = {"FULL_NAME_",
                                                                "FULL_NAME_AND_VERSION_",
                                                                "IS_DEPRECATED_",
                                                                "EXTENT_BYTES_",
                                                                "SERIALIZATION_BUFFER_SIZE_BYTES_",
                                                                "ZOH_ALIAS_ELIGIBLE_",
                                                                "ZOH_ALIAS_REASON_",
                                                                "UNION_OPTION_COUNT_"};

    static constexpr std::array<llvm::StringRef, 0> kNone = {};

    switch (language)
    {
    case CodegenNamingLanguage::Cpp:
        // The struct holds its fields, its constants and the generated statics and member functions
        // in one scope, so a field competes with all of them.
        if (role == IdentifierRole::FieldName)
        {
            return kCppMembers;
        }
        return (role == IdentifierRole::ConstantName) ? llvm::ArrayRef<llvm::StringRef>(kMetadata)
                                                      : llvm::ArrayRef<llvm::StringRef>(kNone);
    case CodegenNamingLanguage::Go:
        // A struct field and a method may not share a name; constants take the same type prefix as
        // the generated ones.
        if (role == IdentifierRole::FieldName)
        {
            return kGoMethods;
        }
        return (role == IdentifierRole::ConstantName) ? llvm::ArrayRef<llvm::StringRef>(kMetadata)
                                                      : llvm::ArrayRef<llvm::StringRef>(kNone);
    case CodegenNamingLanguage::Rust:
        // Constants share the inherent impl with the generated ones. Fields do not: fields and
        // methods occupy separate namespaces.
        return (role == IdentifierRole::ConstantName) ? llvm::ArrayRef<llvm::StringRef>(kMetadata)
                                                      : llvm::ArrayRef<llvm::StringRef>(kNone);
    case CodegenNamingLanguage::Python:
        // A dataclass attribute shadows the method of the same name, so `self.serialize()` would call
        // an int. Constants are safe: the generated ones take a different prefix.
        return (role == IdentifierRole::FieldName) ? llvm::ArrayRef<llvm::StringRef>(kPyMethods)
                                                   : llvm::ArrayRef<llvm::StringRef>(kNone);
    case CodegenNamingLanguage::TypeScript:
        // A property named `constructor` or `prototype` shadows the one every object has. Constants
        // are safe: the generated ones take a different prefix.
        return (role == IdentifierRole::FieldName) ? llvm::ArrayRef<llvm::StringRef>(kTsProperties)
                                                   : llvm::ArrayRef<llvm::StringRef>(kNone);
    case CodegenNamingLanguage::C:
        // `ConstantName` and `MacroName` are one thing in C -- both name a `<Type>_<TOKEN>` macro --
        // so both are claimed against the same list. Fields need nothing: the only member C adds of
        // its own is a union's `_tag_`, which DSDL will not accept as a name.
        if ((role == IdentifierRole::ConstantName) || (role == IdentifierRole::MacroName))
        {
            return kCMacros;
        }
        break;
    }
    return kNone;
}

/// @brief Encodes the underscores that put @p identifier in a namespace the language reserves.
///
/// C reserves identifiers beginning `__` or `_` plus a capital ([reserved.names]); C++ reserves those
/// and any identifier containing `__` anywhere. Appending to the end repairs none of them, so the
/// offending underscores are replaced with the character encoding, which is injective and therefore
/// needs no scope to disambiguate afterwards.
///
/// Only C and C++ have such a namespace; the other four return the identifier unchanged.
std::string encodeReservedNamespace(const CodegenNamingLanguage language, const std::string& identifier, bool& encoded)
{
    encoded = false;
    if (language != CodegenNamingLanguage::C && language != CodegenNamingLanguage::Cpp)
    {
        return identifier;
    }
    if (identifier.empty())
    {
        return identifier;
    }

    const bool interiorRunsReserved = language == CodegenNamingLanguage::Cpp;

    std::string out;
    out.reserve(identifier.size());
    for (std::size_t i = 0; i < identifier.size();)
    {
        if (identifier[i] != '_')
        {
            out.push_back(identifier[i]);
            ++i;
            continue;
        }

        std::size_t run = 0;
        while (i + run < identifier.size() && identifier[i + run] == '_')
        {
            ++run;
        }
        const char next = (i + run < identifier.size()) ? identifier[i + run] : '\0';

        const bool leading  = (i == 0);
        const bool violates = (run >= 2 && (leading || interiorRunsReserved)) ||
                              (leading && run == 1 && std::isupper(static_cast<unsigned char>(next)));
        if (violates)
        {
            for (std::size_t n = 0; n < run; ++n)
            {
                out += "zX005F";
            }
            encoded = true;
        }
        else
        {
            out.append(run, '_');
        }
        i += run;
    }
    return out;
}

/// @brief Runs the shared naming pipeline.
///
/// Stage order is load-bearing and matches what the case-explicit helpers did before this existed:
/// the keyword check runs on the cased but not yet upper-cased form, so a Go constant named `break`
/// becomes `break_` and only then `BREAK_`.
/// @param[in] role The role whose claimed-name set applies, or nullopt for a token this generator
///            constructed rather than a DSDL name being named -- those must not pick up names the
///            generated code owns, because they are not competing for the same scope.
ProjectedIdentifier runPipeline(const CodegenNamingLanguage         language,
                                const std::optional<IdentifierRole> role,
                                const RolePolicy&                   policy,
                                const llvm::StringRef               name)
{
    std::string out;
    switch (policy.caseStyle)
    {
    case CaseStyle::Preserve:
        out = name.str();
        break;
    case CaseStyle::Snake:
        out = normalizeSnakeCase(name);
        break;
    case CaseStyle::Pascal:
        out = normalizePascalCase(name);
        break;
    }

    bool escaped = false;
    if (out.empty())
    {
        out     = (policy.caseStyle == CaseStyle::Pascal) ? "X" : "_";
        escaped = true;
    }

    if (policy.escape)
    {
        for (char& c : out)
        {
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            {
                c       = '_';
                escaped = true;
            }
        }
        if (std::isdigit(static_cast<unsigned char>(out.front())))
        {
            out.insert(out.begin(), '_');
            escaped = true;
        }
    }

    // Keywords are a property of the identifier as cased here: a Go constant named `break` folds to
    // `break`, is escaped to `break_`, and only then becomes `BREAK_`.
    if (policy.strop && keywordSet(language).contains(out))
    {
        out += "_";
        escaped = true;
        // One iteration suffices because no keyword in any table ends in `_`. The table invariant
        // test in NamingPolicyTests.cpp is what keeps that true.
        assert(!keywordSet(language).contains(out));
    }

    if (policy.upper)
    {
        for (char& c : out)
        {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }

    // Names the generated code claims are a property of the *finished* identifier, so this runs after
    // the upper-casing: a Go constant named `full_name` is emitted as FULL_NAME, which is the
    // spelling that has to miss the metadata constant.
    //
    // Independent of `strop`, which governs only the keyword check: a C++ macro token is claimed
    // against the generated statics but never against keywords.
    if (role.has_value())
    {
        for (const auto& owned : runtimeOwnedNames(language, *role))
        {
            if (owned == out)
            {
                out += "_";
                escaped = true;
                break;
            }
        }
    }
    // Only a DSDL name being named: a token this generator constructed carries whatever shape the
    // emitter gave it, and encoding it here would mangle a symbol that has to match something else.
    //
    // A file stem is exempt because it is not an identifier: `__Foo_1_0.h` sits in no namespace the
    // language reserves, and encoding it would rename a file for a hazard that does not exist there.
    // The C and C++ emitters have always written the stem unencoded; what this fixes is the naming
    // manifest, which reported the encoded spelling and so named a header that is not on disk.
    bool reservedEncoded = false;
    if (role.has_value() && (*role != IdentifierRole::FileStem))
    {
        out = encodeReservedNamespace(language, out, reservedEncoded);
    }
    return ProjectedIdentifier{out, escaped || reservedEncoded, reservedEncoded};
}

}  // namespace

LanguageNamingPolicy::LanguageNamingPolicy(const CodegenNamingLanguage language)
    : language_(language)
{
}

const RolePolicy& LanguageNamingPolicy::roleFor(const IdentifierRole role) const
{
    return rolePolicy(language_, role);
}

bool LanguageNamingPolicy::isKeyword(const llvm::StringRef name) const
{
    return keywordSet(language_).contains(name);
}

llvm::ArrayRef<llvm::StringRef> LanguageNamingPolicy::runtimeOwned(const IdentifierRole role) const
{
    return runtimeOwnedNames(language_, role);
}

std::vector<llvm::StringRef> LanguageNamingPolicy::keywords() const
{
    const auto&                  set = keywordSet(language_);
    std::vector<llvm::StringRef> out;
    out.reserve(set.size());
    for (const auto& entry : set)
    {
        out.push_back(entry.getKey());
    }
    return out;
}

const LanguageNamingPolicy& codegenNamingPolicy(const CodegenNamingLanguage language)
{
    static const LanguageNamingPolicy kC(CodegenNamingLanguage::C);
    static const LanguageNamingPolicy kCpp(CodegenNamingLanguage::Cpp);
    static const LanguageNamingPolicy kRust(CodegenNamingLanguage::Rust);
    static const LanguageNamingPolicy kGo(CodegenNamingLanguage::Go);
    static const LanguageNamingPolicy kTs(CodegenNamingLanguage::TypeScript);
    static const LanguageNamingPolicy kPy(CodegenNamingLanguage::Python);

    switch (language)
    {
    case CodegenNamingLanguage::C:
        return kC;
    case CodegenNamingLanguage::Cpp:
        return kCpp;
    case CodegenNamingLanguage::Rust:
        return kRust;
    case CodegenNamingLanguage::Go:
        return kGo;
    case CodegenNamingLanguage::TypeScript:
        return kTs;
    case CodegenNamingLanguage::Python:
        return kPy;
    }
    return kTs;
}

ProjectedIdentifier codegenProjectIdentifierDetailed(const CodegenNamingLanguage language,
                                                     const IdentifierRole        role,
                                                     const llvm::StringRef       name)
{
    return runPipeline(language, role, rolePolicy(language, role), name);
}

std::string codegenProjectIdentifier(const CodegenNamingLanguage language,
                                     const IdentifierRole        role,
                                     const llvm::StringRef       name)
{
    return codegenProjectIdentifierDetailed(language, role, name).identifier;
}

bool codegenIsReservedNamespaceIdentifier(const CodegenNamingLanguage language, const llvm::StringRef identifier)
{
    bool encoded = false;
    (void) encodeReservedNamespace(language, identifier.str(), encoded);
    return encoded;
}

bool codegenIsKeyword(const CodegenNamingLanguage language, const llvm::StringRef name)
{
    return keywordSet(language).contains(name);
}

std::string codegenSanitizeIdentifier(const CodegenNamingLanguage language, const llvm::StringRef name)
{
    return runPipeline(language, std::nullopt, RolePolicy{CaseStyle::Preserve, true, true, false}, name).identifier;
}

std::string codegenToSnakeCaseIdentifier(const CodegenNamingLanguage language, const llvm::StringRef name)
{
    return runPipeline(language, std::nullopt, RolePolicy{CaseStyle::Snake, true, true, false}, name).identifier;
}

std::string codegenToPascalCaseIdentifier(const CodegenNamingLanguage language, const llvm::StringRef name)
{
    return runPipeline(language, std::nullopt, RolePolicy{CaseStyle::Pascal, true, true, false}, name).identifier;
}

std::string codegenToUpperSnakeCaseIdentifier(const CodegenNamingLanguage language, const llvm::StringRef name)
{
    return runPipeline(language, std::nullopt, RolePolicy{CaseStyle::Snake, true, true, true}, name).identifier;
}

NamingScope::NamingScope(const CodegenNamingLanguage language, const llvm::ArrayRef<llvm::StringRef> reserved)
    : language_(language)
{
    for (const auto& name : reserved)
    {
        used_.insert(name);
    }
}

std::string NamingScope::keyOf(const IdentifierRole role, const llvm::StringRef sourceName)
{
    return std::to_string(static_cast<int>(role)) + ":" + sourceName.str();
}

std::string NamingScope::declare(const IdentifierRole role, const llvm::StringRef sourceName)
{
    const std::string key = keyOf(role, sourceName);
    const auto        it  = assigned_.find(key);
    if (it != assigned_.end())
    {
        return it->second;
    }

    const std::string base = codegenProjectIdentifier(language_, role, sourceName);
    // `_` joins the ordinal to the base, except where the base already ends in one. Doubling it
    // would put the result in a namespace C and C++ reserve -- `break_` is what the keyword strop
    // makes of `break`, and `break__2` is an identifier the standard says is not the program's to
    // define. Nothing downstream repairs that: the reserved-namespace encoder runs inside the
    // projection, before this suffix exists.
    const std::string join      = base.empty() || (base.back() != '_') ? "_" : "";
    std::string       candidate = base;
    unsigned          suffix    = 2;
    while (!used_.insert(candidate).second)
    {
        candidate = base + join + std::to_string(suffix);
        ++suffix;
    }
    assigned_[key] = candidate;
    return candidate;
}

std::string NamingScope::get(const IdentifierRole role, const llvm::StringRef sourceName) const
{
    const auto it = assigned_.find(keyOf(role, sourceName));
    if (it == assigned_.end())
    {
        return codegenProjectIdentifier(language_, role, sourceName);
    }
    return it->second;
}

}  // namespace llvmdsdl
