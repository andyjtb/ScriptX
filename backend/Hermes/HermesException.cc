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

#include <ScriptX/ScriptX.h>

namespace script {

void hermes_backend::ExceptionFields::fillMessage(
    const script::Local<script::Value>& exception) const {
  if (!hasMessage_) {
    hasMessage_ = true;
    try {
      auto exp = exception.asObject();
      exception_ = exp;
      message_ = exp.get("message").asString().toString();

      if (exp.has("stack")) stacktrace_ = exp.get("stack").asString().toString();
    } catch (Exception&) {
      message_ = "[another exception in message()]";
    }
  }
}

Exception::Exception(std::string msg) : std::exception(), exception_() {
  exception_.message_ = std::move(msg);
  exception_.hasMessage_ = true;
  exception_.exception_ = Object::newObject();
  exception_.exception_.get().asObject().set("message", exception_.message_);
}

Exception::Exception(const script::Local<script::String>& message)
    : std::exception(), exception_() {
  exception_.message_ = message.toString();
  exception_.hasMessage_ = true;
  exception_.exception_ = Object::newObject();
  exception_.exception_.get().asObject().set("message", exception_.message_);
}

Exception::Exception(const script::Local<script::Value>& exception)
    : std::exception(), exception_(exception) {}

Local<Value> Exception::exception() const { return exception_.exception_.get(); }

std::string Exception::message() const noexcept { return exception_.message_; }

std::string Exception::stacktrace() const noexcept { return exception_.stacktrace_; }

const char* Exception::what() const noexcept { return exception_.message_.c_str(); }

}  // namespace script
