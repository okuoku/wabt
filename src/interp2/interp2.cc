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
    *out_msg = "import signature mismatch";
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
bool Store::HasValueType(Ref ref, ValueType type) const {
  // TODO opt?
  if (!IsValid(ref)) {
    return false;
  }
  if (type == ValueType::Anyref) {
    return true;
  }

  Object* obj = objects.Get(ref.index).get();
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
  return roots.New(ref);
}

Store::RootList::Index Store::CopyRoot(RootList::Index index) {
  return roots.New(roots.Get(index));
}

void Store::DeleteRoot(RootList::Index index) {
  roots.Delete(index);
}

void Store::Collect() {
  std::fill(marks.begin(), marks.end(), false);
  Mark(roots.list);
  for (size_t i = 0; i < marks.size(); ++i) {
    if (!marks[i]) {
      objects.Delete(i);
    }
  }
}

void Store::Mark(Ref ref) {
  marks[ref.index] = true;
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
                         RefPtr<Trap>* out_trap) {
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
                          RefPtr<Trap>* out_trap) {
  return MatchImpl(store, import_type, type_, out_trap);
}

Result DefinedFunc::Call(Store& store,
                         const TypedValues& params,
                         TypedValues* out_results,
                         RefPtr<Trap>* out_trap) {
  RefPtr<Thread> thread = Thread::New(store, Thread::Options());
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
                       RefPtr<Trap>* out_trap) {
  return MatchImpl(store, import_type, type_, out_trap);
}

Result HostFunc::Call(Store& store,
                      const TypedValues& params,
                      TypedValues* out_results,
                      RefPtr<Trap>* out_trap) {
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
                    RefPtr<Trap>* out_trap) {
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
                     RefPtr<Trap>* out_trap) {
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
      RefPtr<Global> global{store, globals_[init.index]};
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
                     RefPtr<Trap>* out_trap) {
  return MatchImpl(store, import_type, desc_.type, out_trap);
}

Result Global::Set(Store& store, Ref ref) {
  if (store.HasValueType(ref, desc_.type.type)) {
    value_.Set(ref);
    return Result::Ok;
  }
  return Result::Error;
}

//// Event ////
Event::Event(Store&, EventDesc desc) : Extern(skind), desc_(desc) {}

void Event::Mark(Store&) {}

Result Event::Match(Store& store,
                    const ImportType& import_type,
                    RefPtr<Trap>* out_trap) {
  return MatchImpl(store, import_type, desc_.type, out_trap);
}

//// ElemSegment ////
ElemSegment::ElemSegment(const ElemDesc* desc, RefPtr<Instance>& inst)
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
RefPtr<Instance> Instance::Instantiate(Store& store,
                                       Ref module,
                                       const RefVec& imports,
                                       RefPtr<Trap>* out_trap) {
  RefPtr<Module> mod{store, module};
  RefPtr<Instance> inst = store.Alloc<Instance>(store, module);

  size_t import_desc_count = mod->desc().imports.size();
  if (imports.size() >= import_desc_count) {
    *out_trap = Trap::New(store, "not enough imports!");
    return {};
  }

  // Imports.
  for (size_t i = 0; i < import_desc_count; ++i) {
    auto&& import_desc = mod->desc().imports[i];
    Ref extern_ref = imports[i];
    RefPtr<Extern> extern_{store, extern_ref};
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
        RefPtr<Table> table{store, inst->tables_[desc.table_index]};
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
        RefPtr<Memory> memory{store, inst->tables_[desc.memory_index]};
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
    RefPtr<Func> func{store, inst->funcs_[start.func_index]};
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

RunResult Thread::Run(Store& store, RefPtr<Trap>* out_trap) {
  const int kDefaultInstructionCount = 1000;
  RunResult result;
  do {
    result = Run(store, kDefaultInstructionCount, out_trap);
  } while (result == RunResult::Ok);
  return result;
}

RunResult Thread::Run(Store&, int num_instructions, RefPtr<Trap>* out_trap) {
  // TODO
  return RunResult::Ok;
}

}  // namespace interp2
}  // namespace wabt
