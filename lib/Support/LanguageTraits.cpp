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
                                        .fieldsApart          = true},
                .lookup              = {.separator                  = "",
                                        .rootPrefix                 = "",
                                        .rootPrefixOnlyWhenShadowed = false,
                                        .enclosingNamespaces        = false,
                                        .ownMembers                 = MemberReach::None,
                                        .selfType                   = "",
                                        .selfInstance               = "",
                                        .selfClass                  = ""},
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
                .definitionName = {.namespaceJoin = "__", .versionInTypeName = true, .typeNameReachesTheType = false},
                .sectionJoin    = "__",
                .sectionNamedAlone              = false,
                .definitionsShareNamespaceScope = true,
                .namespaces                     = NamespaceForm::Joined,
                .fileExtension                  = ".h",
                .directoriesProjected           = false,
                .sourceDirectory                = "",
                .packageDirectory               = false,
                .namespaceFile                  = "",
                .rootFile                       = "",
                .imports                        = ImportNaming::None,
                .helpers                        = HelperNaming::LinkName,
                .qualification                  = Qualification::Shortest,
                .fileAndDirectoryAreOneModule   = false,
                .namespaceAndTypeShareScope     = false,
                .constants                      = ConstantsScope::Enclosing,
                .serviceConstants               = ConstantsScope::Enclosing,
                .constantsAreMacros             = true,
                .generatedConstantSuffix        = "_",
                .arrayMetadataConstants         = true,
                .deprecatedTypeDeclaredApart    = false,
                .freeFunctions                  = {.entryPointJoin       = "__",
                                                   .initializer          = true,
                                                   .accessors            = AccessorNaming::Joined,
                                                   .unionOptionFunctions = true,
                                                   .loweredBodySuffix    = "ir_"},
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
                                        .fieldsApart          = false},
                .lookup              = {.separator                  = "::",
                                        .rootPrefix                 = "::",
                                        .rootPrefixOnlyWhenShadowed = true,
                                        .enclosingNamespaces        = true,
                                        .ownMembers                 = MemberReach::Bare,
                                        .selfType                   = "",
                                        .selfInstance               = "",
                                        .selfClass                  = ""},
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
        // A section is flattened into its service's namespace-scope name rather than nested in it.
        .composition =
            {
                .definitionName    = {.namespaceJoin = "", .versionInTypeName = true, .typeNameReachesTheType = false},
                .sectionJoin       = "_",
                .sectionNamedAlone = false,
                .definitionsShareNamespaceScope = true,
                .namespaces                     = NamespaceForm::Namespace,
                .fileExtension                  = ".hpp",
                .directoriesProjected           = false,
                .sourceDirectory                = "",
                .packageDirectory               = false,
                .namespaceFile                  = "",
                .rootFile                       = "",
                .imports                        = ImportNaming::None,
                .helpers                        = HelperNaming::Binding,
                .qualification                  = Qualification::Rooted,
                .fileAndDirectoryAreOneModule   = false,
                .namespaceAndTypeShareScope     = true,
                .constants                      = ConstantsScope::Type,
                .serviceConstants               = ConstantsScope::Enclosing,
                .constantsAreMacros             = false,
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
                                        .fieldsApart          = true},
                .lookup              = {.separator                  = "::",
                                        .rootPrefix                 = "crate::",
                                        .rootPrefixOnlyWhenShadowed = false,
                                        .enclosingNamespaces        = false,
                                        .ownMembers                 = MemberReach::SelfType,
                                        .selfType                   = "Self",
                                        .selfInstance               = "",
                                        .selfClass                  = ""},
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
                .definitionName    = {.namespaceJoin = "", .versionInTypeName = false, .typeNameReachesTheType = true},
                .sectionJoin       = "",
                .sectionNamedAlone = true,
                .definitionsShareNamespaceScope = false,
                .namespaces                     = NamespaceForm::Module,
                .fileExtension                  = ".rs",
                .directoriesProjected           = true,
                .sourceDirectory                = "src/",
                .packageDirectory               = false,
                .namespaceFile                  = "mod.rs",
                .rootFile                       = "lib.rs",
                .imports                        = ImportNaming::Type,
                .helpers                        = HelperNaming::Module,
                .qualification                  = Qualification::Shortest,
                .fileAndDirectoryAreOneModule   = true,
                .namespaceAndTypeShareScope     = false,
                .constants                      = ConstantsScope::Type,
                .serviceConstants               = ConstantsScope::Module,
                .constantsAreMacros             = false,
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
                                        .fieldsApart          = false},
                .lookup              = {.separator                  = ".",
                                        .rootPrefix                 = "",
                                        .rootPrefixOnlyWhenShadowed = false,
                                        .enclosingNamespaces        = false,
                                        .ownMembers                 = MemberReach::None,
                                        .selfType                   = "",
                                        .selfInstance               = "",
                                        .selfClass                  = ""},
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
                .definitionName    = {.namespaceJoin = "", .versionInTypeName = true, .typeNameReachesTheType = true},
                .sectionJoin       = "",
                .sectionNamedAlone = false,
                .definitionsShareNamespaceScope = true,
                .namespaces                     = NamespaceForm::Package,
                .fileExtension                  = ".go",
                .directoriesProjected           = true,
                .sourceDirectory                = "",
                .packageDirectory               = false,
                .namespaceFile                  = "",
                .rootFile                       = "",
                .imports                        = ImportNaming::Package,
                .helpers                        = HelperNaming::Package,
                .qualification                  = Qualification::Shortest,
                .fileAndDirectoryAreOneModule   = false,
                .namespaceAndTypeShareScope     = false,
                .constants                      = ConstantsScope::Package,
                .serviceConstants               = ConstantsScope::Package,
                .constantsAreMacros             = false,
                .generatedConstantSuffix        = "",
                .arrayMetadataConstants         = false,
                .deprecatedTypeDeclaredApart    = false,
                .freeFunctions                  = {.entryPointJoin       = "",
                                                   .initializer          = false,
                                                   .accessors            = AccessorNaming::Concatenated,
                                                   .unionOptionFunctions = false,
                                                   .loweredBodySuffix    = ""},
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
                                        .fieldsApart          = true},
                .lookup              = {.separator                  = ".",
                                        .rootPrefix                 = "",
                                        .rootPrefixOnlyWhenShadowed = false,
                                        .enclosingNamespaces        = false,
                                        .ownMembers                 = MemberReach::TypeName,
                                        .selfType                   = "",
                                        .selfInstance               = "",
                                        .selfClass                  = ""},
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
        // A type's constants are the module's, where the classification puts them on the type.
        .composition =
            {
                .definitionName    = {.namespaceJoin = "", .versionInTypeName = true, .typeNameReachesTheType = true},
                .sectionJoin       = "",
                .sectionNamedAlone = false,
                .definitionsShareNamespaceScope = false,
                .namespaces                     = NamespaceForm::Module,
                .fileExtension                  = ".ts",
                .directoriesProjected           = true,
                .sourceDirectory                = "",
                .packageDirectory               = false,
                .namespaceFile                  = "",
                .rootFile                       = "index.ts",
                .imports                        = ImportNaming::TypeAndFunctions,
                .helpers                        = HelperNaming::Module,
                .qualification                  = Qualification::Shortest,
                .fileAndDirectoryAreOneModule   = false,
                .namespaceAndTypeShareScope     = false,
                .constants                      = ConstantsScope::Module,
                .serviceConstants               = ConstantsScope::Module,
                .constantsAreMacros             = false,
                .generatedConstantSuffix        = "",
                .arrayMetadataConstants         = false,
                .deprecatedTypeDeclaredApart    = false,
                .freeFunctions                  = {.entryPointJoin       = "",
                                                   .initializer          = false,
                                                   .accessors            = AccessorNaming::VerbFirst,
                                                   .unionOptionFunctions = false,
                                                   .loweredBodySuffix    = ""},
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
                                        .fieldsApart          = false},
                .lookup              = {.separator                  = ".",
                                        .rootPrefix                 = "",
                                        .rootPrefixOnlyWhenShadowed = false,
                                        .enclosingNamespaces        = false,
                                        .ownMembers                 = MemberReach::Instance,
                                        .selfType                   = "",
                                        .selfInstance               = "self",
                                        .selfClass                  = "cls"},
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
        // A type's constants are the module's, where the classification puts them on the class.
        .composition =
            {
                .definitionName    = {.namespaceJoin = "", .versionInTypeName = true, .typeNameReachesTheType = true},
                .sectionJoin       = "",
                .sectionNamedAlone = false,
                .definitionsShareNamespaceScope = false,
                .namespaces                     = NamespaceForm::Module,
                .fileExtension                  = ".py",
                .directoriesProjected           = true,
                .sourceDirectory                = "",
                .packageDirectory               = true,
                .namespaceFile                  = "__init__.py",
                .rootFile                       = "__init__.py",
                .imports                        = ImportNaming::Type,
                .helpers                        = HelperNaming::Module,
                .qualification                  = Qualification::Shortest,
                .fileAndDirectoryAreOneModule   = true,
                .namespaceAndTypeShareScope     = false,
                .constants                      = ConstantsScope::Module,
                .serviceConstants               = ConstantsScope::Module,
                .constantsAreMacros             = false,
                .generatedConstantSuffix        = "",
                .arrayMetadataConstants         = false,
                .deprecatedTypeDeclaredApart    = false,
                .freeFunctions                  = {},
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
