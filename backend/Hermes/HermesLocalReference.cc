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

#include <ScriptX/ScriptX.h>

#include "HermesEngine.h"
#include "HermesHelper.hpp"
#include "HermesRuntime.h"

namespace script {

void valueConstructorCheck(const facebook::jsi::Value& value) {
#ifndef NDEBUG
  if (value.isNull() || value.isUndefined()) throw Exception("null reference");
#endif
}

namespace internal {

ValueHolder::ValueHolder(const facebook::jsi::Value& value)
    : valuePtr(std::make_shared<facebook::jsi::Value>(*script::hermes_backend::currentRuntime(),
                                                      value)) {}

}  // namespace internal

#define REF_IMPL_BASIC_FUNC(ValueType)                                                      \
  Local<ValueType>::Local(const Local<ValueType>& copy) : val_(copy.val_) {}                \
  Local<ValueType>::Local(Local<ValueType>&& move) noexcept : val_(std::move(move.val_)) {} \
  Local<ValueType>::~Local() {}                                                             \
  Local<ValueType>& Local<ValueType>::operator=(const Local& from) {                        \
    Local(from).swap(*this);                                                                \
    return *this;                                                                           \
  }                                                                                         \
  Local<ValueType>& Local<ValueType>::operator=(Local&& move) noexcept {                    \
    Local(std::move(move)).swap(*this);                                                     \
    return *this;                                                                           \
  }                                                                                         \
  void Local<ValueType>::swap(Local& rhs) noexcept { std::swap(val_, rhs.val_); }

#define REF_IMPL_BASIC_EQUALS(ValueType)                                               \
  bool Local<ValueType>::operator==(const script::Local<script::Value>& other) const { \
    return asValue() == other;                                                         \
  }

#define REF_IMPL_BASIC_NOT_VALUE(ValueType)                                         \
  Local<ValueType>::Local(InternalLocalRef val) : val_(val) {                       \
    valueConstructorCheck(*val_.valuePtr);                                          \
  }                                                                                 \
  Local<String> Local<ValueType>::describe() const { return asValue().describe(); } \
  std::string Local<ValueType>::describeUtf8() const { return asValue().describeUtf8(); }

#define REF_IMPL_TO_VALUE(ValueType) \
  Local<Value> Local<ValueType>::asValue() const { return Local<Value>(val_); }

REF_IMPL_BASIC_FUNC(Value)

REF_IMPL_BASIC_FUNC(Object)
REF_IMPL_BASIC_NOT_VALUE(Object)
REF_IMPL_BASIC_EQUALS(Object)
REF_IMPL_TO_VALUE(Object)

REF_IMPL_BASIC_FUNC(String)
REF_IMPL_BASIC_NOT_VALUE(String)
REF_IMPL_BASIC_EQUALS(String)
REF_IMPL_TO_VALUE(String)

REF_IMPL_BASIC_FUNC(Number)
REF_IMPL_BASIC_NOT_VALUE(Number)
REF_IMPL_BASIC_EQUALS(Number)
REF_IMPL_TO_VALUE(Number)

REF_IMPL_BASIC_FUNC(Boolean)
REF_IMPL_BASIC_NOT_VALUE(Boolean)
REF_IMPL_BASIC_EQUALS(Boolean)
REF_IMPL_TO_VALUE(Boolean)

REF_IMPL_BASIC_FUNC(Function)
REF_IMPL_BASIC_NOT_VALUE(Function)
REF_IMPL_BASIC_EQUALS(Function)
REF_IMPL_TO_VALUE(Function)

REF_IMPL_BASIC_FUNC(Array)
REF_IMPL_BASIC_NOT_VALUE(Array)
REF_IMPL_BASIC_EQUALS(Array)
REF_IMPL_TO_VALUE(Array)

REF_IMPL_BASIC_FUNC(ByteBuffer)
REF_IMPL_BASIC_NOT_VALUE(ByteBuffer)
REF_IMPL_BASIC_EQUALS(ByteBuffer)
REF_IMPL_TO_VALUE(ByteBuffer)

REF_IMPL_BASIC_FUNC(Unsupported)
REF_IMPL_BASIC_NOT_VALUE(Unsupported)
REF_IMPL_BASIC_EQUALS(Unsupported)
REF_IMPL_TO_VALUE(Unsupported)

// ==== value ====

Local<Value>::Local() noexcept : val_(facebook::jsi::Value()) {}

Local<Value>::Local(InternalLocalRef hermesLocal) : val_(hermesLocal) {}

bool Local<Value>::isNull() const {
  if (val_.valuePtr == nullptr) return true;
  return hermes_interop::isType(*this, &facebook::jsi::Value::isNull) ||
         hermes_interop::isType(*this, &facebook::jsi::Value::isUndefined);
}

void Local<Value>::reset() {
  val_.valuePtr = std::make_shared<facebook::jsi::Value>(facebook::jsi::Value::null());
}

ValueKind Local<Value>::getKind() const {
  if (isNull()) {
    return ValueKind::kNull;
  } else if (isString()) {
    return ValueKind::kString;
  } else if (isNumber()) {
    return ValueKind::kNumber;
  } else if (isBoolean()) {
    return ValueKind::kBoolean;
  } else if (isFunction()) {
    return ValueKind::kFunction;
  } else if (isArray()) {
    return ValueKind::kArray;
  } else if (isByteBuffer()) {
    return ValueKind::kByteBuffer;
  } else if (isObject()) {
    return ValueKind::kObject;
  } else {
    return ValueKind::kUnsupported;
  }
}

bool Local<Value>::isString() const {
  return hermes_interop::isType(*this, &facebook::jsi::Value::isString);
}

bool Local<Value>::isNumber() const {
  return hermes_interop::isType(*this, &facebook::jsi::Value::isNumber);
}

bool Local<Value>::isBoolean() const {
  return hermes_interop::isType(*this, &facebook::jsi::Value::isBool);
}

bool Local<Value>::isFunction() const {
  return hermes_interop::isObjectType(*this, &facebook::jsi::Object::isFunction);
}

bool Local<Value>::isArray() const {
  return hermes_interop::isObjectType(*this, &facebook::jsi::Object::isArray);
}

bool Local<Value>::isByteBuffer() const {
  if (!isObject()) return false;
  if (hermes_interop::isObjectType(*this, &facebook::jsi::Object::isArrayBuffer)) return true;

  auto& runtime = *hermes_backend::currentRuntime();
  return hermes_backend::isTypedArray(runtime, val_.valuePtr->asObject(runtime));
}

bool Local<Value>::isObject() const {
  return hermes_interop::isType(*this, &facebook::jsi::Value::isObject);
}

bool Local<Value>::isUnsupported() const { return getKind() == ValueKind::kUnsupported; }

Local<String> Local<Value>::asString() const {
  if (isString()) return Local<String>{val_};
  throw Exception("can't cast value as String");
}

Local<Number> Local<Value>::asNumber() const {
  if (isNumber()) return Local<Number>{val_};
  throw Exception("can't cast value as Number");
}

Local<Boolean> Local<Value>::asBoolean() const {
  if (isBoolean()) return Local<Boolean>{val_};
  throw Exception("can't cast value as Boolean");
}

Local<Function> Local<Value>::asFunction() const {
  if (isFunction()) return Local<Function>{val_};
  throw Exception("can't cast value as Function");
}

Local<Array> Local<Value>::asArray() const {
  if (isArray()) return Local<Array>{val_};
  throw Exception("can't cast value as Array");
}

Local<ByteBuffer> Local<Value>::asByteBuffer() const {
  if (isByteBuffer()) return Local<ByteBuffer>{val_};
  throw Exception("can't cast value as ByteBuffer");
}

Local<Object> Local<Value>::asObject() const {
  if (isObject()) return Local<Object>{val_};
  throw Exception("can't cast value as Object");
}

Local<Unsupported> Local<Value>::asUnsupported() const {
  if (isUnsupported()) return Local<Unsupported>{val_};
  throw Exception("can't cast value as Unsupported");
}

bool Local<Value>::operator==(const script::Local<script::Value>& other) const {
  if (isNull()) return other.isNull();
  return facebook::jsi::Value::strictEquals(*hermes_backend::currentRuntime(), *val_.valuePtr,
                                            *other.val_.valuePtr);
}

Local<String> Local<Value>::describe() const {
  return hermes_interop::makeLocal<String>(
      val_.valuePtr->toString(*hermes_backend::currentRuntime()));
}

Local<Value> Local<Object>::get(const script::Local<script::String>& key) const {
  auto& runtime = *hermes_backend::currentRuntime();
  auto result =
      val_.valuePtr->asObject(runtime).getProperty(runtime, key.val_.valuePtr->asString(runtime));
  return hermes_interop::makeLocal<Value>(std::move(result));
}

void Local<Object>::set(const script::Local<script::String>& key,
                        const script::Local<script::Value>& value) const {
  auto& runtime = *hermes_backend::currentRuntime();
  val_.valuePtr->asObject(runtime).setProperty(runtime, key.val_.valuePtr->asString(runtime),
                                               *value.val_.valuePtr);
}

void Local<Object>::remove(const Local<class script::String>& key) const {
  auto& runtime = *hermes_backend::currentRuntime();
  val_.valuePtr->asObject(runtime).deleteProperty(runtime, key.val_.valuePtr->asString(runtime));
}

bool Local<Object>::has(const Local<class script::String>& key) const {
  auto& runtime = *hermes_backend::currentRuntime();
  return val_.valuePtr->asObject(runtime).hasProperty(runtime,
                                                      key.val_.valuePtr->asString(runtime));
}

bool Local<Object>::instanceOf(const Local<class script::Value>& type) const {
  auto& runtime = *hermes_backend::currentRuntime();
  return val_.valuePtr->asObject(runtime).instanceOf(
      runtime, type.val_.valuePtr->asObject(runtime).asFunction(runtime));
}

std::vector<Local<String>> Local<Object>::getKeys() const {
  auto& runtime = *hermes_backend::currentRuntime();
  auto names = val_.valuePtr->asObject(runtime).getPropertyNames(runtime);
  const auto size = names.length(runtime);

  std::vector<Local<String>> ret;
  ret.reserve(size);

  for (size_t i = 0; i < size; ++i)
    ret.emplace_back(Local<String>(names.getValueAtIndex(runtime, i)));

  return ret;
}

float Local<Number>::toFloat() const { return static_cast<float>(toDouble()); }

double Local<Number>::toDouble() const { return val_.valuePtr->asNumber(); }

int32_t Local<Number>::toInt32() const { return static_cast<int32_t>(toDouble()); }

int64_t Local<Number>::toInt64() const { return static_cast<int64_t>(toDouble()); }

bool Local<Boolean>::value() const { return val_.valuePtr->asBool(); }

Local<Value> Local<Function>::callImpl(const Local<Value>& thiz, size_t size,
                                       const Local<Value>* args) const {
  auto& runtime = *hermes_backend::currentRuntime();
  auto arguments = hermes_interop::toJsiVector(args, size);
  auto function = val_.valuePtr->asObject(runtime).asFunction(runtime);

  auto output = Local<Value>();

  try {
    if (thiz.isObject()) {
      const auto& thizObj = thiz.asObject().val_.valuePtr->asObject(runtime);
      output = hermes_interop::makeLocal<Value>(function.callWithThis(
          runtime, thizObj, static_cast<const facebook::jsi::Value*>(arguments.data()), size));
    } else {
      output = hermes_interop::makeLocal<Value>(
          function.callWithThis(runtime, runtime.global(),
                                static_cast<const facebook::jsi::Value*>(arguments.data()), size));
    }
  } catch (facebook::jsi::JSError& e) {
    // Handle JS exceptions here.
    auto val = facebook::jsi::Value(runtime, e.value());
    throw Exception(hermes_interop::makeLocal<Value>(std::move(val)));
  } catch (facebook::jsi::JSIException& e) {
    // Handle JSI exceptions here.
    std::string msg = e.what();
    throw Exception(msg);
  }
  hermes_backend::currentEngine()->scheduleTick();

  return output;
}

size_t Local<Array>::size() const {
  auto& runtime = *hermes_backend::currentRuntime();
  return val_.valuePtr->asObject(runtime).asArray(runtime).size(runtime);
}

Local<Value> Local<Array>::get(size_t index) const {
  auto& runtime = *hermes_backend::currentRuntime();
  auto result = val_.valuePtr->asObject(runtime).asArray(runtime).getValueAtIndex(runtime, index);
  return hermes_interop::makeLocal<Value>(std::move(result));
}

void Local<Array>::set(size_t index, const script::Local<script::Value>& value) const {
  auto& runtime = *hermes_backend::currentRuntime();
  if ((int)index > (int)size() - 1)
    val_.valuePtr->asObject(runtime).asArray(runtime).setLength(runtime, index + 1);

  val_.valuePtr->asObject(runtime).asArray(runtime).setValueAtIndex(
      runtime, index, std::move(*value.val_.valuePtr));
}

void Local<Array>::add(const script::Local<script::Value>& value) const {
  auto& runtime = *hermes_backend::currentRuntime();
  auto* mutableThis = const_cast<Local<Array>*>(this);
  const auto newIndex = size();
  mutableThis->val_.valuePtr->asObject(runtime).asArray(runtime).setLength(runtime, newIndex + 1);

  set(newIndex, value);
}

void Local<Array>::clear() const {
  auto& runtime = *hermes_backend::currentRuntime();
  auto* mutableThis = const_cast<Local<Array>*>(this);
  mutableThis->val_.valuePtr->asObject(runtime).asArray(runtime).setLength(runtime, 0);
}

ByteBuffer::Type Local<ByteBuffer>::getType() const {
  auto& runtime = *hermes_backend::currentRuntime();
  if (hermes_backend::isTypedArray(runtime, val_.valuePtr->asObject(runtime))) {
    auto buffer = hermes_backend::getTypedArray(runtime, val_.valuePtr->asObject(runtime));

    switch (buffer.getKind(runtime)) {
      case hermes_backend::TypedArrayKind::Int8Array:
        return ByteBuffer::Type::kInt8;
      case hermes_backend::TypedArrayKind::Int16Array:
        return ByteBuffer::Type::kInt16;
      case hermes_backend::TypedArrayKind::Int32Array:
        return ByteBuffer::Type::kInt32;
      case hermes_backend::TypedArrayKind::Uint8Array:
        return ByteBuffer::Type::kUint8;
      case hermes_backend::TypedArrayKind::Uint8ClampedArray:
        return ByteBuffer::Type::kUint8;
      case hermes_backend::TypedArrayKind::Uint16Array:
        return ByteBuffer::Type::kUint16;
      case hermes_backend::TypedArrayKind::Uint32Array:
        return ByteBuffer::Type::kUint32;
      case hermes_backend::TypedArrayKind::Float32Array:
        return ByteBuffer::Type::kFloat32;
      case hermes_backend::TypedArrayKind::Float64Array:
        return ByteBuffer::Type::kFloat64;
      case hermes_backend::TypedArrayKind::DataView:
        return ByteBuffer::Type::kUnspecified;
    }
  }

  return ByteBuffer::Type::kFloat32;
}

bool Local<ByteBuffer>::isShared() const { return getRawBytesShared() != nullptr; }

void Local<ByteBuffer>::commit() const {}

void Local<ByteBuffer>::sync() const {}

size_t Local<ByteBuffer>::byteLength() const {
  auto& runtime = *hermes_backend::currentRuntime();
  auto obj = val_.valuePtr->asObject(runtime);
  if (obj.isArrayBuffer(runtime)) return obj.getArrayBuffer(runtime).length(runtime);

  auto typedArray = hermes_backend::getTypedArray(runtime, obj);
  return typedArray.byteLength(runtime);
}

void* Local<ByteBuffer>::getRawBytes() const {
  auto& runtime = *hermes_backend::currentRuntime();
  auto obj = val_.valuePtr->asObject(runtime);
  if (obj.isArrayBuffer(runtime)) return obj.getArrayBuffer(runtime).data(runtime);

  auto typedArray = hermes_backend::getTypedArray(runtime, obj);
  return static_cast<void*>(typedArray.getBuffer(runtime).data(runtime) +
                            typedArray.byteOffset(runtime));
}

std::shared_ptr<void> Local<ByteBuffer>::getRawBytesShared() const {
  return std::shared_ptr<void>(getRawBytes(), [global = Global<ByteBuffer>(*this)](void* ptr) {});
}

}  // namespace script
