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

#include "gtest/gtest.h"

#include "src/binary-reader.h"
#include "src/error-formatter.h"

#include "src/interp2/interp2-math.h"
#include "src/interp2/interp2.h"
#include "src/interp2/istream.h"
#include "src/interp2/read-module.h"

using namespace wabt;
using namespace wabt::interp2;

TEST(ReadModule, Empty) {
  std::vector<u8> data = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};

  Errors errors;
  ReadBinaryOptions options;
  ModuleDesc module;
  Result result =
      ReadModule(data.data(), data.size(), options, &errors, &module);
  ASSERT_EQ(Result::Ok, result)
      << FormatErrorsToString(errors, Location::Type::Binary);
}

TEST(ReadModule, MVP) {
  // (module
  //   (type (;0;) (func (param i32) (result i32)))
  //   (type (;1;) (func (param f32) (result f32)))
  //   (type (;2;) (func))
  //   (import "foo" "bar" (func (;0;) (type 0)))
  //   (func (;1;) (type 1) (param f32) (result f32)
  //     (f32.const 0x1.5p+5 (;=42;)))
  //   (func (;2;) (type 2))
  //   (table (;0;) 1 2 funcref)
  //   (memory (;0;) 1)
  //   (global (;0;) i32 (i32.const 1))
  //   (export "quux" (func 1))
  //   (start 2)
  //   (elem (;0;) (i32.const 0) 0 1)
  //   (data (;0;) (i32.const 2) "hello"))
  std::vector<u8> data = {
      0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0e, 0x03, 0x60,
      0x01, 0x7f, 0x01, 0x7f, 0x60, 0x01, 0x7d, 0x01, 0x7d, 0x60, 0x00, 0x00,
      0x02, 0x0b, 0x01, 0x03, 0x66, 0x6f, 0x6f, 0x03, 0x62, 0x61, 0x72, 0x00,
      0x00, 0x03, 0x03, 0x02, 0x01, 0x02, 0x04, 0x05, 0x01, 0x70, 0x01, 0x01,
      0x02, 0x05, 0x03, 0x01, 0x00, 0x01, 0x06, 0x06, 0x01, 0x7f, 0x00, 0x41,
      0x01, 0x0b, 0x07, 0x08, 0x01, 0x04, 0x71, 0x75, 0x75, 0x78, 0x00, 0x01,
      0x08, 0x01, 0x02, 0x09, 0x08, 0x01, 0x00, 0x41, 0x00, 0x0b, 0x02, 0x00,
      0x01, 0x0a, 0x0c, 0x02, 0x07, 0x00, 0x43, 0x00, 0x00, 0x28, 0x42, 0x0b,
      0x02, 0x00, 0x0b, 0x0b, 0x0b, 0x01, 0x00, 0x41, 0x02, 0x0b, 0x05, 0x68,
      0x65, 0x6c, 0x6c, 0x6f,
  };

  Errors errors;
  ReadBinaryOptions options;
  ModuleDesc module;
  Result result =
      ReadModule(data.data(), data.size(), options, &errors, &module);
  ASSERT_EQ(Result::Ok, result)
      << FormatErrorsToString(errors, Location::Type::Binary);

  EXPECT_EQ(3u, module.func_types.size());
  EXPECT_EQ(1u, module.imports.size());
  EXPECT_EQ(2u, module.funcs.size());
  EXPECT_EQ(1u, module.tables.size());
  EXPECT_EQ(1u, module.memories.size());
  EXPECT_EQ(1u, module.globals.size());
  EXPECT_EQ(0u, module.events.size());
  EXPECT_EQ(1u, module.exports.size());
  EXPECT_EQ(1u, module.starts.size());
  EXPECT_EQ(1u, module.elems.size());
  EXPECT_EQ(1u, module.datas.size());
}

namespace {

// (func (export "fac") (param $n i32) (result i32)
//   (local $result i32)
//   (local.set $result (i32.const 1))
//   (loop (result i32)
//     (local.set $result
//       (i32.mul
//         (br_if 1 (local.get $result) (i32.eqz (local.get $n)))
//         (local.get $n)))
//     (local.set $n (i32.sub (local.get $n) (i32.const 1)))
//     (br 0)))
const std::vector<u8> s_fac_module = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x06, 0x01,
    0x60, 0x01, 0x7f, 0x01, 0x7f, 0x03, 0x02, 0x01, 0x00, 0x07, 0x07,
    0x01, 0x03, 0x66, 0x61, 0x63, 0x00, 0x00, 0x0a, 0x22, 0x01, 0x20,
    0x01, 0x01, 0x7f, 0x41, 0x01, 0x21, 0x01, 0x03, 0x7f, 0x20, 0x01,
    0x20, 0x00, 0x45, 0x0d, 0x01, 0x20, 0x00, 0x6c, 0x21, 0x01, 0x20,
    0x00, 0x41, 0x01, 0x6b, 0x21, 0x00, 0x0c, 0x00, 0x0b, 0x0b,
};

void ExpectBufferStrEq(OutputBuffer& buf, const char* str) {
  char buf_str[buf.size() + 1];
  memcpy(buf_str, buf.data.data(), sizeof(buf_str));
  buf_str[buf.size()] = 0;
  EXPECT_STREQ(buf_str, str);
}

}  // namespace

TEST(ReadModule, Disassemble) {
  Errors errors;
  ReadBinaryOptions options;
  ModuleDesc module;
  Result result = ReadModule(s_fac_module.data(), s_fac_module.size(), options,
                             &errors, &module);
  ASSERT_EQ(Result::Ok, result)
      << FormatErrorsToString(errors, Location::Type::Binary);

  MemoryStream stream;
  module.istream.Disassemble(&stream);
  auto buf = stream.ReleaseOutputBuffer();

  ExpectBufferStrEq(*buf,
R"(   0| alloca 1
   6| i32.const 1
  12| local.set $1, %[-1]
  18| local.get $1
  24| local.get $3
  30| i32.eqz %[-1]
  32| br_unless @44, %[-1]
  38| br @84
  44| local.get $3
  50| i32.mul %[-2], %[-1]
  52| local.set $1, %[-1]
  58| local.get $2
  64| i32.const 1
  70| i32.sub %[-2], %[-1]
  72| local.set $2, %[-1]
  78| br @18
  84| drop_keep $2 $1
  94| return
)");
}

TEST(Interp, Fac) {
  Errors errors;
  ReadBinaryOptions options;
  ModuleDesc module;
  Result result = ReadModule(s_fac_module.data(), s_fac_module.size(), options,
                             &errors, &module);
  ASSERT_EQ(Result::Ok, result)
      << FormatErrorsToString(errors, Location::Type::Binary);

  Store store;
  auto mod = Module::New(store, module);
  RefPtr<Trap> trap;
  auto inst = Instance::Instantiate(store, mod.ref(), {}, &trap);
  ASSERT_TRUE(inst) << trap->message();
  ASSERT_EQ(1u, inst->exports().size());

  RefPtr<DefinedFunc> func{store, inst->exports()[0]};
  TypedValues results;
  result = func->Call(store, {TypedValue(ValueType::I32, Value(s32(5)))},
                      &results, &trap);

  ASSERT_EQ(Result::Ok, result);
  EXPECT_EQ(1u, results.size());
  EXPECT_EQ(ValueType::I32, results[0].type);
  EXPECT_EQ(120u, results[0].value.Get<u32>());
}

TEST(Interp, Fac_Trace) {
  Errors errors;
  ReadBinaryOptions options;
  ModuleDesc module;
  Result result = ReadModule(s_fac_module.data(), s_fac_module.size(), options,
                             &errors, &module);
  ASSERT_EQ(Result::Ok, result)
      << FormatErrorsToString(errors, Location::Type::Binary);

  Store store;
  auto mod = Module::New(store, module);
  RefPtr<Trap> trap;
  auto inst = Instance::Instantiate(store, mod.ref(), {}, &trap);
  ASSERT_TRUE(inst) << trap->message();

  ASSERT_EQ(1u, inst->exports().size());
  RefPtr<DefinedFunc> func{store, inst->exports()[0]};

  MemoryStream stream;
  TypedValues results;
  result = func->Call(store, {TypedValue(ValueType::I32, Value(s32(2)))},
                      &results, &trap, &stream);
  ASSERT_EQ(Result::Ok, result);

  auto buf = stream.ReleaseOutputBuffer();
  ExpectBufferStrEq(*buf,
R"(   0| alloca 1
   6| i32.const 1
  12| local.set $1, 1
  18| local.get $1
  24| local.get $3
  30| i32.eqz 2
  32| br_unless @44, 0
  44| local.get $3
  50| i32.mul 1, 2
  52| local.set $1, 2
  58| local.get $2
  64| i32.const 1
  70| i32.sub 2, 1
  72| local.set $2, 1
  78| br @18
  18| local.get $1
  24| local.get $3
  30| i32.eqz 1
  32| br_unless @44, 0
  44| local.get $3
  50| i32.mul 2, 1
  52| local.set $1, 2
  58| local.get $2
  64| i32.const 1
  70| i32.sub 1, 1
  72| local.set $2, 0
  78| br @18
  18| local.get $1
  24| local.get $3
  30| i32.eqz 0
  32| br_unless @44, 1
  38| br @84
  84| drop_keep $2 $1
  94| return
)");
}
