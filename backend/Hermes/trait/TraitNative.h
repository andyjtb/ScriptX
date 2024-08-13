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

namespace script {

namespace hermes_backend {

struct ArgumentsData {
  HermesEngine* engine_;
  const facebook::jsi::Value& thiz_;
  size_t argc_;
  const facebook::jsi::Value* argv_;
};

struct HermesScriptClassState {
  HermesScriptClassState() = default;
  HermesScriptClassState(HermesEngine* scriptEngine, const Local<Value>& obj);

  HermesEngine* scriptEngine_ = nullptr;
  const void* classDefine = nullptr;
  void* polymorphicPointer = nullptr;
  Local<Value> internalStore_;
  std::shared_ptr<facebook::jsi::Value> weakRef_;
};

struct SharedScriptClassHolder : public facebook::jsi::NativeState {
  SharedScriptClassHolder(ScriptClass* sc) : sc(sc) {}
  ~SharedScriptClassHolder();

  ScriptClass* sc = nullptr;
};

struct NonOwningSharedScriptClassHolder : public facebook::jsi::NativeState {
  NonOwningSharedScriptClassHolder(ScriptClass* sc) : sc(sc) {}

  ScriptClass* sc = nullptr;
};

}  // namespace hermes_backend

template <>
struct internal::ImplType<::script::Arguments> {
  using type = hermes_backend::ArgumentsData;
};

template <>
struct internal::ImplType<::script::ScriptClass> {
  using type = hermes_backend::HermesScriptClassState;
};

}  // namespace script