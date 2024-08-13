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
#include <utility>
#include "../../src/NativeConverter.hpp"

#include "HermesEngine.h"

namespace script {

namespace hermes_backend {
struct HermesEngine::BookKeepFetcher {
  template <typename T>
  static ::script::internal::GlobalWeakBookkeeping* get(const T* ref) {
    if (!ref) return nullptr;
    auto& val = ref->val_;
    if (!val.engine_) return nullptr;
    return &val.engine_->globalWeakBookkeeping_;
  }

  template <typename T>
  static ::script::internal::GlobalWeakBookkeeping::HandleType& handle(const T* ref) {
    auto& val = const_cast<T*>(ref)->val_;
    return val.handle_;
  }
};

struct HermesBookKeepFetcher : HermesEngine::BookKeepFetcher {};
using BookKeep = ::script::internal::GlobalWeakBookkeeping::Helper<HermesBookKeepFetcher>;

}  // namespace hermes_backend

template <typename T>
Global<T>::Global() noexcept : val_() {}

template <typename T>
Global<T>::Global(const script::Local<T>& localReference) : val_() {
  val_.valuePtr = localReference.val_.valuePtr;
  val_.engine_ = hermes_backend::currentEngine();
  hermes_backend::BookKeep::keep(this);
}

template <typename T>
Global<T>::Global(const script::Weak<T>& weak)
    : Global(::script::converter::Converter<Local<T>>::toCpp(weak.getValue())) {}

template <typename T>
Global<T>::Global(const script::Global<T>& copy) : val_() {
  *this = copy;
}

template <typename T>
Global<T>::Global(script::Global<T>&& move) noexcept : val_() {
  *this = std::move(move);
}

template <typename T>
Global<T>::~Global() {
  if (isEmpty()) return;
  hermes_backend::BookKeep::remove(this);
}

template <typename T>
Global<T>& Global<T>::operator=(const script::Global<T>& assign) {
  if (this != &assign) {
    bool wasEmpty = isEmpty();
    val_ = assign.val_;
    hermes_backend::BookKeep::afterCopy(wasEmpty, this, &assign);
  }
  return *this;
}

template <typename T>
Global<T>& Global<T>::operator=(script::Global<T>&& move) noexcept {
  if (this != &move) {
    bool wasEmpty = isEmpty();
    val_ = std::move(move.val_);
    hermes_backend::BookKeep::afterMove(wasEmpty, this, &move);
  }
  return *this;
}

template <typename T>
void Global<T>::swap(Global& rhs) noexcept {
  std::swap(val_.valuePtr, rhs.val_.valuePtr);
  std::swap(val_.engine_, rhs.val_.engine_);
  hermes_backend::BookKeep::afterSwap(this, &rhs);
}

template <typename T>
Global<T>& Global<T>::operator=(const script::Local<T>& assign) {
  *this = Global<T>(assign);
  return *this;
}

template <typename T>
Local<T> Global<T>::get() const {
  if (isEmpty()) throw Exception("get on empty Global");
  return Local<T>(val_);
}

template <typename T>
Local<Value> Global<T>::getValue() const {
  if (isEmpty()) return {};
  return Local<Value>(val_);
}

template <typename T>
bool Global<T>::isEmpty() const {
  return val_.engine_ == nullptr;
}

template <typename T>
void Global<T>::reset() {
  if (isEmpty()) return;
  val_.valuePtr = nullptr;
  hermes_backend::BookKeep::remove(this);
  val_.engine_ = nullptr;
}

// == Weak ==

template <typename T>
Weak<T>::Weak() noexcept : val_() {}

template <typename T>
Weak<T>::~Weak() {
  if (isEmpty()) return;
  hermes_backend::BookKeep::remove(this);
}

template <typename T>
Weak<T>::Weak(const script::Local<T>& localReference) : val_() {
  if (auto shared = hermes_interop::toShared(localReference)) val_.valuePtr = shared;
  val_.engine_ = hermes_backend::currentEngine();
  hermes_backend::BookKeep::keep(this);
}

template <typename T>
Weak<T>::Weak(const script::Global<T>& globalReference)
    : Weak(::script::converter::Converter<Local<T>>::toCpp(globalReference.getValue())) {}

template <typename T>
Weak<T>::Weak(const script::Weak<T>& copy) {
  *this = copy;
}

template <typename T>
Weak<T>::Weak(script::Weak<T>&& move) noexcept {
  *this = std::move(move);
}

template <typename T>
Weak<T>& Weak<T>::operator=(const script::Weak<T>& assign) {
  if (this != &assign) {
    bool wasEmpty = isEmpty();
    val_ = assign.val_;
    hermes_backend::BookKeep::afterCopy(wasEmpty, this, &assign);
  }
  return *this;
}

template <typename T>
Weak<T>& Weak<T>::operator=(script::Weak<T>&& move) noexcept {
  if (this != &move) {
    bool wasEmpty = isEmpty();
    val_ = std::move(move.val_);
    hermes_backend::BookKeep::afterMove(wasEmpty, this, &move);
  }
  return *this;
}

template <typename T>
void Weak<T>::swap(Weak& rhs) noexcept {
  if (&rhs != this) {
    std::swap(val_.valuePtr, rhs.val_.valuePtr);
    std::swap(val_.engine_, rhs.val_.engine_);
    hermes_backend::BookKeep::afterSwap(this, &rhs);
  }
}

template <typename T>
Weak<T>& Weak<T>::operator=(const script::Local<T>& assign) {
  *this = Weak<T>(assign);
  return *this;
}

template <typename T>
Local<T> Weak<T>::get() const {
  auto value = getValue();
  if (value.isNull()) throw Exception("get on null Weak");
  return converter::Converter<Local<T>>::toCpp(value);
}

template <typename T>
Local<Value> Weak<T>::getValue() const {
  if (isEmpty()) return {};
  if (auto shared = val_.valuePtr.lock(); shared != nullptr) return Local<Value>(*shared);
  return {};
}

template <typename T>
bool Weak<T>::isEmpty() const {
  return val_.engine_ == nullptr;
}

template <typename T>
void Weak<T>::reset() noexcept {
  if (isEmpty()) return;
  val_.valuePtr.reset();
  hermes_backend::BookKeep::remove(this);
  val_.engine_ = nullptr;
}

template class Global<Value>;
template class Global<Object>;

}  // namespace script
