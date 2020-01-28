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

#include <map>

#include "src/binary-reader-nop.h"
#include "src/feature.h"
#include "src/interp2/interp2.h"
#include "src/stream.h"
#include "src/type-checker.h"

namespace wabt {
namespace interp2 {

namespace {

Type ToCommon(ValueType type) {
  switch (type) {
    case ValueType::I32: return Type::I32;
    case ValueType::I64: return Type::I64;
    case ValueType::F32: return Type::F32;
    case ValueType::F64: return Type::F64;
    case ValueType::V128: return Type::V128;
    case ValueType::Funcref: return Type::Funcref;
    case ValueType::Anyref: return Type::Anyref;
    case ValueType::Nullref: return Type::Nullref;
    case ValueType::Exnref: return Type::Exnref;
    default: WABT_UNREACHABLE;
  }
}

TypeVector ToCommon(ValueTypes types) {
  TypeVector result;
  result.reserve(types.size());
  for (auto type: types) {
    result.push_back(ToCommon(type));
  }
  return result;
}

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
  Istream::Offset offset;
  Istream::Offset fixup_offset;
};

struct FixupMap {
  using Fixups = std::vector<Istream::Offset>;

  void Clear();
  void Append(Index, Istream::Offset);
  void Resolve(Istream&, Index);

  std::map<Index, Fixups> map;
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

  Result BeginFunctionBody(Index index, Offset size) override;
  Result EndFunctionBody(Index index) override;
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
  Result OnSimdLaneOpExpr(Opcode opcode, uint64_t value) override;
  Result OnSimdShuffleOpExpr(Opcode opcode, ::v128 value) override;
  Result OnLoadSplatExpr(Opcode opcode,
                               uint32_t alignment_log2,
                               Address offset) override;

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
  Label* GetLabel(Index depth);
  Label* TopLabel();
  void PushLabel(Istream::Offset offset = Istream::kInvalidOffset,
                 Istream::Offset fixup_offset = Istream::kInvalidOffset);
  void PopLabel();

  void PrintError(const char* format, ...);

  Result GetDropCount(Index keep_count,
                      size_t type_stack_limit,
                      Index* out_drop_count);
  Result GetBrDropKeepCount(Index depth,
                            Index* out_drop_count,
                            Index* out_keep_count);
  Result GetReturnDropKeepCount(Index* out_drop_count, Index* out_keep_count);
  Result GetReturnCallDropKeepCount(FuncSignature* sig,
                                    Index keep_extra,
                                    Index* out_drop_count,
                                    Index* out_keep_count);
  Result EmitBr(Index depth, Index drop_count, Index keep_count);
  void FixupTopLabel();

  void GetBlockSignature(Type sig_type,
                         TypeVector* out_param_types,
                         TypeVector* out_result_types);

  Result CheckLocal(Index local_index);
  Result CheckGlobal(Index global_index);
  Result CheckDataSegment(Index data_segment_index);
  Result CheckElemSegment(Index elem_segment_index);
  Result CheckHasMemory(Opcode opcode);
  Result CheckHasTable(Opcode opcode);
  Result CheckAlign(uint32_t alignment_log2, Address natural_alignment);
  Result CheckAtomicAlign(uint32_t alignment_log2, Address natural_alignment);

  Features features_;
  Errors* errors_ = nullptr;
  ModuleDesc& module_;
  Istream& istream_;

  TypeChecker typechecker_;

  FuncDesc* func_;
  std::vector<Label> label_stack_;
  ValueTypes param_and_local_types_;
  FixupMap depth_fixups_;
  FixupMap func_fixups_;

  InitExpr init_expr_;
  u32 local_decl_count_;
  u32 local_count_;

  std::vector<FuncType> func_types_;      // Includes imported and defined.
  std::vector<TableType> table_types_;    // Includes imported and defined.
  std::vector<MemoryType> memory_types_;  // Includes imported and defined.
  std::vector<GlobalType> global_types_;  // Includes imported and defined.
  std::vector<EventType> event_types_;    // Includes imported and defined.
};

void FixupMap::Clear() {
  // TODO
}

void FixupMap::Append(Index, Istream::Offset) {
  // TODO
}

void FixupMap::Resolve(Istream&, Index) {
  // TODO
}

BinaryReaderInterp::BinaryReaderInterp(ModuleDesc* module,
                                       Errors* errors,
                                       const Features& features)
    : features_(features),
      errors_(errors),
      module_(*module),
      istream_(module->istream),
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
  module_.func_types.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::OnType(Index index,
                                  Index param_count,
                                  Type* param_types,
                                  Index result_count,
                                  Type* result_types) {
  module_.func_types.push_back(FuncType(ToInterp(param_count, param_types),
                                        ToInterp(result_count, result_types)));
  return Result::Ok;
}

Result BinaryReaderInterp::OnImportFunc(Index import_index,
                                        string_view module_name,
                                        string_view field_name,
                                        Index func_index,
                                        Index sig_index) {
  FuncType& func_type = module_.func_types[sig_index];
  module_.imports.push_back(ImportDesc{ImportType(
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
  module_.imports.push_back(ImportDesc{ImportType(
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
  module_.imports.push_back(ImportDesc{ImportType(
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
  module_.imports.push_back(ImportDesc{ImportType(
      module_name.to_string(), field_name.to_string(), global_type.Clone())});
  global_types_.push_back(global_type);
  return Result::Ok;
}

Result BinaryReaderInterp::OnFunctionCount(Index count) {
  module_.funcs.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::OnFunction(Index index, Index sig_index) {
  FuncType& func_type = module_.func_types[sig_index];
  module_.funcs.push_back(FuncDesc{func_type, 0});
  func_types_.push_back(func_type);
  return Result::Ok;
}

Result BinaryReaderInterp::OnTableCount(Index count) {
  module_.tables.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::OnTable(Index index,
                                   Type elem_type,
                                   const wabt::Limits* elem_limits) {
  TableType table_type{ToInterp(elem_type), ToInterp(*elem_limits)};
  module_.tables.push_back(TableDesc{table_type});
  table_types_.push_back(table_type);
  return Result::Ok;
}

Result BinaryReaderInterp::OnMemoryCount(Index count) {
  module_.memories.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::OnMemory(Index index, const wabt::Limits* limits) {
  MemoryType memory_type{ToInterp(*limits)};
  module_.memories.push_back(MemoryDesc{memory_type});
  memory_types_.push_back(memory_type);
  return Result::Ok;
}

Result BinaryReaderInterp::OnGlobalCount(Index count) {
  module_.globals.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::BeginGlobal(Index index, Type type, bool mutable_) {
  GlobalType global_type{ToInterp(type), ToMutability(mutable_)};
  module_.globals.push_back(GlobalDesc{global_type, InitExpr{}});
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
  GlobalDesc& global = module_.globals.back();
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
  Index num_global_imports = global_types_.size() - module_.globals.size();
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
  module_.exports.push_back(
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
  module_.starts.push_back(StartDesc{func_index});
  return Result::Ok;
}

Result BinaryReaderInterp::OnElemSegmentCount(Index count) {
  module_.elems.reserve(count);
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
  module_.elems.push_back(desc);
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

  ElemDesc& elem = module_.elems.back();
  elem.offset = init_expr_;
  return Result::Ok;
}

Result BinaryReaderInterp::OnElemSegmentElemExprCount(Index index,
                                                      Index count) {
  ElemDesc& elem = module_.elems.back();
  elem.elements.reserve(count);
  return Result::Ok;
}

Result BinaryReaderInterp::OnElemSegmentElemExpr_RefNull(Index segment_index) {
  ElemDesc& elem = module_.elems.back();
  elem.elements.push_back(ElemExpr{ElemKind::RefNull, 0});
  return Result::Ok;
}

Result BinaryReaderInterp::OnElemSegmentElemExpr_RefFunc(Index segment_index,
                                                         Index func_index) {
  ElemDesc& elem = module_.elems.back();
  elem.elements.push_back(ElemExpr{ElemKind::RefFunc, func_index});
  return Result::Ok;
}

Result BinaryReaderInterp::OnDataCount(Index count) {
  module_.datas.reserve(count);
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

  DataDesc& data = module_.datas.back();
  data.offset = init_expr_;
  return Result::Ok;
}

Result BinaryReaderInterp::BeginDataSegment(Index index,
                                            Index memory_index,
                                            uint8_t flags) {
  DataDesc desc;
  desc.mode = ToSegmentMode(flags);
  desc.memory_index = memory_index;
  module_.datas.push_back(desc);
  return Result::Ok;
}

Result BinaryReaderInterp::OnDataSegmentData(Index index,
                                             const void* src_data,
                                             Address size) {
  DataDesc& dst_data = module_.datas.back();
  if (size > 0) {
    dst_data.data.resize(size);
    memcpy(dst_data.data.data(), src_data, size);
  }
  return Result::Ok;
}

Label* BinaryReaderInterp::GetLabel(Index depth) {
  assert(depth < label_stack_.size());
  return &label_stack_[label_stack_.size() - depth - 1];
}

Label* BinaryReaderInterp::TopLabel() {
  return GetLabel(0);
}

void BinaryReaderInterp::PushLabel(Istream::Offset offset,
                                   Istream::Offset fixup_offset) {
  label_stack_.push_back(Label{offset, fixup_offset});
}

void BinaryReaderInterp::PopLabel() {
  label_stack_.pop_back();
}

Result BinaryReaderInterp::GetDropCount(Index keep_count,
                                        size_t type_stack_limit,
                                        Index* out_drop_count) {
  assert(typechecker_.type_stack_size() >= type_stack_limit);
  Index type_stack_count = typechecker_.type_stack_size() - type_stack_limit;
  // The keep_count may be larger than the type_stack_count if the typechecker
  // is currently unreachable. In that case, it doesn't matter what value we
  // drop, but 0 is a reasonable choice.
  *out_drop_count =
      type_stack_count >= keep_count ? type_stack_count - keep_count : 0;
  return Result::Ok;
}

Result BinaryReaderInterp::GetBrDropKeepCount(Index depth,
                                              Index* out_drop_count,
                                              Index* out_keep_count) {
  TypeChecker::Label* label;
  CHECK_RESULT(typechecker_.GetLabel(depth, &label));
  Index keep_count = label->br_types().size();
  CHECK_RESULT(
      GetDropCount(keep_count, label->type_stack_limit, out_drop_count));
  *out_keep_count = keep_count;
  return wabt::Result::Ok;
}

Result BinaryReaderInterp::GetReturnDropKeepCount(Index* out_drop_count,
                                                  Index* out_keep_count) {
  CHECK_RESULT(GetBrDropKeepCount(label_stack_.size() - 1, out_drop_count,
                                  out_keep_count));
  *out_drop_count += param_and_local_types_.size();
  return Result::Ok;
}

Result BinaryReaderInterp::GetReturnCallDropKeepCount(FuncSignature* sig,
                                                      Index keep_extra,
                                                      Index* out_drop_count,
                                                      Index* out_keep_count) {
  Index keep_count = static_cast<Index>(sig->param_types.size()) + keep_extra;
  CHECK_RESULT(GetDropCount(keep_count, 0, out_drop_count));
  *out_drop_count += current_func_->param_and_local_types.size();
  *out_keep_count = keep_count;
  return Result::Ok;
}

void BinaryReaderInterp::FixupTopLabel() {
  depth_fixups_.Resolve(istream_, label_stack_.size() - 1);
}

void BinaryReaderInterp::GetBlockSignature(Type sig_type,
                                           TypeVector* out_param_types,
                                           TypeVector* out_result_types) {
  if (IsTypeIndex(sig_type)) {
    FuncType& func_type = module_.func_types[GetTypeIndex(sig_type)];
    *out_param_types = ToCommon(func_type.params);
    *out_result_types = ToCommon(func_type.results);
  } else {
    out_param_types->clear();
    *out_result_types = GetInlineTypeVector(sig_type);
  }
}

wabt::Result BinaryReaderInterp::BeginFunctionBody(Index index, Offset size) {
  func_ = &module_.funcs[index];
  func_->code_offset = istream_.offset();

  depth_fixups_.Clear();
  label_stack_.clear();

  func_fixups_.Resolve(istream_, index);

  // Append param types.
  for (ValueType param : func_->type.params) {
    param_and_local_types_.push_back(param);
  }

  CHECK_RESULT(typechecker_.BeginFunction(ToCommon(func_->type.results)));

  // Push implicit func label (equivalent to return).
  PushLabel();
  return Result::Ok;
}

Result BinaryReaderInterp::EndFunctionBody(Index index) {
  FixupTopLabel();
  Index drop_count, keep_count;
  CHECK_RESULT(GetReturnDropKeepCount(&drop_count, &keep_count));
  CHECK_RESULT(typechecker_.EndFunction());
  istream_.EmitDropKeep(drop_count, keep_count);
  istream_.Emit(Opcode::Return);
  PopLabel();
  func_ = nullptr;
  return Result::Ok;
}

Result BinaryReaderInterp::OnLocalDeclCount(Index count) {
  local_decl_count_ = count;
  local_count_ = 0;
  return Result::Ok;
}

Result BinaryReaderInterp::OnLocalDecl(Index decl_index,
                                       Index count,
                                       Type type) {
  local_count_ += count;
  for (Index i = 0; i < count; ++i) {
    param_and_local_types_.push_back(ToInterp(type));
  }

  if (decl_index == local_decl_count_ - 1) {
    istream_.Emit(Opcode::InterpAlloca);
    istream_.Emit(local_count_);
  }
  return Result::Ok;
}

Result BinaryReaderInterp::CheckHasMemory(wabt::Opcode opcode) {
  if (module_.memories.empty()) {
    PrintError("%s requires an imported or defined memory.", opcode.GetName());
    return Result::Error;
  }
  return Result::Ok;
}

Result BinaryReaderInterp::CheckHasTable(wabt::Opcode opcode) {
  if (module_.tables.empty()) {
    PrintError("%s requires an imported or defined table.", opcode.GetName());
    return Result::Error;
  }
  return Result::Ok;
}

Result BinaryReaderInterp::CheckAlign(uint32_t alignment_log2,
                                      Address natural_alignment) {
  if (alignment_log2 >= 32 || (1U << alignment_log2) > natural_alignment) {
    PrintError("alignment must not be larger than natural alignment (%u)",
               natural_alignment);
    return Result::Error;
  }
  return Result::Ok;
}

Result BinaryReaderInterp::CheckAtomicAlign(uint32_t alignment_log2,
                                            Address natural_alignment) {
  if (alignment_log2 >= 32 || (1U << alignment_log2) != natural_alignment) {
    PrintError("alignment must be equal to natural alignment (%u)",
               natural_alignment);
    return Result::Error;
  }
  return Result::Ok;
}

Result BinaryReaderInterp::OnOpcode(Opcode opcode) {
  if (func_ == nullptr || label_stack_.empty()) {
    PrintError("Unexpected instruction after end of function");
    return Result::Error;
  }
  return Result::Ok;
}


wabt::Result BinaryReaderInterp::OnUnaryExpr(wabt::Opcode opcode) {
  CHECK_RESULT(typechecker_.OnUnary(opcode));
  istream_.Emit(opcode);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnTernaryExpr(wabt::Opcode opcode) {
  CHECK_RESULT(typechecker_.OnTernary(opcode));
  istream_.Emit(opcode);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnSimdLaneOpExpr(wabt::Opcode opcode,
                                                  uint64_t value) {
  CHECK_RESULT(typechecker_.OnSimdLaneOp(opcode, value));
  istream_.Emit(opcode);
  istream_.Emit(static_cast<u8>(value));
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnSimdShuffleOpExpr(wabt::Opcode opcode,
                                                     ::v128 value) {
  CHECK_RESULT(typechecker_.OnSimdShuffleOp(opcode, value));
  istream_.Emit(opcode);
  istream_.Emit(Bitcast<v128>(value));
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnLoadSplatExpr(wabt::Opcode opcode,
                                                 uint32_t alignment_log2,
                                                 Address offset) {
  CHECK_RESULT(CheckHasMemory(opcode));
  CHECK_RESULT(CheckAlign(alignment_log2, opcode.GetMemorySize()));
  CHECK_RESULT(typechecker_.OnLoad(opcode));
  istream_.Emit(opcode);
  istream_.Emit(offset);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnAtomicLoadExpr(Opcode opcode,
                                                  uint32_t alignment_log2,
                                                  Address offset) {
  CHECK_RESULT(CheckHasMemory(opcode));
  CHECK_RESULT(CheckAtomicAlign(alignment_log2, opcode.GetMemorySize()));
  CHECK_RESULT(typechecker_.OnAtomicLoad(opcode));
  istream_.Emit(opcode);
  istream_.Emit(offset);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnAtomicStoreExpr(Opcode opcode,
                                                   uint32_t alignment_log2,
                                                   Address offset) {
  CHECK_RESULT(CheckHasMemory(opcode));
  CHECK_RESULT(CheckAtomicAlign(alignment_log2, opcode.GetMemorySize()));
  CHECK_RESULT(typechecker_.OnAtomicStore(opcode));
  istream_.Emit(opcode);
  istream_.Emit(offset);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnAtomicRmwExpr(Opcode opcode,
                                                 uint32_t alignment_log2,
                                                 Address offset) {
  CHECK_RESULT(CheckHasMemory(opcode));
  CHECK_RESULT(CheckAtomicAlign(alignment_log2, opcode.GetMemorySize()));
  CHECK_RESULT(typechecker_.OnAtomicRmw(opcode));
  istream_.Emit(opcode);
  istream_.Emit(offset);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnAtomicRmwCmpxchgExpr(Opcode opcode,
                                                        uint32_t alignment_log2,
                                                        Address offset) {
  CHECK_RESULT(CheckHasMemory(opcode));
  CHECK_RESULT(CheckAtomicAlign(alignment_log2, opcode.GetMemorySize()));
  CHECK_RESULT(typechecker_.OnAtomicRmwCmpxchg(opcode));
  istream_.Emit(opcode);
  istream_.Emit(offset);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnBinaryExpr(wabt::Opcode opcode) {
  CHECK_RESULT(typechecker_.OnBinary(opcode));
  istream_.Emit(opcode);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnBlockExpr(Type sig_type) {
  TypeVector param_types, result_types;
  GetBlockSignature(sig_type, &param_types, &result_types);
  CHECK_RESULT(typechecker_.OnBlock(param_types, result_types));
  PushLabel();
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnLoopExpr(Type sig_type) {
  TypeVector param_types, result_types;
  GetBlockSignature(sig_type, &param_types, &result_types);
  CHECK_RESULT(typechecker_.OnLoop(param_types, result_types));
  PushLabel(istream_.offset(), Istream::kInvalidOffset);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnIfExpr(Type sig_type) {
  TypeVector param_types, result_types;
  GetBlockSignature(sig_type, &param_types, &result_types);
  CHECK_RESULT(typechecker_.OnIf(param_types, result_types));
  istream_.Emit(Opcode::InterpBrUnless);
  Istream::Offset fixup_offset = istream_.offset();
  istream_.Emit(Istream::kInvalidOffset);
  PushLabel(Istream::kInvalidOffset, fixup_offset);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnElseExpr() {
  CHECK_RESULT(typechecker_.OnElse());
  Label* label = TopLabel();
  Istream::Offset fixup_cond_offset = label->fixup_offset;
  istream_.Emit(Opcode::Br);
  label->fixup_offset = istream_.offset();
  istream_.Emit(Istream::kInvalidOffset);
  istream_.EmitAt(fixup_cond_offset, istream_.offset());
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnEndExpr() {
  TypeChecker::Label* label;
  CHECK_RESULT(typechecker_.GetLabel(0, &label));
  LabelType label_type = label->label_type;
  CHECK_RESULT(typechecker_.OnEnd());
  if (label_type == LabelType::If || label_type == LabelType::Else) {
    istream_.EmitAt(TopLabel()->fixup_offset, istream_.offset());
  }
  FixupTopLabel();
  PopLabel();
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnBrExpr(Index depth) {
  Index drop_count, keep_count;
  CHECK_RESULT(GetBrDropKeepCount(depth, &drop_count, &keep_count));
  CHECK_RESULT(typechecker_.OnBr(depth));
  istream_.EmitBr(depth, drop_count, keep_count);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnBrIfExpr(Index depth) {
  Index drop_count, keep_count;
  CHECK_RESULT(typechecker_.OnBrIf(depth));
  CHECK_RESULT(GetBrDropKeepCount(depth, &drop_count, &keep_count));
  /* flip the br_if so if <cond> is true it can drop values from the stack */
  istream_.Emit(Opcode::InterpBrUnless);
  Istream::Offset fixup_br_offset = istream_.offset();
  istream_.Emit(Istream::kInvalidOffset);
  istream_.EmitBr(depth, drop_count, keep_count);
  istream_.EmitAt(fixup_br_offset, istream_.offset());
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnBrTableExpr(Index num_targets,
                                               Index* target_depths,
                                               Index default_target_depth) {
  CHECK_RESULT(typechecker_.BeginBrTable());
  istream_.Emit(Opcode::BrTable);
  istream_.Emit(num_targets);
  Istream::Offset fixup_table_offset = istream_.offset();
  istream_.Emit(Istream::kInvalidOffset);
  /* not necessary for the interp, but it makes it easier to disassemble.
   * This opcode specifies how many bytes of data follow. */
  istream_.Emit(Opcode::InterpData);
  istream_.Emit((num_targets + 1) * WABT_TABLE_ENTRY_SIZE);
  istream_.EmitAt(fixup_table_offset, istream_.offset());

  for (Index i = 0; i <= num_targets; ++i) {
    Index depth = i != num_targets ? target_depths[i] : default_target_depth;
    CHECK_RESULT(typechecker_.OnBrTableTarget(depth));
    istream_.EmitBrTableOffset(depth);
  }

  CHECK_RESULT(typechecker_.EndBrTable());
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnCallExpr(Index func_index) {
  Func* func = GetFuncByModuleIndex(func_index);
  FuncSignature* sig = env_->GetFuncSignature(func->sig_index);
  CHECK_RESULT(typechecker_.OnCall(sig->param_types, sig->result_types));

  if (func->is_host) {
    istream_.Emit(Opcode::InterpCallHost);
    istream_.Emit(func_index);
  } else {
    istream_.Emit(Opcode::Call);
    istream_.EmitFuncOffset(cast<DefinedFunc>(func), func_index);
  }

  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnCallIndirectExpr(Index sig_index,
                                                    Index table_index) {
  if (!has_table) {
    PrintError("found call_indirect operator, but no table");
    return wabt::Result::Error;
  }
  FuncSignature* sig = GetSignatureByModuleIndex(sig_index);
  CHECK_RESULT(
      typechecker_.OnCallIndirect(sig->param_types, sig->result_types));

  istream_.Emit(Opcode::CallIndirect);
  istream_.Emit(table_index);
  istream_.Emit(sig_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnReturnCallExpr(Index func_index) {
  Func* func = GetFuncByModuleIndex(func_index);
  FuncSignature* sig = env_->GetFuncSignature(func->sig_index);

  Index drop_count, keep_count;
  CHECK_RESULT(GetReturnCallDropKeepCount(sig, 0, &drop_count, &keep_count));
  // The typechecker must be run after we get the drop/keep counts, since it
  // will change the type stack.
  CHECK_RESULT(typechecker_.OnReturnCall(sig->param_types, sig->result_types));
  istream_.EmitDropKeep(drop_count, keep_count);

  if (func->is_host) {
    istream_.Emit(Opcode::InterpCallHost);
    istream_.Emit(func_index);
    istream_.Emit(Opcode::Return);
  } else {
    istream_.Emit(Opcode::ReturnCall);
    istream_.EmitFuncOffset(cast<DefinedFunc>(func), func_index);
  }

  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnReturnCallIndirectExpr(Index sig_index,
                                                          Index table_index) {
  if (!has_table) {
    PrintError("found return_call_indirect operator, but no table");
    return wabt::Result::Error;
  }
  FuncSignature* sig = GetSignatureByModuleIndex(sig_index);

  Index drop_count, keep_count;
  // +1 to include the index of the function.
  CHECK_RESULT(GetReturnCallDropKeepCount(sig, +1, &drop_count, &keep_count));
  // The typechecker must be run after we get the drop/keep counts, since it
  // changes the type stack.
  CHECK_RESULT(
      typechecker_.OnReturnCallIndirect(sig->param_types, sig->result_types));
  istream_.EmitDropKeep(drop_count, keep_count);

  istream_.Emit(Opcode::ReturnCallIndirect);
  istream_.Emit(table_index);
  istream_.Emit(sig_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnCompareExpr(wabt::Opcode opcode) {
  return OnBinaryExpr(opcode);
}

wabt::Result BinaryReaderInterp::OnConvertExpr(wabt::Opcode opcode) {
  return OnUnaryExpr(opcode);
}

wabt::Result BinaryReaderInterp::OnDropExpr() {
  CHECK_RESULT(typechecker_.OnDrop());
  istream_.Emit(Opcode::Drop);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnI32ConstExpr(uint32_t value) {
  CHECK_RESULT(typechecker_.OnConst(Type::I32));
  istream_.Emit(Opcode::I32Const);
  istream_.Emit(value);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnI64ConstExpr(uint64_t value) {
  CHECK_RESULT(typechecker_.OnConst(Type::I64));
  istream_.Emit(Opcode::I64Const);
  istream_.EmitI64(value);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnF32ConstExpr(uint32_t value_bits) {
  CHECK_RESULT(typechecker_.OnConst(Type::F32));
  istream_.Emit(Opcode::F32Const);
  istream_.Emit(value_bits);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnF64ConstExpr(uint64_t value_bits) {
  CHECK_RESULT(typechecker_.OnConst(Type::F64));
  istream_.Emit(Opcode::F64Const);
  istream_.EmitI64(value_bits);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnV128ConstExpr(::v128 value_bits) {
  CHECK_RESULT(typechecker_.OnConst(Type::V128));
  istream_.Emit(Opcode::V128Const);
  istream_.Emit(Bitcast<v128>(value_bits));
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnGlobalGetExpr(Index global_index) {
  CHECK_RESULT(CheckGlobal(global_index));
  Type type = GetGlobalTypeByModuleIndex(global_index);
  CHECK_RESULT(typechecker_.OnGlobalGet(type));
  istream_.Emit(Opcode::GlobalGet);
  istream_.Emit(global_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnGlobalSetExpr(Index global_index) {
  CHECK_RESULT(CheckGlobal(global_index));
  Global* global = GetGlobalByModuleIndex(global_index);
  if (!global->mutable_) {
    PrintError("can't global.set on immutable global at index %" PRIindex ".",
               global_index);
    return wabt::Result::Error;
  }
  CHECK_RESULT(typechecker_.OnGlobalSet(global->type));
  istream_.Emit(Opcode::GlobalSet);
  istream_.Emit(global_index);
  return wabt::Result::Ok;
}

Index BinaryReaderInterp::TranslateLocalIndex(Index local_index) {
  return typechecker_.type_stack_size() +
         current_func_->param_and_local_types.size() - local_index;
}

wabt::Result BinaryReaderInterp::OnLocalGetExpr(Index local_index) {
  CHECK_RESULT(CheckLocal(local_index));
  Type type = GetLocalTypeByIndex(current_func_, local_index);
  // Get the translated index before calling typechecker_.OnLocalGet because it
  // will update the type stack size. We need the index to be relative to the
  // old stack size.
  Index translated_local_index = TranslateLocalIndex(local_index);
  CHECK_RESULT(typechecker_.OnLocalGet(type));
  istream_.Emit(Opcode::LocalGet);
  istream_.Emit(translated_local_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnLocalSetExpr(Index local_index) {
  CHECK_RESULT(CheckLocal(local_index));
  Type type = GetLocalTypeByIndex(current_func_, local_index);
  CHECK_RESULT(typechecker_.OnLocalSet(type));
  istream_.Emit(Opcode::LocalSet);
  istream_.Emit(TranslateLocalIndex(local_index));
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnLocalTeeExpr(Index local_index) {
  CHECK_RESULT(CheckLocal(local_index));
  Type type = GetLocalTypeByIndex(current_func_, local_index);
  CHECK_RESULT(typechecker_.OnLocalTee(type));
  istream_.Emit(Opcode::LocalTee);
  istream_.Emit(TranslateLocalIndex(local_index));
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnLoadExpr(wabt::Opcode opcode,
                                            uint32_t alignment_log2,
                                            Address offset) {
  CHECK_RESULT(CheckHasMemory(opcode));
  CHECK_RESULT(CheckAlign(alignment_log2, opcode.GetMemorySize()));
  CHECK_RESULT(typechecker_.OnLoad(opcode));
  istream_.Emit(opcode);
  istream_.Emit(offset);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnStoreExpr(wabt::Opcode opcode,
                                             uint32_t alignment_log2,
                                             Address offset) {
  CHECK_RESULT(CheckHasMemory(opcode));
  CHECK_RESULT(CheckAlign(alignment_log2, opcode.GetMemorySize()));
  CHECK_RESULT(typechecker_.OnStore(opcode));
  istream_.Emit(opcode);
  istream_.Emit(offset);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnMemoryGrowExpr() {
  CHECK_RESULT(CheckHasMemory(wabt::Opcode::MemoryGrow));
  CHECK_RESULT(typechecker_.OnMemoryGrow());
  istream_.Emit(Opcode::MemoryGrow);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnMemorySizeExpr() {
  CHECK_RESULT(CheckHasMemory(wabt::Opcode::MemorySize));
  CHECK_RESULT(typechecker_.OnMemorySize());
  istream_.Emit(Opcode::MemorySize);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnTableGrowExpr(Index table_index) {
  Table* table = GetTableByModuleIndex(table_index);
  CHECK_RESULT(typechecker_.OnTableGrow(table->elem_type));
  istream_.Emit(Opcode::TableGrow);
  istream_.Emit(table_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnTableSizeExpr(Index table_index) {
  CHECK_RESULT(typechecker_.OnTableSize());
  istream_.Emit(Opcode::TableSize);
  istream_.Emit(table_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnTableFillExpr(Index table_index) {
  Table* table = GetTableByModuleIndex(table_index);
  CHECK_RESULT(typechecker_.OnTableFill(table->elem_type));
  istream_.Emit(Opcode::TableFill);
  istream_.Emit(table_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnRefFuncExpr(Index func_index) {
  CHECK_RESULT(typechecker_.OnRefFuncExpr(func_index));
  istream_.Emit(Opcode::RefFunc);
  istream_.Emit(func_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnRefNullExpr() {
  CHECK_RESULT(typechecker_.OnRefNullExpr());
  istream_.Emit(Opcode::RefNull);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnRefIsNullExpr() {
  CHECK_RESULT(typechecker_.OnRefIsNullExpr());
  istream_.Emit(Opcode::RefIsNull);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnNopExpr() {
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnReturnExpr() {
  Index drop_count, keep_count;
  CHECK_RESULT(GetReturnDropKeepCount(&drop_count, &keep_count));
  CHECK_RESULT(typechecker_.OnReturn());
  istream_.EmitDropKeep(drop_count, keep_count);
  istream_.Emit(Opcode::Return);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnSelectExpr(Type result_type) {
  CHECK_RESULT(typechecker_.OnSelect(result_type));
  istream_.Emit(Opcode::Select);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnUnreachableExpr() {
  CHECK_RESULT(typechecker_.OnUnreachable());
  istream_.Emit(Opcode::Unreachable);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnAtomicWaitExpr(Opcode opcode,
                                                  uint32_t alignment_log2,
                                                  Address offset) {
  CHECK_RESULT(CheckHasMemory(opcode));
  CHECK_RESULT(CheckAtomicAlign(alignment_log2, opcode.GetMemorySize()));
  CHECK_RESULT(typechecker_.OnAtomicWait(opcode));
  istream_.Emit(opcode);
  istream_.Emit(offset);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnAtomicNotifyExpr(Opcode opcode,
                                                    uint32_t alignment_log2,
                                                    Address offset) {
  CHECK_RESULT(CheckHasMemory(opcode));
  CHECK_RESULT(CheckAtomicAlign(alignment_log2, opcode.GetMemorySize()));
  CHECK_RESULT(typechecker_.OnAtomicNotify(opcode));
  istream_.Emit(opcode);
  istream_.Emit(offset);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnMemoryCopyExpr() {
  CHECK_RESULT(CheckHasMemory(Opcode::MemoryCopy));
  CHECK_RESULT(typechecker_.OnMemoryCopy());
  istream_.Emit(Opcode::MemoryCopy);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnDataDropExpr(Index segment_index) {
  CHECK_RESULT(CheckDataSegment(segment_index));
  CHECK_RESULT(typechecker_.OnDataDrop(segment_index));
  istream_.Emit(Opcode::DataDrop);
  istream_.Emit(segment_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnMemoryFillExpr() {
  CHECK_RESULT(CheckHasMemory(Opcode::MemoryFill));
  CHECK_RESULT(typechecker_.OnMemoryFill());
  istream_.Emit(Opcode::MemoryFill);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnMemoryInitExpr(Index segment_index) {
  CHECK_RESULT(CheckHasMemory(Opcode::MemoryInit));
  CHECK_RESULT(CheckDataSegment(segment_index));
  CHECK_RESULT(typechecker_.OnMemoryInit(segment_index));
  istream_.Emit(Opcode::MemoryInit);
  istream_.Emit(segment_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnTableGetExpr(Index table_index) {
  const Table* table = GetTableByModuleIndex(table_index);
  CHECK_RESULT(typechecker_.OnTableGet(table->elem_type));
  istream_.Emit(Opcode::TableGet);
  istream_.Emit(table_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnTableSetExpr(Index table_index) {
  const Table* table = GetTableByModuleIndex(table_index);
  CHECK_RESULT(typechecker_.OnTableSet(table->elem_type));
  istream_.Emit(Opcode::TableSet);
  istream_.Emit(table_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnTableCopyExpr(Index dst_index,
                                                 Index src_index) {
  CHECK_RESULT(CheckHasTable(Opcode::TableCopy));
  CHECK_RESULT(typechecker_.OnTableCopy());
  istream_.Emit(Opcode::TableCopy);
  istream_.Emit(dst_index);
  istream_.Emit(src_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnElemDropExpr(Index segment_index) {
  CHECK_RESULT(CheckElemSegment(segment_index));
  CHECK_RESULT(typechecker_.OnElemDrop(segment_index));
  istream_.Emit(Opcode::ElemDrop);
  istream_.Emit(segment_index);
  return wabt::Result::Ok;
}

wabt::Result BinaryReaderInterp::OnTableInitExpr(Index segment_index,
                                                 Index table_index) {
  CHECK_RESULT(CheckHasTable(Opcode::TableInit));
  CHECK_RESULT(CheckElemSegment(segment_index));
  CHECK_RESULT(typechecker_.OnTableInit(table_index, segment_index));
  istream_.Emit(Opcode::TableInit);
  istream_.Emit(table_index);
  istream_.Emit(segment_index);
  return wabt::Result::Ok;
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
