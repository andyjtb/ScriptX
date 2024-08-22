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

#include "../../src/Exception.h"
#include "../../src/Reference.h"
#include "../../src/Scope.h"
#include "../../src/Value.h"

#include "HermesHelper.hpp"
#include "HermesRuntime.h"
#include "HermesTypedArrayApi.h"

namespace script {

Local<Object> Object::newObject() {
  return hermes_interop::makeLocal<Object>(jsi::Object(*hermes_backend::currentRuntime()));
}

Local<Object> Object::newObjectImpl(const Local<Value>& type, size_t size,
                                    const Local<Value>* args) {
  auto& runtime = *hermes_interop::currentEngineRuntime();
  auto arguments = hermes_interop::toJsiVector(args, size);
  auto constructor = hermes_interop::toHermes(type)->asObject(runtime).asFunction(runtime);

  return hermes_interop::makeLocal<Object>(
      runtime.callAsConstructor(constructor, arguments.data(), arguments.size()));
}

Local<String> String::newString(const char* utf8) {
  return hermes_interop::makeLocal<String>(
      jsi::String::createFromUtf8(*hermes_backend::currentRuntime(), utf8));
}

Local<String> String::newString(std::string_view utf8) {
  return hermes_interop::makeLocal<String>(
      jsi::String::createFromUtf8(*hermes_backend::currentRuntime(), reinterpret_cast<const uint8_t*> (utf8.data()), utf8.size()));
}

Local<String> String::newString(const std::string& utf8) {
  return hermes_interop::makeLocal<String>(
      jsi::String::createFromUtf8(*hermes_backend::currentRuntime(), utf8));
}

#if defined(__cpp_char8_t)

Local<String> String::newString(const char8_t* utf8) { return newString(std::u8string_view(utf8)); }

Local<String> String::newString(std::u8string_view utf8) {
  return hermes_interop::makeLocal<String>(
      jsi::String::createFromUtf8(*hermes_backend::currentRuntime(),
                                  reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size()));
}

Local<String> String::newString(const std::u8string& utf8) {
  return newString(std::u8string_view(utf8));
}

#endif

Local<Number> Number::newNumber(float value) { return newNumber(static_cast<double>(value)); }

Local<Number> Number::newNumber(double value) {
  return hermes_interop::makeLocal<Number>(jsi::Value(*hermes_backend::currentRuntime(), value));
}

Local<Number> Number::newNumber(int32_t value) { return newNumber(static_cast<double>(value)); }

Local<Number> Number::newNumber(int64_t value) { return newNumber(static_cast<double>(value)); }

Local<Boolean> Boolean::newBoolean(bool value) {
  return hermes_interop::makeLocal<Boolean>(jsi::Value(*hermes_backend::currentRuntime(), value));
}

Local<Null> Null::newNull() {
  return hermes_interop::makeLocal<Null>(jsi::Value(nullptr));
}

namespace {
struct PrivateData {
  PrivateData(script::FunctionCallback&& cb) : callback_(std::move(cb)) {}
  script::FunctionCallback callback_;
};
}  // namespace

Local<Function> Function::newFunction(script::FunctionCallback callback) {
  auto& runtime = *hermes_backend::currentRuntime();
  const auto funcName = jsi::PropNameID::forAscii(runtime, "CppFunction");
  const auto sharedCallback = std::make_shared<PrivateData>(std::move(callback));

  const jsi::HostFunctionType cb = [engine = hermes_backend::currentEngine(), sharedCallback](
                                       jsi::Runtime& rt, const jsi::Value& thisVal,
                                       const jsi::Value* args,
                                       size_t count) -> facebook::jsi::Value {
    if (&rt != hermes_interop::getEngineRuntime(engine)) {
      throw Exception(std::string("Invalid Runtime"));
    }

    auto scriptArgs = hermes_interop::makeArguments(engine, thisVal, args, count);
    const auto res = sharedCallback->callback_(scriptArgs);

    return hermes_interop::moveHermes(res);
  };

  return hermes_interop::makeLocal<Function>(
      jsi::Function::createFromHostFunction(*hermes_backend::currentRuntime(), funcName, 1, cb));
}

Local<Array> Array::newArray(size_t size) {
  return hermes_interop::makeLocal<Array>(
      facebook::jsi::Array(*hermes_backend::currentRuntime(), size));
}

Local<Array> Array::newArrayImpl(size_t size, const Local<Value>* args) {
  auto arr = newArray(size);
  for (size_t i = 0; i < size; i++) arr.set(i, args[i]);
  return arr;
}

Local<ByteBuffer> ByteBuffer::newByteBuffer(size_t size) {
  auto backingData = std::make_shared<internal::BackingData>(size);
  auto res = hermes_interop::makeLocal<ByteBuffer>(
      facebook::jsi::ArrayBuffer(*hermes_backend::currentRuntime(), backingData));

  res.val_.backingData_ = backingData;
  return res;
}

Local<script::ByteBuffer> ByteBuffer::newByteBuffer(void* nativeBuffer, size_t size) {
  auto backingData = std::make_shared<internal::BackingData>(nativeBuffer, size);
  auto res = hermes_interop::makeLocal<ByteBuffer>(
      facebook::jsi::ArrayBuffer(*hermes_backend::currentRuntime(), backingData));

  res.val_.backingData_ = backingData;
  return res;
}

Local<ByteBuffer> ByteBuffer::newByteBuffer(std::shared_ptr<void> nativeBuffer, size_t size) {
  auto backingData = std::make_shared<internal::BackingData>(nativeBuffer, size);
  auto res = hermes_interop::makeLocal<ByteBuffer>(
      facebook::jsi::ArrayBuffer(*hermes_backend::currentRuntime(), backingData));

  res.val_.backingData_ = backingData;
  return res;
}

}  // namespace script