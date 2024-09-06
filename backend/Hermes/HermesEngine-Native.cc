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

#include "../../src/Native.hpp"
#include "../../src/utils/Helper.hpp"
#include "HermesEngine.h"

#include "HermesHelper.hpp"
#include "HermesJsiArgsTransform.h"
#include "HermesReference.hpp"

namespace script::hermes_backend {

void HermesEngine::performRegisterNativeClass(
    internal::TypeIndex typeIndex, const internal::ClassDefineState* classDefine,
    script::ScriptClass* (*instanceTypeToScriptClass)(void*)) {
  Tracer traceRegister(this, classDefine->className);

  Local<Value> object;
  ClassRegistryData registry{};
  registry.instanceTypeToScriptClass = instanceTypeToScriptClass;

  if (classDefine->hasInstanceDefine()) {
    defineInstance(classDefine, object, registry);
  } else {
    object = Object::newObject();
  }

  registerStaticDefine(classDefine->staticDefine, object.asObject());

  auto ns =
      ::script::internal::getNamespaceObject(this, classDefine->nameSpace, getGlobal()).asObject();
  ns.set(classDefine->className, object);

  classRegistry_.emplace(classDefine, registry);
}

void HermesEngine::defineInstance(const internal::ClassDefineState* classDefine,
                                  Local<Value>& object, ClassRegistryData& registry) {
  //  JSClassDefinition instanceDefine = kJSClassDefinitionEmpty;
  //  instanceDefine.attributes = kJSClassAttributeNone;
  //  instanceDefine.className = classDefine->className.c_str();
  //
  //  instanceDefine.finalize = [](JSObjectRef thiz) {
  //    auto* t = static_cast<ScriptClass*>(JSObjectGetPrivate(thiz));
  //    auto engine = script::internal::scriptDynamicCast<JscEngine*>(t->getScriptEngine());
  //    if (!engine->isDestroying()) {
  //      utils::Message dtor([](auto& msg) {},
  //                          [](auto& msg) { delete static_cast<ScriptClass*>(msg.ptr0); });
  //      dtor.tag = engine;
  //      dtor.ptr0 = t;
  //      engine->messageQueue()->postMessage(dtor);
  //    } else {
  //      delete t;
  //    }
  //  };
  //  auto clazz = JSClassCreate(&instanceDefine);
  //  registry.instanceClass = clazz;
  //
  //  JSClassDefinition staticDefine = kJSClassDefinitionEmpty;
  //
  //  staticDefine.callAsConstructor = createConstructor();
  //  staticDefine.hasInstance = [](JSContextRef ctx, JSObjectRef constructor,
  //                                JSValueRef possibleInstance, JSValueRef*) -> bool {
  //    auto engine = static_cast<JscEngine*>(JSObjectGetPrivate(JSContextGetGlobalObject(ctx)));
  //    auto def = static_cast<internal::ClassDefineState*>(JSObjectGetPrivate(constructor));
  //      return engine->performIsInstanceOf(make<Local<Value>>(possibleInstance), def);
  //  };
  //
  //  auto staticClass = JSClassCreate(&staticDefine);
  //  object = Local<Object>(
  //      JSObjectMake(context_, staticClass,
  //      const_cast<internal::ClassDefineState*>(classDefine)));
  //  // not used anymore
  //  JSClassRelease(staticClass);

  registry.constructor = Global<Object>(createConstructor(classDefine));
  object = registry.constructor.getValue();

  auto prototype = defineInstancePrototype(classDefine);
  object.asObject().set("prototype", prototype);

  registry.prototype = prototype;
}

Local<Object> HermesEngine::createConstructor(const internal::ClassDefineState* classDefine) {
  auto& rt = getRt();
  auto constructor = jsi::Function::createFromHostFunction(
      rt, jsi::PropNameID::forAscii(rt, "constructor"), 1,
      [engine = hermes_backend::currentEngine(), classDefine](
          jsi::Runtime& runtime, const jsi::Value& thisValue, const jsi::Value* arguments,
          size_t count) -> jsi::Value {
        if (&runtime != hermes_interop::getEngineRuntime(engine)) {
          throw Exception(std::string("Invalid Runtime"));
        }
        Tracer trace(engine, classDefine->className);

        auto it = engine->classRegistry_.find(classDefine);
        assert(it != engine->classRegistry_.end());

        ClassRegistryData& registry = it->second;

        auto scriptArgs = hermes_interop::makeArguments(engine, thisValue, arguments, count);

        StackFrameScope stack;
        void* thiz = nullptr;
        if (count == 1 && arguments[0].isObject()) {
          if (arguments[0].asObject(runtime).hasNativeState(runtime)) {
            if (auto ptr = arguments[0].asObject(runtime).getNativeState(runtime)) {
              // this logic is for
              // ScriptClass::ScriptClass(ConstructFromCpp<T>)

              if (auto* scriptClass = dynamic_cast<NonOwningSharedScriptClassHolder*>(ptr.get())) {
                thiz = scriptClass->sc;
              } else {
                throw Exception("NativeState is of incorrect type");
              }
            }
          }
        }

        if (thiz == nullptr) {
          // this logic is for
          // ScriptClass::ScriptClass(const Local<Object>& thiz)
          thiz = classDefine->instanceDefine.constructor(scriptArgs);

          if (thiz == nullptr) {
            throw Exception("can't create class " + classDefine->className);
          }
        }

        auto scriptClass = registry.instanceTypeToScriptClass(thiz);
        scriptClass->internalState_.scriptEngine_ = engine;
        scriptClass->internalState_.classDefine = classDefine;
        scriptClass->internalState_.polymorphicPointer = thiz;
        scriptClass->internalState_.internalStore_ =
            hermes_interop::makeLocal<Value>(facebook::jsi::Array(runtime, 0));

        if (registry.prototype.val_.valuePtr != nullptr) {
          auto Object = runtime.global().getPropertyAsObject(runtime, "Object");
          auto createFunc = Object.getPropertyAsFunction(runtime, "create");

          auto obj = createFunc.call(runtime, *registry.prototype.val_.valuePtr);
          obj.asObject(runtime).setNativeState(
              runtime, std::make_shared<SharedScriptClassHolder>(scriptClass));

          return obj;
        }

        thisValue.asObject(runtime).setNativeState(
            runtime, std::make_shared<SharedScriptClassHolder>(scriptClass));

        return {};
      });

  return hermes_interop::makeLocal<Object>(
      rt.global().getPropertyAsFunction(rt, "makeNativeClass").call(rt, constructor));
}

Local<Object> HermesEngine::defineInstancePrototype(const internal::ClassDefineState* classDefine) {
  Local<Object> proto = Object::newObject();

  defineInstanceFunction(classDefine, proto);

  if (!classDefine->instanceDefine.properties.empty()) defineInstanceProperties(classDefine, proto);

  return proto;
}

void* HermesEngine::getThisPointer(jsi::Runtime& rt, const jsi::Value& thisVal) {
  if (!thisVal.asObject(rt).hasNativeState(rt)) {
    throw Exception(std::string("No private data added to Native Instance"));
  }

  auto privateData = thisVal.asObject(rt).getNativeState(rt);
  auto* privateClass = dynamic_cast<SharedScriptClassHolder*>(privateData.get());
  if (privateClass == nullptr) {
    throw Exception(std::string("Private data added to Native Instance isn't of ScriptClass"));
  }

  return privateClass->sc->internalState_.polymorphicPointer;
}

void HermesEngine::defineInstanceFunction(const internal::ClassDefineState* classDefine,
                                          Local<Object>& prototypeObject) {
  for (auto& f : classDefine->instanceDefine.functions) {
    StackFrameScope stack;
    const jsi::HostFunctionType cb = [engine = hermes_backend::currentEngine(), f](
                                         jsi::Runtime& rt, const jsi::Value& thisVal,
                                         const jsi::Value* args,
                                         size_t count) -> facebook::jsi::Value {
      if (&rt != hermes_interop::getEngineRuntime(engine)) {
        throw Exception(std::string("Invalid Runtime"));
      }

      Tracer trace(engine, f.traceName);

      auto scriptArgs = hermes_interop::makeArguments(engine, thisVal, args, count);
      const auto res = (f.callback)(getThisPointer(rt, thisVal), scriptArgs);

      return hermes_interop::moveHermes(res);
    };

    const auto functionName = facebook::jsi::PropNameID::forAscii(getRt(), f.name);
    const auto name = String::newString(f.name);
    prototypeObject.set(
        name, hermes_interop::makeLocal<Function>(
                  facebook::jsi::Function::createFromHostFunction(getRt(), functionName, 1, cb)));
  }
}

void HermesEngine::defineInstanceProperties(const internal::ClassDefineState* classDefine,
                                            const Local<Object>& prototype) {
  auto Object = getRt().global().getPropertyAsObject(getRt(), "Object");
  auto defineProperties = Object.getPropertyAsFunction(getRt(), "defineProperties");
  auto get = String::newString("get");
  auto set = String::newString("set");

  auto allProperties = Object::newObject();

  for (auto& prop : classDefine->instanceDefine.properties) {
    Local<Value> getter = newInstanceGetter(prop);
    Local<Value> setter = newInstanceSetter(prop);

    auto desc = Object::newObject();
    if (!getter.isNull()) desc.set(get, std::move(getter));
    if (!setter.isNull()) desc.set(set, std::move(setter));

    allProperties.set(prop.name.c_str(), desc);
  }

  const auto res =
      defineProperties.call(getRt(), *prototype.val_.valuePtr, *allProperties.val_.valuePtr);
  assert(res.isObject());

  hermes_interop::toHermes(prototype)->asObject(getRt()).setToStringTag(getRt(), jsi::String::createFromUtf8 (getRt(), classDefine->className));
}

void HermesEngine::registerStaticDefine(const internal::StaticDefine& staticDefine,
                                        const Local<Object>& object) {
  for (auto& func : staticDefine.functions) {
    StackFrameScope stack;
    Local<Function> jsFunc = newStaticFunction(func);
    object.set(func.name, std::move(jsFunc));
  }

  if (!staticDefine.properties.empty()) {
    auto Object = getRt().global().getPropertyAsObject(getRt(), "Object");
    auto defineProperties = Object.getPropertyAsFunction(getRt(), "defineProperties");
    auto get = String::newString("get");
    auto set = String::newString("set");

    auto allProperties = Object::newObject();

    for (auto& prop : staticDefine.properties) {
      Local<Value> getter = newStaticGetter(prop);
      Local<Value> setter = newStaticSetter(prop);

      auto desc = Object::newObject();
      if (!getter.isNull()) desc.set(get, std::move(getter));
      if (!setter.isNull()) desc.set(set, std::move(setter));

      allProperties.set(prop.name.c_str(), desc);
    }

    const auto res =
        defineProperties.call(getRt(), *object.val_.valuePtr, *allProperties.val_.valuePtr);
    assert(res.isObject());
  }
}

Local<Value> HermesEngine::newStaticSetter(const internal::StaticDefine::PropertyDefine& prop) {
  // setter may null
  if (prop.setter == nullptr) return {};

  const jsi::HostFunctionType cb = [engine = hermes_backend::currentEngine(), prop](
                                       jsi::Runtime& rt, const jsi::Value& thisVal,
                                       const jsi::Value* args,
                                       size_t count) -> facebook::jsi::Value {
    if (&rt != hermes_interop::getEngineRuntime(engine)) {
      throw Exception(std::string("Invalid Runtime"));
    }

    Tracer trace(engine, prop.name);

    auto scriptArgs = hermes_interop::makeArguments(engine, thisVal, args, count);
    (prop.setter)(scriptArgs[0]);

    return {};
  };

  const auto functionName = facebook::jsi::PropNameID::forAscii(getRt(), "setter-" + prop.name);
  return hermes_interop::makeLocal<Function>(
      facebook::jsi::Function::createFromHostFunction(getRt(), functionName, 1, cb));
}

Local<Value> HermesEngine::newStaticGetter(const internal::StaticDefine::PropertyDefine& prop) {
  if (prop.getter == nullptr) return {};

  const jsi::HostFunctionType cb = [engine = hermes_backend::currentEngine(), prop](
                                       jsi::Runtime& rt, const jsi::Value& thisVal,
                                       const jsi::Value* args,
                                       size_t count) -> facebook::jsi::Value {
    if (&rt != hermes_interop::getEngineRuntime(engine)) {
      throw Exception(std::string("Invalid Runtime"));
    }

    Tracer trace(engine, prop.name);
    const auto res = (prop.getter)();

    return hermes_interop::moveHermes(res);
  };

  const auto functionName = facebook::jsi::PropNameID::forAscii(getRt(), "getter-" + prop.name);
  return hermes_interop::makeLocal<Function>(
      facebook::jsi::Function::createFromHostFunction(getRt(), functionName, 0, std::move(cb)));
}

Local<Function> HermesEngine::newStaticFunction(
    const internal::StaticDefine::FunctionDefine& func) {
  const jsi::HostFunctionType cb = [engine = hermes_backend::currentEngine(), func](
                                       jsi::Runtime& rt, const jsi::Value& thisVal,
                                       const jsi::Value* args,
                                       size_t count) -> facebook::jsi::Value {
    if (&rt != hermes_interop::getEngineRuntime(engine)) {
      throw Exception(std::string("Invalid Runtime"));
    }

    Tracer trace(engine, func.traceName);
    auto scriptArgs = hermes_interop::makeArguments(engine, thisVal, args, count);
    const auto res = (func.callback)(scriptArgs);

    return hermes_interop::moveHermes(res);
  };

  const auto functionName = facebook::jsi::PropNameID::forAscii(getRt(), func.name);
  return hermes_interop::makeLocal<Function>(
      facebook::jsi::Function::createFromHostFunction(getRt(), functionName, 1, cb));
}

Local<Value> HermesEngine::newInstanceSetter(const internal::InstanceDefine::PropertyDefine& prop) {
  // setter may null
  if (prop.setter == nullptr) return {};

  const jsi::HostFunctionType cb = [engine = hermes_backend::currentEngine(), prop](
                                       jsi::Runtime& rt, const jsi::Value& thisVal,
                                       const jsi::Value* args,
                                       size_t count) -> facebook::jsi::Value {
    if (&rt != hermes_interop::getEngineRuntime(engine)) {
      throw Exception(std::string("Invalid Runtime"));
    }

    Tracer trace(engine, prop.name);

    auto scriptArgs = hermes_interop::makeArguments(engine, thisVal, args, count);
    (prop.setter)(getThisPointer(rt, thisVal), scriptArgs[0]);

    return {};
  };

  const auto functionName = facebook::jsi::PropNameID::forAscii(getRt(), "setter-" + prop.name);
  return hermes_interop::makeLocal<Function>(
      facebook::jsi::Function::createFromHostFunction(getRt(), functionName, 1, cb));
}

Local<Value> HermesEngine::newInstanceGetter(const internal::InstanceDefine::PropertyDefine& prop) {
  if (prop.getter == nullptr) return {};

  const jsi::HostFunctionType cb = [engine = hermes_backend::currentEngine(), prop](
                                       jsi::Runtime& rt, const jsi::Value& thisVal,
                                       const jsi::Value* args,
                                       size_t count) -> facebook::jsi::Value {
    if (&rt != hermes_interop::getEngineRuntime(engine)) {
      throw Exception(std::string("Invalid Runtime"));
    }

    Tracer trace(engine, prop.name);
    const auto res = (prop.getter)(getThisPointer(rt, thisVal));

    return hermes_interop::moveHermes(res);
  };

  const auto functionName = facebook::jsi::PropNameID::forAscii(getRt(), "getter-" + prop.name);
  return hermes_interop::makeLocal<Function>(
      facebook::jsi::Function::createFromHostFunction(getRt(), functionName, 0, std::move(cb)));
}

Local<Object> HermesEngine::performNewNativeClass(internal::TypeIndex typeIndex,
                                                  const internal::ClassDefineState* classDefine,
                                                  size_t size, const Local<script::Value>* args) {
  auto it = classRegistry_.find(classDefine);
  if (it != classRegistry_.end()) {
    Local<Value> constructor(it->second.constructor.val_);
    return Object::newObjectImpl(constructor, size, args);
  }

  throw Exception("class define[" + classDefine->className + "] is not registered");
}

bool HermesEngine::performIsInstanceOf(const Local<script::Value>& value,
                                       const internal::ClassDefineState* classDefine) {
  if (!value.isObject()) return false;
  auto it = classRegistry_.find(const_cast<internal::ClassDefineState*>(classDefine));

  if (it != classRegistry_.end() && !it->second.constructor.isEmpty()) {
    auto& rt = getRt();
    auto obj = hermes_interop::toHermes(value)->getObject(rt);
    if (runtime_->instanceOf(obj,
                             it->second.constructor.val_.valuePtr->asObject(rt).asFunction(rt))) {
      return true;
    }

    if (obj.hasNativeState(rt)) {
      void* privateData = obj.getNativeState(rt).get();
      return static_cast<SharedScriptClassHolder*>(privateData)->sc->internalState_.classDefine ==
             classDefine;
    }
  }

  return false;
}

void* HermesEngine::performGetNativeInstance(const Local<script::Value>& value,
                                             const internal::ClassDefineState* classDefine) {
  if (value.isObject() && performIsInstanceOf(value, classDefine)) {
    auto& rt = getRt();
    auto obj = value.val_.valuePtr->getObject(rt);
    void* privateData = obj.getNativeState(rt).get();
    return static_cast<SharedScriptClassHolder*>(privateData)
        ->sc->internalState_.polymorphicPointer;
  }
  return nullptr;
}

}  // namespace script::hermes_backend
