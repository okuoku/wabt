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
  EXPECT_EQ(Result::Ok, result);
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
  EXPECT_EQ(Result::Ok, result)
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
