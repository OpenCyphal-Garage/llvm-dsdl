//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// The row of each language dsdlc generates.
///
//===----------------------------------------------------------------------===//

#include "llvmdsdl/Support/LanguageTraits.h"

#include <array>
#include <cstddef>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"

#include "llvmdsdl/Support/BodyInterface.h"
#include "llvmdsdl/Support/Language.h"

namespace llvmdsdl
{
namespace
{

// In the order of `Language`, which `languageTraits` indexes by.
constexpr std::array<LanguageTraits, 6> kTraits{{
    {
        .language = Language::C,
        .name     = "c",
        .classification =
            {
                .scopes              = {},
                .nestedTypes         = false,
                .methods             = MethodForm::None,
                .internalLinkage     = InternalLinkage::Static,
                .errors              = ErrorConvention::StatusCode,
                .typeConstants       = ConstantsScope::Enclosing,
                .reservedUnderscores = ReservedUnderscores::Leading,
                .nameClasses         = {.typesApartFromValues = false,
                                        .modulesAmongTypes    = false,
                                        .tags                 = true,
                                        .macros               = true,
                                        .fieldsApart          = true,
                                        .typeNameAmongMembers = false},
                .lookup              = {.separator                  = "",
                                        .rootPrefix                 = "",
                                        .rootPrefixOnlyWhenShadowed = false,
                                        .enclosingNamespaces        = false,
                                        .ownMembers                 = MemberReach::None,
                                        .selfType                   = "",
                                        .selfInstance               = "",
                                        .selfClass                  = ""},
                .ifExpressions       = false,
            },
        // Handed pointers throughout, and a structure is its bytes.
        .body =
            {
                .nullability              = {.objectPointer = true, .rawPointer = true, .accessorBuffer = true},
                .accessorsReturnViews     = false,
                .accessorsTakeMemberTypes = true,
                .objectsAreByteImages     = true,
                .bodiesAnswerSize         = false,
                .boolArrays               = BoolArrayStorage::Packed,
            },
        // No scope below the file, so a composed name carries the whole path.
        .composition =
            {
                .definitionName   = {.namespaceJoin = "__", .versionInTypeName = true, .typeNameReachesTheType = false},
                .sectionJoin      = "__",
                .sectionEnclosure = SectionEnclosure::None,
                .definitionsShareNamespaceScope = true,
                .namespaces                     = NamespaceForm::Joined,
                .fileExtension                  = ".h",
                .directoriesProjected           = false,
                .sourceDirectory                = "",
                .packageDirectory               = false,
                .namespaceFile                  = "",
                .rootFile                       = "",
                .imports                        = ImportNaming::None,
                .helpers                        = HelperPlacement::Module,
                .fileAndDirectoryAreOneModule   = false,
                .namespaceAndTypeShareScope     = false,
                .constantsAreMacros             = true,
                .serviceConstants               = ConstantsScope::Enclosing,
                .generatedConstantSuffix        = "_",
                .arrayMetadataConstants         = true,
                .deprecatedTypeDeclaredApart    = false,
                .freeFunctions                  = {.entryPointJoin       = "__",
                                                   .initializer          = true,
                                                   .accessors            = AccessorNaming::Joined,
                                                   .unionOptionFunctions = true,
                                                   .bodiesCompiledApart  = true},
            },
    },
    {
        .language = Language::Cpp,
        .name     = "cpp",
        .classification =
            {
                .scopes              = {.namespaces = true, .classes = true, .modules = false},
                .nestedTypes         = true,
                .methods             = MethodForm::Member,
                .internalLinkage     = InternalLinkage::PrivateMember,
                .errors              = ErrorConvention::StatusCode,
                .typeConstants       = ConstantsScope::Type,
                .reservedUnderscores = ReservedUnderscores::LeadingAndInterior,
                .nameClasses         = {.typesApartFromValues = false,
                                        .modulesAmongTypes    = true,
                                        .tags                 = false,
                                        .macros               = true,
                                        .fieldsApart          = false,
                                        .typeNameAmongMembers = true},
                .lookup              = {.separator                  = "::",
                                        .rootPrefix                 = "::",
                                        .rootPrefixOnlyWhenShadowed = true,
                                        .enclosingNamespaces        = true,
                                        .ownMembers                 = MemberReach::Bare,
                                        .selfType                   = "",
                                        .selfInstance               = "",
                                        .selfClass                  = ""},
                .ifExpressions       = false,
            },
        // Serialise and deserialise are members, handed their buffer and its size by pointer; a
        // field accessor takes a span.
        .body =
            {
                .nullability              = {.objectPointer = false, .rawPointer = true, .accessorBuffer = false},
                .accessorsReturnViews     = true,
                .accessorsTakeMemberTypes = false,
                .objectsAreByteImages     = true,
                .bodiesAnswerSize         = false,
                .boolArrays               = BoolArrayStorage::PackedWhenFixed,
            },
        // A service is a struct that holds its own facts and encloses its two sections.
        .composition =
            {
                .definitionName   = {.namespaceJoin = "", .versionInTypeName = true, .typeNameReachesTheType = false},
                .sectionJoin      = "",
                .sectionEnclosure = SectionEnclosure::ServiceType,
                .definitionsShareNamespaceScope = true,
                .namespaces                     = NamespaceForm::Namespace,
                .fileExtension                  = ".hpp",
                .directoriesProjected           = false,
                .sourceDirectory                = "",
                .packageDirectory               = false,
                .namespaceFile                  = "",
                .rootFile                       = "",
                .imports                        = ImportNaming::None,
                .helpers                        = HelperPlacement::Type,
                .fileAndDirectoryAreOneModule   = false,
                .namespaceAndTypeShareScope     = true,
                .constantsAreMacros             = false,
                .serviceConstants               = ConstantsScope::Type,
                .generatedConstantSuffix        = "",
                .arrayMetadataConstants         = true,
                .deprecatedTypeDeclaredApart    = true,
                .freeFunctions                  = {},
            },
    },
    {
        .language = Language::Rust,
        .name     = "rust",
        .classification =
            {
                .scopes              = {.namespaces = false, .classes = false, .modules = true},
                .nestedTypes         = false,
                .methods             = MethodForm::Impl,
                .internalLinkage     = InternalLinkage::PrivateByDefault,
                .errors              = ErrorConvention::Result,
                .typeConstants       = ConstantsScope::Type,
                .reservedUnderscores = ReservedUnderscores::None,
                .nameClasses         = {.typesApartFromValues = true,
                                        .modulesAmongTypes    = true,
                                        .tags                 = false,
                                        .macros               = false,
                                        .fieldsApart          = true,
                                        .typeNameAmongMembers = false},
                .lookup              = {.separator                  = "::",
                                        .rootPrefix                 = "crate::",
                                        .rootPrefixOnlyWhenShadowed = false,
                                        .enclosingNamespaces        = false,
                                        .ownMembers                 = MemberReach::SelfType,
                                        .selfType                   = "Self",
                                        .selfInstance               = "",
                                        .selfClass                  = ""},
                .ifExpressions       = true,
            },
        // Handed a reference, a slice and a local, none of which can be null. A type encodes itself
        // into new bytes, and reads a new value of itself and the bytes it read; a view borrows what
        // it reads for the value's lifetime.
        .body =
            {
                .nullability              = {.objectPointer = false, .rawPointer = false, .accessorBuffer = false},
                .accessorsReturnViews     = true,
                .accessorsTakeMemberTypes = false,
                .objectsAreByteImages     = true,
                .bodiesAnswerSize         = true,
                .boolArrays               = BoolArrayStorage::PerElement,
                .wireImage                = {.appends            = false,
                                             .answersNew         = true,
                                             .reads              = false,
                                             .makes              = true,
                                             .makesAnswerUsed    = true,
                                             .readerKeepsNothing = false},
            },
        // Each definition and version is a module, which is what encloses a service's sections.
        .composition =
            {
                .definitionName   = {.namespaceJoin = "", .versionInTypeName = false, .typeNameReachesTheType = true},
                .sectionJoin      = "",
                .sectionEnclosure = SectionEnclosure::Module,
                .definitionsShareNamespaceScope = false,
                .namespaces                     = NamespaceForm::Module,
                .fileExtension                  = ".rs",
                .directoriesProjected           = true,
                .sourceDirectory                = "src/",
                .packageDirectory               = false,
                .namespaceFile                  = "mod.rs",
                .rootFile                       = "lib.rs",
                .imports                        = ImportNaming::Type,
                .helpers                        = HelperPlacement::Module,
                .fileAndDirectoryAreOneModule   = true,
                .namespaceAndTypeShareScope     = false,
                .constantsAreMacros             = false,
                .serviceConstants               = ConstantsScope::Module,
                .generatedConstantSuffix        = "",
                .arrayMetadataConstants         = false,
                .deprecatedTypeDeclaredApart    = true,
                .freeFunctions                  = {},
            },
    },
    {
        .language = Language::Go,
        .name     = "go",
        .classification =
            {
                .scopes              = {},
                .nestedTypes         = false,
                .methods             = MethodForm::Receiver,
                .internalLinkage     = InternalLinkage::LowerCaseInitial,
                .errors              = ErrorConvention::ValueAndError,
                .typeConstants       = ConstantsScope::Package,
                .reservedUnderscores = ReservedUnderscores::None,
                .nameClasses         = {.typesApartFromValues = false,
                                        .modulesAmongTypes    = false,
                                        .tags                 = false,
                                        .macros               = false,
                                        .fieldsApart          = false,
                                        .typeNameAmongMembers = false},
                .lookup              = {.separator                  = ".",
                                        .rootPrefix                 = "",
                                        .rootPrefixOnlyWhenShadowed = false,
                                        .enclosingNamespaces        = false,
                                        .ownMembers                 = MemberReach::None,
                                        .selfType                   = "",
                                        .selfInstance               = "",
                                        .selfClass                  = ""},
                .ifExpressions       = false,
            },
        // Handed an object a caller may still omit, beside a slice. The encoding package's
        // BinaryAppender, BinaryMarshaler and BinaryUnmarshaler move the wire image, and the last may
        // keep none of the data it is handed.
        .body =
            {
                .nullability              = {.objectPointer = true, .rawPointer = false, .accessorBuffer = false},
                .accessorsReturnViews     = true,
                .accessorsTakeMemberTypes = false,
                .objectsAreByteImages     = true,
                .bodiesAnswerSize         = true,
                .boolArrays               = BoolArrayStorage::PerElement,
                .wireImage =
                    {.appends = true, .answersNew = true, .reads = true, .makes = false, .readerKeepsNothing = true},
            },
        // A package holds a whole DSDL namespace, so a constant's name carries its type's.
        .composition =
            {
                .definitionName   = {.namespaceJoin = "", .versionInTypeName = true, .typeNameReachesTheType = true},
                .sectionJoin      = "",
                .sectionEnclosure = SectionEnclosure::None,
                .definitionsShareNamespaceScope = true,
                .namespaces                     = NamespaceForm::Package,
                .fileExtension                  = ".go",
                .directoriesProjected           = true,
                .sourceDirectory                = "",
                .packageDirectory               = false,
                .namespaceFile                  = "doc.go",
                .rootFile                       = "",
                .imports                        = ImportNaming::Package,
                .helpers                        = HelperPlacement::Package,
                .fileAndDirectoryAreOneModule   = false,
                .namespaceAndTypeShareScope     = false,
                .constantsAreMacros             = false,
                .serviceConstants               = ConstantsScope::Package,
                .generatedConstantSuffix        = "",
                .arrayMetadataConstants         = false,
                .deprecatedTypeDeclaredApart    = false,
                .freeFunctions                  = {.entryPointJoin       = "",
                                                   .initializer          = false,
                                                   .accessors            = AccessorNaming::Concatenated,
                                                   .unionOptionFunctions = false,
                                                   .bodiesCompiledApart  = false},
            },
    },
    {
        .language = Language::TypeScript,
        .name     = "ts",
        .classification =
            {
                .scopes              = {.namespaces = true, .classes = true, .modules = false},
                .nestedTypes         = true,
                .methods             = MethodForm::Member,
                .internalLinkage     = InternalLinkage::NotExported,
                .errors              = ErrorConvention::Exception,
                .typeConstants       = ConstantsScope::Type,
                .reservedUnderscores = ReservedUnderscores::None,
                .nameClasses         = {.typesApartFromValues = true,
                                        .modulesAmongTypes    = false,
                                        .tags                 = false,
                                        .macros               = false,
                                        .fieldsApart          = true,
                                        .typeNameAmongMembers = false},
                .lookup              = {.separator                  = ".",
                                        .rootPrefix                 = "",
                                        .rootPrefixOnlyWhenShadowed = false,
                                        .enclosingNamespaces        = false,
                                        .ownMembers                 = MemberReach::TypeName,
                                        .selfType                   = "",
                                        .selfInstance               = "",
                                        .selfClass                  = ""},
                .ifExpressions       = false,
            },
        // Handed an object a caller may still omit, beside a `Uint8Array`; an object has no layout. A
        // module encodes a value into new bytes, and reads a new value and the bytes it read.
        .body =
            {
                .nullability              = {.objectPointer = true, .rawPointer = false, .accessorBuffer = false},
                .accessorsReturnViews     = true,
                .accessorsTakeMemberTypes = false,
                .objectsAreByteImages     = false,
                .bodiesAnswerSize         = true,
                .boolArrays               = BoolArrayStorage::PerElement,
                .wireImage                = {.appends            = false,
                                             .answersNew         = true,
                                             .reads              = false,
                                             .makes              = true,
                                             .makesAnswerUsed    = true,
                                             .readerKeepsNothing = false},
            },
        // A type is an interface, and a `const` of the same name holds its constants, its facts and its
        // functions.
        .composition =
            {
                .definitionName   = {.namespaceJoin = "", .versionInTypeName = true, .typeNameReachesTheType = true},
                .sectionJoin      = "",
                .sectionEnclosure = SectionEnclosure::None,
                .definitionsShareNamespaceScope = false,
                .namespaces                     = NamespaceForm::Module,
                .fileExtension                  = ".ts",
                .directoriesProjected           = true,
                .sourceDirectory                = "",
                .packageDirectory               = false,
                .namespaceFile                  = "index.ts",
                .rootFile                       = "index.ts",
                .imports                        = ImportNaming::Type,
                .helpers                        = HelperPlacement::Module,
                .fileAndDirectoryAreOneModule   = true,
                .namespaceAndTypeShareScope     = false,
                .constantsAreMacros             = false,
                .serviceConstants               = ConstantsScope::Module,
                .generatedConstantSuffix        = "",
                .arrayMetadataConstants         = false,
                .deprecatedTypeDeclaredApart    = false,
                .freeFunctions                  = {.entryPointJoin       = "",
                                                   .initializer          = false,
                                                   .accessors            = AccessorNaming::None,
                                                   .unionOptionFunctions = false,
                                                   .bodiesCompiledApart  = false},
            },
    },
    {
        .language = Language::Python,
        .name     = "python",
        .classification =
            {
                .scopes              = {.namespaces = false, .classes = true, .modules = false},
                .nestedTypes         = true,
                .methods             = MethodForm::Member,
                .internalLinkage     = InternalLinkage::UnderscorePrefix,
                .errors              = ErrorConvention::Exception,
                .typeConstants       = ConstantsScope::Type,
                .reservedUnderscores = ReservedUnderscores::None,
                .nameClasses         = {.typesApartFromValues = false,
                                        .modulesAmongTypes    = true,
                                        .tags                 = false,
                                        .macros               = false,
                                        .fieldsApart          = false,
                                        .typeNameAmongMembers = false},
                .lookup              = {.separator                  = ".",
                                        .rootPrefix                 = "",
                                        .rootPrefixOnlyWhenShadowed = false,
                                        .enclosingNamespaces        = false,
                                        .ownMembers                 = MemberReach::Instance,
                                        .selfType                   = "",
                                        .selfInstance               = "self",
                                        .selfClass                  = "cls"},
                .ifExpressions       = false,
            },
        // Handed an object a caller may still omit, beside a `memoryview`; an object has no layout. A
        // class serialises itself into new bytes, and deserialises a new instance of itself.
        .body =
            {
                .nullability              = {.objectPointer = true, .rawPointer = false, .accessorBuffer = false},
                .accessorsReturnViews     = true,
                .accessorsTakeMemberTypes = false,
                .objectsAreByteImages     = false,
                .bodiesAnswerSize         = true,
                .boolArrays               = BoolArrayStorage::PerElement,
                .wireImage =
                    {.appends = false, .answersNew = true, .reads = false, .makes = true, .readerKeepsNothing = false},
            },
        // A service is named by an alias of its request, so its own facts are the module's.
        .composition =
            {
                .definitionName   = {.namespaceJoin = "", .versionInTypeName = true, .typeNameReachesTheType = true},
                .sectionJoin      = "",
                .sectionEnclosure = SectionEnclosure::None,
                .definitionsShareNamespaceScope = false,
                .namespaces                     = NamespaceForm::Module,
                .fileExtension                  = ".py",
                .directoriesProjected           = true,
                .sourceDirectory                = "",
                .packageDirectory               = true,
                .namespaceFile                  = "__init__.py",
                .rootFile                       = "__init__.py",
                .imports                        = ImportNaming::Type,
                .helpers                        = HelperPlacement::Type,
                .fileAndDirectoryAreOneModule   = true,
                .namespaceAndTypeShareScope     = false,
                .constantsAreMacros             = false,
                .serviceConstants               = ConstantsScope::Module,
                .generatedConstantSuffix        = "",
                .arrayMetadataConstants         = false,
                .deprecatedTypeDeclaredApart    = false,
                .freeFunctions                  = {},
                // The width the DSDL sources are written to, so their documentation keeps its lines.
                .lineLength = 120,
            },
    },
}};

// `languageTraits` indexes the table by the enumerator, so a row out of order answers for the wrong
// language rather than failing.
constexpr bool inEnumerationOrder()
{
    for (std::size_t i = 0; i < kTraits.size(); ++i)
    {
        if (static_cast<std::size_t>(kTraits[i].language) != i)
        {
            return false;
        }
    }
    return true;
}
static_assert(inEnumerationOrder(), "kTraits must list the languages in the order of `Language`");

}  // namespace

llvm::ArrayRef<LanguageTraits> allLanguageTraits()
{
    return kTraits;
}

const LanguageTraits& languageTraits(const Language language)
{
    return kTraits[static_cast<std::size_t>(language)];
}

const LanguageTraits* languageTraitsNamed(const llvm::StringRef name)
{
    for (const LanguageTraits& row : kTraits)
    {
        if (row.name == name)
        {
            return &row;
        }
    }
    return nullptr;
}

}  // namespace llvmdsdl
