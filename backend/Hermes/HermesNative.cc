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

void ScriptClass::performConstructFromCpp(internal::TypeIndex typeIndex,
                                          const internal::ClassDefineState* classDefine) {
  auto engine = hermes_backend::currentEngine();
  auto& runtime = *hermes_interop::getEngineRuntime(engine);

  auto jsiObj = facebook::jsi::Object(runtime);
  jsiObj.setNativeState(runtime,
                        std::make_shared<hermes_backend::NonOwningSharedScriptClassHolder>(this));

  auto thiz = hermes_interop::makeLocal<Value>(std::move(jsiObj));
  auto obj = engine->performNewNativeClass(typeIndex, classDefine, 1, &thiz);

  if (obj == thiz)
    internalState_.weakRef_ = hermes_interop::toShared(thiz);
  else
    internalState_.weakRef_ = hermes_interop::toShared(obj);
}

hermes_backend::HermesScriptClassState::HermesScriptClassState(HermesEngine* scriptEngine,
                                                               const Local<Value>& obj)
    : scriptEngine_(scriptEngine) {}

ScriptClass::ScriptClass(const script::Local<script::Object>& scriptObject)
    : internalState_(hermes_backend::currentEngine(), scriptObject) {}

Local<Object> ScriptClass::getScriptObject() const {
  auto* runtime = hermes_interop::getEngineRuntime(internalState_.scriptEngine_);

  if (auto val = internalState_.weakRef_)
    return hermes_interop::makeLocal<Object>(val->asObject(*runtime));

  return Object::newObject();
}

Local<Array> ScriptClass::getInternalStore() const {
  auto& store = internalState_.internalStore_;
  if (!store.isArray()) {
    auto* runtime = hermes_interop::getEngineRuntime(internalState_.scriptEngine_);
    const_cast<ScriptClass*>(this)->internalState_.internalStore_ =
        hermes_interop::makeLocal<Value>(facebook::jsi::Array(*runtime, 0));
  }
  return internalState_.internalStore_.asArray();
}

ScriptEngine* ScriptClass::getScriptEngine() const { return internalState_.scriptEngine_; }

bool ScriptClass::isScriptObjectNull() const {
  if (auto val = internalState_.weakRef_) return val->isNull();
  return true;
}

ScriptClass::~ScriptClass() = default;
}  // namespace script