//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

#include <iostream>
#include <string>
#include <vector>

#include <llvm/ADT/ArrayRef.h>
#include <llvm/Support/Error.h>

#include "llvmdsdl/CodeGen/Vocabulary.h"

#include "UnitTests.h"

namespace
{

using llvmdsdl::vocabulary::Concept;
using llvmdsdl::vocabulary::parseFile;
using llvmdsdl::vocabulary::Set;

/// @brief Parses @p text expecting an error, and checks the message holds @p fragment.
bool rejects(const std::string& text, const std::string& fragment, const std::string& what)
{
    auto file = parseFile(text, "test.yaml");
    if (file)
    {
        std::cerr << what << ": accepted\n";
        return false;
    }
    const std::string message = llvm::toString(file.takeError());
    if (!message.contains(fragment))
    {
        std::cerr << what << ": the error does not say '" << fragment << "': " << message << "\n";
        return false;
    }
    return true;
}

constexpr const char* const kCetl = "vocabulary: 1\n"
                                    "language: cpp\n"
                                    "profiles: [autosar]\n"
                                    "bindings:\n"
                                    "  span:\n"
                                    "    type: \"cetl::pf20::span<{element}>\"\n"
                                    "    include: ['\"cetl/pf20/span.hpp\"']\n";

constexpr const char* const kAcme = "vocabulary: 1\n"
                                    "language: cpp\n"
                                    "bindings:\n"
                                    "  span:\n"
                                    "    type: \"acme::ByteView<{element}>\"\n"
                                    "    include: ['\"acme/byte_view.hpp\"', \"<cstddef>\"]\n"
                                    "    operations:\n"
                                    "      data: \"{self}.ptr()\"\n"
                                    "      size: \"{self}.length()\"\n"
                                    "      subspan: \"{self}.tail({offset})\"\n"
                                    "      make: \"{type}::of({data}, {size})\"\n";

bool runParsing()
{
    auto cetl = parseFile(kCetl, "cetl.yaml");
    if (!cetl)
    {
        std::cerr << "the CETL file was rejected: " << llvm::toString(cetl.takeError()) << "\n";
        return false;
    }
    if (cetl->language != "cpp" || cetl->profiles != std::vector<std::string>{"autosar"} ||
        cetl->bindings.size() != 1 || cetl->bindings.front().first != Concept::Span ||
        cetl->bindings.front().second.type != "cetl::pf20::span<{element}>" ||
        cetl->bindings.front().second.includes != std::vector<std::string>{"\"cetl/pf20/span.hpp\""} ||
        !cetl->bindings.front().second.operations.empty() || cetl->bindings.front().second.origin != "cetl.yaml")
    {
        std::cerr << "the CETL file was read wrongly\n";
        return false;
    }

    auto acme = parseFile(kAcme, "acme.yaml");
    if (!acme)
    {
        std::cerr << "the acme file was rejected: " << llvm::toString(acme.takeError()) << "\n";
        return false;
    }
    if (!acme->profiles.empty() || acme->bindings.front().second.operations.size() != 4)
    {
        std::cerr << "the acme file was read wrongly\n";
        return false;
    }

    return rejects("vocabulary: 2\nlanguage: cpp\nbindings:\n  span:\n    type: \"x<{element}>\"\n    include: []\n",
                   "vocabulary version 2",
                   "another version") &&
           rejects("language: cpp\nbindings:\n  span:\n    type: \"x<{element}>\"\n    include: []\n",
                   "missing required key 'vocabulary'",
                   "no version") &&
           rejects("vocabulary: 1\nlanguage: cobol\nbindings:\n  span:\n    type: \"x<{element}>\"\n    include: []\n",
                   "unknown language 'cobol'",
                   "an unknown language") &&
           rejects("vocabulary: 1\nlanguage: cpp\nprofiles: [fast]\nbindings:\n  span:\n    type: \"x<{element}>\"\n"
                   "    include: []\n",
                   "'fast' is not a cpp profile",
                   "an unknown profile") &&
           rejects("vocabulary: 1\nlanguage: cpp\nprofiles: [std, std]\nbindings:\n  span:\n"
                   "    type: \"x<{element}>\"\n    include: []\n",
                   "listed twice",
                   "a repeated profile") &&
           rejects("vocabulary: 1\nlanguage: cpp\nbindings: {}\n", "binds nothing", "no bindings") &&
           rejects("vocabulary: 1\nlanguage: cpp\nbindings:\n  vector:\n    type: \"x<{element}>\"\n    include: []\n",
                   "unknown concept 'vector'",
                   "an unknown concept") &&
           rejects("vocabulary: 1\nlanguage: rust\nbindings:\n  span:\n    type: \"x<{element}>\"\n    include: []\n",
                   "span is fixed in rust",
                   "a concept the language fixes") &&
           rejects("vocabulary: 1\nlanguage: cpp\nbindings:\n  span:\n    type: \"x<{elem}>\"\n    include: []\n",
                   "unknown placeholder '{elem}'",
                   "an unknown placeholder") &&
           rejects("vocabulary: 1\nlanguage: cpp\nbindings:\n  span:\n    type: \"x\"\n    include: []\n",
                   "must use the placeholder '{element}'",
                   "a missing placeholder") &&
           rejects("vocabulary: 1\nlanguage: cpp\nbindings:\n  span:\n    type: \"x<{element>\"\n    include: []\n",
                   "no '}' closes",
                   "an unclosed placeholder") &&
           rejects("vocabulary: 1\nlanguage: cpp\nbindings:\n  span:\n    type: \"x<{element}>\"\n"
                   "    include: [span]\n",
                   "must be an #include operand",
                   "a bare include") &&
           rejects("vocabulary: 1\nlanguage: cpp\nbindings:\n  span:\n    type: \"x<{element}>\"\n    include: []\n"
                   "    operations:\n      first: \"{self}.front()\"\n",
                   "span has no operation 'first'",
                   "an unknown operation") &&
           rejects("vocabulary: 1\nlanguage: cpp\nbindings:\n  span:\n    type: \"x<{element}>\"\n    include: []\n"
                   "    operations:\n      subspan: \"{self}.tail()\"\n",
                   "must use the placeholder '{offset}'",
                   "an operation missing a placeholder") &&
           rejects("vocabulary: 1\nlanguage: cpp\nbindings:\n  span:\n    type: \"x<{element}>\"\n    include: []\n"
                   "    colour: red\n",
                   "unknown key 'colour'",
                   "an unknown key") &&
           rejects("vocabulary: 1\nlanguage: cpp\nbindings:\n  span:\n    type: \"x<{element}>\"\n    include: []\n"
                   "  span:\n    type: \"y<{element}>\"\n    include: []\n",
                   "duplicated mapping key 'span'",
                   "a concept bound twice in one file") &&
           rejects("vocabulary: 1\nlanguage: cpp\nbindings:\n  span:\n    type: \"x<{element}>\"\n",
                   "missing required key 'include'",
                   "a binding without includes");
}

bool runResolution()
{
    // The built-in binding alone: std and pmr resolve to std::span, and autosar to nothing.
    auto builtIn = Set::load("cpp", {});
    if (!builtIn)
    {
        std::cerr << "the built-in cpp vocabulary failed to load: " << llvm::toString(builtIn.takeError()) << "\n";
        return false;
    }
    if (builtIn->files().size() != 1 || builtIn->files().front().path != "built-in")
    {
        std::cerr << "the built-in cpp vocabulary is not one file named built-in\n";
        return false;
    }
    for (const char* const profile : {"std", "pmr"})
    {
        auto vocabulary = builtIn->resolve(profile);
        if (!vocabulary)
        {
            std::cerr << profile << " did not resolve: " << llvm::toString(vocabulary.takeError()) << "\n";
            return false;
        }
        if (vocabulary->type(Concept::Span, {{"element", "const std::uint8_t"}}) != "std::span<const std::uint8_t>" ||
            vocabulary->operation(Concept::Span, "data", {{"self", "buffer"}}) != "buffer.data()" ||
            vocabulary->operation(Concept::Span, "size", {{"self", "buffer"}}) != "buffer.size()" ||
            vocabulary->operation(Concept::Span, "subspan", {{"self", "buf"}, {"offset", "at"}}) != "buf.subspan(at)" ||
            vocabulary->operation(Concept::Span, "make", {{"type", "S"}, {"data", "p"}, {"size", "n"}}) != "S(p, n)" ||
            vocabulary->includes(Concept::Span) != llvm::ArrayRef<std::string>{"<span>"} ||
            vocabulary->binding(Concept::Span).origin != "built-in")
        {
            std::cerr << profile << " resolved span to something other than std::span\n";
            return false;
        }
    }
    {
        auto autosar = builtIn->resolve("autosar");
        if (autosar)
        {
            std::cerr << "autosar resolved without a binding\n";
            return false;
        }
        const std::string message = llvm::toString(autosar.takeError());
        if (!message.contains("the cpp autosar profile has no binding for span") || !message.contains("--vocabulary"))
        {
            std::cerr << "the unbound error does not name the profile, the concept and the flag: " << message << "\n";
            return false;
        }
    }

    // A language that binds nothing loads an empty set, and refuses any file.
    auto rust = Set::load("rust", {});
    if (!rust || !rust->files().empty() || !rust->resolve("std"))
    {
        std::cerr << "rust did not load an empty vocabulary\n";
        return false;
    }
    {
        const std::vector<std::string> files{"anything.yaml"};
        auto                           refused = Set::load("rust", files);
        if (refused)
        {
            std::cerr << "rust accepted a vocabulary file\n";
            return false;
        }
        if (!llvm::toString(refused.takeError()).contains("--vocabulary does not apply to rust"))
        {
            std::cerr << "rust's refusal does not say why\n";
            return false;
        }
    }
    {
        const std::vector<std::string> files{"/nonexistent/vocabulary.yaml"};
        auto                           missing = Set::load("cpp", files);
        if (missing || !llvm::toString(missing.takeError()).contains("cannot read vocabulary file"))
        {
            std::cerr << "a missing file was not reported as unreadable\n";
            return false;
        }
    }
    return true;
}

}  // namespace

bool runVocabularyTests()
{
    return runParsing() && runResolution();
}
