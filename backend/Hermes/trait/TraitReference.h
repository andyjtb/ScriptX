/*
 * Tencent is pleased to support the open source community by making ScriptX available.
 * Copyright (C) 2021 THL A29 Limited, a Tencent company.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <string>
#include "../../src/types.h"
#include "../../src/utils/GlobalWeakBookkeeping.hpp"

#include "../HermesHelper.h"

namespace script::internal {

struct BackingData : public facebook::jsi::MutableBuffer {
  BackingData(size_t s) {
    data_ = std::shared_ptr<uint8_t>(new uint8_t[s], std::default_delete<uint8_t[]>());
    size_ = s;
  }
  BackingData(void* data, size_t s) : BackingData(s) { std::memcpy(data_.get(), data, s); }

  BackingData(std::shared_ptr<void> data, size_t s) {
    data_ = data;
    size_ = s;
  }

  size_t size() const override { return size_; }
  uint8_t* data() override { return reinterpret_cast<uint8_t*>(data_.get()); }

  std::shared_ptr<void> data_;
  size_t size_;
};

class ValueHolder {
 public:
  ValueHolder() {}
  ValueHolder(const facebook::jsi::Value& value);
  ValueHolder(facebook::jsi::Value&& value)
      : valuePtr(std::make_shared<facebook::jsi::Value>(std::move(value))) {}

  ValueHolder(const ValueHolder& other) : valuePtr(other.valuePtr) {}
  ValueHolder(ValueHolder&& other) : valuePtr(std::move(other.valuePtr)) {}

  ValueHolder& operator=(const ValueHolder& assign) {
    valuePtr = assign.valuePtr;
    return *this;
  }

  ValueHolder& operator=(ValueHolder&& move) noexcept {
    valuePtr = std::move(move.valuePtr);
    return *this;
  }

  std::shared_ptr<facebook::jsi::Value> valuePtr;
};

class GlobalValueHolder : public ValueHolder {
 public:
  GlobalValueHolder() {}
  GlobalValueHolder(const facebook::jsi::Value& value);
  GlobalValueHolder(facebook::jsi::Value&& value) : ValueHolder(value) {}

  GlobalValueHolder(const GlobalValueHolder& other) : ValueHolder(other) {}
  GlobalValueHolder(GlobalValueHolder&& other) : ValueHolder(std::move(other)) {}

  GlobalValueHolder& operator=(const GlobalValueHolder& assign) {
    valuePtr = assign.valuePtr;
    engine_ = assign.engine_;
    return *this;
  }

  GlobalValueHolder& operator=(GlobalValueHolder&& move) noexcept {
    valuePtr = std::move(move.valuePtr);
    engine_ = move.engine_;
    move.engine_ = nullptr;
    return *this;
  }

  hermes_backend::HermesEngine* engine_ = nullptr;
  internal::GlobalWeakBookkeeping::HandleType handle_{};
};

class WeakValueHolder {
 public:
  WeakValueHolder() {}
  WeakValueHolder(const WeakValueHolder& other)
      : valuePtr(other.valuePtr), engine_(other.engine_) {}
  WeakValueHolder(const std::shared_ptr<facebook::jsi::Value>& val) : valuePtr(val) {}

  WeakValueHolder& operator=(const WeakValueHolder& assign) {
    valuePtr = assign.valuePtr;
    engine_ = assign.engine_;
    return *this;
  }

  WeakValueHolder& operator=(WeakValueHolder&& move) noexcept {
    valuePtr = std::move(move.valuePtr);
    engine_ = move.engine_;
    move.engine_ = nullptr;
    return *this;
  }

  std::weak_ptr<facebook::jsi::Value> valuePtr;
  hermes_backend::HermesEngine* engine_ = nullptr;
  internal::GlobalWeakBookkeeping::HandleType handle_{};
};

class ByteBufferState : public ValueHolder {
 public:
  ByteBufferState() {}
  ByteBufferState(const facebook::jsi::Value& value)
      : ValueHolder(std::forward<const facebook::jsi::Value&>(value)) {}
  ByteBufferState(facebook::jsi::Value&& value)
      : ValueHolder(std::forward<facebook::jsi::Value&&>(value)) {}

  ByteBufferState(const ValueHolder& other) : ValueHolder(other) {}
  ByteBufferState(const ByteBufferState& other) : ValueHolder(other) {}

  std::shared_ptr<BackingData> backingData_;
};

template <typename T>
struct ImplType<Local<T>> {
  using type = ValueHolder;
};

template <typename T>
struct ImplType<Global<T>> {
  using type = GlobalValueHolder;
};

template <typename T>
struct ImplType<Weak<T>> {
  using type = WeakValueHolder;
};

template <>
struct ImplType<Local<ByteBuffer>> {
  using type = ByteBufferState;
};

}  // namespace script::internal