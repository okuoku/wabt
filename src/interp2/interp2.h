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

#ifndef WABT_INTERP2_H_
#define WABT_INTERP2_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "src/cast.h"
#include "src/common.h"
#include "src/opcode.h"
#include "src/result.h"
#include "src/string-view.h"

#include "src/interp2/istream.h"

namespace wabt {
namespace interp2 {

class Store;
class Object;
class Trap;
class DataSegment;
class ElemSegment;
class Instance;
template <typename T> class RefPtr;

using s8 = int8_t;
using u8 = uint8_t;
using s16 = int16_t;
using u16 = uint16_t;
using s32 = int32_t;
using u32 = uint32_t;
using Index = uint32_t;
using s64 = int64_t;
using u64 = uint64_t;
using f32 = float;
using f64 = double;
using v128 = ::v128;

using Buffer = std::vector<u8>;

using ValueType = wabt::Type;
using ValueTypes = std::vector<ValueType>;

template <typename T> bool HasType(ValueType);
template <typename T> void RequireType(ValueType);
bool IsReference(ValueType);
bool TypesMatch(ValueType expected, ValueType actual);

using ExternKind = ExternalKind;
enum class Mutability { Const, Var };
enum class EventAttr { Exception };
using SegmentMode = SegmentKind;
enum class ElemKind { RefNull, RefFunc };

const char* GetName(Mutability);
const char* GetName(ValueType);
const char* GetName(ExternKind);

enum class ObjectKind {
  Null,
  Foreign,
  Trap,
  DefinedFunc,
  HostFunc,
  Table,
  Memory,
  Global,
  Event,
  Module,
  Instance,
  Thread,
};

enum class InitExprKind {
  I32,
  I64,
  F32,
  F64,
  V128,
  GlobalGet,
  RefNull,
  RefFunc
};

struct InitExpr {
  InitExprKind kind;
  union {
    u32 i32;
    u64 i64;
    f32 f32;
    f64 f64;
    v128 v128;
    Index index;
  };
};

struct Ref {
  static const Ref Null;

  Ref() = default;
  explicit Ref(size_t index);

  friend bool operator==(Ref, Ref);
  friend bool operator!=(Ref, Ref);

  size_t index;
};
using RefVec = std::vector<Ref>;

//// Types ////

using Limits = wabt::Limits;
Result CanGrow(const Limits&, u32 old_size, u32 delta, u32* new_size);
Result Match(const Limits& expected,
             const Limits& actual,
             std::string* out_msg);

struct ExternType {
  explicit ExternType(ExternKind);
  virtual ~ExternType() {}
  virtual std::unique_ptr<ExternType> Clone() = 0;

  ExternKind kind;
};

struct FuncType : ExternType {
  static const ExternKind skind = ExternKind::Func;
  static bool classof(const ExternType* type);

  explicit FuncType(ValueTypes params, ValueTypes results);

  std::unique_ptr<ExternType> Clone() override;

  friend Result Match(const FuncType& expected,
                      const FuncType& actual,
                      std::string* out_msg);

  ValueTypes params;
  ValueTypes results;
};

struct TableType : ExternType {
  static const ExternKind skind = ExternKind::Table;
  static bool classof(const ExternType* type);

  explicit TableType(ValueType, Limits);

  std::unique_ptr<ExternType> Clone() override;

  friend Result Match(const TableType& expected,
                      const TableType& actual,
                      std::string* out_msg);

  ValueType element;
  Limits limits;
};

struct MemoryType : ExternType {
  static const ExternKind skind = ExternKind::Memory;
  static bool classof(const ExternType* type);

  explicit MemoryType(Limits);

  std::unique_ptr<ExternType> Clone() override;

  friend Result Match(const MemoryType& expected,
                      const MemoryType& actual,
                      std::string* out_msg);

  Limits limits;
};

struct GlobalType : ExternType {
  static const ExternKind skind = ExternKind::Global;
  static bool classof(const ExternType* type);

  explicit GlobalType(ValueType, Mutability);

  std::unique_ptr<ExternType> Clone() override;

  friend Result Match(const GlobalType& expected,
                      const GlobalType& actual,
                      std::string* out_msg);

  ValueType type;
  Mutability mut;
};

struct EventType : ExternType {
  static const ExternKind skind = ExternKind::Event;
  static bool classof(const ExternType* type);

  explicit EventType(EventAttr, const ValueTypes&);

  std::unique_ptr<ExternType> Clone() override;

  friend Result Match(const EventType& expected,
                      const EventType& actual,
                      std::string* out_msg);

  EventAttr attr;
  ValueTypes signature;
};

struct ImportType {
  explicit ImportType(std::string module,
                      std::string name,
                      std::unique_ptr<ExternType>);
  ImportType(const ImportType&);
  ImportType& operator=(const ImportType&);

  std::string module;
  std::string name;
  std::unique_ptr<ExternType> type;
};

struct ExportType {
  explicit ExportType(std::string name, std::unique_ptr<ExternType>);
  ExportType(const ExportType&);
  ExportType& operator=(const ExportType&);

  std::string name;
  std::unique_ptr<ExternType> type;
};

//// Structure ////

struct ImportDesc {
  ImportType type;
};

struct FuncDesc {
  FuncType type;
  u32 code_offset;
};

struct TableDesc {
  TableType type;
};

struct MemoryDesc {
  MemoryType type;
};

struct GlobalDesc {
  GlobalType type;
  InitExpr init;
};

struct EventDesc {
  EventType type;
};

struct ExportDesc {
  ExportType type;
  Index index;
};

struct StartDesc {
  Index func_index;
};

struct DataDesc {
  Buffer data;
  SegmentMode mode;
  Index memory_index;
  InitExpr offset;
};

struct ElemExpr {
  ElemKind kind;
  Index index;
};

struct ElemDesc {
  std::vector<ElemExpr> elements;
  ValueType type;
  SegmentMode mode;
  Index table_index;
  InitExpr offset;
};

struct ModuleDesc {
  std::vector<FuncType> func_types;
  std::vector<ImportDesc> imports;
  std::vector<FuncDesc> funcs;
  std::vector<TableDesc> tables;
  std::vector<MemoryDesc> memories;
  std::vector<GlobalDesc> globals;
  std::vector<EventDesc> events;
  std::vector<ExportDesc> exports;
  std::vector<StartDesc> starts;
  std::vector<ElemDesc> elems;
  std::vector<DataDesc> datas;
  Istream istream;
};

//// Runtime ////

struct Frame {
  explicit Frame(Ref func, u32 offset);

  void Mark(Store&);

  Ref func;
  u32 offset;
};

template <typename T>
struct FreeList {
  using Index = size_t;

  bool IsValid(Index) const;

  template <typename... Args>
  Index New(Args&&...);
  void Delete(Index);

  const T& Get(Index) const;
  T& Get(Index);

  std::vector<T> list;
  std::vector<size_t> free;
};

class Store {
 public:
  using ObjectList = FreeList<std::unique_ptr<Object>>;
  using RootList = FreeList<Ref>;

  bool IsValid(Ref) const;
  bool HasValueType(Ref, ValueType) const;
  template <typename T>
  bool Is(Ref) const;

  template <typename T, typename... Args>
  RefPtr<T> Alloc(Args&&...);
  template <typename T>
  Result Get(Ref, RefPtr<T>* out);
  template <typename T>
  RefPtr<T> UnsafeGet(Ref);

  RootList::Index NewRoot(Ref);
  RootList::Index CopyRoot(RootList::Index);
  void DeleteRoot(RootList::Index);

  void Collect();
  void Mark(Ref);
  void Mark(const RefVec&);

 private:
  template <typename T>
  friend class RefPtr;

  ObjectList objects_;
  RootList roots_;
  std::vector<bool> marks_;
};

template <typename T>
class RefPtr {
 public:
  RefPtr();
  RefPtr(Store&, Ref);
  RefPtr(const RefPtr&);
  RefPtr& operator=(const RefPtr&);
  RefPtr(RefPtr&&);
  RefPtr& operator=(RefPtr&&);
  ~RefPtr();

  T* get();
  T* operator->();
  T& operator*();
  explicit operator bool() const;

  const T* get() const;
  const T* operator->() const;
  const T& operator*() const;

  Ref ref() const;

 private:
  T* obj_;
  Store* store_;
  Store::RootList::Index root_index_;
};

union Value {
  Value() = default;
  explicit Value(s32);
  explicit Value(u32);
  explicit Value(s64);
  explicit Value(u64);
  explicit Value(f32);
  explicit Value(f64);
  explicit Value(v128);
  explicit Value(Ref);

  template <typename T>
  T Get() const;
  template <typename T>
  void Set(T);

  u32 i32_;
  u64 i64_;
  f32 f32_;
  f64 f64_;
  v128 v128_;
  Ref ref_;
};

struct TypedValue {
  explicit TypedValue(ValueType, Value);
  explicit TypedValue(Store&, ValueType, Value);

  ValueType type;
  Value value;
  RefPtr<Object> ref;
};
using TypedValues = std::vector<TypedValue>;

using Finalizer = void (*)(void* user_data);

class Object {
 public:
  static bool classof(const Object* obj);

  Object(const Object&) = delete;
  Object& operator=(const Object&) = delete;

  virtual ~Object();

  ObjectKind kind() const;
  Ref self() const;

 protected:
  friend Store;
  explicit Object(ObjectKind);
  virtual void Mark(Store&) = 0;

  ObjectKind kind_;
  Finalizer finalizer_ = nullptr;
  void* user_data_ = nullptr;
  Ref self_ = Ref::Null;
};

class Foreign : public Object {
 public:
  static const ObjectKind skind = ObjectKind::Foreign;
  static bool classof(const Object* obj);

  static RefPtr<Foreign> New(Store&, void*);

  void* ptr();

 private:
  friend Store;
  explicit Foreign(Store&, void*);
  void Mark(Store&) override;

  void* ptr_;
};

class Trap : public Object {
 public:
  static const ObjectKind skind = ObjectKind::Trap;
  static bool classof(const Object* obj);

  static RefPtr<Trap> New(
      Store&,
      const std::string& msg,
      const std::vector<Frame>& trace = std::vector<Frame>());

  std::string message() const;

 private:
  friend Store;
  explicit Trap(Store&,
                const std::string& msg,
                const std::vector<Frame>& trace = std::vector<Frame>());
  void Mark(Store&) override;

  std::string message_;
  std::vector<Frame> trace_;
};

class Extern : public Object {
 public:
  static bool classof(const Object* obj);

  virtual Result Match(Store&, const ImportType&, RefPtr<Trap>* out_trap) = 0;

 protected:
  friend Store;
  explicit Extern(ObjectKind);

  template <typename T>
  Result MatchImpl(Store&,
                   const ImportType&,
                   const T& actual,
                   RefPtr<Trap>* out_trap);
};

class Func : public Extern {
 public:
  static bool classof(const Object* obj);

  virtual Result Call(Store&,
                      const TypedValues& params,
                      TypedValues* out_results,
                      RefPtr<Trap>* out_trap) = 0;

  const FuncType& func_type();

 protected:
  explicit Func(ObjectKind, FuncType);

  FuncType type_;
};

class DefinedFunc : public Func {
 public:
  static bool classof(const Object* obj);
  static const ObjectKind skind = ObjectKind::DefinedFunc;

  static RefPtr<DefinedFunc> New(Store&, Ref instance, FuncDesc);

  Result Match(Store&, const ImportType&, RefPtr<Trap>* out_trap) override;

  Result Call(Store&,
              const TypedValues& params,
              TypedValues* out_results,
              RefPtr<Trap>* out_trap) override;

  Ref instance() const;
  const FuncDesc& desc() const;

 private:
  friend Store;
  explicit DefinedFunc(Store&, Ref instance, FuncDesc);
  void Mark(Store&) override;

  Ref instance_;
  FuncDesc desc_;
};

class HostFunc : public Func {
 public:
  static bool classof(const Object* obj);
  static const ObjectKind skind = ObjectKind::HostFunc;

  using Callback = Result (*)(const TypedValues& params,
                              TypedValues* out_results,
                              std::string* out_msg,
                              void* user_data);

  static RefPtr<HostFunc> New(Store&, FuncType, Callback, void* user_data);

  Result Match(Store&, const ImportType&, RefPtr<Trap>* out_trap) override;

  Result Call(Store&,
              const TypedValues& params,
              TypedValues* out_results,
              RefPtr<Trap>* out_trap) override;
 private:
  friend Store;
  explicit HostFunc(Store&, FuncType, Callback, void* user_data);
  void Mark(Store&) override;

  Callback callback_;
  void* user_data_;
};

class Table : public Extern {
 public:
  static bool classof(const Object* obj);
  static const ObjectKind skind = ObjectKind::Table;

  static RefPtr<Table> New(Store&, TableDesc);

  Result Match(Store&, const ImportType&, RefPtr<Trap>* out_trap) override;

  bool IsValidRange(u32 offset, u32 size) const;

  Result Get(u32 offset, Ref* out) const;
  Result Set(Store&, u32 offset, Ref);
  Result Grow(Store&, u32 count, Ref);
  Result Fill(Store&, u32 offset, Ref, u32 size);
  Result Init(Store&,
              u32 dst_offset,
              const ElemSegment&,
              u32 src_offset,
              u32 size);
  static Result Copy(Store&,
                     Table& dst,
                     u32 dst_offset,
                     const Table& src,
                     u32 src_offset,
                     u32 size);

  // Unsafe API.
  Ref UnsafeGet(u32 offset) const;

  const TableDesc& desc() const;
  const RefVec& elements() const;

 private:
  friend Store;
  explicit Table(Store&, TableDesc);
  void Mark(Store&) override;

  TableDesc desc_;
  RefVec elements_;
};

class Memory : public Extern {
 public:
  static bool classof(const Object* obj);
  static const ObjectKind skind = ObjectKind::Memory;

  static RefPtr<Memory> New(Store&, MemoryDesc);

  Result Match(Store&, const ImportType&, RefPtr<Trap>* out_trap) override;

  bool IsValidAccess(u32 offset, u32 addend, size_t size) const;

  template <typename T>
  Result Load(u32 offset, u32 addend, T* out) const;
  template <typename T>
  Result Store(u32 offset, u32 addend, T);
  Result Grow(u32 pages);
  Result Fill(u32 offset, u8 value, u32 size);
  Result Init(u32 dst_offset, const DataSegment&, u32 src_offset, u32 size);
  static Result Copy(Memory& dst,
                     u32 dst_offset,
                     const Memory& src,
                     u32 src_offset,
                     u32 size);

  u32 ByteSize() const;
  u32 PageSize() const;

  // Unsafe API.
  template <typename T>
  T UnsafeLoad(u32 offset, u32 addend) const;

 private:
  friend class Store;
  explicit Memory(class Store&, MemoryDesc);
  void Mark(class Store&) override;

  MemoryDesc desc_;
  Buffer data_;
  u32 pages_;
};

class Global : public Extern {
 public:
  static bool classof(const Object* obj);
  static const ObjectKind skind = ObjectKind::Global;

  static RefPtr<Global> New(Store&, GlobalDesc, Value);

  Result Match(Store&, const ImportType&, RefPtr<Trap>* out_trap) override;

  Value Get() const;
  template <typename T>
  Result Get(T* out) const;
  template <typename T>
  Result Set(T);
  Result Set(Store&, Ref);

  template <typename T>
  T UnsafeGet() const;
  void UnsafeSet(Value);

 private:
  friend Store;
  explicit Global(Store&, GlobalDesc, Value);
  void Mark(Store&) override;

  GlobalDesc desc_;
  Value value_;
};

class Event : public Extern {
 public:
  static bool classof(const Object* obj);
  static const ObjectKind skind = ObjectKind::Event;

  static RefPtr<Event> New(Store&, EventDesc);

  Result Match(Store&, const ImportType&, RefPtr<Trap>* out_trap) override;

 private:
  friend Store;
  explicit Event(Store&, EventDesc);
  void Mark(Store&) override;

  EventDesc desc_;
};

class ElemSegment {
 public:
  explicit ElemSegment(const ElemDesc*, RefPtr<Instance>&);

  bool IsValidRange(u32 offset, u32 size) const;
  void Drop();

  const ElemDesc& desc() const;
  const RefVec& elements() const;
  u32 size() const;

 private:
  friend Instance;
  void Mark(Store&);

  const ElemDesc* desc_;  // Borrowed from the Module.
  RefVec elements_;
};

class DataSegment {
 public:
  explicit DataSegment(const DataDesc*);

  bool IsValidRange(u32 offset, u32 size) const;
  void Drop();

  const DataDesc& desc() const;
  u32 size() const;

 private:
  const DataDesc* desc_;  // Borrowed from the Module.
  u32 size_;
};

class Module : public Object {
 public:
  static bool classof(const Object* obj);
  static const ObjectKind skind = ObjectKind::Module;

  static RefPtr<Module> New(Store&, ModuleDesc);

  const ModuleDesc& desc() const;
  const std::vector<ImportType>& import_types() const;
  const std::vector<ExportType>& export_types() const;

 private:
  friend Store;
  friend Instance;
  explicit Module(Store&, ModuleDesc);
  void Mark(Store&) override;

  ModuleDesc desc_;
  std::vector<ImportType> import_types_;
  std::vector<ExportType> export_types_;
};

class Instance : public Object {
 public:
  static bool classof(const Object* obj);
  static const ObjectKind skind = ObjectKind::Instance;

  static RefPtr<Instance> Instantiate(Store&,
                                      Ref module,
                                      const RefVec& imports,
                                      RefPtr<Trap>* out_trap);

  Ref module() const;
  const RefVec& imports() const;
  const RefVec& funcs() const;
  const RefVec& tables() const;
  const RefVec& memories() const;
  const RefVec& globals() const;
  const RefVec& events() const;
  const RefVec& exports() const;

 private:
  friend Store;
  friend ElemSegment;
  friend DataSegment;
  explicit Instance(Store&, Ref module);
  void Mark(Store&) override;

  Value ResolveInitExpr(Store&, InitExpr);

  Ref module_;
  RefVec imports_;
  RefVec funcs_;
  RefVec tables_;
  RefVec memories_;
  RefVec globals_;
  RefVec events_;
  RefVec exports_;
  std::vector<ElemSegment> elems_;
  std::vector<DataSegment> datas_;
};

enum class RunResult {
  Ok,
  Return,
  Trap,
};

// TODO: kinda weird to have a thread as an object, but it makes reference
// marking simpler.
class Thread : public Object {
 public:
  static bool classof(const Object* obj);
  static const ObjectKind skind = ObjectKind::Thread;

  struct Options {
    static const u32 kDefaultValueStackSize = 64 * 1024 / sizeof(Value);
    static const u32 kDefaultCallStackSize = 64 * 1024 / sizeof(Frame);

    u32 value_stack_size = kDefaultValueStackSize;
    u32 call_stack_size = kDefaultCallStackSize;
  };

  static RefPtr<Thread> New(Store&, const Options&);

  RunResult Run(Store&, RefPtr<Trap>* out_trap);
  RunResult Run(Store&, int num_instructions, RefPtr<Trap>* out_trap);
  RunResult Step(Store&, RefPtr<Trap>* out_trap);

 private:
  friend Store;
  friend DefinedFunc;

  explicit Thread(Store&, const Options&);
  void Mark(Store&) override;

  void PushCall(Ref func, u32 offset);
  RunResult PopCall();
  RunResult DoCall(const RefPtr<Func>&, RefPtr<Trap>* out_trap);
  RunResult DoReturnCall(const RefPtr<Func>&, RefPtr<Trap>* out_trap);

  void PushValues(const TypedValues&);
  void CopyValues(Store&, const ValueTypes&, TypedValues*);

  Value& Pick(Index);

  template <typename T>
  T Pop();
  Value Pop();

  template <typename T>
  void Push(T);
  void Push(Value);

  template <typename R, typename T>
  using UnopFunc = R(T);
  template <typename R, typename T>
  using UnopTrapFunc = RunResult(T, R*, std::string*);
  template <typename R, typename T>
  using BinopFunc = R(T, T);
  template <typename R, typename T>
  using BinopTrapFunc = RunResult(T, T, R*, std::string*);

  template <typename R, typename T>
  RunResult DoUnop(UnopFunc<R, T>);
  template <typename R, typename T>
  RunResult DoUnop(Store&, UnopTrapFunc<R, T>, RefPtr<Trap>* out_trap);
  template <typename R, typename T>
  RunResult DoBinop(BinopFunc<R, T>);
  template <typename R, typename T>
  RunResult DoBinop(Store&, BinopTrapFunc<R, T>, RefPtr<Trap>* out_trap);

  template <typename R, typename T>
  RunResult DoConvert(Store&, RefPtr<Trap>* out_trap);
  template <typename R, typename T>
  RunResult DoReinterpret();

  template <typename T, typename V = T>
  RunResult DoLoad(Store&, RefPtr<Instance>&, Instr, RefPtr<Trap>* out_trap);
  template <typename T, typename V = T>
  RunResult DoStore(Store&, RefPtr<Instance>&, Instr, RefPtr<Trap>* out_trap);

  RunResult StepInternal(Store&,
                         RefPtr<Instance>&,
                         RefPtr<Module>&,
                         RefPtr<Trap>* out_trap);

  std::vector<Frame> frames_;
  std::vector<Value> values_;
  std::vector<bool> refs_;
};

}  // namespace interp2
}  // namespace wabt

#include "src/interp2/interp2-inl.h"

#endif  // WABT_INTERP2_H_
