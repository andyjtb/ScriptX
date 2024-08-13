/*
 * Tencent is pleased to support the open source community by making ScriptX available.
 * Copyright (C) 2023 THL A29 Limited, a Tencent company.  All rights reserved.
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

#include "../../src/Engine.h"
#include "../../src/Exception.h"
#include "../../src/Native.h"
#include "../../src/utils/GlobalWeakBookkeeping.hpp"
#include "../../src/utils/MessageQueue.h"

#include "HermesHelper.h"
#include "HermesTypedArrayApi.h"

namespace script::hermes_backend {

struct SharedScriptClassHolder;

class HermesEngine : public ScriptEngine {
 private:
  bool isDestroying_ = false;
  std::atomic_bool tickScheduled_ = false;

 protected:
  struct ClassRegistryData {
    Global<Object> constructor{};
    Global<Object> prototype{};
    script::ScriptClass* (*instanceTypeToScriptClass)(void*) = nullptr;
  };

  std::shared_ptr<utils::MessageQueue> messageQueue_;
  HermesRuntime* runtime_ = nullptr;

  internal::GlobalWeakBookkeeping globalWeakBookkeeping_{};

  std::unordered_map<const void*, ClassRegistryData> classRegistry_;

 public:
  HermesEngine(std::shared_ptr<::script::utils::MessageQueue> queue);

  HermesEngine();

  SCRIPTX_DISALLOW_COPY_AND_MOVE(HermesEngine);

  void destroy() noexcept override;

  bool isDestroying() const override;

  Local<Object> getGlobal();

  Local<Value> get(const Local<String>& key) override;

  void set(const Local<String>& key, const Local<Value>& value) override;
  using ScriptEngine::set;

  Local<Value> eval(const Local<String>& script, const Local<Value>& sourceFile);
  Local<Value> eval(const Local<String>& script, const Local<String>& sourceFile) override;
  Local<Value> eval(const Local<String>& script) override;

  Local<Value> evalInPlace(const std::string& script) override;
  Local<Value> evalInPlace(const std::string& script, const std::string& sourceFile) override;
  Local<Value> evalInPlace(const char* script, size_t size, const std::string& sourceFile) override;

  using ScriptEngine::eval;

  std::shared_ptr<utils::MessageQueue> messageQueue() override;

  size_t getHeapSize() override;
  void gc() override;

  void adjustAssociatedMemory(int64_t count) override;

  ScriptLanguage getLanguageType() override;

  std::string getEngineVersion() override;

  facebook::jsi::Runtime& getRt();
  facebook::hermes::HermesRuntime* getHermesRuntime();

 protected:
  ~HermesEngine() override;

  void performRegisterNativeClass(
      internal::TypeIndex typeIndex, const internal::ClassDefineState* classDefine,
      script::ScriptClass* (*instanceTypeToScriptClass)(void*)) override;

  Local<Object> performNewNativeClass(internal::TypeIndex typeIndex,
                                      const internal::ClassDefineState* classDefine, size_t size,
                                      const Local<script::Value>* args) override;

  void* performGetNativeInstance(const Local<script::Value>& value,
                                 const internal::ClassDefineState* classDefine) override;

  bool performIsInstanceOf(const Local<script::Value>& value,
                           const internal::ClassDefineState* classDefine) override;

 private:
  template <typename T, typename... Args>
  static T make(Args&&... args) {
    return T(std::forward<Args>(args)...);
  }

  template <typename T>
  friend class ::script::Local;

  template <typename T>
  friend class ::script::Global;

  template <typename T>
  friend class ::script::Weak;

  friend class ::script::Object;

  friend class ::script::Array;

  friend class ::script::Function;

  friend class ::script::ByteBuffer;

  friend class ::script::ScriptEngine;

  friend class ::script::Exception;

  friend class ::script::Arguments;

  friend class ::script::ScriptClass;

  friend class HermesEngineScope;

  friend struct ::script::hermes_interop;
  template <typename T>
  friend struct MakeLocalInternal;

  friend HermesRuntime* currentRuntime();

  friend SharedScriptClassHolder;

  struct BookKeepFetcher;
  friend struct HermesBookKeepFetcher;

  Local<Function> newStaticFunction(const internal::StaticDefine::FunctionDefine& func);

  Local<Value> newStaticGetter(const internal::StaticDefine::PropertyDefine& prop);
  Local<Value> newStaticSetter(const internal::StaticDefine::PropertyDefine& prop);

  Local<Value> newInstanceGetter(const internal::InstanceDefine::PropertyDefine& prop);
  Local<Value> newInstanceSetter(const internal::InstanceDefine::PropertyDefine& prop);

  void registerStaticDefine(const internal::StaticDefine& staticDefine,
                            const Local<Object>& object);

  void defineInstance(const internal::ClassDefineState* classDefine, Local<Value>& object,
                      ClassRegistryData& registry);

  Local<Object> createConstructor(const internal::ClassDefineState* classDefine);

  Local<Object> defineInstancePrototype(const internal::ClassDefineState* classDefine);

  void defineInstanceFunction(const internal::ClassDefineState* classDefine,
                              Local<Object>& prototypeObject);

  void defineInstanceProperties(const internal::ClassDefineState* classDefine,
                                const Local<Object>& prototype);

  static void* getThisPointer(jsi::Runtime& rt, const jsi::Value& thisVal);

  void scheduleTick();

  void deleteScriptClass(ScriptClass* sc);

  Local<Value> evalInPlaceInternal(const std::shared_ptr<facebook::jsi::Buffer>& buffer, const std::string& sourceFile);

  std::unique_ptr<hermes_backend::InvalidateCacheOnDestroy> invalidatePropNameCache;
};

}  // namespace script::hermes_backend