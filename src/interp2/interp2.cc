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

#include "src/interp2/interp2.h"

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <limits>

#include "src/make-unique.h"

namespace wabt {
namespace interp2 {

const char* GetName(Mutability mut) {
  static const char* kNames[] = {"immutable", "mutable"};
  return kNames[int(mut)];
}

const char* GetName(ValueType type) {
  return GetTypeName(type);
}

const char* GetName(ExternKind kind) {
  return GetKindName(kind);
}

//// Refs ////
// static
const Ref Ref::Null{0};

//// Limits ////
Result Match(const Limits& expected,
             const Limits& actual,
             std::string* out_msg) {
  if (actual.initial < expected.initial) {
    *out_msg = StringPrintf("actual size (%" PRIu64
                            ") smaller than declared (%" PRIu64 ")",
                            actual.initial, expected.initial);
    return Result::Error;
  }

  if (expected.has_max) {
    if (!actual.has_max) {
      *out_msg = StringPrintf(
          "max size (unspecified) larger than declared (%" PRIu64 ")",
          expected.max);
      return Result::Error;
    } else if (actual.max > expected.max) {
      *out_msg = StringPrintf("max size (%" PRIu64
                              ") larger than declared (%" PRIu64 ")",
                              actual.max, expected.max);
      return Result::Error;
    }
  }

  return Result::Ok;
}

//// FuncType ////
std::unique_ptr<ExternType> FuncType::Clone() {
  return MakeUnique<FuncType>(*this);
}

Result Match(const FuncType& expected,
             const FuncType& actual,
             std::string* out_msg) {
  if (expected.params != actual.params || expected.results != actual.results) {
    if (out_msg) {
      *out_msg = "import signature mismatch";
    }
    return Result::Error;
  }
  return Result::Ok;
}

//// TableType ////
std::unique_ptr<ExternType> TableType::Clone() {
  return MakeUnique<TableType>(*this);
}

Result Match(const TableType& expected,
             const TableType& actual,
             std::string* out_msg) {
  if (expected.element != actual.element) {
    *out_msg =
        StringPrintf("type mismatch in imported table, expected %s but got %s.",
                     GetName(expected.element), GetName(actual.element));
    return Result::Error;
  }

  if (Failed(Match(expected.limits, actual.limits, out_msg))) {
    return Result::Error;
  }

  return Result::Ok;
}

//// MemoryType ////
std::unique_ptr<ExternType> MemoryType::Clone() {
  return MakeUnique<MemoryType>(*this);
}

Result Match(const MemoryType& expected,
             const MemoryType& actual,
             std::string* out_msg) {
  return Match(expected.limits, actual.limits, out_msg);
}

//// GlobalType ////
std::unique_ptr<ExternType> GlobalType::Clone() {
  return MakeUnique<GlobalType>(*this);
}

Result Match(const GlobalType& expected,
             const GlobalType& actual,
             std::string* out_msg) {
  if (actual.mut != expected.mut) {
    *out_msg = StringPrintf(
        "mutability mismatch in imported global, expected %s but got %s.",
        GetName(actual.mut), GetName(expected.mut));
    return Result::Error;
  }

  if (actual.type != expected.type &&
      (expected.mut == Mutability::Var ||
       !TypesMatch(expected.type, actual.type))) {
    *out_msg = StringPrintf(
        "type mismatch in imported global, expected %s but got %s.",
        GetName(expected.type), GetName(actual.type));
    return Result::Error;
  }

  return Result::Ok;
}

//// EventType ////
std::unique_ptr<ExternType> EventType::Clone() {
  return MakeUnique<EventType>(*this);
}

Result Match(const EventType& expected,
             const EventType& actual,
             std::string* out_msg) {
  // TODO signature
  return Result::Ok;
}

//// Limits ////
Result CanGrow(const Limits& limits, u32 old_size, u32 delta, u32* new_size) {
  if (limits.max >= delta && old_size < limits.max - delta) {
    *new_size = old_size + delta;
    return Result::Ok;
  }
  return Result::Error;
}

//// Store ////
Store::Store() {
  Ref ref{objects_.New(new Object(ObjectKind::Null))};
  assert(ref == Ref::Null);
  roots_.New(ref);
}

bool Store::HasValueType(Ref ref, ValueType type) const {
  // TODO opt?
  if (!IsValid(ref)) {
    return false;
  }
  if (type == ValueType::Anyref) {
    return true;
  }

  Object* obj = objects_.Get(ref.index).get();
  switch (type) {
    case ValueType::Funcref:
      return obj->kind() == ObjectKind::DefinedFunc ||
             obj->kind() == ObjectKind::HostFunc;
    case ValueType::Nullref:
      return ref.index == 0;
    case ValueType::Exnref:  // TODO
      return false;
    default:
      return false;
  }
}

Store::RootList::Index Store::NewRoot(Ref ref) {
  return roots_.New(ref);
}

Store::RootList::Index Store::CopyRoot(RootList::Index index) {
  return roots_.New(roots_.Get(index));
}

void Store::DeleteRoot(RootList::Index index) {
  roots_.Delete(index);
}

void Store::Collect() {
  std::fill(marks_.begin(), marks_.end(), false);
  Mark(roots_.list);
  for (size_t i = 0; i < marks_.size(); ++i) {
    if (!marks_[i]) {
      objects_.Delete(i);
    }
  }
}

void Store::Mark(Ref ref) {
  marks_[ref.index] = true;
}

void Store::Mark(const RefVec& refs) {
  for (auto&& ref: refs) {
    Mark(ref);
  }
}

//// Object ////
Object::~Object() {
  if (finalizer_) {
    finalizer_(user_data_);
  }
}

//// Foreign ////
Foreign::Foreign(Store&, void* ptr) : Object(skind), ptr_(ptr) {}

void Foreign::Mark(Store&) {}

//// Frame ////
void Frame::Mark(Store& store) {
  store.Mark(func);
}

//// Trap ////
Trap::Trap(Store& store,
           const std::string& msg,
           const std::vector<Frame>& trace)
    : Object(skind), message_(msg), trace_(trace) {}

void Trap::Mark(Store& store) {
  for (auto&& frame : trace_) {
    frame.Mark(store);
  }
}

//// Extern ////
template <typename T>
Result Extern::MatchImpl(Store& store,
                         const ImportType& import_type,
                         const T& actual,
                         Trap::Ptr* out_trap) {
  const T* extern_type = dyn_cast<T>(import_type.type.get());
  if (!extern_type) {
    *out_trap = Trap::New(
        store,
        StringPrintf("expected import \"%s.%s\" to have kind %s, not %s",
                     import_type.module.c_str(), import_type.name.c_str(),
                     GetName(import_type.type->kind), GetName(T::skind)));
    return Result::Error;
  }

  std::string msg;
  if (Failed(interp2::Match(*extern_type, actual, &msg))) {
    *out_trap = Trap::New(store, msg);
    return Result::Error;
  }

  return Result::Ok;
}

//// Func ////
Func::Func(ObjectKind kind, FuncType type) : Extern(kind), type_(type) {}

//// DefinedFunc ////
DefinedFunc::DefinedFunc(Store& store, Ref instance, FuncDesc desc)
    : Func(skind, desc.type), instance_(instance), desc_(desc) {}

void DefinedFunc::Mark(Store& store) {
  store.Mark(instance_);
}

Result DefinedFunc::Match(Store& store,
                          const ImportType& import_type,
                          Trap::Ptr* out_trap) {
  return MatchImpl(store, import_type, type_, out_trap);
}

Result DefinedFunc::Call(Store& store,
                         const TypedValues& params,
                         TypedValues* out_results,
                         Trap::Ptr* out_trap) {
  Thread::Ptr thread = Thread::New(store, Thread::Options());
  assert(params.size() == type_.params.size());
  thread->PushValues(params);
  thread->PushCall(self_, desc_.code_offset);
  RunResult result = thread->Run(store, out_trap);
  if (result == RunResult::Trap) {
    return Result::Error;
  }
  thread->CopyValues(store, type_.results, out_results);
  return Result::Ok;
}

//// HostFunc ////
HostFunc::HostFunc(Store&, FuncType type, Callback callback, void* user_data)
    : Func(skind, type), callback_(callback), user_data_(user_data) {}

void HostFunc::Mark(Store&) {}

Result HostFunc::Match(Store& store,
                       const ImportType& import_type,
                       Trap::Ptr* out_trap) {
  return MatchImpl(store, import_type, type_, out_trap);
}

Result HostFunc::Call(Store& store,
                      const TypedValues& params,
                      TypedValues* out_results,
                      Trap::Ptr* out_trap) {
  std::string msg;
  Result result = callback_(params, out_results, &msg, user_data_);
  if (Failed(result)) {
    *out_trap = Trap::New(store, msg);
  }
  return result;
}

//// Table ////
Table::Table(Store&, TableDesc desc) : Extern(skind), desc_(desc) {
  elements_.resize(desc.type.limits.initial);
}

void Table::Mark(Store& store) {
  store.Mark(elements_);
}

Result Table::Match(Store& store,
                    const ImportType& import_type,
                    Trap::Ptr* out_trap) {
  return MatchImpl(store, import_type, desc_.type, out_trap);
}

bool Table::IsValidRange(u32 offset, u32 size) const {
  size_t elem_size = elements_.size();
  return size <= elem_size && offset <= elem_size - size;
}

Result Table::Get(u32 offset, Ref* out) const {
  if (IsValidRange(offset, 1)) {
    *out = elements_[offset];
    return Result::Ok;
  }
  return Result::Error;
}

Ref Table::UnsafeGet(u32 offset) const {
  assert(IsValidRange(offset, 1));
  return elements_[offset];
}

Result Table::Set(Store& store, u32 offset, Ref ref) {
  if (IsValidRange(offset, 1) && store.HasValueType(ref, desc_.type.element)) {
    elements_[offset] = ref;
    return Result::Ok;
  }
  return Result::Error;
}

Result Table::Grow(Store& store, u32 count, Ref ref) {
  size_t old_size = elements_.size();
  u32 new_size;
  if (store.HasValueType(ref, desc_.type.element) &&
      CanGrow(desc_.type.limits, old_size, count, &new_size)) {
    elements_.resize(new_size);
    Fill(store, old_size, ref, new_size - old_size);
    return Result::Ok;
  }
  return Result::Error;
}

Result Table::Fill(Store& store, u32 offset, Ref ref, u32 size) {
  if (IsValidRange(offset, size) &&
      store.HasValueType(ref, desc_.type.element)) {
    std::fill(elements_.begin() + offset, elements_.begin() + offset + size,
              ref);
    return Result::Ok;
  }
  return Result::Error;
}

Result Table::Init(Store& store,
                   u32 dst_offset,
                   const ElemSegment& src,
                   u32 src_offset,
                   u32 size) {
  if (IsValidRange(dst_offset, size) && src.IsValidRange(src_offset, size) &&
      TypesMatch(desc_.type.element, src.desc().type)) {
    std::copy(src.elements().begin() + src_offset,
              src.elements().begin() + src_offset + size,
              elements_.begin() + dst_offset);
    return Result::Ok;
  }
  return Result::Error;
}

// static
Result Table::Copy(Store& store,
                   Table& dst,
                   u32 dst_offset,
                   const Table& src,
                   u32 src_offset,
                   u32 size) {
  if (dst.IsValidRange(dst_offset, size) &&
      src.IsValidRange(src_offset, size) &&
      TypesMatch(dst.desc_.type.element, src.desc_.type.element)) {
    std::move(src.elements_.begin() + src_offset,
              src.elements_.begin() + src_offset + size,
              dst.elements_.begin() + dst_offset);
    return Result::Ok;
  }
  return Result::Error;
}

//// Memory ////
Memory::Memory(class Store&, MemoryDesc desc)
    : Extern(skind),
      desc_(desc),
      pages_(desc.type.limits.initial) {
  data_.resize(pages_ * WABT_PAGE_SIZE);
}

void Memory::Mark(class Store&) {}

Result Memory::Match(class Store& store,
                     const ImportType& import_type,
                     Trap::Ptr* out_trap) {
  return MatchImpl(store, import_type, desc_.type, out_trap);
}

Result Memory::Grow(u32 count) {
  u32 new_pages;
  if (CanGrow(desc_.type.limits, pages_, count, &new_pages)) {
    pages_ = new_pages;
    data_.resize(new_pages * WABT_PAGE_SIZE);
    return Result::Ok;
  }
  return Result::Error;
}

Result Memory::Fill(u32 offset, u8 value, u32 size) {
  if (IsValidAccess(offset, 0, size)) {
    std::fill(data_.begin() + offset, data_.begin() + offset + size, value);
    return Result::Ok;
  }
  return Result::Error;
}

Result Memory::Init(u32 dst_offset,
                    const DataSegment& src,
                    u32 src_offset,
                    u32 size) {
  if (IsValidAccess(dst_offset, 0, size) &&
      src.IsValidRange(src_offset, size)) {
    std::copy(src.desc().data.begin() + src_offset,
              src.desc().data.begin() + src_offset + size,
              data_.begin() + dst_offset);
    return Result::Ok;
  }
  return Result::Error;
}

// static
Result Memory::Copy(Memory& dst,
                    u32 dst_offset,
                    const Memory& src,
                    u32 src_offset,
                    u32 size) {
  if (dst.IsValidAccess(dst_offset, 0, size) &&
      src.IsValidAccess(src_offset, 0, size)) {
    std::move(src.data_.begin() + src_offset,
              src.data_.begin() + src_offset + size,
              dst.data_.begin() + dst_offset);
    return Result::Ok;
  }
  return Result::Error;
}

Value Instance::ResolveInitExpr(Store& store, InitExpr init) {
  Value result;
  switch (init.kind) {
    case InitExprKind::I32:      result.Set(init.i32); break;
    case InitExprKind::I64:      result.Set(init.i64); break;
    case InitExprKind::F32:      result.Set(init.f32); break;
    case InitExprKind::F64:      result.Set(init.f64); break;
    case InitExprKind::V128:     result.Set(init.v128); break;
    case InitExprKind::GlobalGet: {
      Global::Ptr global{store, globals_[init.index]};
      result = global->Get();
      break;
    }
    case InitExprKind::RefFunc: {
      result.Set(funcs_[init.index]);
      break;
    }
    case InitExprKind::RefNull:
      result.Set(Ref::Null);
      break;
  }
  return result;
}

//// Global ////
Global::Global(Store& store, GlobalDesc desc, Value value)
    : Extern(skind), desc_(desc), value_(value) {}

void Global::Mark(Store& store) {
  if (IsReference(desc_.type.type)) {
    store.Mark(value_.ref_);
  }
}

Result Global::Match(Store& store,
                     const ImportType& import_type,
                     Trap::Ptr* out_trap) {
  return MatchImpl(store, import_type, desc_.type, out_trap);
}

Result Global::Set(Store& store, Ref ref) {
  if (store.HasValueType(ref, desc_.type.type)) {
    value_.Set(ref);
    return Result::Ok;
  }
  return Result::Error;
}

void Global::UnsafeSet(Value value) {
  value_ = value;
}

//// Event ////
Event::Event(Store&, EventDesc desc) : Extern(skind), desc_(desc) {}

void Event::Mark(Store&) {}

Result Event::Match(Store& store,
                    const ImportType& import_type,
                    Trap::Ptr* out_trap) {
  return MatchImpl(store, import_type, desc_.type, out_trap);
}

//// ElemSegment ////
ElemSegment::ElemSegment(const ElemDesc* desc, Instance::Ptr& inst)
    : desc_(desc) {
  elements_.reserve(desc->elements.size());
  for (auto&& elem_expr : desc->elements) {
    switch (elem_expr.kind) {
      case ElemKind::RefNull:
        elements_.emplace_back(Ref::Null);
        break;

      case ElemKind::RefFunc:
        elements_.emplace_back(inst->funcs_[elem_expr.index]);
        break;
    }
  }
}

bool ElemSegment::IsValidRange(u32 offset, u32 size) const {
  u32 elem_size = this->size();
  return size <= elem_size && offset <= elem_size - size;
}

//// DataSegment ////
DataSegment::DataSegment(const DataDesc* desc)
    : desc_(desc), size_(desc->data.size()) {}

bool DataSegment::IsValidRange(u32 offset, u32 size) const {
  u32 data_size = size_;
  return size <= data_size && offset <= data_size - size;
}

//// Module ////
Module::Module(Store&, ModuleDesc desc)
    : Object(skind), desc_(std::move(desc)) {
  for (auto&& import: desc_.imports) {
    import_types_.emplace_back(import.type);
  }

  for (auto&& export_: desc_.exports) {
    export_types_.emplace_back(export_.type);
  }
}

void Module::Mark(Store&) {}

//// ElemSegment ////
void ElemSegment::Mark(Store& store) {
  store.Mark(elements_);
}

//// Instance ////
Instance::Instance(Store& store, Ref module) : Object(skind), module_(module) {
  assert(store.Is<Module>(module));
}

// static
Instance::Ptr Instance::Instantiate(Store& store,
                                    Ref module,
                                    const RefVec& imports,
                                    Trap::Ptr* out_trap) {
  Module::Ptr mod{store, module};
  Instance::Ptr inst = store.Alloc<Instance>(store, module);

  size_t import_desc_count = mod->desc().imports.size();
  if (imports.size() > import_desc_count) {
    *out_trap = Trap::New(store, "not enough imports!");
    return {};
  }

  // Imports.
  for (size_t i = 0; i < import_desc_count; ++i) {
    auto&& import_desc = mod->desc().imports[i];
    Ref extern_ref = imports[i];
    Extern::Ptr extern_{store, extern_ref};
    if (Failed(extern_->Match(store, import_desc.type, out_trap))) {
      return {};
    }

    inst->imports_.push_back(extern_ref);

    switch (import_desc.type.type->kind) {
      case ExternKind::Func:   inst->funcs_.push_back(extern_ref); break;
      case ExternKind::Table:  inst->tables_.push_back(extern_ref); break;
      case ExternKind::Memory: inst->memories_.push_back(extern_ref); break;
      case ExternKind::Global: inst->globals_.push_back(extern_ref); break;
      case ExternKind::Event:  inst->events_.push_back(extern_ref); break;
    }
  }

  // Funcs.
  for (auto&& desc : mod->desc().funcs) {
    inst->funcs_.push_back(DefinedFunc::New(store, inst.ref(), desc).ref());
  }

  // Tables.
  for (auto&& desc : mod->desc().tables) {
    inst->tables_.push_back(Table::New(store, desc).ref());
  }

  // Memories.
  for (auto&& desc : mod->desc().memories) {
    inst->memories_.push_back(Memory::New(store, desc).ref());
  }

  // Globals.
  for (auto&& desc : mod->desc().globals) {
    inst->globals_.push_back(
        Global::New(store, desc, inst->ResolveInitExpr(store, desc.init))
            .ref());
  }

  // Events.
  for (auto&& desc : mod->desc().events) {
    inst->events_.push_back(Event::New(store, desc).ref());
  }

  // Exports.
  for (auto&& desc : mod->desc().exports){
    Ref ref;
    switch (desc.type.type->kind) {
      case ExternKind::Func:   ref = inst->funcs_[desc.index]; break;
      case ExternKind::Table:  ref = inst->tables_[desc.index]; break;
      case ExternKind::Memory: ref = inst->memories_[desc.index]; break;
      case ExternKind::Global: ref = inst->globals_[desc.index]; break;
      case ExternKind::Event:  ref = inst->events_[desc.index]; break;
    }
    inst->exports_.push_back(ref);
  }

  // Elems.
  for (auto&& desc : mod->desc().elems) {
    inst->elems_.emplace_back(&desc, inst);
  }

  // Datas.
  for (auto&& desc : mod->desc().datas) {
    inst->datas_.emplace_back(&desc);
  }

  // Initialization.
  enum Pass { Check, Init };
  for (auto pass : {Check, Init}) {
    // Elems.
    for (auto&& segment : inst->elems_) {
      auto&& desc = segment.desc();
      if (desc.mode == SegmentMode::Active) {
        Result result;
        Table::Ptr table{store, inst->tables_[desc.table_index]};
        u32 offset = inst->ResolveInitExpr(store, desc.offset).Get<u32>();
        if (pass == Check) {
          result = table->IsValidRange(offset, segment.size()) ? Result::Ok
                                                               : Result::Error;
        } else {
          result = table->Init(store, offset, segment, 0, segment.size());
          segment.Drop();
        }

        if (Failed(result)) {
          *out_trap = Trap::New(store, "table.init out of bounds");
          return {};
        }
      }
    }

    // Data.
    for (auto&& segment : inst->datas_) {
      auto&& desc = segment.desc();
      if (desc.mode == SegmentMode::Active) {
        Result result;
        Memory::Ptr memory{store, inst->tables_[desc.memory_index]};
        u32 offset = inst->ResolveInitExpr(store, desc.offset).Get<u32>();
        if (pass == Check) {
          result = memory->IsValidAccess(offset, 0, segment.size())
                       ? Result::Ok
                       : Result::Error;
        } else {
          result = memory->Init(offset, segment, 0, segment.size());
          segment.Drop();
        }

        if (Failed(result)) {
          *out_trap = Trap::New(store, "memory.init out of bounds");
          return {};
        }
      }
    }
  }

  // Start.
  for (auto&& start : mod->desc().starts) {
    Func::Ptr func{store, inst->funcs_[start.func_index]};
    TypedValues results;
    if (Failed(func->Call(store, {}, &results, out_trap))) {
      return {};
    }
  }

  return inst;
}

void Instance::Mark(Store& store) {
  store.Mark(module_);
  store.Mark(imports_);
  store.Mark(funcs_);
  store.Mark(memories_);
  store.Mark(tables_);
  store.Mark(globals_);
  store.Mark(events_);
  store.Mark(exports_);
  for (auto&& elem : elems_) {
    elem.Mark(store);
  }
}

//// Thread ////
Thread::Thread(Store&, const Options& options) : Object(skind) {
  frames_.reserve(options.call_stack_size);
  values_.reserve(options.value_stack_size);
  refs_.reserve(options.value_stack_size);
}

void Thread::Mark(Store& store) {
  for (auto&& frame : frames_) {
    frame.Mark(store);
  }
  for (u32 i = 0; i < refs_.size(); ++i) {
    if (refs_[i]) {
      store.Mark(values_[i].ref_);
    }
  }
}

void Thread::PushValues(const TypedValues& values) {
  for (auto&& value: values) {
    values_.push_back(value.value);
    refs_.push_back(IsReference(value.type));
  }
}

void Thread::PushCall(Ref func, u32 offset) {
  frames_.emplace_back(func, offset);
}

RunResult Thread::PopCall() {
  frames_.pop_back();
  if (frames_.empty()) {
    return RunResult::Return;
  }
  return RunResult::Ok;
}

RunResult Thread::DoReturnCall(const Func::Ptr& func, Trap::Ptr* out_trap) {
  // TODO
  return RunResult::Ok;
}

void Thread::CopyValues(Store& store,
                        const ValueTypes& types,
                        TypedValues* out_values) {
  assert(values_.size() == types.size());
  out_values->clear();
  out_values->reserve(values_.size());
  for (size_t i = 0; i < values_.size(); ++i) {
    out_values->emplace_back(store, types[i], values_[i]);
  }
}

RunResult Thread::Run(Store& store, Trap::Ptr* out_trap) {
  const int kDefaultInstructionCount = 1000;
  RunResult result;
  do {
    result = Run(store, kDefaultInstructionCount, out_trap);
  } while (result == RunResult::Ok);
  return result;
}

RunResult Thread::Run(Store& store, int num_instructions, Trap::Ptr* out_trap) {
  DefinedFunc::Ptr func{store, frames_.back().func};
  Instance::Ptr inst{store, func->instance()};
  Module::Ptr mod{store, inst->module()};
  for (;num_instructions > 0; --num_instructions) {
    auto result = StepInternal(store, inst, mod, out_trap);
    if (result != RunResult::Ok) {
      return result;
    }
  }
  return RunResult::Ok;
}

RunResult Thread::Step(Store& store, Trap::Ptr* out_trap) {
  DefinedFunc::Ptr func{store, frames_.back().func};
  Instance::Ptr inst{store, func->instance()};
  Module::Ptr mod{store, inst->module()};
  return StepInternal(store, inst, mod, out_trap);
}

Value& Thread::Pick(Index index) {
  assert(index > 0 && index <= values_.size());
  return values_[values_.size() - index];
}

template <typename T>
T Thread::Pop() {
  return Pop().Get<T>();
}

Value Thread::Pop() {
  refs_.pop_back();
  auto value = values_.back();
  values_.pop_back();
  return value;
}

template <typename T>
void Thread::Push(T value) {
  Push(Value(value));
}

template <>
void Thread::Push<bool>(bool value) {
  Push(Value(static_cast<u32>(value ? 1 : 0)));
}

void Thread::Push(Value value) {
  values_.push_back(value);
  refs_.push_back(false);
}

void Thread::Push(Ref ref) {
  values_.push_back(Value(ref));
  refs_.push_back(true);
}

#define TRAP(msg) *out_trap = Trap::New(store, (msg), frames_), RunResult::Trap
#define TRAP_IF(cond, msg)     \
  if (WABT_UNLIKELY((cond))) { \
    return TRAP(msg);          \
  }
#define TRAP_UNLESS(cond, msg) TRAP_IF(!(cond), msg)

template <typename T> T ShiftMask(T val) { return val & (sizeof(T)*8-1); }

template <typename T> bool IntEqz(T val) { return val == 0; }
template <typename T> bool Eq(T lhs, T rhs) { return lhs == rhs; }
template <typename T> bool Ne(T lhs, T rhs) { return lhs != rhs; }
template <typename T> bool Lt(T lhs, T rhs) { return lhs < rhs; }
template <typename T> bool Le(T lhs, T rhs) { return lhs <= rhs; }
template <typename T> bool Gt(T lhs, T rhs) { return lhs > rhs; }
template <typename T> bool Ge(T lhs, T rhs) { return lhs >= rhs; }
template <typename T> T IntClz(T val) { return Clz(val); }
template <typename T> T IntCtz(T val) { return Ctz(val); }
template <typename T> T IntPopcnt(T val) { return Popcount(val); }
template <typename T> T Add(T lhs, T rhs) { return lhs + rhs; }
template <typename T> T Sub(T lhs, T rhs) { return lhs - rhs; }
template <typename T> T Mul(T lhs, T rhs) { return lhs * rhs; }
template <typename T> T IntAnd(T lhs, T rhs) { return lhs & rhs; }
template <typename T> T IntOr(T lhs, T rhs) { return lhs | rhs; }
template <typename T> T IntXor(T lhs, T rhs) { return lhs ^ rhs; }
template <typename T> T IntShl(T lhs, T rhs) { return lhs << ShiftMask(rhs); }
template <typename T> T IntShr(T lhs, T rhs) { return lhs >> ShiftMask(rhs); }

template <typename T>
T IntRotl(T lhs, T rhs) {
  return (lhs << ShiftMask(rhs)) | (lhs >> ShiftMask<T>(-rhs));
}

template <typename T>
T IntRotr(T lhs, T rhs) {
  return (lhs >> ShiftMask(rhs)) | (lhs << ShiftMask<T>(-rhs));
}

template <typename T, int N>
T IntExtend(T val) {
  // Hacker's delight 2.6 - sign extension
  auto bit = T{1} << N;
  auto mask = (bit << 1) - 1;
  return ((val & mask) ^ bit) - bit;
}

// i{32,64}.{div,rem}_s are special-cased because they trap when dividing the
// max signed value by -1. The modulo operation on x86 uses the same
// instruction to generate the quotient and the remainder.
template <typename T,
          typename std::enable_if<std::is_signed<T>::value, int>::type = 0>
bool IsNormalDivRem(T lhs, T rhs) {
  return !(lhs == std::numeric_limits<T>::min() && rhs == -1);
}

template <typename T,
          typename std::enable_if<!std::is_signed<T>::value, int>::type = 0>
bool IsNormalDivRem(T lhs, T rhs) {
  return true;
}

template <typename T>
RunResult IntDiv(T lhs, T rhs, T* out, std::string* out_msg) {
  if (WABT_UNLIKELY(rhs == 0)) { *out_msg = "integer divide by zero"; return RunResult::Trap; }
  if (WABT_LIKELY(IsNormalDivRem(lhs, rhs))) {
    *out = lhs / rhs;
    return RunResult::Ok;
  } else {
    *out_msg = "integer overflow";
    return RunResult::Trap;
  }
}

template <typename T>
RunResult IntRem(T lhs, T rhs, T* out, std::string* out_msg) {
  if (WABT_UNLIKELY(rhs == 0)) { *out_msg = "integer divide by zero"; return RunResult::Trap; }
  if (WABT_LIKELY(IsNormalDivRem(lhs, rhs))) {
    *out = lhs % rhs;
  } else {
    *out = 0;
  }
  return RunResult::Ok;
}

template <typename T> T FloatAbs(T val) { return std::abs(val); }
template <typename T> T FloatNeg(T val) { return -val; }
template <typename T> T FloatCeil(T val) { return std::ceil(val); }
template <typename T> T FloatFloor(T val) { return std::floor(val); }
template <typename T> T FloatTrunc(T val) { return std::trunc(val); }
template <typename T> T FloatNearest(T val) { return std::nearbyint(val); }
template <typename T> T FloatSqrt(T val) { return std::sqrt(val); }
template <typename T> T FloatCopysign(T lhs, T rhs) { return std::copysign(lhs, rhs); }

template <typename T>
T FloatDiv(T lhs, T rhs) {
  // IEE754 specifies what should happen when dividing a float by zero, but
  // C/C++ says it is undefined behavior.
  if (WABT_UNLIKELY(rhs == 0)) {
    return std::isnan(lhs) || lhs == 0
               ? std::numeric_limits<T>::quiet_NaN()
               : std::copysign(std::numeric_limits<T>::infinity(), lhs * rhs);
  }
  return lhs / rhs;
}

template <typename T>
T FloatMin(T lhs, T rhs) {
  if (WABT_UNLIKELY(std::isnan(lhs) || std::isnan(rhs))) {
    return std::numeric_limits<T>::quiet_NaN();
  } else if (WABT_UNLIKELY(lhs == 0 || rhs == 0)) {
    return std::signbit(lhs) ? lhs : rhs;
  } else {
    return std::min(lhs, rhs);
  }
}

template <typename T>
T FloatMax(T lhs, T rhs) {
  if (WABT_UNLIKELY(std::isnan(lhs) || std::isnan(rhs))) {
    return std::numeric_limits<T>::quiet_NaN();
  } else if (WABT_UNLIKELY(lhs == 0 || rhs == 0)) {
    return std::signbit(lhs) ? rhs : lhs;
  } else {
    return std::max(lhs, rhs);
  }
}

template <typename R, typename T> bool CanConvert(T val) { return true; }
template <> bool CanConvert<s32, f32>(f32 val) { return val >= -2147483648.f && val < 2147483648.f; }
template <> bool CanConvert<s32, f64>(f64 val) { return val >= -2147483648. && val <= 2147483647.; }
template <> bool CanConvert<u32, f32>(f32 val) { return val > -1.f && val <= 4294967296.f; }
template <> bool CanConvert<u32, f64>(f64 val) { return val > -1. && val <= 4294967295.; }
template <> bool CanConvert<s64, f32>(f32 val) { return val >= -9223372036854775808.f && val < 9223372036854775808.f; }
template <> bool CanConvert<s64, f64>(f64 val) { return val >= -9223372036854775808. && val < 9223372036854775808.; }
template <> bool CanConvert<u64, f32>(f32 val) { return val > -1.f && val < 18446744073709551616.f; }
template <> bool CanConvert<u64, f64>(f64 val) { return val > -1. && val < 18446744073709551616.; }

template <typename R, typename T>
R IntTruncSat(T val) {
  if (WABT_UNLIKELY(std::isnan(val))) {
    return 0;
  } else if (WABT_UNLIKELY(!CanConvert<R>(val))) {
    return std::signbit(val) ? std::numeric_limits<R>::min()
                             : std::numeric_limits<R>::max();
  } else {
    return static_cast<R>(val);
  }
}

RunResult Thread::StepInternal(Store& store,
                               Instance::Ptr& inst,
                               Module::Ptr& mod,
                               Trap::Ptr* out_trap) {
  u32& pc = frames_.back().offset;
  auto instr = mod->desc().istream.Read(&pc);
  switch (instr.op) {
    case Opcode::Unreachable:
      return TRAP("unreachable executed");

    case Opcode::Br:
      pc = instr.imm_u32;
      break;

    case Opcode::BrIf:
      if (Pop<u32>()) {
        pc = instr.imm_u32;
      }
      break;

    case Opcode::BrTable: {
      auto key = Pop<u32>();
      if (key >= instr.imm_u32) {
        key = instr.imm_u32;
      }
      pc += key * 16;  // TODO: magic number
      break;
    }

    case Opcode::Return:
      return PopCall();

    case Opcode::Call: {
      Ref new_func_ref = inst->funcs()[instr.imm_u32];
      DefinedFunc::Ptr new_func{store, new_func_ref};
      PushCall(new_func_ref, new_func->desc().code_offset);
      break;
    }

    case Opcode::CallIndirect:
    case Opcode::ReturnCallIndirect: {
      Table::Ptr table{store, inst->tables()[instr.imm_u32x2.fst]};
      auto&& func_type = mod->desc().func_types[instr.imm_u32x2.snd];
      auto entry = Pop<u32>();
      TRAP_IF(entry >= table->elements().size(), "undefined table index");
      auto new_func_ref = table->elements()[entry];
      TRAP_IF(new_func_ref == Ref::Null, "uninitialized table element");
      Func::Ptr new_func{store, new_func_ref};
      TRAP_IF(Failed(Match(new_func->func_type(), func_type, nullptr)),
              "call indirect func type mismatch");
      if (instr.op == Opcode::ReturnCallIndirect) {
        return DoReturnCall(new_func, out_trap);
      } else {
        return DoCall(new_func, out_trap);
      }
    }

    case Opcode::Drop:
      Pop();
      break;

    case Opcode::Select: {
      auto cond = Pop<u32>();
      Value false_ = Pop();
      Value true_ = Pop();
      Push(cond ? true_ : false_);
      break;
    }

    case Opcode::LocalGet:
      Push(Pick(instr.imm_u32));
      break;

    case Opcode::LocalSet: {
      auto value = Pop();
      Pick(instr.imm_u32) = value;
      break;
    }

    case Opcode::LocalTee:
      Pick(instr.imm_u32) = Pick(1);
      break;

    case Opcode::GlobalGet: {
      Global::Ptr global{store, inst->globals()[instr.imm_u32]};
      Push(global->Get());
      break;
    }

    case Opcode::GlobalSet: {
      Global::Ptr global{store, inst->globals()[instr.imm_u32]};
      global->UnsafeSet(Pop());
      break;
    }

    case Opcode::I32Load:    return DoLoad<u32>(store, inst, instr, out_trap);
    case Opcode::I64Load:    return DoLoad<u64>(store, inst, instr, out_trap);
    case Opcode::F32Load:    return DoLoad<f32>(store, inst, instr, out_trap);
    case Opcode::F64Load:    return DoLoad<f64>(store, inst, instr, out_trap);
    case Opcode::I32Load8S:  return DoLoad<s8, s32>(store, inst, instr, out_trap);
    case Opcode::I32Load8U:  return DoLoad<u8, u32>(store, inst, instr, out_trap);
    case Opcode::I32Load16S: return DoLoad<s16, s32>(store, inst, instr, out_trap);
    case Opcode::I32Load16U: return DoLoad<u16, u32>(store, inst, instr, out_trap);
    case Opcode::I64Load8S:  return DoLoad<s8, s64>(store, inst, instr, out_trap);
    case Opcode::I64Load8U:  return DoLoad<u8, u64>(store, inst, instr, out_trap);
    case Opcode::I64Load16S: return DoLoad<s16, s64>(store, inst, instr, out_trap);
    case Opcode::I64Load16U: return DoLoad<u16, u64>(store, inst, instr, out_trap);
    case Opcode::I64Load32S: return DoLoad<s32, s64>(store, inst, instr, out_trap);
    case Opcode::I64Load32U: return DoLoad<u32, u64>(store, inst, instr, out_trap);

    case Opcode::I32Store:   return DoStore<u32>(store, inst, instr, out_trap);
    case Opcode::I64Store:   return DoStore<u64>(store, inst, instr, out_trap);
    case Opcode::F32Store:   return DoStore<f32>(store, inst, instr, out_trap);
    case Opcode::F64Store:   return DoStore<f64>(store, inst, instr, out_trap);
    case Opcode::I32Store8:  return DoStore<u8, u32>(store, inst, instr, out_trap);
    case Opcode::I32Store16: return DoStore<u16, u32>(store, inst, instr, out_trap);
    case Opcode::I64Store8:  return DoStore<u8, u64>(store, inst, instr, out_trap);
    case Opcode::I64Store16: return DoStore<u16, u64>(store, inst, instr, out_trap);
    case Opcode::I64Store32: return DoStore<u32, u64>(store, inst, instr, out_trap);

    case Opcode::MemorySize: {
      Memory::Ptr memory{store, inst->memories()[instr.imm_u32]};
      Push(memory->PageSize());
      break;
    }

    case Opcode::MemoryGrow: {
      Memory::Ptr memory{store, inst->memories()[instr.imm_u32]};
      u32 old_size = memory->PageSize();
      if (Failed(memory->Grow(Pop<u32>()))) {
        Push<s32>(-1);
      } else {
        Push<u32>(old_size);
      }
      break;
    }

    case Opcode::I32Const:
    case Opcode::F32Const:
      Push<u32>(instr.imm_u32);
      break;

    case Opcode::I64Const:
    case Opcode::F64Const:
      Push<u64>(instr.imm_u64);
      break;

    case Opcode::I32Eqz: return DoUnop(IntEqz<u32>);
    case Opcode::I32Eq:  return DoBinop(Eq<u32>);
    case Opcode::I32Ne:  return DoBinop(Ne<u32>);
    case Opcode::I32LtS: return DoBinop(Lt<s32>);
    case Opcode::I32LtU: return DoBinop(Lt<u32>);
    case Opcode::I32GtS: return DoBinop(Gt<s32>);
    case Opcode::I32GtU: return DoBinop(Gt<u32>);
    case Opcode::I32LeS: return DoBinop(Le<s32>);
    case Opcode::I32LeU: return DoBinop(Le<u32>);
    case Opcode::I32GeS: return DoBinop(Ge<s32>);
    case Opcode::I32GeU: return DoBinop(Ge<u32>);

    case Opcode::I64Eqz: return DoUnop(IntEqz<u64>);
    case Opcode::I64Eq:  return DoBinop(Eq<u64>);
    case Opcode::I64Ne:  return DoBinop(Ne<u64>);
    case Opcode::I64LtS: return DoBinop(Lt<s64>);
    case Opcode::I64LtU: return DoBinop(Lt<u64>);
    case Opcode::I64GtS: return DoBinop(Gt<s64>);
    case Opcode::I64GtU: return DoBinop(Gt<u64>);
    case Opcode::I64LeS: return DoBinop(Le<s64>);
    case Opcode::I64LeU: return DoBinop(Le<u64>);
    case Opcode::I64GeS: return DoBinop(Ge<s64>);
    case Opcode::I64GeU: return DoBinop(Ge<u64>);

    case Opcode::F32Eq:  return DoBinop(Eq<f32>);
    case Opcode::F32Ne:  return DoBinop(Ne<f32>);
    case Opcode::F32Lt:  return DoBinop(Lt<f32>);
    case Opcode::F32Gt:  return DoBinop(Gt<f32>);
    case Opcode::F32Le:  return DoBinop(Le<f32>);
    case Opcode::F32Ge:  return DoBinop(Ge<f32>);

    case Opcode::F64Eq:  return DoBinop(Eq<f64>);
    case Opcode::F64Ne:  return DoBinop(Ne<f64>);
    case Opcode::F64Lt:  return DoBinop(Lt<f64>);
    case Opcode::F64Gt:  return DoBinop(Gt<f64>);
    case Opcode::F64Le:  return DoBinop(Le<f64>);
    case Opcode::F64Ge:  return DoBinop(Ge<f64>);

    case Opcode::I32Clz:    return DoUnop(IntClz<u32>);
    case Opcode::I32Ctz:    return DoUnop(IntCtz<u32>);
    case Opcode::I32Popcnt: return DoUnop(IntPopcnt<u32>);
    case Opcode::I32Add:    return DoBinop(Add<u32>);
    case Opcode::I32Sub:    return DoBinop(Sub<u32>);
    case Opcode::I32Mul:    return DoBinop(Mul<u32>);
    case Opcode::I32DivS:   return DoBinop(store, IntDiv<s32>, out_trap);
    case Opcode::I32DivU:   return DoBinop(store, IntDiv<u32>, out_trap);
    case Opcode::I32RemS:   return DoBinop(store, IntRem<s32>, out_trap);
    case Opcode::I32RemU:   return DoBinop(store, IntRem<u32>, out_trap);
    case Opcode::I32And:    return DoBinop(IntAnd<u32>);
    case Opcode::I32Or:     return DoBinop(IntOr<u32>);
    case Opcode::I32Xor:    return DoBinop(IntXor<u32>);
    case Opcode::I32Shl:    return DoBinop(IntShl<u32>);
    case Opcode::I32ShrS:   return DoBinop(IntShr<s32>);
    case Opcode::I32ShrU:   return DoBinop(IntShr<u32>);
    case Opcode::I32Rotl:   return DoBinop(IntRotl<u32>);
    case Opcode::I32Rotr:   return DoBinop(IntRotr<u32>);

    case Opcode::I64Clz:    return DoUnop(IntClz<u64>);
    case Opcode::I64Ctz:    return DoUnop(IntCtz<u64>);
    case Opcode::I64Popcnt: return DoUnop(IntPopcnt<u64>);
    case Opcode::I64Add:    return DoBinop(Add<u64>);
    case Opcode::I64Sub:    return DoBinop(Sub<u64>);
    case Opcode::I64Mul:    return DoBinop(Mul<u64>);
    case Opcode::I64DivS:   return DoBinop(store, IntDiv<s64>, out_trap);
    case Opcode::I64DivU:   return DoBinop(store, IntDiv<u64>, out_trap);
    case Opcode::I64RemS:   return DoBinop(store, IntRem<s64>, out_trap);
    case Opcode::I64RemU:   return DoBinop(store, IntRem<u64>, out_trap);
    case Opcode::I64And:    return DoBinop(IntAnd<u64>);
    case Opcode::I64Or:     return DoBinop(IntOr<u64>);
    case Opcode::I64Xor:    return DoBinop(IntXor<u64>);
    case Opcode::I64Shl:    return DoBinop(IntShl<u64>);
    case Opcode::I64ShrS:   return DoBinop(IntShr<s64>);
    case Opcode::I64ShrU:   return DoBinop(IntShr<u64>);
    case Opcode::I64Rotl:   return DoBinop(IntRotl<u64>);
    case Opcode::I64Rotr:   return DoBinop(IntRotr<u64>);

    case Opcode::F32Abs:     return DoUnop(FloatAbs<f32>);
    case Opcode::F32Neg:     return DoUnop(FloatNeg<f32>);
    case Opcode::F32Ceil:    return DoUnop(FloatCeil<f32>);
    case Opcode::F32Floor:   return DoUnop(FloatFloor<f32>);
    case Opcode::F32Trunc:   return DoUnop(FloatTrunc<f32>);
    case Opcode::F32Nearest: return DoUnop(FloatNearest<f32>);
    case Opcode::F32Sqrt:    return DoUnop(FloatSqrt<f32>);
    case Opcode::F32Add:      return DoBinop(Add<f32>);
    case Opcode::F32Sub:      return DoBinop(Sub<f32>);
    case Opcode::F32Mul:      return DoBinop(Mul<f32>);
    case Opcode::F32Div:      return DoBinop(FloatDiv<f32>);
    case Opcode::F32Min:      return DoBinop(FloatMin<f32>);
    case Opcode::F32Max:      return DoBinop(FloatMax<f32>);
    case Opcode::F32Copysign: return DoBinop(FloatCopysign<f32>);

    case Opcode::F64Abs:     return DoUnop(FloatAbs<f64>);
    case Opcode::F64Neg:     return DoUnop(FloatNeg<f64>);
    case Opcode::F64Ceil:    return DoUnop(FloatCeil<f64>);
    case Opcode::F64Floor:   return DoUnop(FloatFloor<f64>);
    case Opcode::F64Trunc:   return DoUnop(FloatTrunc<f64>);
    case Opcode::F64Nearest: return DoUnop(FloatNearest<f64>);
    case Opcode::F64Sqrt:    return DoUnop(FloatSqrt<f64>);
    case Opcode::F64Add:      return DoBinop(Add<f64>);
    case Opcode::F64Sub:      return DoBinop(Sub<f64>);
    case Opcode::F64Mul:      return DoBinop(Mul<f64>);
    case Opcode::F64Div:      return DoBinop(FloatDiv<f64>);
    case Opcode::F64Min:      return DoBinop(FloatMin<f64>);
    case Opcode::F64Max:      return DoBinop(FloatMax<f64>);
    case Opcode::F64Copysign: return DoBinop(FloatCopysign<f64>);

    case Opcode::I32WrapI64:      return DoConvert<u32, u64>(store, out_trap);
    case Opcode::I32TruncF32S:    return DoConvert<s32, f32>(store, out_trap);
    case Opcode::I32TruncF32U:    return DoConvert<u32, f32>(store, out_trap);
    case Opcode::I32TruncF64S:    return DoConvert<s32, f64>(store, out_trap);
    case Opcode::I32TruncF64U:    return DoConvert<u32, f64>(store, out_trap);
    case Opcode::I64ExtendI32S:   return DoConvert<s64, s32>(store, out_trap);
    case Opcode::I64ExtendI32U:   return DoConvert<u64, u32>(store, out_trap);
    case Opcode::I64TruncF32S:    return DoConvert<s64, f32>(store, out_trap);
    case Opcode::I64TruncF32U:    return DoConvert<u64, f32>(store, out_trap);
    case Opcode::I64TruncF64S:    return DoConvert<s64, f64>(store, out_trap);
    case Opcode::I64TruncF64U:    return DoConvert<u64, f64>(store, out_trap);
    case Opcode::F32ConvertI32S:  return DoConvert<f32, s32>(store, out_trap);
    case Opcode::F32ConvertI32U:  return DoConvert<f32, u32>(store, out_trap);
    case Opcode::F32ConvertI64S:  return DoConvert<f32, s64>(store, out_trap);
    case Opcode::F32ConvertI64U:  return DoConvert<f32, u64>(store, out_trap);
    case Opcode::F32DemoteF64:    return DoConvert<f32, f64>(store, out_trap);
    case Opcode::F64ConvertI32S:  return DoConvert<f64, s32>(store, out_trap);
    case Opcode::F64ConvertI32U:  return DoConvert<f64, u32>(store, out_trap);
    case Opcode::F64ConvertI64S:  return DoConvert<f64, s64>(store, out_trap);
    case Opcode::F64ConvertI64U:  return DoConvert<f64, u64>(store, out_trap);
    case Opcode::F64PromoteF32:   return DoConvert<f64, f64>(store, out_trap);

    case Opcode::I32ReinterpretF32: return DoReinterpret<u32, f32>();
    case Opcode::F32ReinterpretI32: return DoReinterpret<f32, u32>();
    case Opcode::I64ReinterpretF64: return DoReinterpret<u64, f64>();
    case Opcode::F64ReinterpretI64: return DoReinterpret<f64, u64>();

    case Opcode::I32Extend8S:   return DoUnop(IntExtend<u32, 7>);
    case Opcode::I32Extend16S:  return DoUnop(IntExtend<u32, 15>);
    case Opcode::I64Extend8S:   return DoUnop(IntExtend<u64, 7>);
    case Opcode::I64Extend16S:  return DoUnop(IntExtend<u64, 15>);
    case Opcode::I64Extend32S:  return DoUnop(IntExtend<u64, 31>);

    case Opcode::InterpAlloca:
      values_.resize(values_.size() + instr.imm_u32);
      refs_.resize(refs_.size() + instr.imm_u32);
      break;

    case Opcode::InterpBrUnless:
      if (!Pop<u32>()) {
        pc = instr.imm_u32;
      }
      break;

    case Opcode::InterpCallHost: {
      Ref new_func_ref = inst->funcs()[instr.imm_u32];
      Func::Ptr new_func{store, new_func_ref};
      return DoCall(new_func, out_trap);
    }

    case Opcode::InterpDropKeep: {
      auto drop = instr.imm_u32x2.fst;
      auto keep = instr.imm_u32x2.snd;
      std::move(values_.end() - keep, values_.end(), values_.end() - drop - keep);
      std::move(refs_.end() - keep, refs_.end(), refs_.end() - drop - keep);
      values_.resize(values_.size() - drop);
      refs_.resize(refs_.size() - drop);
      break;
    }

    case Opcode::I32TruncSatF32S: return DoUnop(IntTruncSat<s32, f32>);
    case Opcode::I32TruncSatF32U: return DoUnop(IntTruncSat<u32, f32>);
    case Opcode::I32TruncSatF64S: return DoUnop(IntTruncSat<s32, f64>);
    case Opcode::I32TruncSatF64U: return DoUnop(IntTruncSat<u32, f64>);
    case Opcode::I64TruncSatF32S: return DoUnop(IntTruncSat<s64, f32>);
    case Opcode::I64TruncSatF32U: return DoUnop(IntTruncSat<u64, f32>);
    case Opcode::I64TruncSatF64S: return DoUnop(IntTruncSat<s64, f64>);
    case Opcode::I64TruncSatF64U: return DoUnop(IntTruncSat<u64, f64>);

    case Opcode::MemoryInit: return DoMemoryInit(store, inst, instr, out_trap);
    case Opcode::DataDrop:   return DoDataDrop(inst, instr);
    case Opcode::MemoryCopy: return DoMemoryCopy(store, inst, instr, out_trap);
    case Opcode::MemoryFill: return DoMemoryFill(store, inst, instr, out_trap);

    case Opcode::TableInit: return DoTableInit(store, inst, instr, out_trap);
    case Opcode::ElemDrop:  return DoElemDrop(inst, instr);
    case Opcode::TableCopy: return DoTableCopy(store, inst, instr, out_trap);
    case Opcode::TableGet:  return DoTableGet(store, inst, instr, out_trap);
    case Opcode::TableSet:  return DoTableSet(store, inst, instr, out_trap);
    case Opcode::TableGrow: return DoTableGrow(store, inst, instr, out_trap);
    case Opcode::TableSize: return DoTableSize(store, inst, instr);
    case Opcode::TableFill: return DoTableFill(store, inst, instr, out_trap);

    case Opcode::RefNull:
      Push<Ref>(Ref::Null);
      break;

    case Opcode::RefIsNull:
      Push(Pop<Ref>() == Ref::Null);
      break;

    case Opcode::RefFunc:
      Push(inst->funcs()[instr.imm_u32]);
      break;

    // The following opcodes are either never generated or should never be
    // executed.
    case Opcode::Nop:
    case Opcode::Block:
    case Opcode::Loop:
    case Opcode::If:
    case Opcode::Else:
    case Opcode::End:
    case Opcode::ReturnCall:
    case Opcode::SelectT:

    case Opcode::Try:
    case Opcode::Catch:
    case Opcode::Throw:
    case Opcode::Rethrow:
    case Opcode::BrOnExn:
    case Opcode::InterpData:
    case Opcode::Invalid:
      WABT_UNREACHABLE;
      break;
  }

  return RunResult::Ok;
}

RunResult Thread::DoCall(const Func::Ptr& func, Trap::Ptr* out_trap) {
  if (auto* host_func = dyn_cast<HostFunc>(func.get())) {
    // TODO
  } else {
    auto* defined_func = cast<DefinedFunc>(func.get());
    PushCall(defined_func->self(), defined_func->desc().code_offset);
  }
  return RunResult::Ok;
}

template <typename T, typename V>
RunResult Thread::DoLoad(Store& store,
                         Instance::Ptr& inst,
                         Instr instr,
                         Trap::Ptr* out_trap) {
  Memory::Ptr memory{store, inst->memories()[instr.imm_u32x2.fst]};
  u32 offset = Pop<u32>();
  V val;
  TRAP_IF(Failed(memory->Load(offset, instr.imm_u32x2.snd, &val)),
          StringPrintf("access at %u+%u >= max value %u", offset,
                       instr.imm_u32x2.snd, memory->ByteSize()));
  Push(static_cast<T>(val));
  return RunResult::Ok;
}

template <typename T, typename V>
RunResult Thread::DoStore(Store& store,
                          Instance::Ptr& inst,
                          Instr instr,
                          Trap::Ptr* out_trap) {
  Memory::Ptr memory{store, inst->memories()[instr.imm_u32x2.fst]};
  u32 offset = Pop<u32>();
  T val = static_cast<T>(Pop<V>());
  TRAP_IF(Failed(memory->Store(offset, instr.imm_u32x2.snd, val)),
          StringPrintf("access at %u+%u >= max value %u", offset,
                       instr.imm_u32x2.snd, memory->ByteSize()));
  return RunResult::Ok;
}

template <typename R, typename T>
RunResult Thread::DoUnop(UnopFunc<R, T> f) {
  Push<T>(f(Pop<T>()));
  return RunResult::Ok;
}

template <typename R, typename T>
RunResult Thread::DoUnop(Store& store,
                         UnopTrapFunc<R, T> f,
                         Trap::Ptr* out_trap) {
  T out;
  std::string msg;
  TRAP_IF(f(Pop<T>(), &out, &msg) == RunResult::Trap, msg);
  Push<T>(out);
  return RunResult::Ok;
}

template <typename R, typename T>
RunResult Thread::DoBinop(BinopFunc<R, T> f) {
  auto rhs = Pop<T>();
  auto lhs = Pop<T>();
  Push<T>(f(lhs, rhs));
  return RunResult::Ok;
}

template <typename R, typename T>
RunResult Thread::DoBinop(Store& store,
                          BinopTrapFunc<R, T> f,
                          Trap::Ptr* out_trap) {
  auto rhs = Pop<T>();
  auto lhs = Pop<T>();
  T out;
  std::string msg;
  TRAP_IF(f(lhs, rhs, &out, &msg) == RunResult::Trap, msg);
  Push<T>(out);
  return RunResult::Ok;
}

template <typename R, typename T>
RunResult Thread::DoConvert(Store& store, Trap::Ptr* out_trap) {
  auto val = Pop<T>();
  TRAP_UNLESS(CanConvert<R>(val), "integer overflow");
  Push<R>(static_cast<R>(val));
  return RunResult::Ok;
}

template <typename R, typename T>
RunResult Thread::DoReinterpret() {
  Push(Bitcast<R>(Pop<T>()));
  return RunResult::Ok;
}

RunResult Thread::DoMemoryInit(Store& store,
                               Instance::Ptr& inst,
                               Instr instr,
                               Trap::Ptr* out_trap) {
  Memory::Ptr memory{store, inst->memories()[instr.imm_u32x2.fst]};
  auto&& data = inst->datas()[instr.imm_u32x2.snd];
  auto size = Pop<u32>();
  auto src = Pop<u32>();
  auto dst = Pop<u32>();
  TRAP_IF(Failed(memory->Init(dst, data, src, size)),
          "memory.init out of bounds");
  return RunResult::Ok;
}

RunResult Thread::DoDataDrop(Instance::Ptr& inst, Instr instr) {
  inst->datas()[instr.imm_u32].Drop();
  return RunResult::Ok;
}

RunResult Thread::DoMemoryCopy(Store& store,
                               Instance::Ptr& inst,
                               Instr instr,
                               Trap::Ptr* out_trap) {
  Memory::Ptr mem_dst{store, inst->memories()[instr.imm_u32x2.fst]};
  Memory::Ptr mem_src{store, inst->memories()[instr.imm_u32x2.snd]};
  auto size = Pop<u32>();
  auto src = Pop<u32>();
  auto dst = Pop<u32>();
  TRAP_IF(Failed(Memory::Copy(*mem_dst, dst, *mem_src, src, size)),
          "memory.copy out of bounds");
  return RunResult::Ok;
}

RunResult Thread::DoMemoryFill(Store& store,
                               Instance::Ptr& inst,
                               Instr instr,
                               Trap::Ptr* out_trap) {
  Memory::Ptr memory{store, inst->memories()[instr.imm_u32]};
  auto size = Pop<u32>();
  auto value = Pop<u32>();
  auto dst = Pop<u32>();
  TRAP_IF(Failed(memory->Fill(dst, value, size)), "memory.fill out of bounds");
  return RunResult::Ok;
}

RunResult Thread::DoTableInit(Store& store,
                              Instance::Ptr& inst,
                              Instr instr,
                              Trap::Ptr* out_trap) {
  Table::Ptr table{store, inst->tables()[instr.imm_u32x2.fst]};
  auto&& elem = inst->elems()[instr.imm_u32x2.snd];
  auto size = Pop<u32>();
  auto src = Pop<u32>();
  auto dst = Pop<u32>();
  TRAP_IF(Failed(table->Init(store, dst, elem, src, size)),
          "table.init out of bounds");
  return RunResult::Ok;
}

RunResult Thread::DoElemDrop(Instance::Ptr& inst, Instr instr) {
  inst->elems()[instr.imm_u32].Drop();
  return RunResult::Ok;
}

RunResult Thread::DoTableCopy(Store& store,
                              Instance::Ptr& inst,
                              Instr instr,
                              Trap::Ptr* out_trap) {
  Table::Ptr table_dst{store, inst->tables()[instr.imm_u32x2.fst]};
  Table::Ptr table_src{store, inst->tables()[instr.imm_u32x2.snd]};
  auto size = Pop<u32>();
  auto src = Pop<u32>();
  auto dst = Pop<u32>();
  TRAP_IF(Failed(Table::Copy(store, *table_dst, dst, *table_src, src, size)),
          "table.copy out of bounds");
  return RunResult::Ok;
}

RunResult Thread::DoTableGet(Store& store,
                             Instance::Ptr& inst,
                             Instr instr,
                             Trap::Ptr* out_trap) {
  Table::Ptr table{store, inst->tables()[instr.imm_u32]};
  auto index = Pop<u32>();
  Ref ref;
  TRAP_IF(
      Failed(table->Get(index, &ref)),
      StringPrintf("table.get at %u >= max value %u", index, table->size()));
  Push(ref);
  return RunResult::Ok;
}

RunResult Thread::DoTableSet(Store& store,
                             Instance::Ptr& inst,
                             Instr instr,
                             Trap::Ptr* out_trap) {
  Table::Ptr table{store, inst->tables()[instr.imm_u32]};
  auto ref = Pop<Ref>();
  auto index = Pop<u32>();
  TRAP_IF(
      Failed(table->Set(store, index, ref)),
      StringPrintf("table.set at %u >= max value %u", index, table->size()));
  return RunResult::Ok;
}

RunResult Thread::DoTableGrow(Store& store,
                              Instance::Ptr& inst,
                              Instr instr,
                              Trap::Ptr* out_trap) {
  Table::Ptr table{store, inst->tables()[instr.imm_u32]};
  u32 old_size = table->size();
  auto delta = Pop<u32>();
  auto ref = Pop<Ref>();
  if (Failed(table->Grow(store, delta, ref))) {
    Push<s32>(-1);
  } else {
    Push<u32>(old_size);
  }
  return RunResult::Ok;
}

RunResult Thread::DoTableSize(Store& store, Instance::Ptr& inst, Instr instr) {
  Table::Ptr table{store, inst->tables()[instr.imm_u32]};
  Push<u32>(table->size());
  return RunResult::Ok;
}

RunResult Thread::DoTableFill(Store& store,
                              Instance::Ptr& inst,
                              Instr instr,
                              Trap::Ptr* out_trap) {
  Table::Ptr table{store, inst->tables()[instr.imm_u32]};
  auto size = Pop<u32>();
  auto value = Pop<Ref>();
  auto dst = Pop<u32>();
  TRAP_IF(Failed(table->Fill(store, dst, value, size)),
          "table.fill out of bounds");
  return RunResult::Ok;
}

}  // namespace interp2
}  // namespace wabt
