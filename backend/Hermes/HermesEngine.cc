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

#include "HermesEngine.h"
#include "../../src/Utils.h"

#include "HermesHelper.hpp"
#include "HermesRuntime.h"

namespace script::hermes_backend {

HermesEngine::HermesEngine(std::shared_ptr<utils::MessageQueue> queue)
    : messageQueue_(queue ? std::move(queue) : std::make_shared<utils::MessageQueue>()) {
  const auto runtimeConfig = hermes::vm::RuntimeConfig::Builder()
                                 .withIntl(false)
                                 .withEnableHermesInternal(true)
                                 .withMicrotaskQueue(true)
                                 .withES6Class(true)
#if HERMES_ENABLE_DEBUGGER
                                 .withSampleProfiling (true)
#endif
                                 .build();

  // Create the Hermes runtime.
  auto runtime = facebook::hermes::makeHermesRuntime(runtimeConfig);
  runtime_ = std::make_unique<HermesRuntime>(std::move(runtime), 0, runtimeConfig).release();

  invalidatePropNameCache = std::make_unique<InvalidateCacheOnDestroy>(*runtime_);
}

HermesEngine::HermesEngine() : HermesEngine(std::shared_ptr<utils::MessageQueue>{}) {}

HermesEngine::~HermesEngine() = default;

void HermesEngine::destroy() noexcept {
  isDestroying_ = true;
  ScriptEngine::destroyUserData();
  messageQueue_->removeMessageByTag(this);
  runtime_->drainMicrotasks(-1);

  globalWeakBookkeeping_.clear();
  classRegistry_.clear();

  invalidatePropNameCache = nullptr;
  delete runtime_;
}

Local<Object> HermesEngine::getGlobal() {
  return hermes_interop::makeLocal<Object>(runtime_->global());
}

Local<Value> HermesEngine::get(const Local<String>& key) {
  return hermes_interop::makeLocal<Value>(
      runtime_->global().getProperty(*runtime_, key.toString().c_str()));
}

void HermesEngine::set(const Local<String>& key, const Local<Value>& value) {
  try {
    auto copy = value;
    runtime_->global().setProperty(*runtime_, key.toString().c_str(), *copy.val_.valuePtr);
  } catch (const facebook::jsi::JSIException& e) {
    throw Exception(std::string(e.what()));
  }
}

Local<Value> HermesEngine::eval(const Local<String>& script) {
  return eval(script, Local<Value>());
}

Local<Value> HermesEngine::eval(const Local<String>& script, const Local<String>& sourceFile) {
  return eval(script, sourceFile.asValue());
}

Local<Value> HermesEngine::eval(const Local<String>& script, const Local<Value>& sourceFile) {
  Tracer trace(this, "HermesEngine::eval");
  facebook::jsi::Value ret;

  try {
    ret = runtime_->evaluateJavaScript(
        std::make_unique<facebook::jsi::StringBuffer>(script.toString()),
        sourceFile.describeUtf8());
  } catch (facebook::jsi::JSError& e) {
    auto val = facebook::jsi::Value(*runtime_, e.value());
    throw Exception(hermes_interop::makeLocal<Value>(std::move(val)));
  } catch (facebook::jsi::JSIException& e) {
    // Handle JSI exceptions here.
    std::string msg = e.what();
    throw Exception(msg);
  }

  runtime_->drainMicrotasks(-1);
  return Local<Value>(std::move(ret));
}

Local<Value> HermesEngine::evalInPlace(const std::string& script) {
  return evalInPlace(script, "");
}

Local<Value> HermesEngine::evalInPlace(const std::string& script, const std::string& sourceFile) {
  return evalInPlaceInternal(std::make_unique<facebook::jsi::StringBuffer>(script), sourceFile);
}

struct RawDataBuffer : public facebook::jsi::Buffer {
  RawDataBuffer(const uint8_t* data, size_t size)
    : _data (data),
      _size (size)
  {}

  virtual size_t size() const { return _size; }
  virtual const uint8_t* data() const { return _data; }

  const uint8_t* _data = nullptr;
  const size_t _size = 0;
};

Local<Value> HermesEngine::evalInPlace(const char* data, size_t size, const std::string& sourceFile) {
  return evalInPlaceInternal(std::make_unique<RawDataBuffer>(reinterpret_cast<const uint8_t*> (data), size), sourceFile);
}

Local<Value> HermesEngine::evalInPlaceInternal(const std::shared_ptr<facebook::jsi::Buffer>& buffer, const std::string& sourceFile) {
  Tracer trace(this, "HermesEngine::evalInPlace");
  facebook::jsi::Value ret;

  try {
    ret = runtime_->evaluateJavaScript(buffer,
                                       sourceFile);
  } catch (facebook::jsi::JSError& e) {
    // Handle JS exceptions here.
    auto val = facebook::jsi::Value(*runtime_, e.value());
    throw Exception(hermes_interop::makeLocal<Value>(std::move(val)));
  } catch (facebook::jsi::JSIException& e) {
    // Handle JSI exceptions here.
    std::string msg = e.what();
    throw Exception(msg);
  }

  runtime_->drainMicrotasks(-1);
  return Local<Value>(std::move(ret));
}

std::shared_ptr<utils::MessageQueue> HermesEngine::messageQueue() { return messageQueue_; }

size_t HermesEngine::getHeapSize() {
  const auto info = runtime_->instrumentation().getHeapInfo(false);
  return info.at("hermes_heapSize");
}

void HermesEngine::gc() {
  if (!isDestroying()) runtime_->instrumentation().collectGarbage("c++ engine function called");
}

void HermesEngine::adjustAssociatedMemory(int64_t count) {}

ScriptLanguage HermesEngine::getLanguageType() { return ScriptLanguage::kJavaScript; }

std::string HermesEngine::getEngineVersion() { return runtime_->description() + " vUnknown"; }

facebook::hermes::HermesRuntime* HermesEngine::getHermesRuntime()
{
    if (runtime_ == nullptr || runtime_->runtime_ == nullptr)
        return nullptr;

    return runtime_->runtime_.get();
}

bool HermesEngine::isDestroying() const { return isDestroying_; }

void HermesEngine::scheduleTick() {
  bool no = false;
  if (tickScheduled_.compare_exchange_strong(no, true)) {
    utils::Message tick(
        [](auto& m) {
          auto eng = static_cast<HermesEngine*>(m.ptr0);
          EngineScope scope(eng);
          ;
          while (!eng->runtime_->drainMicrotasks(-1)) {
          }
          eng->tickScheduled_ = false;
        },
        [](auto& m) {

        });
    tick.ptr0 = this;
    tick.tag = this;
    messageQueue_->postMessage(tick);
  }
}

void HermesEngine::deleteScriptClass(script::ScriptClass* sc) {
  if (!isDestroying()) {
    utils::Message dtor([](auto& msg) {},
                        [](auto& msg) { delete static_cast<ScriptClass*>(msg.ptr0); });
    dtor.tag = this;
    dtor.ptr0 = sc;
    messageQueue()->postMessage(dtor);
  } else {
    delete sc;
  }
}

facebook::jsi::Runtime& HermesEngine::getRt() { return *hermes_interop::getEngineRuntime(this); }

}  // namespace script::hermes_backend
