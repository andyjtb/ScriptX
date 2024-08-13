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

#include <cassert>
#include <optional>
#include <string>
#include "../../src/Native.h"
#include "../../src/Reference.h"
#include "../../src/Scope.h"
#include "HermesEngine.h"
#include "HermesHelper.h"
#include "HermesRuntime.h"

namespace script::hermes_backend {

template <typename T>
struct MakeLocalInternal {
  static Local<T> make(facebook::jsi::Value&& value) {
    return HermesEngine::make<Local<T>>(std::move(value));
  }
};

inline HermesEngine* currentEngine() {
  return &EngineScope::currentEngineCheckedAs<HermesEngine>();
}

inline HermesRuntime* currentRuntime() { return currentEngine()->runtime_; }

}  // namespace script::hermes_backend

namespace script {

struct hermes_interop {
  static hermes_backend::HermesRuntime* getEngineRuntime(hermes_backend::HermesEngine* engine) {
    return engine->runtime_;
  }

  static hermes_backend::HermesRuntime* currentEngineRuntime() {
    return ::script::hermes_backend::currentRuntime();
  }

  template <typename T>
  static Local<T> makeLocal(facebook::jsi::Value&& value) {
    return ::script::hermes_backend::MakeLocalInternal<T>::make(std::move(value));
  }

  static bool isType(const Local<Value>& val, std::function<bool(facebook::jsi::Value&)> isFunc) {
    if (val.val_.valuePtr == nullptr) return false;

    return isFunc(*val.val_.valuePtr);
  }

  static bool isObjectType(const Local<Value>& val,
                           bool (facebook::jsi::Object::*isFunc)(facebook::jsi::Runtime&) const) {
    return isType(val, &facebook::jsi::Value::isObject) &&
           (val.val_.valuePtr->asObject(*currentEngineRuntime()).*isFunc)(*currentEngineRuntime());
  }

  /**
   * @param engine
   * @param thiz not own
   * @param argc
   * @param argv not own
   * @return
   */
  static script::Arguments makeArguments(hermes_backend::HermesEngine* engine,
                                         const facebook::jsi::Value& thisVal,
                                         const facebook::jsi::Value* args, size_t count) {
    return script::Arguments(hermes_backend::ArgumentsData{engine, thisVal, count, args});
  }

  static facebook::jsi::Value* toHermes(const Local<Value>& val) { return val.val_.valuePtr.get(); }
  static std::shared_ptr<facebook::jsi::Value> toShared(const Local<Value>& val) {
    return val.val_.valuePtr;
  }
  static facebook::jsi::Value&& moveHermes(const Local<Value>& val) {
    return std::move(*val.val_.valuePtr.get());
  }

  static std::vector<facebook::jsi::Value> toJsiVector(const Local<Value>* args, size_t size) {
    auto& runtime = *hermes_backend::currentRuntime();
    std::vector<facebook::jsi::Value> arguments;
    arguments.reserve(size);

    for (size_t i = 0; i < size; i++) arguments.emplace_back(runtime, *args[i].val_.valuePtr);
    return arguments;
  }

  using ArgumentsData = hermes_backend::ArgumentsData;
  static ArgumentsData extractArguments(const Arguments& args) { return args.callbackInfo_; }
};

}  // namespace script
