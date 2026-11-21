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

#include <ScriptX/ScriptX.h>

#include "HermesHelper.hpp"

namespace script {

Arguments::Arguments(InternalCallbackInfoType callbackInfo) : callbackInfo_(callbackInfo) {}

Arguments::~Arguments() = default;

Local<Object> Arguments::thiz() const {
  return hermes_interop::makeLocal<Object>(
      callbackInfo_.thiz_.asObject(*hermes_interop::getEngineRuntime(callbackInfo_.engine_)));
}

bool Arguments::hasThiz() const { return callbackInfo_.thiz_.isObject(); }

size_t Arguments::size() const { return callbackInfo_.argc_; }

Local<Value> Arguments::operator[](size_t i) const {
  if (i >= size()) {
    return {};
  }
  return hermes_interop::makeLocal<Value>(facebook::jsi::Value(
      *hermes_interop::getEngineRuntime(callbackInfo_.engine_), callbackInfo_.argv_[i]));
}

ScriptEngine* Arguments::engine() const { return callbackInfo_.engine_; }

namespace hermes_backend {

SharedScriptClassHolder::~SharedScriptClassHolder() {
  auto engine = script::internal::scriptDynamicCast<HermesEngine*>(sc->getScriptEngine());
  engine->deleteScriptClass(sc);
}

}  // namespace hermes_backend

void ScriptClass::performConstructFromCpp(void* derivedPtr, internal::TypeIndex typeIndex,
                                          const internal::ClassDefineState* classDefine) {
  auto engine = hermes_backend::currentEngine();
  auto& runtime = *hermes_interop::getEngineRuntime(engine);

  auto jsiObj = facebook::jsi::Object(runtime);
  // Store both the ScriptClass* (this) and the derived T* (derivedPtr).
  // derivedPtr is needed for polymorphicPointer when T has multiple inheritance
  // with a polymorphic base class, where T* != ScriptClass*.
  jsiObj.setNativeState(
      runtime, std::make_shared<hermes_backend::NonOwningSharedScriptClassHolder>(this, derivedPtr));

  auto thiz = hermes_interop::makeLocal<Value>(std::move(jsiObj));
  auto obj = engine->performNewNativeClass(typeIndex, classDefine, 1, &thiz);

  // Script owns the instance through its native state, and the constructor recorded it weakly, as for
  // a `new` from script; the strong reference only lasts until the caller has handed it to script.
  engine->extendLifeTimeToNextLoop(hermes_interop::toShared(obj));
}

hermes_backend::HermesScriptClassState::HermesScriptClassState(HermesEngine* scriptEngine,
                                                               const Local<Value>& obj)
    : scriptEngine_(scriptEngine) {
  auto& runtime = *hermes_interop::getEngineRuntime(scriptEngine);
  scriptOwnedRef_ = std::make_unique<facebook::jsi::WeakObject>(
      runtime, hermes_interop::toShared(obj)->asObject(runtime));
}

ScriptClass::ScriptClass(const script::Local<script::Object>& scriptObject)
    : internalState_(hermes_backend::currentEngine(), scriptObject) {}

namespace {

facebook::jsi::Value lockScriptObject(const hermes_backend::HermesScriptClassState& state) {
  if (auto& weak = state.scriptOwnedRef_) {
    return weak->lock(*hermes_interop::getEngineRuntime(state.scriptEngine_));
  }
  return {};
}

}  // namespace

Local<Object> ScriptClass::getScriptObject() const {
  if (internalState_.scriptEngine_->isReleasingRuntime()) {
    throw EngineDestroyedError("getScriptObject while the script engine is destroyed");
  }

  auto object = lockScriptObject(internalState_);
  if (object.isObject()) return hermes_interop::makeLocal<Object>(std::move(object));

  throw Exception("can't getScriptObject once it has been collected");
}

Local<Array> ScriptClass::getInternalStore() const {
  auto* engine = internalState_.scriptEngine_;

  if (engine->isReleasingRuntime()) {
    throw InternalStoreUnavailable("getInternalStore while the script engine is destroyed");
  }

  auto& runtime = *hermes_interop::getEngineRuntime(engine);
  auto object = lockScriptObject(internalState_);

  if (!object.isObject()) {
    throw InternalStoreUnavailable("getInternalStore once the script object is collected");
  }

  if (auto& cached = internalState_.internalStoreRef_) {
    auto store = cached->lock(runtime);
    if (store.isObject()) return hermes_interop::makeLocal<Array>(std::move(store));
  }

  auto& stores = *engine->internalStores_;

  try {
    auto store = stores.get.callWithThis(runtime, stores.map, object);

    if (!store.isObject()) {
      store = facebook::jsi::Array(runtime, 0);
      stores.set.callWithThis(runtime, stores.map, object, store);
    }

    internalState_.internalStoreRef_ =
        std::make_unique<facebook::jsi::WeakObject>(runtime, store.getObject(runtime));
    return hermes_interop::makeLocal<Array>(std::move(store));
  } catch (const facebook::jsi::JSError& e) {
    throw Exception(hermes_interop::makeLocal<Value>(facebook::jsi::Value(runtime, e.value())));
  } catch (const facebook::jsi::JSIException& e) {
    throw Exception(std::string(e.what()));
  }
}

ScriptEngine* ScriptClass::getScriptEngine() const { return internalState_.scriptEngine_; }

bool ScriptClass::isScriptObjectNull() const {
  return internalState_.scriptEngine_->isReleasingRuntime() ||
         !lockScriptObject(internalState_).isObject();
}

ScriptClass::~ScriptClass() = default;
}  // namespace script