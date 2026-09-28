// RUN: %dsdl-opt %s | %dsdl-opt | FileCheck %s
// RUN: %dsdl-opt --mlir-print-op-generic %s | %dsdl-opt | FileCheck %s

// A surface tree for each language prints and parses back, in the custom form and in the generic
// one. Each tree declares one name in two classes its language keeps apart: a C structure's tag
// and its typedef, a TypeScript interface and a constant, a Rust module and a function, a Rust
// field and an accessor, and a Go package's two files importing one package under one name. Two versions of one C definition
// declare one type name, as the unversioned scheme generates them, and a TypeScript namespace's
// directory and a definition's module of one name are two paths.

module {
  dsdl.schema @uavcan.file.Path.2.0 attributes {full_name = "uavcan.file.Path", major = 2 : i32, minor = 0 : i32, sealed} {
    dsdl.field {name = "path", type_name = "saturated uint8[<=255]"}
    dsdl.serialization_plan attributes {max_bits = 2048 : i64, min_bits = 8 : i64} {
      dsdl.align {bits = 8 : i32}
    }
  }
  dsdl.schema @uavcan.file.List.0.2 attributes {full_name = "uavcan.file.List", major = 0 : i32, minor = 2 : i32, sealed, service} {
    dsdl.field {name = "entry_index", section = "request", type_name = "saturated uint32"}
    dsdl.serialization_plan attributes {max_bits = 32 : i64, min_bits = 32 : i64, section = "request"} {
      dsdl.align {bits = 8 : i32}
    }
    dsdl.serialization_plan attributes {max_bits = 8 : i64, min_bits = 8 : i64, section = "response"} {
      dsdl.align {bits = 8 : i32}
    }
  }
  dsdl.schema @ns.Multi.1.0 attributes {full_name = "ns.Multi", major = 1 : i32, minor = 0 : i32, sealed} {
    dsdl.field {name = "value", type_name = "saturated uint8"}
    dsdl.serialization_plan attributes {max_bits = 8 : i64, min_bits = 8 : i64} {
      dsdl.align {bits = 8 : i32}
    }
  }
  dsdl.schema @ns.Multi.2.0 attributes {full_name = "ns.Multi", major = 2 : i32, minor = 0 : i32, sealed} {
    dsdl.field {name = "value", type_name = "saturated uint16"}
    dsdl.serialization_plan attributes {max_bits = 16 : i64, min_bits = 16 : i64} {
      dsdl.align {bits = 8 : i32}
    }
  }
  func.func private @uavcan.file.List.0.2.request.serialize()
  func.func private @uavcan.file.List.0.2.request.get.entry_index()
  func.func private @uavcan.file.List.0.2.request.plan.capacity_check()

  // CHECK-LABEL: dsdl.surface target = "rust" {
  // CHECK-NEXT:    dsdl.scope root "llvmdsdl_generated" path = "src/lib.rs" {
  // CHECK-NEXT:      dsdl.decl "dsdl_runtime" kind = module origin = generated
  // CHECK:           dsdl.scope module "list_0_2" path = "src/uavcan/file/list_0_2.rs" {
  // CHECK-NEXT:        dsdl.decl "Path" kind = import class = type of = @uavcan.file.Path.2.0
  // CHECK-NEXT:        dsdl.decl "capacity_check_request" kind = helper visibility = private of = @uavcan.file.List.0.2.request.plan.capacity_check
  // CHECK-NEXT:        dsdl.scope type "Request" of = @uavcan.file.List.0.2 section = "request" {
  // CHECK-NEXT:          dsdl.decl "entry_index" kind = field of = @uavcan.file.List.0.2 section = "request" member = "entry_index"
  // CHECK-NEXT:          dsdl.decl "FULL_NAME" kind = constant origin = generated fact = full_name of = @uavcan.file.List.0.2 section = "request"
  // CHECK-NEXT:          dsdl.decl "serialize" kind = entry of = @uavcan.file.List.0.2.request.serialize
  // CHECK-NEXT:          dsdl.decl "entry_index" kind = accessor origin = generated of = @uavcan.file.List.0.2.request.get.entry_index
  // CHECK-NEXT:        }
  // CHECK-NEXT:        dsdl.decl "List" kind = alias origin = generated
  // CHECK:           dsdl.decl "file" kind = helper
  dsdl.surface target = "rust" {
    dsdl.scope root "llvmdsdl_generated" path = "src/lib.rs" {
      dsdl.decl "dsdl_runtime" kind = module origin = generated
      dsdl.scope module "uavcan" path = "src/uavcan/mod.rs" {
        dsdl.scope module "file" path = "src/uavcan/file/mod.rs" {
          dsdl.scope module "list_0_2" path = "src/uavcan/file/list_0_2.rs" {
            dsdl.decl "Path" kind = import class = type of = @uavcan.file.Path.2.0
            dsdl.decl "capacity_check_request" kind = helper visibility = private of = @uavcan.file.List.0.2.request.plan.capacity_check
            dsdl.scope type "Request" of = @uavcan.file.List.0.2 section = "request" {
              dsdl.decl "entry_index" kind = field of = @uavcan.file.List.0.2 section = "request" member = "entry_index"
              dsdl.decl "FULL_NAME" kind = constant origin = generated fact = full_name of = @uavcan.file.List.0.2 section = "request"
              dsdl.decl "serialize" kind = entry of = @uavcan.file.List.0.2.request.serialize
              dsdl.decl "entry_index" kind = accessor origin = generated of = @uavcan.file.List.0.2.request.get.entry_index
            }
            dsdl.decl "List" kind = alias origin = generated
          }
        }
        dsdl.decl "file" kind = helper
      }
    }
  }

  // CHECK-LABEL: dsdl.surface target = "c" {
  // CHECK:           dsdl.scope file "Path_2_0" path = "uavcan/file/Path_2_0.h" {
  // CHECK-NEXT:        dsdl.decl "UAVCAN_FILE_PATH_2_0_INCLUDED_" kind = guard origin = generated
  // CHECK-NEXT:        dsdl.decl "uavcan_file_Path_2_0" kind = tag
  // CHECK-NEXT:        dsdl.scope type "uavcan_file_Path_2_0" of = @uavcan.file.Path.2.0 {
  // CHECK-NEXT:          dsdl.decl "path" kind = field of = @uavcan.file.Path.2.0 member = "path"
  // CHECK-NEXT:        }
  // CHECK-NEXT:        dsdl.decl "uavcan_file_Path_2_0_FULL_NAME_" kind = constant class = macro origin = generated
  dsdl.surface target = "c" {
    dsdl.scope root "dsdl_out" {
      dsdl.scope file "Path_2_0" path = "uavcan/file/Path_2_0.h" {
        dsdl.decl "UAVCAN_FILE_PATH_2_0_INCLUDED_" kind = guard origin = generated
        dsdl.decl "uavcan_file_Path_2_0" kind = tag
        dsdl.scope type "uavcan_file_Path_2_0" of = @uavcan.file.Path.2.0 {
          dsdl.decl "path" kind = field of = @uavcan.file.Path.2.0 member = "path"
        }
        dsdl.decl "uavcan_file_Path_2_0_FULL_NAME_" kind = constant class = macro origin = generated
      }
      dsdl.scope file "Multi_1_0" path = "ns/Multi_1_0.h" {
        dsdl.scope type "ns__Multi" of = @ns.Multi.1.0 {
        }
      }
      dsdl.scope file "Multi_2_0" path = "ns/Multi_2_0.h" {
        dsdl.scope type "ns__Multi" of = @ns.Multi.2.0 {
        }
      }
      dsdl.scope file "List_0_2" path = "uavcan/file/List_0_2.h" {
        dsdl.decl "UAVCAN_FILE_LIST_0_2_INCLUDED_" kind = guard origin = generated
        dsdl.decl "uavcan_file_List_0_2__Request" kind = tag
        dsdl.scope type "uavcan_file_List_0_2__Request" of = @uavcan.file.List.0.2 section = "request" {
          dsdl.decl "entry_index" kind = field of = @uavcan.file.List.0.2 section = "request" member = "entry_index"
        }
      }
    }
  }

  // CHECK-LABEL: dsdl.surface target = "cpp" profile = "std" {
  // CHECK-LABEL: dsdl.surface target = "cpp" profile = "pmr" {
  // CHECK:             dsdl.decl "_memory_resource" kind = field visibility = private origin = generated
  dsdl.surface target = "cpp" profile = "std" {
    dsdl.scope root "dsdl_out" {
      dsdl.scope namespace "uavcan" {
        dsdl.scope namespace "file" {
          dsdl.scope file "Path_2_0" path = "uavcan/file/Path_2_0.hpp" {
            dsdl.scope type "Path_2_0" of = @uavcan.file.Path.2.0 {
              dsdl.decl "path" kind = field of = @uavcan.file.Path.2.0 member = "path"
            }
          }
        }
      }
    }
  }
  dsdl.surface target = "cpp" profile = "pmr" {
    dsdl.scope root "dsdl_out" {
      dsdl.scope namespace "uavcan" {
        dsdl.scope namespace "file" {
          dsdl.scope file "Path_2_0" path = "uavcan/file/Path_2_0.hpp" {
            dsdl.scope type "Path_2_0" of = @uavcan.file.Path.2.0 {
              dsdl.decl "path" kind = field of = @uavcan.file.Path.2.0 member = "path"
              dsdl.decl "_memory_resource" kind = field visibility = private origin = generated
            }
          }
        }
      }
    }
  }

  // CHECK-LABEL: dsdl.surface target = "go" {
  // CHECK:             dsdl.scope file "path_2_0" path = "uavcan/file/path_2_0.go" {
  // CHECK-NEXT:          dsdl.decl "dsdlruntime" kind = import class = value
  // CHECK:             dsdl.scope file "list_0_2" path = "uavcan/file/list_0_2.go" {
  // CHECK-NEXT:          dsdl.decl "dsdlruntime" kind = import class = value
  dsdl.surface target = "go" {
    dsdl.scope root "m" {
      dsdl.scope package "dsdlruntime" {
      }
      dsdl.scope package "uavcan" {
        dsdl.scope package "file" {
          dsdl.scope file "path_2_0" path = "uavcan/file/path_2_0.go" {
            dsdl.decl "dsdlruntime" kind = import class = value
            dsdl.scope type "Path" of = @uavcan.file.Path.2.0 {
              dsdl.decl "Path" kind = field of = @uavcan.file.Path.2.0 member = "path"
            }
          }
          dsdl.scope file "list_0_2" path = "uavcan/file/list_0_2.go" {
            dsdl.decl "dsdlruntime" kind = import class = value
            dsdl.scope type "ListRequest" of = @uavcan.file.List.0.2 section = "request" {
              dsdl.decl "EntryIndex" kind = field of = @uavcan.file.List.0.2 section = "request" member = "entry_index"
            }
          }
        }
      }
    }
  }

  // CHECK-LABEL: dsdl.surface target = "ts" {
  // CHECK:             dsdl.scope type "Path" of = @uavcan.file.Path.2.0 {
  // CHECK:             dsdl.decl "Path" kind = constant origin = generated
  dsdl.surface target = "ts" {
    dsdl.scope root "m" {
      dsdl.decl "dsdl_runtime" kind = module origin = generated
      dsdl.scope module "uavcan" {
        dsdl.scope module "file" {
          dsdl.scope namespace "path_2_0" {
          }
          dsdl.scope module "path_2_0" path = "uavcan/file/path_2_0.ts" {
            dsdl.decl "dsdlRuntime" kind = import class = value
            dsdl.scope type "Path" of = @uavcan.file.Path.2.0 {
              dsdl.decl "path" kind = field of = @uavcan.file.Path.2.0 member = "path"
            }
            dsdl.decl "Path" kind = constant origin = generated
          }
        }
      }
    }
  }

  // CHECK-LABEL: dsdl.surface target = "python" {
  // CHECK-NEXT:    dsdl.scope root "dsdl_gen" path = "dsdl_gen/__init__.py" {
  dsdl.surface target = "python" {
    dsdl.scope root "dsdl_gen" path = "dsdl_gen/__init__.py" {
      dsdl.decl "__version__" kind = constant origin = generated
      dsdl.decl "_runtime_loader" kind = module origin = generated
      dsdl.scope module "uavcan" path = "dsdl_gen/uavcan/__init__.py" {
        dsdl.scope module "file" path = "dsdl_gen/uavcan/file/__init__.py" {
          dsdl.scope module "path_2_0" path = "dsdl_gen/uavcan/file/path_2_0.py" {
            dsdl.decl "dataclass" kind = import class = value
            dsdl.decl "field" kind = import class = value
            dsdl.scope type "Path_2_0" of = @uavcan.file.Path.2.0 {
              dsdl.decl "path" kind = field of = @uavcan.file.Path.2.0 member = "path"
            }
          }
        }
      }
    }
  }
}
