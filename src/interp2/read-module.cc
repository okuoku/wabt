/*
 * Copyright 2020 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/interp2/read-module.h"

#include "src/binary-reader-nop.h"
#include "src/feature.h"
#include "src/interp2/interp2.h"
#include "src/stream.h"
#include "src/type-checker.h"

namespace wabt {
namespace interp2 {

namespace {

using IstreamOffset = uint32_t;
#if 0
static const IstreamOffset kInvalidIstreamOffset = ~0;

using IndexVector = std::vector<Index>;
using IstreamOffsetVector = std::vector<IstreamOffset>;
using IstreamOffsetVectorVector = std::vector<IstreamOffsetVector>;
#endif

ValueType ToInterp(Type type) {
  switch (type) {
    case Type::I32: return ValueType::I32;
    case Type::I64: return ValueType::I64;
    case Type::F32: return ValueType::F32;
    case Type::F64: return ValueType::F64;
    case Type::V128: return ValueType::V128;
    case Type::Funcref: return ValueType::Funcref;
    case Type::Anyref: return ValueType::Anyref;
    case Type::Nullref: return ValueType::Nullref;
    case Type::Exnref: return ValueType::Exnref;
    default: WABT_UNREACHABLE;
  }
};

ValueTypes ToInterp(Index count, Type* types) {
  ValueTypes result;
  result.reserve(count);
  for (Index i = 0; i < count; ++i) {
    result.push_back(ToInterp(types[i]));
  }
  return result;
}

Limits ToInterp(wabt::Limits limits) {
  return Limits{u32(limits.initial), u32(limits.max), limits.has_max};
}

Mutability ToMutability(bool mut) {
  return mut ? Mutability::Var : Mutability::Const;
}

ExternKind ToInterp(ExternalKind kind) {
  switch (kind) {
    case ExternalKind::Func:   return ExternKind::Func;
    case ExternalKind::Table:  return ExternKind::Table;
    case ExternalKind::Memory: return ExternKind::Memory;
    case ExternalKind::Global: return ExternKind::Global;
    case ExternalKind::Event:  return ExternKind::Event;
    default: WABT_UNREACHABLE;
  }
}

SegmentMode ToSegmentMode(uint8_t flags) {
  return (flags & SegPassive) ? SegmentMode::Passive : SegmentMode::Active;
}

struct Label {
  Label(IstreamOffset offset, IstreamOffset fixup_offset);

  IstreamOffset offset;
  IstreamOffset fixup_offset;
};

class BinaryReaderInterp : public BinaryReaderNop {
 public:
  BinaryReaderInterp(ModuleDesc* module,
                     Errors* errors,
                     const Features& features);

  ValueType GetType(InitExpr);

  // Implement BinaryReader.
  bool OnError(const Error&) override;

  Result OnTypeCount(Index count) override;
  Result OnType(Index index,
                Index param_count,
                Type* param_types,
                Index result_count,
                Type* result_types) override;

  Result OnImportFunc(Index import_index,
                      string_view module_name,
                      string_view field_name,
                      Index func_index,
                      Index sig_index) override;
  Result OnImportTable(Index import_index,
                       string_view module_name,
                       string_view field_name,
                       Index table_index,
                       Type elem_type,
                       const wabt::Limits* elem_limits) override;
  Result OnImportMemory(Index import_index,
                        string_view module_name,
                        string_view field_name,
                        Index memory_index,
                        const wabt::Limits* page_limits) override;
  Result OnImportGlobal(Index import_index,
                        string_view module_name,
                        string_view field_name,
                        Index global_index,
                        Type type,
                        bool mutable_) override;

  Result OnFunctionCount(Index count) override;
  Result OnFunction(Index index, Index sig_index) override;

  Result OnTableCount(Index count) override;
  Result OnTable(Index index,
                       Type elem_type,
                       const wabt::Limits* elem_limits) override;

  Result OnMemoryCount(Index count) override;
  Result OnMemory(Index index, const wabt::Limits* limits) override;

  Result OnGlobalCount(Index count) override;
  Result BeginGlobal(Index index, Type type, bool mutable_) override;
  Result EndGlobalInitExpr(Index index) override;

  Result OnExport(Index index,
                  ExternalKind kind,
                  Index item_index,
                  string_view name) override;

  Result OnStartFunction(Index func_index) override;

#if 0
  Result BeginFunctionBody(Index index, Offset size) override;
  Result OnLocalDeclCount(Index count) override;
  Result OnLocalDecl(Index decl_index, Index count, Type type) override;

  Result OnOpcode(Opcode Opcode) override;
  Result OnAtomicLoadExpr(Opcode opcode,
                                uint32_t alignment_log2,
                                Address offset) override;
  Result OnAtomicStoreExpr(Opcode opcode,
                                 uint32_t alignment_log2,
                                 Address offset) override;
  Result OnAtomicRmwExpr(Opcode opcode,
                               uint32_t alignment_log2,
                               Address offset) override;
  Result OnAtomicRmwCmpxchgExpr(Opcode opcode,
                                      uint32_t alignment_log2,
                                      Address offset) override;
  Result OnAtomicWaitExpr(Opcode opcode,
                                uint32_t alignment_log2,
                                Address offset) override;
  Result OnAtomicNotifyExpr(Opcode opcode,
                                uint32_t alignment_log2,
                                Address offset) override;
  Result OnBinaryExpr(Opcode opcode) override;
  Result OnBlockExpr(Type sig_type) override;
  Result OnBrExpr(Index depth) override;
  Result OnBrIfExpr(Index depth) override;
  Result OnBrTableExpr(Index num_targets,
                             Index* target_depths,
                             Index default_target_depth) override;
  Result OnCallExpr(Index func_index) override;
  Result OnCallIndirectExpr(Index sig_index, Index table_index) override;
  Result OnReturnCallExpr(Index func_index) override;
  Result OnReturnCallIndirectExpr(Index sig_index,
                                        Index table_index) override;
  Result OnCompareExpr(Opcode opcode) override;
  Result OnConvertExpr(Opcode opcode) override;
  Result OnDropExpr() override;
  Result OnElseExpr() override;
  Result OnEndExpr() override;
  Result OnF32ConstExpr(uint32_t value_bits) override;
  Result OnF64ConstExpr(uint64_t value_bits) override;
  Result OnV128ConstExpr(::v128 value_bits) override;
  Result OnGlobalGetExpr(Index global_index) override;
  Result OnGlobalSetExpr(Index global_index) override;
  Result OnI32ConstExpr(uint32_t value) override;
  Result OnI64ConstExpr(uint64_t value) override;
  Result OnIfExpr(Type sig_type) override;
  Result OnLoadExpr(Opcode opcode,
                          uint32_t alignment_log2,
                          Address offset) override;
  Result OnLocalGetExpr(Index local_index) override;
  Result OnLocalSetExpr(Index local_index) override;
  Result OnLocalTeeExpr(Index local_index) override;
  Result OnLoopExpr(Type sig_type) override;
  Result OnMemoryCopyExpr() override;
  Result OnDataDropExpr(Index segment_index) override;
  Result OnMemoryGrowExpr() override;
  Result OnMemoryFillExpr() override;
  Result OnMemoryInitExpr(Index segment_index) override;
  Result OnMemorySizeExpr() override;
  Result OnRefFuncExpr(Index func_index) override;
  Result OnRefNullExpr() override;
  Result OnRefIsNullExpr() override;
  Result OnNopExpr() override;
  Result OnReturnExpr() override;
  Result OnSelectExpr(Type result_type) override;
  Result OnStoreExpr(Opcode opcode,
                           uint32_t alignment_log2,
                           Address offset) override;
  Result OnUnaryExpr(Opcode opcode) override;
  Result OnTableCopyExpr(Index dst_index, Index src_index) override;
  Result OnTableGetExpr(Index table_index) override;
  Result OnTableSetExpr(Index table_index) override;
  Result OnTableGrowExpr(Index table_index) override;
  Result OnTableSizeExpr(Index table_index) override;
  Result OnTableFillExpr(Index table_index) override;
  Result OnElemDropExpr(Index segment_index) override;
  Result OnTableInitExpr(Index segment_index, Index table_index) override;
  Result OnTernaryExpr(Opcode opcode) override;
  Result OnUnreachableExpr() override;
  Result EndFunctionBody(Index index) override;
  Result OnSimdLaneOpExpr(Opcode opcode, uint64_t value) override;
  Result OnSimdShuffleOpExpr(Opcode opcode, ::v128 value) override;
  Result OnLoadSplatExpr(Opcode opcode,
                               uint32_t alignment_log2,
                               Address offset) override;

#endif

  Result OnElemSegmentCount(Index count) override;
  Result BeginElemSegment(Index index,
                                Index table_index,
                                uint8_t flags,
                                Type elem_type) override;
  Result EndElemSegmentInitExpr(Index index) override;
  Result OnElemSegmentElemExprCount(Index index, Index count) override;
  Result OnElemSegmentElemExpr_RefNull(Index segment_index) override;
  Result OnElemSegmentElemExpr_RefFunc(Index segment_index,
                                             Index func_index) override;

  Result OnDataCount(Index count) override;
  Result EndDataSegmentInitExpr(Index index) override;
  Result BeginDataSegment(Index index,
                                Index memory_index,
                                uint8_t flags) override;
  Result OnDataSegmentData(Index index,
                                 const void* data,
                                 Address size) override;

  Result OnInitExprF32ConstExpr(Index index, uint32_t value) override;
  Result OnInitExprF64ConstExpr(Index index, uint64_t value) override;
  Result OnInitExprV128ConstExpr(Index index, ::v128 value) override;
  Result OnInitExprGlobalGetExpr(Index index,
                                       Index global_index) override;
  Result OnInitExprI32ConstExpr(Index index, uint32_t value) override;
  Result OnInitExprI64ConstExpr(Index index, uint64_t value) override;
  Result OnInitExprRefNull(Index index) override;
  Result OnInitExprRefFunc(Index index, Index func_index) override;

 private:
  void PrintError(const char* format, ...);

  Features features_;
  Errors* errors_ = nullptr;
  ModuleDesc* module_ = nullptr;
  TypeChecker typechecker_;
  std::vector<Label> label_stack_;
  InitExpr init_expr_;

  std::vector<FuncType> func_types_;      // Includes imported and defined.
  std::vector<TableType> table_types_;    // Includes imported and defined.
  std::vector<MemoryType> memory_types_;  // Includes imported and defined.
  std::vector<GlobalType> global_types_;  // Includes imported and defined.
  std::vector<EventType> event_types_;    // Includes imported and defined.
};

BinaryReaderInterp::BinaryReaderInterp(ModuleDesc* module,
                                       Errors* errors,
                                       const Features& features)
    : features_(features),
      errors_(errors),
      module_(module),
      typechecker_(features) {
  typechecker_.set_error_callback(
      [this](const char* msg) { PrintError("%s", msg); });
}

void WABT_PRINTF_FORMAT(2, 3) BinaryReaderInterp::PrintError(const char* format,
                                                             ...) {
  WABT_SNPRINTF_ALLOCA(buffer, length, format);
  errors_->emplace_back(ErrorLevel::Error, Location(kInvalidOffset), buffer);
}

bool BinaryReaderInterp::OnError(const Error& error) {
  errors_->push_back(error);
  return true;
}

Result BinaryReaderInterp::OnTypeCount(Index count) {
  module_->func_types.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::OnType(Index index,
                                  Index param_count,
                                  Type* param_types,
                                  Index result_count,
                                  Type* result_types) {
  module_->func_types.push_back(FuncType(ToInterp(param_count, param_types),
                                         ToInterp(result_count, result_types)));
  return Result::Ok;
}

Result BinaryReaderInterp::OnImportFunc(Index import_index,
                                        string_view module_name,
                                        string_view field_name,
                                        Index func_index,
                                        Index sig_index) {
  FuncType& func_type = module_->func_types[sig_index];
  module_->imports.push_back(ImportDesc{ImportType(
      module_name.to_string(), field_name.to_string(), func_type.Clone())});
  func_types_.push_back(func_type);
  return Result::Ok;
}

Result BinaryReaderInterp::OnImportTable(Index import_index,
                                         string_view module_name,
                                         string_view field_name,
                                         Index table_index,
                                         Type elem_type,
                                         const wabt::Limits* elem_limits) {
  TableType table_type{ToInterp(elem_type), ToInterp(*elem_limits)};
  module_->imports.push_back(ImportDesc{ImportType(
      module_name.to_string(), field_name.to_string(), table_type.Clone())});
  table_types_.push_back(table_type);
  return Result::Ok;
}

Result BinaryReaderInterp::OnImportMemory(Index import_index,
                                          string_view module_name,
                                          string_view field_name,
                                          Index memory_index,
                                          const wabt::Limits* page_limits) {
  MemoryType memory_type{ToInterp(*page_limits)};
  module_->imports.push_back(ImportDesc{ImportType(
      module_name.to_string(), field_name.to_string(), memory_type.Clone())});
  memory_types_.push_back(memory_type);
  return Result::Ok;
}

Result BinaryReaderInterp::OnImportGlobal(Index import_index,
                                          string_view module_name,
                                          string_view field_name,
                                          Index global_index,
                                          Type type,
                                          bool mutable_) {
  GlobalType global_type{ToInterp(type), ToMutability(mutable_)};
  module_->imports.push_back(ImportDesc{ImportType(
      module_name.to_string(), field_name.to_string(), global_type.Clone())});
  global_types_.push_back(global_type);
  return Result::Ok;
}

Result BinaryReaderInterp::OnFunctionCount(Index count) {
  module_->funcs.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::OnFunction(Index index, Index sig_index) {
  FuncType& func_type = module_->func_types[sig_index];
  module_->funcs.push_back(FuncDesc{func_type, 0});
  func_types_.push_back(func_type);
  return Result::Ok;
}

Result BinaryReaderInterp::OnTableCount(Index count) {
  module_->tables.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::OnTable(Index index,
                                   Type elem_type,
                                   const wabt::Limits* elem_limits) {
  TableType table_type{ToInterp(elem_type), ToInterp(*elem_limits)};
  module_->tables.push_back(TableDesc{table_type});
  table_types_.push_back(table_type);
  return Result::Ok;
}

Result BinaryReaderInterp::OnMemoryCount(Index count) {
  module_->memories.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::OnMemory(Index index, const wabt::Limits* limits) {
  MemoryType memory_type{ToInterp(*limits)};
  module_->memories.push_back(MemoryDesc{memory_type});
  memory_types_.push_back(memory_type);
  return Result::Ok;
}

Result BinaryReaderInterp::OnGlobalCount(Index count) {
  module_->globals.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::BeginGlobal(Index index, Type type, bool mutable_) {
  GlobalType global_type{ToInterp(type), ToMutability(mutable_)};
  module_->globals.push_back(GlobalDesc{global_type, InitExpr{}});
  global_types_.push_back(global_type);
  return Result::Ok;
}

ValueType BinaryReaderInterp::GetType(InitExpr init) {
  switch (init_expr_.kind) {
    case InitExprKind::I32:        return ValueType::I32;
    case InitExprKind::I64:        return ValueType::I64;
    case InitExprKind::F32:        return ValueType::F32;
    case InitExprKind::F64:        return ValueType::F64;
    case InitExprKind::V128:       return ValueType::V128;
    case InitExprKind::GlobalGet:  return global_types_[init.index].type;
    case InitExprKind::RefNull:    return ValueType::Nullref;
    case InitExprKind::RefFunc:    return ValueType::Funcref;
    default: WABT_UNREACHABLE;
  }
}

Result BinaryReaderInterp::EndGlobalInitExpr(Index index) {
  GlobalDesc& global = module_->globals.back();
  ValueType type = GetType(global.init);
  if (!TypesMatch(global.type.type, type)) {
    PrintError("type mismatch in global, expected %s but got %s.",
               GetName(global.type.type), GetName(type));
    return Result::Error;
  }

  global.init = init_expr_;
  return Result::Ok;
}

Result BinaryReaderInterp::OnInitExprF32ConstExpr(Index index,
                                                  uint32_t value_bits) {
  init_expr_.kind = InitExprKind::F32;
  init_expr_.f32 = Bitcast<f32>(value_bits);
  return Result::Ok;
}

Result BinaryReaderInterp::OnInitExprF64ConstExpr(Index index,
                                                  uint64_t value_bits) {
  init_expr_.kind = InitExprKind::F64;
  init_expr_.f64 = Bitcast<f64>(value_bits);
  return Result::Ok;
}

Result BinaryReaderInterp::OnInitExprV128ConstExpr(Index index,
                                                   ::v128 value_bits) {
  init_expr_.kind = InitExprKind::V128;
  init_expr_.v128 = Bitcast<v128>(value_bits);
  return Result::Ok;
}

Result BinaryReaderInterp::OnInitExprGlobalGetExpr(Index index,
                                                   Index global_index) {
  Index num_global_imports = global_types_.size() - module_->globals.size();
  if (global_index >= num_global_imports) {
    PrintError("initializer expression can only reference an imported global");
    return Result::Error;
  }
  if (global_types_[global_index].mut == Mutability::Var) {
    PrintError("initializer expression cannot reference a mutable global");
    return Result::Error;
  }

  init_expr_.kind = InitExprKind::GlobalGet;
  init_expr_.index = global_index;
  return Result::Ok;
}

Result BinaryReaderInterp::OnInitExprI32ConstExpr(Index index, uint32_t value) {
  init_expr_.kind = InitExprKind::I32;
  init_expr_.i32 = value;
  return Result::Ok;
}

Result BinaryReaderInterp::OnInitExprI64ConstExpr(Index index, uint64_t value) {
  init_expr_.kind = InitExprKind::I64;
  init_expr_.i64 = value;
  return Result::Ok;
}

Result BinaryReaderInterp::OnInitExprRefNull(Index index) {
  init_expr_.kind = InitExprKind::RefNull;
  return Result::Ok;
}

Result BinaryReaderInterp::OnInitExprRefFunc(Index index, Index func_index) {
  init_expr_.kind = InitExprKind::RefFunc;
  init_expr_.index = index;
  return Result::Ok;
}

Result BinaryReaderInterp::OnExport(Index index,
                                    ExternalKind kind,
                                    Index item_index,
                                    string_view name) {
  std::unique_ptr<ExternType> type;
  switch (kind) {
    case ExternalKind::Func:   type = func_types_[item_index].Clone(); break;
    case ExternalKind::Table:  type = table_types_[item_index].Clone(); break;
    case ExternalKind::Memory: type = memory_types_[item_index].Clone(); break;
    case ExternalKind::Global: type = global_types_[item_index].Clone(); break;
    case ExternalKind::Event:  type = event_types_[item_index].Clone(); break;
  }
  module_->exports.push_back(
      ExportDesc{ExportType(name.to_string(), std::move(type)), item_index});
  return Result::Ok;
}

Result BinaryReaderInterp::OnStartFunction(Index func_index) {
  FuncType& func_type = func_types_[func_index];
  if (func_type.params.size() != 0) {
    PrintError("start function must be nullary");
    return Result::Error;
  }
  if (func_type.results.size() != 0) {
    PrintError("start function must not return anything");
    return Result::Error;
  }
  module_->starts.push_back(StartDesc{func_index});
  return Result::Ok;
}

Result BinaryReaderInterp::OnElemSegmentCount(Index count) {
  module_->elems.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::BeginElemSegment(Index index,
                                            Index table_index,
                                            uint8_t flags,
                                            Type elem_type) {
  ElemDesc desc;
  desc.type = ToInterp(elem_type);
  desc.mode = ToSegmentMode(flags);
  desc.table_index = table_index;
  module_->elems.push_back(desc);
  return Result::Ok;
}

Result BinaryReaderInterp::EndElemSegmentInitExpr(Index index) {
  ValueType type = GetType(init_expr_);
  if (type != ValueType::I32) {
    PrintError(
        "type mismatch in elem segment initializer expression, expected i32 "
        "but got %s",
        GetName(type));
    return Result::Error;
  }

  ElemDesc& elem = module_->elems.back();
  elem.offset = init_expr_;
  return Result::Ok;
}

Result BinaryReaderInterp::OnElemSegmentElemExprCount(Index index,
                                                      Index count) {
  ElemDesc& elem = module_->elems.back();
  elem.elements.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::OnElemSegmentElemExpr_RefNull(Index segment_index) {
  ElemDesc& elem = module_->elems.back();
  elem.elements.push_back(ElemExpr{ElemKind::RefNull, 0});
  return Result::Ok;
}

Result BinaryReaderInterp::OnElemSegmentElemExpr_RefFunc(Index segment_index,
                                                         Index func_index) {
  ElemDesc& elem = module_->elems.back();
  elem.elements.push_back(ElemExpr{ElemKind::RefFunc, func_index});
  return Result::Ok;
}

Result BinaryReaderInterp::OnDataCount(Index count) {
  module_->datas.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::EndDataSegmentInitExpr(Index index) {
  ValueType type = GetType(init_expr_);
  if (type != ValueType::I32) {
    PrintError(
        "type mismatch in data segment initializer expression, expected i32 "
        "but got %s",
        GetName(type));
    return Result::Error;
  }

  DataDesc& data = module_->datas.back();
  data.offset = init_expr_;
  return Result::Ok;
}

Result BinaryReaderInterp::BeginDataSegment(Index index,
                                            Index memory_index,
                                            uint8_t flags) {
  DataDesc desc;
  desc.mode = ToSegmentMode(flags);
  desc.memory_index = memory_index;
  module_->datas.push_back(desc);
  return Result::Ok;
}

Result BinaryReaderInterp::OnDataSegmentData(Index index,
                                             const void* src_data,
                                             Address size) {
  DataDesc& dst_data = module_->datas.back();
  if (size > 0) {
    dst_data.data.resize(size);
    memcpy(dst_data.data.data(), src_data, size);
  }
  return Result::Ok;
}

}  // namespace

Result ReadModule(const void* data,
                  size_t size,
                  const ReadBinaryOptions& options,
                  Errors* errors,
                  ModuleDesc* out_module) {
  BinaryReaderInterp reader(out_module, errors, options.features);
  return ReadBinary(data, size, &reader, options);
}

}  // namespace interp2
}  // namespace wabt
