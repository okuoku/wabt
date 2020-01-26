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

#include <cassert>
#include <string>

namespace wabt {
namespace interp2 {

//// Ref ////
inline Ref::Ref(size_t index) : index(index) {}

//// FuncType ////
// static
inline bool FuncType::classof(const ExternType* type) {
  return type->kind == skind;
}

//// TableType ////
// static
inline bool TableType::classof(const ExternType* type) {
  return type->kind == skind;
}

//// MemoryType ////
// static
inline bool MemoryType::classof(const ExternType* type) {
  return type->kind == skind;
}

//// GlobalType ////
// static
inline bool GlobalType::classof(const ExternType* type) {
  return type->kind == skind;
}

//// EventType ////
// static
inline bool EventType::classof(const ExternType* type) {
  return type->kind == skind;
}

//// ImportType ////
inline ImportType::ImportType(const ImportType& other)
    : module(other.module), name(other.name), type(other.type->Clone()) {}

inline ImportType& ImportType::operator=(const ImportType& other) {
  if (this != &other) {
    module = other.module;
    name = other.name;
    type = other.type->Clone();
  }
  return *this;
}

//// ExportType ////
inline ExportType::ExportType(const ExportType& other)
    : name(other.name), type(other.type->Clone()) {}

inline ExportType& ExportType::operator=(const ExportType& other) {
  if (this != &other) {
    name = other.name;
    type = other.type->Clone();
  }
  return *this;
}

//// Frame ////
inline Frame::Frame(Ref func, u32 offset) : func(func), offset(offset) {}

//// FreeList ////
template <typename T>
bool FreeList<T>::IsValid(Index index) const {
  return index < list.size();
}

template <typename T>
template <typename... Args>
typename FreeList<T>::Index FreeList<T>::New(Args&&... args) {
  if (!free.empty()) {
    Index index = free.back();
    free.pop_back();
    list[index] = T(std::forward<Args>(args)...);
    return index;
  }
  list.emplace_back(std::forward<Args>(args)...);
  return list.size() - 1;
}

template <typename T>
void FreeList<T>::Delete(Index index) {
  assert(IsValid(index));
  list[index].~T();
  free.push_back(index);
}

template <typename T>
const T& FreeList<T>::Get(Index index) const {
  assert(IsValid(index));
  return list[index];
}

template <typename T>
T& FreeList<T>::Get(Index index) {
  assert(IsValid(index));
  return list[index];
}

//// RefPtr ////
template <typename T>
RefPtr<T>::RefPtr() : obj_(nullptr), store_(nullptr), root_index_(0) {}

template <typename T>
RefPtr<T>::RefPtr(Store& store, Ref ref) {
  assert(store.Is<T>(ref));
  root_index_ = store.NewRoot(ref);
  obj_ = static_cast<T*>(store.objects.Get(ref.index).get());
  store_ = &store;
}

template <typename T>
RefPtr<T>::RefPtr(const RefPtr& other)
    : obj_(other.obj_), store_(other.store_) {
  root_index_ = store_->CopyRoot(other.root_index_);
}

template <typename T>
RefPtr<T>& RefPtr<T>::operator=(const RefPtr& other) {
  obj_ = other.obj_;
  store_ = other.store_;
  root_index_ = store_->CopyRoot(other.root_index_);
}

template <typename T>
RefPtr<T>::RefPtr(RefPtr&& other)
    : obj_(other.obj_), root_index_(other.root_index_) {
  other.obj_ = nullptr;
  other.root_index_ = 0;
}

template <typename T>
RefPtr<T>& RefPtr<T>::operator=(RefPtr&& other) {
  obj_ = other.obj_;
  root_index_ = other.root_index_;
  other.obj_ = nullptr;
  other.root_index_ = 0;
  return *this;
}

template <typename T>
RefPtr<T>::~RefPtr() {
  if (obj_) {
    store_->DeleteRoot(root_index_);
  }
}

template <typename T>
T* RefPtr<T>::get() {
  return obj_;
}

template <typename T>
T* RefPtr<T>::operator->() {
  return obj_;
}

template <typename T>
T& RefPtr<T>::operator*() {
  return *obj_;
}

template <typename T>
RefPtr<T>::operator bool() {
  return obj_ != nullptr;
}

template <typename T>
Ref RefPtr<T>::ref() const {
  return store_ ? store_->roots.Get(root_index_) : Ref::Null;
}

//// ValueType ////
inline bool IsReference(ValueType type) { return type >= ValueType::Anyref; }
template <> inline bool HasType<s32>(ValueType type) { return type == ValueType::I32; }
template <> inline bool HasType<u32>(ValueType type) { return type == ValueType::I32; }
template <> inline bool HasType<s64>(ValueType type) { return type == ValueType::I64; }
template <> inline bool HasType<u64>(ValueType type) { return type == ValueType::I64; }
template <> inline bool HasType<f32>(ValueType type) { return type == ValueType::F32; }
template <> inline bool HasType<f64>(ValueType type) { return type == ValueType::F64; }
template <> inline bool HasType<Ref>(ValueType type) { return IsReference(type); }

template <typename T> void RequireType(ValueType type) {
  assert(HasType<T>(type));
}

inline bool TypesMatch(ValueType expected, ValueType actual) {
  if (expected == actual) {
    return true;
  }
  if (!IsReference(expected)) {
    return false;
  }
  if (expected == ValueType::Anyref || actual == ValueType::Nullref) {
    return true;
  }
  return false;
}

//// Value ////
template <> inline s32 Value::Get<s32>() const { return i32; }
template <> inline u32 Value::Get<u32>() const { return i32; }
template <> inline s64 Value::Get<s64>() const { return i64; }
template <> inline u64 Value::Get<u64>() const { return i64; }
template <> inline f32 Value::Get<f32>() const { return f32; }
template <> inline f64 Value::Get<f64>() const { return f64; }
template <> inline v128 Value::Get<v128>() const { return v128; }
template <> inline Ref Value::Get<Ref>() const { return ref; }

template <> inline void Value::Set<s32>(s32 val) { i32 = val; }
template <> inline void Value::Set<u32>(u32 val) { i32 = val; }
template <> inline void Value::Set<s64>(s64 val) { i64 = val; }
template <> inline void Value::Set<u64>(u64 val) { i64 = val; }
template <> inline void Value::Set<interp2::f32>(interp2::f32 val) { f32 = val; }
template <> inline void Value::Set<interp2::f64>(interp2::f64 val) { f64 = val; }
template <> inline void Value::Set<interp2::v128>(interp2::v128 val) { v128 = val; }
template <> inline void Value::Set<Ref>(Ref val) { ref = val; }

//// Store ////
inline bool Store::IsValid(Ref ref) const {
  return objects.IsValid(ref.index) && objects.Get(ref.index);
}

template <typename T>
bool Store::Is(Ref ref) const {
  return objects.IsValid(ref.index) && isa<T>(objects.Get(ref.index).get());
}

template <typename T>
Result Store::Get(Ref ref, RefPtr<T>* out) {
  if (Is<T>(ref)) {
    *out = RefPtr<T>(*this, ref);
    return Result::Ok;
  }
  return Result::Error;
}

template <typename T, typename... Args>
RefPtr<T> Store::Alloc(Args&&... args) {
  Ref ref{objects.New(new T(std::forward<Args>(args)...))};
  RefPtr<T> ptr{*this, ref};
  ptr->self_ = ref;
  return ptr;
}

//// TypedValue ////
inline TypedValue::TypedValue(ValueType type, Value value)
    : type(type), value(value) {
  // Use the constructor with Store instead when creating a reference.
  assert(!IsReference(type));
}

inline TypedValue::TypedValue(Store& store, ValueType type, Value value)
    : type(type), value(value) {
  if (IsReference(type)) {
    ref = RefPtr<Object>(store, value.ref);
  }
}

//// Object ////
// static
inline bool Object::classof(const Object* obj) {
  return true;
}

inline Object::Object(ObjectKind kind) : kind_(kind) {}

inline ObjectKind Object::kind() const {
  return kind_;
}

//// Foreign ////
// static
inline bool Foreign::classof(const Object* obj) {
  return obj->kind() == skind;
}

// static
inline RefPtr<Foreign> Foreign::New(Store& store, void* ptr) {
  return store.Alloc<Foreign>(store, ptr);
}

inline void* Foreign::ptr() {
  return ptr_;
}

//// Trap ////
// static
inline bool Trap::classof(const Object* obj) {
  return obj->kind() == skind;
}

// static
inline RefPtr<Trap> Trap::New(Store& store,
                              const std::string& msg,
                              const std::vector<Frame>& trace) {
  return store.Alloc<Trap>(store, msg, trace);
}

//// Extern ////
// static
inline bool Extern::classof(const Object* obj) {
  switch (obj->kind()) {
    case ObjectKind::DefinedFunc:
    case ObjectKind::HostFunc:
    case ObjectKind::Table:
    case ObjectKind::Memory:
    case ObjectKind::Global:
    case ObjectKind::Event:
      return true;
    default:
      return false;
  }
}

inline Extern::Extern(ObjectKind kind) : Object(kind) {}

//// Func ////
// static
inline bool Func::classof(const Object* obj) {
  switch (obj->kind()) {
    case ObjectKind::DefinedFunc:
    case ObjectKind::HostFunc:
      return true;
    default:
      return false;
  }
}

inline const FuncType& Func::func_type() {
  return type_;
}

//// DefinedFunc ////
// static
inline bool DefinedFunc::classof(const Object* obj) {
  return obj->kind() == skind;
}

// static
inline RefPtr<DefinedFunc> DefinedFunc::New(Store& store,
                                            Ref instance,
                                            FuncDesc desc) {
  return store.Alloc<DefinedFunc>(store, instance, desc);
}

//// HostFunc ////
// static
inline bool HostFunc::classof(const Object* obj) {
  return obj->kind() == skind;
}

// static
inline RefPtr<HostFunc> HostFunc::New(Store& store,
                                      FuncType type,
                                      Callback cb,
                                      void* user_data) {
  return store.Alloc<HostFunc>(store, type, cb, user_data);
}

//// Table ////
// static
inline bool Table::classof(const Object* obj) {
  return obj->kind() == skind;
}

// static
inline RefPtr<Table> Table::New(Store& store, TableDesc desc) {
  return store.Alloc<Table>(store, desc);
}

//// Memory ////
// static
inline bool Memory::classof(const Object* obj) {
  return obj->kind() == skind;
}

// static
inline RefPtr<Memory> Memory::New(interp2::Store& store, MemoryDesc desc) {
  return store.Alloc<Memory>(store, desc);
}

inline bool Memory::IsValidAccess(u32 offset, u32 addend, size_t size) const {
  size_t data_size = data_.size();
  return size <= data_size && addend <= data_size - size &&
         offset <= data_size - size - addend;
}

template <typename T>
Result Memory::Load(u32 offset, u32 addend, T* out) const {
  if (IsValidAccess(offset, addend, sizeof(T))) {
    memcpy(out, data_.data() + offset + addend, sizeof(T));
    return Result::Ok;
  }
  return Result::Error;
}

template <typename T>
T Memory::UnsafeLoad(u32 offset, u32 addend) const {
  assert(IsValidAccess(offset, addend, sizeof(T)));
  T val;
  memcpy(&val, data_.data() + offset + addend, sizeof(T));
  return val;
}

template <typename T>
Result Memory::Store(u32 offset, u32 addend, T val) {
  if (IsValidAccess(offset, addend, sizeof(T))) {
    memcpy(data_.data() + offset + addend, &val, sizeof(T));
    return Result::Ok;
  }
  return Result::Error;
}

//// Global ////
// static
inline bool Global::classof(const Object* obj) {
  return obj->kind() == skind;
}

// static
inline RefPtr<Global> Global::New(Store& store, GlobalDesc desc, Value value) {
  return store.Alloc<Global>(store, desc, value);
}

inline Value Global::Get() const {
  return value_;
}

template <typename T>
Result Global::Get(T* out) const {
  if (HasType<T>(desc_.type.type)) {
    *out = value_.Get<T>();
    return Result::Ok;
  }
  return Result::Error;
}

template <typename T>
T Global::UnsafeGet() const {
  RequireType<T>(desc_.type.type);
  return value_.Get<T>();
}

template <typename T>
Result Global::Set(T val) {
  if (desc_.type.mut == Mutability::Var && HasType<T>(desc_.type.type)) {
    value_.Set(val);
    return Result::Ok;
  }
  return Result::Error;
}

//// Event ////
// static
inline bool Event::classof(const Object* obj) {
  return obj->kind() == skind;
}

// static
inline RefPtr<Event> Event::New(Store& store, EventDesc desc) {
  return store.Alloc<Event>(store, desc);
}

//// ElemSegment ////
inline void ElemSegment::Drop() {
  elements_.clear();
}

inline const ElemDesc& ElemSegment::desc() const {
  return *desc_;
}

inline const RefVec& ElemSegment::elements() const {
  return elements_;
}

inline u32 ElemSegment::size() const {
  return elements_.size();
}

//// DataSegment ////
inline void DataSegment::Drop() {
  size_ = 0;
}

inline const DataDesc& DataSegment::desc() const {
  return *desc_;
}

inline u32 DataSegment::size() const {
  return size_;
}

//// Module ////
// static
inline bool Module::classof(const Object* obj) {
  return obj->kind() == skind;
}

// static
inline RefPtr<Module> Module::New(Store& store, ModuleDesc desc) {
  return store.Alloc<Module>(store, std::move(desc));
}

inline const ModuleDesc& Module::desc() const {
  return desc_;
}

inline const std::vector<ImportType>& Module::import_types() const {
  return import_types_;
}

inline const std::vector<ExportType>& Module::export_types() const {
  return export_types_;
}

//// Instance ////
// static
inline bool Instance::classof(const Object* obj) {
  return obj->kind() == skind;
}

inline Ref Instance::module() const {
  return module_;
}

inline const RefVec& Instance::imports() const {
  return imports_;
}

inline const RefVec& Instance::funcs() const {
  return funcs_;
}

inline const RefVec& Instance::tables() const {
  return tables_;
}

inline const RefVec& Instance::memories() const {
  return memories_;
}

inline const RefVec& Instance::globals() const {
  return globals_;
}

inline const RefVec& Instance::events() const {
  return events_;
}

inline const RefVec& Instance::exports() const {
  return exports_;
}

//// Thread ////
// static
inline bool Thread::classof(const Object* obj) {
  return obj->kind() == skind;
}

// static
inline RefPtr<Thread> Thread::New(Store& store, const Options& options) {
  return store.Alloc<Thread>(store, options);
}

}  // namespace interp2
}  // namespace wabt
