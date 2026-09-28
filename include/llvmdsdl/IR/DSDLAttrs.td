//===----------------------------------------------------------------------===//
//
// Part of the OpenCyphal project, under the MIT licence
// SPDX-License-Identifier: MIT
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
///
/// @file
/// TableGen attribute definitions for the DSDL MLIR dialect.
///
//===----------------------------------------------------------------------===//
#ifndef LLVMDSDL_IR_DSDL_ATTRS_TD
#define LLVMDSDL_IR_DSDL_ATTRS_TD

include "mlir/IR/AttrTypeBase.td"
include "mlir/IR/EnumAttr.td"
include "llvmdsdl/IR/DSDLBase.td"

def DSDL_RationalAttr : AttrDef<DSDL_Dialect, "Rational"> {
  let mnemonic = "rational";
  let summary = "Canonical rational value (numerator/denominator)";
  let parameters = (ins StringRefParameter<"canonical form">:$value);
  let assemblyFormat = "`<` $value `>`";
}

def DSDL_VersionAttr : AttrDef<DSDL_Dialect, "Version"> {
  let mnemonic = "version";
  let summary = "Major/minor type version";
  let parameters = (ins "uint32_t":$major, "uint32_t":$minor);
  let assemblyFormat = "`<` $major `,` $minor `>`";
}

//===----------------------------------------------------------------------===//
// The surface tree
//===----------------------------------------------------------------------===//

def DSDL_ScopeKind : I32Enum<"ScopeKind", "What a scope of the surface tree is",
                             [I32EnumCase<"Root", 0, "root">,
                              I32EnumCase<"Namespace", 1, "namespace">,
                              I32EnumCase<"Module", 2, "module">,
                              I32EnumCase<"Package", 3, "package">,
                              I32EnumCase<"File", 4, "file">,
                              I32EnumCase<"Type", 5, "type">]> {
  let cppNamespace = "::mlir::dsdl";
}

def DSDL_ScopeKindAttr : EnumAttr<DSDL_Dialect, DSDL_ScopeKind, "scope_kind">;

def DSDL_DeclKind
    : I32Enum<"DeclKind", "What a declaration of the surface tree declares",
              [I32EnumCase<"Field", 0, "field">,
               I32EnumCase<"Constant", 1, "constant">,
               I32EnumCase<"Option", 2, "option">,
               I32EnumCase<"Entry", 3, "entry">,
               I32EnumCase<"Accessor", 4, "accessor">,
               I32EnumCase<"Helper", 5, "helper">,
               I32EnumCase<"Method", 6, "method">,
               I32EnumCase<"Wrapper", 7, "wrapper">,
               I32EnumCase<"Alias", 8, "alias">, I32EnumCase<"Tag", 9, "tag">,
               I32EnumCase<"Import", 10, "import">,
               I32EnumCase<"Module", 11, "module">,
               I32EnumCase<"Guard", 12, "guard">]> {
  let cppNamespace = "::mlir::dsdl";
}

def DSDL_DeclKindAttr : EnumAttr<DSDL_Dialect, DSDL_DeclKind, "decl_kind">;

def DSDL_NameClass
    : I32Enum<"NameClass", "The class of names a declaration is made in",
              [I32EnumCase<"Value", 0, "value">, I32EnumCase<"Type", 1, "type">,
               I32EnumCase<"Module", 2, "module">, I32EnumCase<"Tag", 3, "tag">,
               I32EnumCase<"Macro", 4, "macro">]> {
  let cppNamespace = "::mlir::dsdl";
}

def DSDL_NameClassAttr : EnumAttr<DSDL_Dialect, DSDL_NameClass, "name_class">;

def DSDL_Visibility
    : I32Enum<"Visibility",
              "Whether a declaration is part of the output's interface",
              [I32EnumCase<"Public", 0, "public">,
               I32EnumCase<"Private", 1, "private">]> {
  let cppNamespace = "::mlir::dsdl";
}

def DSDL_VisibilityAttr : EnumAttr<DSDL_Dialect, DSDL_Visibility, "visibility">;

def DSDL_Origin : I32Enum<"Origin", "Where a declared name comes from",
                          [I32EnumCase<"Definition", 0, "definition">,
                           I32EnumCase<"Generated", 1, "generated">]> {
  let cppNamespace = "::mlir::dsdl";
}

def DSDL_OriginAttr : EnumAttr<DSDL_Dialect, DSDL_Origin, "origin">;

#endif // LLVMDSDL_IR_DSDL_ATTRS_TD
