//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include "llvmdsdl/CodeGen/Vocabulary.h"

#include "llvmdsdl/CodeGen/EmbeddedSources.h"
#include "llvmdsdl/Support/Language.h"
#include "llvmdsdl/Support/LanguageTraits.h"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/ADT/Twine.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ErrorHandling.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/YAMLTraits.h>
#include <llvm/Support/raw_ostream.h>

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace llvmdsdl::vocabulary
{

// The concepts, and the languages that bind them.

namespace
{

constexpr llvm::StringRef kSelf    = "self";
constexpr llvm::StringRef kElement = "element";
constexpr llvm::StringRef kOffset  = "offset";
constexpr llvm::StringRef kType    = "type";
constexpr llvm::StringRef kData    = "data";
constexpr llvm::StringRef kSize    = "size";

constexpr llvm::StringRef kSpanTypePlaceholders[] = {kElement};
constexpr llvm::StringRef kSelfAlone[]            = {kSelf};
constexpr llvm::StringRef kSelfAndOffset[]        = {kSelf, kOffset};
constexpr llvm::StringRef kTypeDataAndSize[]      = {kType, kData, kSize};

// The standard library's shape is the contract: a binding spells only what its library does
// differently. `subspan` is reached with an offset the plan has already clamped to the size, and
// `make` with a pointer to at least `size` bytes; `type` is the span's type as the binding spells it.
constexpr Operation kSpanOperations[] = {
    {"data", "{self}.data()", kSelfAlone},
    {"size", "{self}.size()", kSelfAlone},
    {"subspan", "{self}.subspan({offset})", kSelfAndOffset},
    {"make", "{type}({data}, {size})", kTypeDataAndSize},
};

constexpr ConceptSpec kConcepts[] = {
    {Concept::Span, "span", kSpanTypePlaceholders, kSpanOperations},
};

constexpr llvm::StringRef kCppProfiles[]  = {"std", "pmr", "autosar"};
constexpr llvm::StringRef kRustProfiles[] = {"std", "no-std-alloc"};
constexpr Concept         kCppBindable[]  = {Concept::Span};

// In Rust, Go, TypeScript and Python a byte view is the language's own slice, so span is fixed
// there; C has no type for it beyond a pointer and a size.
constexpr LanguageSpec kLanguages[] = {
    {Language::C, {}, {}},
    {Language::Cpp, kCppProfiles, kCppBindable},
    {Language::Rust, kRustProfiles, {}},
    {Language::Go, {}, {}},
    {Language::TypeScript, {}, {}},
    {Language::Python, {}, {}},
};

std::string joined(const llvm::ArrayRef<llvm::StringRef> names)
{
    std::string out;
    for (const llvm::StringRef name : names)
    {
        out += (out.empty() ? "" : ", ") + name.str();
    }
    return out;
}

std::string conceptNames()
{
    std::string out;
    for (const ConceptSpec& role : kConcepts)
    {
        out += (out.empty() ? "" : ", ") + role.name.str();
    }
    return out;
}

std::string operationNames(const ConceptSpec& role)
{
    std::string out;
    for (const Operation& operation : role.operations)
    {
        out += (out.empty() ? "" : ", ") + operation.name.str();
    }
    return out;
}

llvm::Error fileError(const llvm::StringRef path, const llvm::Twine& message)
{
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "vocabulary file %s: %s",
                                   path.str().c_str(),
                                   message.str().c_str());
}

/// @brief Checks that @p spelling uses only @p allowed placeholders, and every one of them.
llvm::Error checkPlaceholders(const llvm::StringRef                 spelling,
                              const llvm::ArrayRef<llvm::StringRef> allowed,
                              const llvm::Twine&                    what,
                              const llvm::StringRef                 path)
{
    llvm::SmallVector<bool, 4> seen(allowed.size(), false);
    for (std::size_t at = 0; at < spelling.size(); ++at)
    {
        if (spelling[at] == '}')
        {
            return fileError(path, what + " '" + spelling + "' has a '}' that closes no placeholder");
        }
        if (spelling[at] != '{')
        {
            continue;
        }
        const std::size_t close = spelling.find('}', at);
        if (close == llvm::StringRef::npos)
        {
            return fileError(path, what + " '" + spelling + "' has a '{' that no '}' closes");
        }
        const llvm::StringRef name  = spelling.slice(at + 1, close);
        const auto* const     found = llvm::find(allowed, name);
        if (found == allowed.end())
        {
            return fileError(path,
                             what + " '" + spelling + "' uses an unknown placeholder '{" + name +
                                 "}'; it may use: " + joined(allowed));
        }
        seen[static_cast<std::size_t>(found - allowed.begin())] = true;
        at                                                      = close;
    }
    for (std::size_t index = 0; index < allowed.size(); ++index)
    {
        if (!seen[index])
        {
            return fileError(path, what + " '" + spelling + "' must use the placeholder '{" + allowed[index] + "}'");
        }
    }
    return llvm::Error::success();
}

/// @brief @p spelling with each placeholder replaced by what it stands for.
std::string fillPlaceholders(const llvm::StringRef spelling, const Placeholders placeholders)
{
    std::string out;
    out.reserve(spelling.size());
    for (std::size_t at = 0; at < spelling.size(); ++at)
    {
        if (spelling[at] != '{')
        {
            out += spelling[at];
            continue;
        }
        const std::size_t     close = spelling.find('}', at);
        const llvm::StringRef name  = spelling.slice(at + 1, close);
        const auto* const found = llvm::find_if(placeholders, [&](const auto& entry) { return entry.first == name; });
        if (found == placeholders.end())
        {
            llvm::report_fatal_error("vocabulary: no value for the placeholder '" + name + "' of '" + spelling + "'");
        }
        out += found->second;
        at = close;
    }
    return out;
}

bool isIncludeOperand(const llvm::StringRef include)
{
    return include.size() > 2 &&
           ((include.front() == '<' && include.back() == '>') || (include.front() == '"' && include.back() == '"'));
}

}  // namespace

llvm::ArrayRef<ConceptSpec> concepts()
{
    return kConcepts;
}

const ConceptSpec& spec(const Concept role)
{
    for (const ConceptSpec& entry : kConcepts)
    {
        if (entry.id == role)
        {
            return entry;
        }
    }
    llvm::report_fatal_error("vocabulary: a concept with no specification");
}

std::optional<Concept> conceptNamed(const llvm::StringRef name)
{
    for (const ConceptSpec& entry : kConcepts)
    {
        if (entry.name == name)
        {
            return entry.id;
        }
    }
    return std::nullopt;
}

llvm::ArrayRef<LanguageSpec> languages()
{
    return kLanguages;
}

const LanguageSpec* languageNamed(const llvm::StringRef name)
{
    const LanguageTraits* const traits = languageTraitsNamed(name);
    if (traits == nullptr)
    {
        return nullptr;
    }
    for (const LanguageSpec& entry : kLanguages)
    {
        if (entry.language == traits->language)
        {
            return &entry;
        }
    }
    return nullptr;
}

// The file format.

namespace
{

struct OperationsYaml final
{
    std::vector<std::pair<std::string, std::string>> entries;
};

struct BindingYaml final
{
    std::string              type;
    std::vector<std::string> include;
    OperationsYaml           operations;
};

struct BindingsYaml final
{
    std::vector<std::pair<std::string, BindingYaml>> entries;
};

struct FileYaml final
{
    int                      version{0};
    std::string              language;
    std::vector<std::string> profiles;
    BindingsYaml             bindings;
};

}  // namespace

}  // namespace llvmdsdl::vocabulary

template <>
struct llvm::yaml::CustomMappingTraits<llvmdsdl::vocabulary::OperationsYaml>
{
    static void inputOne(IO& io, StringRef key, llvmdsdl::vocabulary::OperationsYaml& value)
    {
        value.entries.emplace_back(key.str(), std::string{});
        io.mapRequired(value.entries.back().first, value.entries.back().second);
    }
    static void output(IO& /*io*/, llvmdsdl::vocabulary::OperationsYaml& /*value*/) {}
};

template <>
struct llvm::yaml::MappingTraits<llvmdsdl::vocabulary::BindingYaml>
{
    static void mapping(IO& io, llvmdsdl::vocabulary::BindingYaml& binding)
    {
        io.mapRequired("type", binding.type);
        io.mapRequired("include", binding.include);
        io.mapOptional("operations", binding.operations);
    }
};

template <>
struct llvm::yaml::CustomMappingTraits<llvmdsdl::vocabulary::BindingsYaml>
{
    static void inputOne(IO& io, StringRef key, llvmdsdl::vocabulary::BindingsYaml& value)
    {
        value.entries.emplace_back(key.str(), llvmdsdl::vocabulary::BindingYaml{});
        io.mapRequired(value.entries.back().first, value.entries.back().second);
    }
    static void output(IO& /*io*/, llvmdsdl::vocabulary::BindingsYaml& /*value*/) {}
};

template <>
struct llvm::yaml::MappingTraits<llvmdsdl::vocabulary::FileYaml>
{
    static void mapping(IO& io, llvmdsdl::vocabulary::FileYaml& file)
    {
        io.mapRequired("vocabulary", file.version);
        io.mapRequired("language", file.language);
        io.mapOptional("profiles", file.profiles);
        io.mapRequired("bindings", file.bindings);
    }
};

namespace llvmdsdl::vocabulary
{

namespace
{

constexpr int kFormatVersion = 1;

void collectDiagnostic(const llvm::SMDiagnostic& diagnostic, void* const context)
{
    auto&                    text = *static_cast<std::string*>(context);
    llvm::raw_string_ostream os(text);
    diagnostic.print(nullptr, os, /*ShowColors=*/false);
}

llvm::Expected<Binding> validateBinding(const BindingYaml& raw, const ConceptSpec& role, const llvm::StringRef path)
{
    if (auto err = checkPlaceholders(raw.type, role.typePlaceholders, "the " + role.name + " type", path))
    {
        return std::move(err);
    }
    for (const std::string& include : raw.include)
    {
        if (!isIncludeOperand(include))
        {
            return fileError(path,
                             "the " + role.name + " include '" + include +
                                 "' must be an #include operand: <header> or \"header\"");
        }
    }
    Binding binding;
    binding.type     = raw.type;
    binding.includes = raw.include;
    binding.origin   = path.str();
    for (const auto& [name, spelling] : raw.operations.entries)
    {
        const auto* const operation =
            llvm::find_if(role.operations, [&](const Operation& entry) { return entry.name == name; });
        if (operation == role.operations.end())
        {
            return fileError(path,
                             role.name + " has no operation '" + name +
                                 "'; its operations are: " + operationNames(role));
        }
        if (auto err =
                checkPlaceholders(spelling, operation->placeholders, "the " + role.name + " operation " + name, path))
        {
            return std::move(err);
        }
        binding.operations.emplace_back(name, spelling);
    }
    return binding;
}

}  // namespace

llvm::Expected<File> parseFile(const llvm::StringRef text, const llvm::StringRef path)
{
    std::string       diagnostics;
    llvm::yaml::Input in(llvm::MemoryBufferRef(text, path), nullptr, collectDiagnostic, &diagnostics);
    FileYaml          raw;
    in >> raw;
    if (in.error())
    {
        while (!diagnostics.empty() && (diagnostics.back() == '\n'))
        {
            diagnostics.pop_back();
        }
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "%s",
                                       diagnostics.empty() ? ("vocabulary file " + path.str() + ": malformed").c_str()
                                                           : diagnostics.c_str());
    }
    if (raw.version != kFormatVersion)
    {
        return fileError(path,
                         "vocabulary version " + llvm::Twine(raw.version) + " is not one this dsdlc reads; it reads " +
                             llvm::Twine(kFormatVersion));
    }
    const LanguageSpec* const language = languageNamed(raw.language);
    if (language == nullptr)
    {
        return fileError(path, "unknown language '" + raw.language + "'");
    }
    const llvm::StringRef languageName = languageTraits(language->language).name;
    for (const std::string& profile : raw.profiles)
    {
        if (!llvm::is_contained(language->profiles, llvm::StringRef(profile)))
        {
            return fileError(path,
                             "'" + profile + "' is not a " + languageName + " profile" +
                                 (language->profiles.empty() ? llvm::Twine("; ") + languageName + " has none"
                                                             : "; the profiles are: " + joined(language->profiles)));
        }
        if (llvm::count(raw.profiles, profile) > 1)
        {
            return fileError(path, "the profile '" + profile + "' is listed twice");
        }
    }
    if (raw.bindings.entries.empty())
    {
        return fileError(path, "binds nothing");
    }

    File file;
    file.path     = path.str();
    file.language = raw.language;
    file.profiles = raw.profiles;
    for (const auto& [name, rawBinding] : raw.bindings.entries)
    {
        const std::optional<Concept> role = conceptNamed(name);
        if (!role)
        {
            return fileError(path, "unknown concept '" + name + "'; the concepts are: " + conceptNames());
        }
        if (!llvm::is_contained(language->bindable, *role))
        {
            return fileError(path, name + " is fixed in " + languageName + " and cannot be bound");
        }
        auto binding = validateBinding(rawBinding, spec(*role), path);
        if (!binding)
        {
            return binding.takeError();
        }
        file.bindings.emplace_back(*role, std::move(*binding));
    }
    return file;
}

// Resolution.

const Binding& Vocabulary::binding(const Concept role) const
{
    for (const auto& [bound, binding] : bindings_)
    {
        if (bound == role)
        {
            return binding;
        }
    }
    llvm::report_fatal_error("vocabulary: the " + spec(role).name + " concept was not resolved");
}

std::string Vocabulary::type(const Concept role, const Placeholders placeholders) const
{
    return fillPlaceholders(binding(role).type, placeholders);
}

std::string Vocabulary::operation(const Concept role, const llvm::StringRef name, const Placeholders placeholders) const
{
    const Binding& bound = binding(role);
    for (const auto& [overridden, spelling] : bound.operations)
    {
        if (overridden == name)
        {
            return fillPlaceholders(spelling, placeholders);
        }
    }
    for (const Operation& operation : spec(role).operations)
    {
        if (operation.name == name)
        {
            return fillPlaceholders(operation.spelling, placeholders);
        }
    }
    llvm::report_fatal_error("vocabulary: " + spec(role).name + " has no operation '" + name + "'");
}

llvm::ArrayRef<std::string> Vocabulary::includes(const Concept role) const
{
    return binding(role).includes;
}

llvm::Expected<Set> Set::load(const llvm::StringRef language, const llvm::ArrayRef<std::string> paths)
{
    Set set;
    set.language_ = languageNamed(language);
    if (!paths.empty() && ((set.language_ == nullptr) || set.language_->bindable.empty()))
    {
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "--vocabulary does not apply to %s: its backend binds no concept",
                                       language.str().c_str());
    }
    if (set.language_ == nullptr)
    {
        return set;
    }
    if (const auto builtIn = embedded_sources::find("vocabulary/" + language.str() + ".yaml"))
    {
        auto file = parseFile(*builtIn, "built-in");
        if (!file)
        {
            return file.takeError();
        }
        set.files_.push_back(std::move(*file));
    }
    for (const std::string& path : paths)
    {
        auto buffer = llvm::MemoryBuffer::getFile(path);
        if (!buffer)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "cannot read vocabulary file %s: %s",
                                           path.c_str(),
                                           buffer.getError().message().c_str());
        }
        auto file = parseFile((*buffer)->getBuffer(), path);
        if (!file)
        {
            return file.takeError();
        }
        if (file->language != language)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "vocabulary file %s binds %s, and the target language is %s",
                                           path.c_str(),
                                           file->language.c_str(),
                                           language.str().c_str());
        }
        set.files_.push_back(std::move(*file));
    }
    return set;
}

llvm::Expected<Vocabulary> Set::resolve(const llvm::StringRef profile) const
{
    Vocabulary vocabulary;
    if (language_ == nullptr)
    {
        return vocabulary;
    }
    for (const Concept role : language_->bindable)
    {
        const Binding* bound = nullptr;
        for (const File& file : llvm::reverse(files_))
        {
            if (!file.profiles.empty() && !llvm::is_contained(file.profiles, profile.str()))
            {
                continue;
            }
            const auto found = llvm::find_if(file.bindings, [&](const auto& entry) { return entry.first == role; });
            if (found != file.bindings.end())
            {
                bound = &found->second;
                break;
            }
        }
        if (bound == nullptr)
        {
            return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                           "the %s %s profile has no binding for %s: pass --vocabulary <file> with one",
                                           languageTraits(language_->language).name.str().c_str(),
                                           profile.str().c_str(),
                                           spec(role).name.str().c_str());
        }
        vocabulary.bindings_.emplace_back(role, *bound);
    }
    return vocabulary;
}

llvm::ArrayRef<File> Set::files() const
{
    return files_;
}

}  // namespace llvmdsdl::vocabulary
