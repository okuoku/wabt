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

#include "src/interp2/istream.h"

namespace wabt {
namespace interp2 {

template <typename T>
void Istream::EmitAt(Offset offset, T val) {
  u32 new_size = offset + sizeof(T);
  if (new_size > data_.size()) {
    data_.resize(new_size);
  }
  memcpy(data_.data() + offset, &val, sizeof(val));
}

template <typename T>
void Istream::EmitInternal(T val) {
  EmitAt(offset_, val);
  offset_ += sizeof(T);
}

void Istream::Emit(u32 val) {
  EmitInternal(val);
}

void Istream::Emit(Opcode op) {
  EmitInternal(static_cast<u16>(op));
}

void Istream::Emit(Opcode op, u8 val) {
  Emit(op);
  EmitInternal(val);
}

void Istream::Emit(Opcode op, u32 val) {
  Emit(op);
  EmitInternal(val);
}

void Istream::Emit(Opcode op, u64 val) {
  Emit(op);
  EmitInternal(val);
}

void Istream::Emit(Opcode op, v128 val) {
  Emit(op);
  EmitInternal(val);
}

void Istream::Emit(Opcode op, u32 val1, u32 val2) {
  Emit(op);
  EmitInternal(val1);
  EmitInternal(val2);
}

void Istream::EmitDropKeep(u32 drop, u32 keep) {
  if (drop > 0) {
    if (drop == 1 && keep == 0) {
      Emit(Opcode::Drop);
    } else {
      Emit(Opcode::InterpDropKeep, drop, keep);
    }
  }
}

Istream::Offset Istream::EmitFixupU32() {
  auto result = offset_;
  EmitInternal(kInvalidOffset);
  return result;
}

void Istream::ResolveFixupU32(Offset fixup_offset) {
  EmitAt(fixup_offset, offset_);
}

Istream::Offset Istream::offset() const {
  return offset_;
}

}  // namespace interp2
}  // namespace wabt
