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

#include "HermesScope.h"
#include "../../src/Scope.h"

#include "HermesEngine.h"
#include "HermesRuntime.h"

namespace script::hermes_backend {

HermesEngineScope::HermesEngineScope(HermesEngine& engine, HermesEngine* previous)
    : scope_(*engine.runtime_), engine_(engine) {}

HermesEngineScope::~HermesEngineScope() {
  engine_.getRt().drainMicrotasks(-1);
}

}  // namespace script::hermes_backend
