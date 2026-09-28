// RUN: not %dsdl-opt --split-input-file %s 2>&1 | FileCheck %s

// The surface verifier rejects a tree that its target could not compile or write: a name declared
// twice in one class, a class the language does not have, a path written twice or outside the
// output directory, and an entity named by `of` that the module does not hold.

// A second value of one name in one scope.
// CHECK: error: 'dsdl.decl' op declares 'serialize' as a value in scope 'Request', which already declares it as a value
// CHECK: note: declared here
dsdl.surface target = "rust" {
  dsdl.scope root "llvmdsdl_generated" path = "src/lib.rs" {
    dsdl.scope type "Request" {
      dsdl.decl "serialize" kind = entry
      dsdl.decl "serialize" kind = method origin = generated
    }
  }
}

// -----

// C++ declares a namespace beside a structure, and the two may not share a name.
// CHECK: error: 'dsdl.scope' op declares 'file' as a module in scope 'uavcan', which already declares it as a type
// CHECK: note: declared here
dsdl.surface target = "cpp" profile = "std" {
  dsdl.scope root "dsdl_out" {
    dsdl.scope namespace "uavcan" {
      dsdl.scope type "file" {
      }
      dsdl.scope namespace "file" {
      }
    }
  }
}

// -----

// Rust declares a definition's file and a namespace's directory as one module.
// CHECK: error: 'dsdl.scope' op declares 'file_1_0' as a module in scope 'ns', which already declares it as a module
// CHECK: note: declared here
dsdl.surface target = "rust" {
  dsdl.scope root "llvmdsdl_generated" path = "src/lib.rs" {
    dsdl.scope namespace "ns" path = "src/ns/mod.rs" {
      dsdl.scope module "file_1_0" path = "src/ns/file_1_0.rs" {
      }
      dsdl.scope namespace "file_1_0" path = "src/ns/file_1_0/mod.rs" {
      }
    }
  }
}

// -----

// A Go package is one class of names across every file in it.
// CHECK: error: 'dsdl.decl' op declares 'Path' as a value in scope 'file', which already declares it as a type
// CHECK: note: declared here
dsdl.surface target = "go" {
  dsdl.scope root "m" {
    dsdl.scope package "file" {
      dsdl.scope file "path_2_0" path = "file/path_2_0.go" {
        dsdl.scope type "Path" {
        }
      }
      dsdl.scope file "list_0_2" path = "file/list_0_2.go" {
        dsdl.decl "Path" kind = constant
      }
    }
  }
}

// -----

// A Go file's import may not meet a name its package declares.
// CHECK: error: 'dsdl.decl' op imports 'dsdlruntime' into file 'list_0_2', where scope 'file' declares it as a value
// CHECK: note: declared here
dsdl.surface target = "go" {
  dsdl.scope root "m" {
    dsdl.scope package "file" {
      dsdl.scope file "path_2_0" path = "file/path_2_0.go" {
        dsdl.decl "dsdlruntime" kind = constant
      }
      dsdl.scope file "list_0_2" path = "file/list_0_2.go" {
        dsdl.decl "dsdlruntime" kind = import class = value
      }
    }
  }
}

// -----

// Rust has no structure tags.
// CHECK: error: 'dsdl.decl' op declares 'Foo' as a tag, a class of name a rust surface does not have
dsdl.surface target = "rust" {
  dsdl.scope root "llvmdsdl_generated" path = "src/lib.rs" {
    dsdl.decl "Foo" kind = tag
  }
}

// -----

// A C macro is one name across every header a translation unit can include.
// CHECK: error: 'dsdl.decl' op declares 'NS_INCLUDED_' as a macro in the translation unit, which already declares it as a macro
// CHECK: note: declared here
dsdl.surface target = "c" {
  dsdl.scope root "dsdl_out" {
    dsdl.scope file "A_1_0" path = "ns/A_1_0.h" {
      dsdl.decl "NS_INCLUDED_" kind = guard origin = generated
    }
    dsdl.scope file "B_1_0" path = "ns/B_1_0.h" {
      dsdl.decl "NS_INCLUDED_" kind = guard origin = generated
    }
  }
}

// -----

// CHECK: error: 'dsdl.scope' op is written to 'ns/A_1_0.h', as scope 'A_1_0' is
// CHECK: note: written here
dsdl.surface target = "c" {
  dsdl.scope root "dsdl_out" {
    dsdl.scope file "A_1_0" path = "ns/A_1_0.h" {
    }
    dsdl.scope file "a_1_0" path = "ns/A_1_0.h" {
    }
  }
}

// -----

// CHECK: error: 'dsdl.scope' op is written to '../A_1_0.h', which is not a path within the output directory
dsdl.surface target = "c" {
  dsdl.scope root "dsdl_out" {
    dsdl.scope file "A_1_0" path = "../A_1_0.h" {
    }
  }
}

// -----

// CHECK: error: 'dsdl.decl' op names @ns.Missing.1.0.serialize, which resolves to nothing
dsdl.surface target = "c" {
  dsdl.scope root "dsdl_out" {
    dsdl.decl "ns_Missing_1_0__serialize_" kind = entry of = @ns.Missing.1.0.serialize
  }
}

// -----

// CHECK: error: 'dsdl.scope' op names @ns.Msg.1.0.serialize, which is not a schema
func.func private @ns.Msg.1.0.serialize()
dsdl.surface target = "c" {
  dsdl.scope root "dsdl_out" {
    dsdl.scope type "ns_Msg_1_0" of = @ns.Msg.1.0.serialize {
    }
  }
}

// -----

// CHECK: error: 'dsdl.scope' op names section 'request' of @ns.Msg.1.0, which has no such section
dsdl.schema @ns.Msg.1.0 attributes {full_name = "ns.Msg", major = 1 : i32, minor = 0 : i32, sealed} {
  dsdl.field {name = "value", type_name = "saturated uint8"}
  dsdl.serialization_plan attributes {max_bits = 8 : i64, min_bits = 8 : i64} {
    dsdl.align {bits = 8 : i32}
  }
}
dsdl.surface target = "c" {
  dsdl.scope root "dsdl_out" {
    dsdl.scope type "ns_Msg_1_0" of = @ns.Msg.1.0 section = "request" {
    }
  }
}

// -----

// CHECK: error: 'dsdl.decl' op names member 'count' of @ns.Msg.1.0, which declares no such field or constant
dsdl.schema @ns.Msg.1.0 attributes {full_name = "ns.Msg", major = 1 : i32, minor = 0 : i32, sealed} {
  dsdl.field {name = "value", type_name = "saturated uint8"}
  dsdl.serialization_plan attributes {max_bits = 8 : i64, min_bits = 8 : i64} {
    dsdl.align {bits = 8 : i32}
  }
}
dsdl.surface target = "c" {
  dsdl.scope root "dsdl_out" {
    dsdl.scope type "ns_Msg_1_0" of = @ns.Msg.1.0 {
      dsdl.decl "count" kind = field of = @ns.Msg.1.0 member = "count"
    }
  }
}

// -----

// The object lane reads C's tree.
// CHECK: error: 'dsdl.surface' op names target 'obj', which is no language
dsdl.surface target = "obj" {
  dsdl.scope root "dsdl_out" {
  }
}

// -----

// CHECK: error: 'dsdl.surface' op is a second surface of target 'go' and profile ''
// CHECK: note: the first
dsdl.surface target = "go" {
  dsdl.scope root "m" {
  }
}
dsdl.surface target = "go" {
  dsdl.scope root "m" {
  }
}

// -----

// CHECK: error: 'dsdl.surface' op holds one root scope and nothing else
dsdl.surface target = "go" {
}

// -----

// CHECK: error: 'dsdl.scope' op is the surface's scope, which only a root scope is
dsdl.surface target = "go" {
  dsdl.scope package "m" {
  }
}

// -----

// CHECK: error: 'dsdl.scope' op is a root scope inside another scope
dsdl.surface target = "go" {
  dsdl.scope root "m" {
    dsdl.scope root "n" {
    }
  }
}

// -----

// CHECK: error: 'dsdl.scope' op has no name
dsdl.surface target = "python" {
  dsdl.scope root "dsdl_gen" {
    dsdl.scope module "" {
    }
  }
}

// -----

// CHECK: error: 'dsdl.scope' op is a file scope with no path
dsdl.surface target = "c" {
  dsdl.scope root "dsdl_out" {
    dsdl.scope file "A_1_0" {
    }
  }
}

// -----

// CHECK: error: 'dsdl.scope' op names an entity, which only a type scope does
dsdl.schema @ns.Msg.1.0 attributes {full_name = "ns.Msg", major = 1 : i32, minor = 0 : i32, sealed} {
  dsdl.field {name = "value", type_name = "saturated uint8"}
  dsdl.serialization_plan attributes {max_bits = 8 : i64, min_bits = 8 : i64} {
    dsdl.align {bits = 8 : i32}
  }
}
dsdl.surface target = "rust" {
  dsdl.scope root "llvmdsdl_generated" {
    dsdl.scope module "msg_1_0" of = @ns.Msg.1.0 {
    }
  }
}

// -----

// CHECK: error: 'arith.constant' op is inside a surface scope, which holds only scopes and declarations
dsdl.surface target = "rust" {
  dsdl.scope root "llvmdsdl_generated" {
    %zero = arith.constant 0 : i32
  }
}

// -----

// CHECK: error: 'dsdl.decl' op declares a field outside a type scope
dsdl.surface target = "rust" {
  dsdl.scope root "llvmdsdl_generated" {
    dsdl.decl "value" kind = field
  }
}

// -----

// CHECK: error: 'dsdl.decl' op is an import with no class
dsdl.surface target = "rust" {
  dsdl.scope root "llvmdsdl_generated" {
    dsdl.decl "Path" kind = import
  }
}

// -----

// CHECK: error: 'dsdl.decl' op names a section or a member of no entity
dsdl.surface target = "rust" {
  dsdl.scope root "llvmdsdl_generated" {
    dsdl.scope type "Msg" {
      dsdl.decl "value" kind = field member = "value"
    }
  }
}

// -----

// One version of a definition declares a name once, however many versions share it.
// CHECK: error: 'dsdl.scope' op declares 'ns__Multi' as a type in scope '', which already declares it as a type
// CHECK: note: declared here
dsdl.schema @ns.Multi.1.0 attributes {full_name = "ns.Multi", major = 1 : i32, minor = 0 : i32, sealed} {
  dsdl.field {name = "value", type_name = "saturated uint8"}
  dsdl.serialization_plan attributes {max_bits = 8 : i64, min_bits = 8 : i64} {
    dsdl.align {bits = 8 : i32}
  }
}
dsdl.surface target = "c" {
  dsdl.scope root "" {
    dsdl.scope file "Multi_1_0" path = "ns/Multi_1_0.h" {
      dsdl.scope type "ns__Multi" of = @ns.Multi.1.0 {
      }
      dsdl.scope type "ns__Multi" of = @ns.Multi.1.0 {
      }
    }
  }
}
