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

#include "HermesHelper.h"

namespace script::hermes_backend {
class HermesRuntime : public facebook::jsi::RuntimeDecorator<facebook::hermes::HermesRuntime> {
 public:
  using RD = RuntimeDecorator<facebook::hermes::HermesRuntime>;

  HermesRuntime(std::unique_ptr<facebook::hermes::HermesRuntime> runtime, uint64_t globalID,
                const ::hermes::vm::RuntimeConfig& conf)
      : RuntimeDecorator<facebook::hermes::HermesRuntime>(*runtime), runtime_(std::move(runtime)) {}

  std::unique_ptr<facebook::hermes::HermesRuntime> runtime_;

  explicit operator facebook::hermes::HermesRuntime*() const noexcept { return runtime_.get(); }
  explicit operator facebook::hermes::HermesRuntime&() const noexcept { return *runtime_; }

  using RD::callAsConstructor;
  using RD::createStringFromAscii;
  using RD::createStringFromUtf8;
  using RD::instanceOf;
};
}  // namespace script::hermes_backend