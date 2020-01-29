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

#ifndef WABT_INTERP2_ISTREAM_H_
#define WABT_INTERP2_ISTREAM_H_

#include <cstdint>
#include <vector>

#include "src/common.h"
#include "src/opcode.h"

namespace wabt {
namespace interp2 {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using v128 = ::v128;

using Buffer = std::vector<u8>;

struct Istream {
  using Offset = u32;
  static const Offset kInvalidOffset = ~0;

  void Emit(u32);
  void Emit(Opcode);
  void Emit(Opcode, u8);
  void Emit(Opcode, u32);
  void Emit(Opcode, u64);
  void Emit(Opcode, v128);
  void Emit(Opcode, u32, u32);
  void EmitDropKeep(u32 drop, u32 keep);

  Offset EmitFixupU32();
  void ResolveFixupU32(Offset);

  Offset offset() const;

private:
  template <typename T>
  void EmitAt(Offset, T val);
  template <typename T>
  void EmitInternal(T val);

  Buffer data_;
  Offset offset_ = 0;
};

}  // namespace interp2
}  // namespace wabt

#endif  // WABT_INTERP2_ISTREAM_H_
