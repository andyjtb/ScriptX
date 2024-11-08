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

#include <cassert>
#include <cstdint>
#include <optional>
#include <sstream>
#include <tuple>
#include <type_traits>
#include "Native.h"
#include "NativeConverter.hpp"
#include "Reference.h"
#include "Scope.h"
#include "Utils.h"
#include "types.h"

// script::internal are implementation details, not public API
namespace script::internal {

namespace traits {

template <typename Args>
struct TupleTrait;

template <typename HeadT, typename... TailT>
struct TupleTrait<std::tuple<HeadT, TailT...>> {
  using Head = HeadT;
  using Tail = std::tuple<TailT...>;

  static constexpr size_t count = 1 + sizeof...(TailT);

  template <size_t i>
  using Arg = typename std::tuple_element_t<i, std::tuple<HeadT, TailT...>>;
};

template <>
struct TupleTrait<std::tuple<>> {
  using Tail = std::tuple<>;
  static constexpr size_t count = 0;
};

template <typename Func, typename Enable = void>
struct FunctionTrait;

template <typename Ret, typename... Args>
struct FunctionTrait<Ret (*)(Args...)> {
  using ReturnType = Ret;
  using Arguments = std::tuple<Args...>;
};

template <typename C, typename Ret, typename... Args>
struct FunctionTrait<Ret (C::*)(Args...)> : FunctionTrait<Ret (*)(C*, Args...)> {};

template <typename C, typename Ret, typename... Args>
struct FunctionTrait<Ret (C::*)(Args...) const> : FunctionTrait<Ret (*)(C*, Args...)> {};

template <typename C, typename Ret, typename... Args>
struct FunctionTrait<Ret (C::*)(Args...) volatile> : FunctionTrait<Ret (*)(C*, Args...)> {};

template <typename C, typename Ret, typename... Args>
struct FunctionTrait<Ret (C::*)(Args...) const volatile> : FunctionTrait<Ret (*)(C*, Args...)> {};

// functor and lambda
template <typename Functor>
struct FunctionTrait<Functor, std::void_t<decltype(&Functor::operator())>> {
 private:
  using FunctorTrait = FunctionTrait<decltype(&Functor::operator())>;

 public:
  using ReturnType = typename FunctorTrait::ReturnType;
  using Arguments = typename TupleTrait<typename FunctorTrait::Arguments>::Tail;
};

// decay: remove const, reference; function type to function pointer
template <typename Func>
struct FunctionTrait<Func, std::enable_if_t<!std::is_same_v<Func, std::decay_t<Func>>>>
    : FunctionTrait<std::decay_t<Func>> {};

}  // namespace traits

template <typename T>
using FuncTrait = traits::FunctionTrait<T>;

template <typename T>
using ArgsTrait = traits::TupleTrait<typename FuncTrait<T>::Arguments>;

// SFINAE helper
template <typename T, typename...>
using type_t = T;

template <typename T, typename Enable = void>
struct TypeHolder {
  explicit TypeHolder(Local<Value> ref) : ref_(std::move(ref)) {}

  template <typename CppType>
  decltype(auto) toCpp() {
    return TypeConverter<CppType>::toCpp(ref_);
  }

 private:
  Local<Value> ref_;
};

template <typename T>
struct TypeHolder<T, std::enable_if_t<StringLikeConceptCondition(T)>> {
  explicit TypeHolder(const Local<Value>& str) : stringHolder_(str.asString()) {}

  template <typename CppType>
  decltype(auto) toCpp() {
    return TypeConverter<CppType>::toCpp(stringHolder_);
  }

 private:
  StringHolder stringHolder_;
};

template <typename T, typename = void>
struct IsArgsConvertibleHelper : std::false_type {};

template <typename... Args>
struct IsArgsConvertibleHelper<
    std::tuple<Args...>, std::enable_if_t<std::conjunction_v<IsConvertibleHelper<Args>...>, void>>
    : std::true_type {};

template <typename T>
constexpr bool isArgsConvertible = IsArgsConvertibleHelper<T>::value;

inline Local<Value> handleException(const Exception& e, bool nothrow) {
  if (!nothrow) throw e;
#ifndef NDEBUG
  Logger() << e;
#endif
  return {};
}

class OverloadInvalidArguments : public std::exception {};

template <typename>
struct ConvertingFuncCallHelper {};

// extract common code to utils, in order to reduce code size in terms of template specialization
struct ConvertCallHelperUtils {
  /**
   * @param args
   * @param argsCount
   * @param nothrow
   * @return true for failure, abort immediately
   */
  static bool checkArgs(const Arguments& args, size_t argsCount, bool nothrow) {
    if (args.size() != argsCount) {
      // fail fast
      if (nothrow) return true;
      std::ostringstream msg;
      msg << "Argument count mismatch, expect:" << argsCount << " got:" << args.size();
      throw Exception(msg.str());
    }
    return false;
  }

  static Local<Value> handleParamConvertFailure(const Exception& e, bool nothrow,
                                                bool throwForOverload) {
    if (!nothrow && throwForOverload) throw OverloadInvalidArguments();
    return handleException(e, nothrow);
  }

  template <typename RetType>
  static Local<Value> convertAndReturn(RetType&& ret, bool nothrow) {
    try {
      return TypeConverter<RetType>::toScript(std::forward<RetType>(ret));
    } catch (const Exception& e) {
      return handleException(e, nothrow);
    }
  }
};

template <typename Ret, typename... Args>
struct ConvertingFuncCallHelper<std::pair<Ret, std::tuple<Args...>>> {
 private:
  static constexpr auto ArgsLength = sizeof...(Args);

  using TypeHolderTupleType = std::tuple<TypeHolder<Args>...>;

  /**
   * using template matching, to get an index of Args;
   */
  template <typename Func, size_t... index>
  static Local<Value> call(Func& func, const Arguments& args, std::index_sequence<index...>,
                           bool nothrow, bool throwForOverload) {
    std::optional<TypeHolderTupleType> typeHolders;
    std::optional<typename std::tuple<typename ConverterDecay<Args>::type...>> cppArgs;
    // notice: avoid using std::optional::value, iOS support that only on 12+
    // using std::optional::operator* instead

    try {
      if (ConvertCallHelperUtils::checkArgs(args, ArgsLength, nothrow)) {
        return {};
      }
      typeHolders.emplace(args[index]...);
      cppArgs.emplace(std::get<index>(*typeHolders).template toCpp<Args>()...);
    } catch (const Exception& e) {
      return ConvertCallHelperUtils::handleParamConvertFailure(e, nothrow, throwForOverload);
    }

    if constexpr (std::is_same_v<Ret, void>) {
      std::apply(func, *std::move(cppArgs));
      return {};
    } else {
      return ConvertCallHelperUtils::convertAndReturn<Ret>(std::apply(func, *std::move(cppArgs)),
                                                           nothrow);
    }
  }

  template <typename Func, typename Ins, size_t... index>
  static Local<Value> callInstanceFunc(Func& func, Ins* ins, const Arguments& args,
                                       std::index_sequence<index...>, bool nothrow,
                                       bool throwForOverload) {
    std::optional<TypeHolderTupleType> typeHolders;
    std::optional<std::tuple<Ins*, typename ConverterDecay<Args>::type...>> cppArgs;

    try {
      if (ConvertCallHelperUtils::checkArgs(args, ArgsLength, nothrow)) {
        return {};
      }
      typeHolders.emplace(args[index]...);
      cppArgs.emplace(ins, std::get<index>(*typeHolders).template toCpp<Args>()...);
    } catch (const Exception& e) {
      return ConvertCallHelperUtils::handleParamConvertFailure(e, nothrow, throwForOverload);
    }

    if constexpr (std::is_same_v<Ret, void>) {
      std::apply(func, *std::move(cppArgs));
      return {};
    } else {
      return ConvertCallHelperUtils::convertAndReturn(std::apply(func, *std::move(cppArgs)),
                                                      nothrow);
    }
  }

 public:
  template <typename Func>
  static Local<Value> call(Func& func, const Arguments& args, bool nothrow, bool throwForOverload) {
    return call(func, args, std::make_index_sequence<ArgsLength>(), nothrow, throwForOverload);
  }

  template <typename Func, typename Ins>
  static Local<Value> callInstanceFunc(Func& func, Ins* ins, const Arguments& args, bool nothrow,
                                       bool throwForOverload) {
    return callInstanceFunc(func, ins, args, std::make_index_sequence<ArgsLength>(), nothrow,
                            throwForOverload);
  }
};

template <typename T, typename = void>
struct ClassConstructorHelper : std::false_type {};

template <typename T, typename Arg, typename = void>
struct IsScriptClassConstructible : public std::false_type {};

// we don't use std::is_constructible_v here, because it also implies to be destructible
// in our case, the destructible can be protected/private, and we want T be made pointer.
// so just use `new T` syntax to check on it.
template <typename T, typename Arg>
struct IsScriptClassConstructible<T, Arg, std::void_t<decltype(new T(std::declval<Arg>()))>>
    : public std::true_type {};

template <typename T>
struct ClassConstructorHelper<T,
                              std::enable_if_t<IsScriptClassConstructible<T, Local<Object>>::value>>
    : std::true_type {
  static InstanceConstructor ctor() {
    return [](const Arguments& args) -> T* { return new T(args.thiz()); };
  }
};

template <typename T>
struct ClassConstructorHelper<
    T, std::enable_if_t<IsScriptClassConstructible<T, Arguments>::value
                        // add inverse check, to avoid both two specialize can fit
                        && !IsScriptClassConstructible<T, Local<Object>>::value>> : std::true_type {
  static InstanceConstructor ctor() {
    return [](const Arguments& args) -> T* { return new T(args); };
  }
};

// bind static function
template <typename Func>
std::enable_if_t<::script::converter::isConvertible<typename FuncTrait<Func>::ReturnType> &&
                     isArgsConvertible<typename FuncTrait<Func>::Arguments>,
                 FunctionCallback>
bindStaticFunc(Func&& func, bool nothrow, bool throwForOverload = false) {
  return [f = std::forward<Func>(func), nothrow,
          throwForOverload](const Arguments& args) -> Local<Value> {
    using Helper = ConvertingFuncCallHelper<
        std::pair<typename FuncTrait<Func>::ReturnType, typename FuncTrait<Func>::Arguments>>;
    return Helper::call(f, args, nothrow, throwForOverload);
  };
}

// plain overload
inline FunctionCallback bindStaticFunc(FunctionCallback&& func, bool, bool = false) {
  return std::move(func);
}
inline FunctionCallback bindStaticFunc(const FunctionCallback& func, bool, bool = false) {
  return func;
}

template <typename... Func>
FunctionCallback adaptOverLoadedFunction(Func&&... functions) {
  std::vector funcs{bindStaticFunc(std::forward<Func>(functions), false, true)...};
  return [overload = std::move(funcs)](const Arguments& args) -> Local<Value> {
    for (size_t i = 0; i < sizeof...(Func); ++i) {
      try {
        return std::invoke(overload[i], args);
      } catch (const OverloadInvalidArguments&) {
        if (i == sizeof...(Func) - 1) {
          throw Exception("no valid overloaded function chosen");
        }
      }
    }
    return {};
  };
}

// using strict type-check to have better SFINA support
template <typename Func>
std::enable_if_t<::script::converter::isConvertible<typename FuncTrait<Func>::ReturnType> &&
                     ArgsTrait<Func>::count == 0,
                 GetterCallback>
bindStaticGet(Func&& get, bool nothrow) {
  return [g = std::forward<Func>(get), nothrow]() -> Local<Value> {
    using Type = typename FuncTrait<Func>::ReturnType;
    auto&& ret = std::invoke(g);
    try {
      return TypeConverter<Type>::toScript(ret);
    } catch (Exception& e) {
      return handleException(e, nothrow);
    }
  };
}

// type-helper
inline GetterCallback bindStaticGet(GetterCallback&& g, bool) { return std::move(g); }
inline GetterCallback bindStaticGet(const GetterCallback& g, bool) { return g; }

template <typename Func>
// using strict type-check to have better SFINA support
std::enable_if_t<std::is_same_v<typename FuncTrait<Func>::ReturnType, void> &&
                     ArgsTrait<Func>::count == 1 &&
                     isArgsConvertible<typename FuncTrait<Func>::Arguments>,
                 SetterCallback>
bindStaticSet(Func&& set, bool nothrow) {
  return [s = std::forward<Func>(set), nothrow](const Local<Value>& value) {
    using Type = typename ConverterDecay<typename ArgsTrait<Func>::Head>::type;
    std::optional<TypeHolder<Type>> typeHolder;
    std::optional<Type> arg;
    try {
      typeHolder.emplace(value);
      arg = typeHolder->template toCpp<Type>();
    } catch (Exception& e) {
      handleException(e, nothrow);
    }
    std::invoke(s, *std::move(arg));
  };
}

inline SetterCallback bindStaticSet(SetterCallback&& s, bool) { return std::move(s); }
inline SetterCallback bindStaticSet(const SetterCallback& s, bool) { return s; }

template <typename T>
std::enable_if_t<::script::converter::isConvertible<T>, std::pair<GetterCallback, SetterCallback>>
bindStaticProp(T* prop, bool nothrow) {
  if constexpr (!std::is_const_v<T>) {
    return {bindStaticGet([prop]() -> T { return *prop; }, nothrow),
            bindStaticSet([prop](T val) { *prop = val; }, nothrow)};
  } else {
    return {bindStaticGet([prop]() -> T { return *prop; }, nothrow), nullptr};
  }
}

template <typename Class, typename Func>
std::enable_if_t<::script::converter::isConvertible<typename FuncTrait<Func>::ReturnType> &&
                     // if Arg<0> is base class of Class
                     std::is_convertible_v<Class*, typename ArgsTrait<Func>::template Arg<0>> &&
                     isArgsConvertible<typename ArgsTrait<Func>::Tail>,
                 InstanceFunctionCallback>
bindInstanceFunc(Func&& func, bool nothrow, bool throwForOverload = false) {
  if (!func) return {};

  return [f = std::forward<Func>(func), nothrow, throwForOverload](/* Class* */ void* ins,
                                                                   const Arguments& args) {
    using Helper = ConvertingFuncCallHelper<
        std::pair<typename ConverterDecay<typename FuncTrait<Func>::ReturnType>::type,
                  typename ArgsTrait<Func>::Tail>>;

    return Helper::callInstanceFunc(f, static_cast<Class*>(ins), args, nothrow, throwForOverload);
  };
}

template <typename Class>
InstanceFunctionCallback bindInstanceFunc(
    std::function<Local<Value>(Class*, const Arguments& args)>&& func, bool, bool = false) {
  if (!func) return {};

  return [f = std::forward<std::function<Local<Value>(Class*, const Arguments& args)>>(func)](
             /* Class* */ void* thiz, const Arguments& args) {
    return f(static_cast<Class*>(thiz), args);
  };
}

template <typename Class>
InstanceFunctionCallback bindInstanceFunc(
    const std::function<Local<Value>(Class*, const Arguments& args)>& func, bool, bool = false) {
  return bindInstanceFunc(std::function<Local<Value>(Class*, const Arguments& args)>(func), false,
                          false);
}

template <typename Class>
InstanceFunctionCallback bindInstanceFunc(InstanceFunctionCallback&& func, bool, bool = false) {
  return std::move(func);
}

template <typename Class>
InstanceFunctionCallback bindInstanceFunc(const InstanceFunctionCallback& func, bool,
                                          bool = false) {
  return func;
}

template <typename Class, typename... Func>
InstanceFunctionCallback adaptOverloadedInstanceFunction(Func&&... functions) {
  std::vector funcs{bindInstanceFunc<Class>(std::forward<Func>(functions), false, true)...};
  return [overload = std::move(funcs)](/* Class* */ void* thiz,
                                       const Arguments& args) -> Local<Value> {
    for (size_t i = 0; i < sizeof...(Func); ++i) {
      try {
        return std::invoke(overload[i], static_cast<Class*>(thiz), args);
      } catch (const OverloadInvalidArguments&) {
        if (i == sizeof...(Func) - 1) {
          throw Exception("no valid overloaded function chosen");
        }
      }
    }
    return {};
  };
}

template <typename Class, typename Func>
std::enable_if_t<::script::converter::isConvertible<typename FuncTrait<Func>::ReturnType> &&
                     ArgsTrait<Func>::count == 1 &&
                     // if Arg<0> is base class of C
                     std::is_convertible_v<Class*, typename ArgsTrait<Func>::template Arg<0>>,
                 InstanceGetterCallback>
bindInstanceGet(Func&& get, bool nothrow) {
  return [g = std::forward<Func>(get), nothrow](/* Class* */ void* thiz) -> Local<Value> {
    using Type = typename FuncTrait<Func>::ReturnType;
    try {
      return TypeConverter<Type>::toScript(std::invoke(g, static_cast<Class*>(thiz)));
    } catch (Exception& e) {
      return handleException(e, nothrow);
    }
  };
}

template <typename C>
InstanceGetterCallback bindInstanceGet(std::function<Local<Value>(C*)>&& get, bool) {
  if (!get) return {};

  return [g = std::forward<std::function<Local<Value>(C*)>>(get)](
             /* C* */ void* thiz) { return g(static_cast<C*>(thiz)); };
}

template <typename C>
InstanceGetterCallback bindInstanceGet(InstanceGetterCallback&& g, bool) {
  return std::move(g);
}

template <typename C>
InstanceGetterCallback bindInstanceGet(const InstanceGetterCallback& g, bool) {
  return g;
}

template <typename C>
InstanceGetterCallback bindInstanceGet(const std::function<Local<Value>(C*)>& g, bool) {
  return bindInstanceGet(std::function<Local<Value>(void*)>(g), false);
}

template <typename Class, typename Func>
std::enable_if_t<std::is_same_v<typename FuncTrait<Func>::ReturnType, void> &&
                     ArgsTrait<Func>::count == 2 &&
                     // if Arg<0> is base class of C
                     std::is_convertible_v<Class*, typename ArgsTrait<Func>::template Arg<0>> &&
                     isArgsConvertible<typename ArgsTrait<Func>::Tail>,
                 InstanceSetterCallback>
bindInstanceSet(Func&& get, bool nothrow) {
  return [g = std::forward<Func>(get), nothrow](/* Class* */ void* thiz,
                                                const Local<Value>& value) -> void {
    using SecondArg = typename ConverterDecay<typename ArgsTrait<Func>::template Arg<1>>::type;

    std::optional<TypeHolder<SecondArg>> typeHolder;
    std::optional<SecondArg> arg;
    try {
      typeHolder.emplace(value);
      arg = typeHolder->template toCpp<SecondArg>();
    } catch (Exception& e) {
      handleException(e, nothrow);
    }
    std::invoke(g, static_cast<Class*>(thiz), *std::move(arg));
  };
}

template <typename Class>
InstanceSetterCallback bindInstanceSet(std::function<void(Class*, const Local<Value>& value)>&& set,
                                       bool) {
  if (!set) return {};

  return [s = std::forward<std::function<void(Class*, const Local<Value>& value)>>(set)](
             /* Class* */ void* thiz, const Local<Value>& value) {
    s(static_cast<Class*>(thiz), value);
  };
}

template <typename Class>
InstanceSetterCallback bindInstanceSet(
    const std::function<void(Class*, const Local<Value>& value)>& s, bool) {
  return bindInstanceSet(std::function<void(Class*, const Local<Value>& value)>(s), false);
}

template <typename C>
InstanceSetterCallback bindInstanceSet(InstanceSetterCallback&& s, bool) {
  return std::move(s);
}

template <typename C>
InstanceSetterCallback bindInstanceSet(const InstanceSetterCallback&& s, bool) {
  return s;
}

// BaseClass maybe super type of Class or same as Class.
template <typename Class, typename BaseClass, typename T>
std::enable_if_t<std::is_convertible_v<Class*, BaseClass*>&& ::script::converter::isConvertible<T>,
                 std::pair<InstanceGetterCallback, InstanceSetterCallback>>
bindInstanceProp(T BaseClass::*prop, bool nothrow) {
  if constexpr (!std::is_const_v<T>) {
    return {
        bindInstanceGet<Class>([prop](Class* thiz) -> T { return thiz->*prop; }, nothrow),
        bindInstanceSet<Class>([prop](Class* thiz, T val) -> void { thiz->*prop = val; }, nothrow)};
  } else {
    return {bindInstanceGet<Class>([prop](Class* thiz) -> T { return thiz->*prop; }, nothrow),
            nullptr};
  }
}

template <typename T, typename = std::void_t<decltype(&ClassConstructorHelper<T>::ctor)>>
InstanceConstructor bindConstructor() {
  return ClassConstructorHelper<T>::ctor();
}

constexpr bool kBindingNoThrowDefaultValue =
#ifdef SCRIPTX_NO_EXCEPTION_ON_BIND_FUNCTION
    true;
#else
    false;
#endif

template <typename RetType, typename... Args>
std::enable_if_t<::script::converter::isConvertible<RetType> &&
                     isArgsConvertible<std::tuple<Args...>>,
                 std::function<RetType(Args...)>>
createFunctionWrapperInner(const Local<Function>& function, const Local<Value>& thiz,
                           const std::tuple<Args...>*) {
  using EngineImpl = typename ImplType<ScriptEngine>::type;
  return std::function(
      [func = Global<Function>(function), receiver = Global<Value>(thiz),
       engine = EngineScope::currentEngineAs<EngineImpl>()](Args... args) -> RetType {
        // use EngineImpl to avoid possible dynamic_cast
        EngineScope scope(engine);
        auto ret = func.get().call(receiver.getValue(), args...);
        if constexpr (!std::is_void_v<RetType>) {
          return ::script::converter::Converter<RetType>::toCpp(ret);
        }
      });
}

template <typename FuncType>
inline std::function<FuncType> createFunctionWrapper(const Local<Function>& function,
                                                     const Local<Value>& thiz) {
  using FC = traits::FunctionTrait<FuncType>;
  return createFunctionWrapperInner<typename FC::ReturnType>(
      function, thiz, static_cast<typename FC::Arguments*>(nullptr));
}

}  // namespace script::internal

namespace script {

template <typename Func>
inline internal::type_t<Local<Function>,
                        decltype(internal::bindStaticFunc(std::declval<Func>(), false))>
Function::newFunction(Func&& callback, bool nothrow) {
  return newFunction(internal::bindStaticFunc(std::forward<Func>(callback), nothrow));
}

template <typename T>
inline internal::type_t<void, decltype(&internal::TypeConverter<T>::toScript)> Local<Object>::set(
    const Local<String>& key, T&& value) const {
  auto val = internal::TypeConverter<T>::toScript(std::forward<T>(value));
  // static_cast is crucial!!!
  // force the compiler to choose the non-template version
  // to avoid a recursive call
  set(key, static_cast<const Local<Value>&>(val));
}

template <typename StringLike, typename T, typename>
inline internal::type_t<void, decltype(&internal::TypeConverter<T>::toScript)> Local<Object>::set(
    StringLike&& keyStringLike, T&& value) const {
  auto val = internal::TypeConverter<T>::toScript(std::forward<T>(value));
  set(String::newString(std::forward<StringLike>(keyStringLike)),
      static_cast<const Local<Value>&>(val));
}

template <typename T>
inline internal::type_t<void, decltype(&internal::TypeConverter<T>::toScript)> Local<Array>::set(
    size_t index, T&& value) const {
  auto val = internal::TypeConverter<T>::toScript(std::forward<T>(value));
  set(index, static_cast<const Local<Value>&>(val));
}

template <typename T>
inline internal::type_t<void, decltype(&internal::TypeConverter<T>::toScript)>
InternalStoreHelper::set(T&& value) const {
  auto val = internal::TypeConverter<T>::toScript(std::forward<T>(value));
  set(static_cast<const Local<Value>&>(val));
}

template <typename... T>
inline internal::type_t<Local<Value>, decltype(&internal::TypeConverter<T>::toScript)...>
Local<Function>::call(const Local<Value>& thiz, T&&... args) const {
  return call(thiz, {internal::TypeConverter<T>::toScript(std::forward<T>(args))...});
}

template <typename FuncType>
std::function<FuncType> Local<Function>::wrapper(const Local<Value>& thiz) const {
  return internal::createFunctionWrapper<FuncType>(*this, thiz);
}

template <typename... T>
inline internal::type_t<Local<Object>, decltype(&internal::TypeConverter<T>::toScript)...>
Object::newObject(const Local<Value>& type, T&&... args) {
  return newObject(type, {internal::TypeConverter<T>::toScript(std::forward<T>(args))...});
}

inline Local<Object> Object::newObject(const Local<Value>& type,
                                       const std::vector<Local<Value>>& args) {
  return newObjectImpl(type, args.size(), args.data());
}

inline Local<Object> Object::newObject(const Local<Value>& type,
                                       const std::initializer_list<Local<Value>>& args) {
  return newObjectImpl(type, args.size(), args.begin());
}

inline Local<Array> Array::newArray(const std::vector<Local<Value>>& elements) {
  return newArrayImpl(elements.size(), elements.data());
}

inline Local<Array> Array::newArray(const std::initializer_list<Local<Value>>& elements) {
  return newArrayImpl(elements.size(), elements.begin());
}

template <typename... T>
inline internal::type_t<Local<Array>, decltype(&internal::TypeConverter<T>::toScript)...> Array::of(
    T&&... args) {
  return newArray({internal::TypeConverter<T>::toScript(std::forward<T>(args))...});
}

namespace internal {

#ifndef __cpp_char8_t
template <typename StringLike, StringLikeConcept(StringLike)>
std::string extractStringLike(StringLike&& str) {
  return std::string{std::forward<StringLike>(str)};
}
#else
// tag dispatch
inline std::string extractStringLike(const char* str) { return str; }
inline std::string extractStringLike(std::string str) { return str; }
inline std::string extractStringLike(std::string_view str) { return std::string{str}; }

inline std::string extractStringLike(const char8_t* str) {
  return reinterpret_cast<const char*>(str);
}
inline std::string extractStringLike(const std::u8string& str) {
  return std::string{reinterpret_cast<const char*>(str.c_str()), str.size()};
}
inline std::string extractStringLike(std::u8string_view str) {
  return std::string{reinterpret_cast<const char*>(str.data()), str.size()};
}
#endif
}  // namespace internal

template <typename StringLike, typename>
Exception::Exception(StringLike&& messageStringLike)
    : Exception(internal::extractStringLike(std::forward<StringLike>(messageStringLike))) {}

template <typename T>
inline internal::type_t<void, decltype(&internal::TypeConverter<T>::toScript)> ScriptEngine::set(
    const Local<String>& key, T&& value) {
  auto val = internal::TypeConverter<T>::toScript(std::forward<T>(value));
  set(key, static_cast<const Local<Value>&>(val));
}

template <typename StringLike, typename T, typename>
inline internal::type_t<void, decltype(&internal::TypeConverter<T>::toScript)> ScriptEngine::set(
    StringLike&& keyStringLike, T&& value) {
  auto val = internal::TypeConverter<T>::toScript(std::forward<T>(value));
  set(String::newString(std::forward<StringLike>(keyStringLike)),
      static_cast<const Local<Value>&>(val));
}

template <typename D, typename... T>
inline internal::type_t<Local<Object>, decltype(&internal::TypeConverter<T>::toScript)...>
ScriptEngine::newNativeClass(T&&... args) {
  return newNativeClass<D>({internal::TypeConverter<T>::toScript(std::forward<T>(args))...});
}

namespace internal {

class InstanceDefineBuilderState {
 public:
  void* thiz = nullptr;

  // instance
  InstanceConstructor constructor_{};
  std::vector<InstanceDefine::FunctionDefine> insFunctions_{};
  std::vector<InstanceDefine::PropertyDefine> insProperties_{};
};

template <typename T>
class InstanceDefineBuilder : public InstanceDefineBuilderState {
  template <typename...>
  using sfina = ClassDefineBuilder<T>&;

  ClassDefineBuilder<T>& thiz() {
    return *static_cast<ClassDefineBuilder<T>*>(InstanceDefineBuilderState::thiz);
  };

 protected:
  explicit InstanceDefineBuilder(ClassDefineBuilder<T>& thiz) {
    InstanceDefineBuilderState::thiz = &thiz;
  }

 public:
  ClassDefineBuilder<T>& constructor(InstanceConstructor constructor) {
    constructor_ = std::move(constructor);
    return thiz();
  }

  /**
   * A helper method to add constructor to class define
   * if Class T has constructor in signature of
   * 1. T::T(Local<Object>)
   * 2. T::T(Arguments)
   */
  ClassDefineBuilder<T>& constructor() {
    static_assert(ClassConstructorHelper<T>::value);
    constructor_ = internal::bindConstructor<T>();
    return thiz();
  }

  /**
   * A helper method to disallow construct of this Class. (It's constructor always throw exception)
   */
  ClassDefineBuilder<T>& constructor(std::nullptr_t) {
    constructor_ = [](const Arguments&) -> T* { return nullptr; };
    return thiz();
  }

  ClassDefineBuilder<T>& instanceFunction(std::string name, InstanceFunctionCallback func) {
    insFunctions_.push_back(
        typename InstanceDefine::FunctionDefine{std::move(name), std::move(func), {}});
    return thiz();
  }

  template <typename Func>
  sfina<decltype(internal::bindInstanceFunc<T>(std::declval<Func>(), false))> instanceFunction(
      std::string name, Func func, bool nothrow = kBindingNoThrowDefaultValue) {
    insFunctions_.push_back(typename InstanceDefine::FunctionDefine{
        std::move(name), internal::bindInstanceFunc<T>(std::move(func), nothrow), {}});
    return thiz();
  }

  template <typename G, typename S = InstanceSetterCallback>
  sfina<decltype(internal::bindInstanceGet<T>(std::declval<G>(), false)),
        decltype(internal::bindInstanceSet<T>(std::declval<S>(), false))>
  instanceProperty(std::string name, G&& getter, S&& setterCallback = nullptr,
                   bool nothrow = kBindingNoThrowDefaultValue) {
    insProperties_.push_back(typename InstanceDefine::PropertyDefine{
        std::move(name),
        internal::bindInstanceGet<T>(std::forward<G>(getter), nothrow),
        internal::bindInstanceSet<T>(std::forward<S>(setterCallback), nothrow),
        {}});
    return thiz();
  }

  template <typename S>
  sfina<decltype(internal::bindInstanceSet<T>(std::declval<S>(), false))> instanceProperty(
      std::string name, S&& setterCallback, bool nothrow = kBindingNoThrowDefaultValue) {
    insProperties_.push_back(typename InstanceDefine::PropertyDefine{
        std::move(name),
        nullptr,
        internal::bindInstanceSet<T>(std::forward<S>(setterCallback), nothrow),
        {}});
    return thiz();
  }

  template <typename P, typename BaseClass>
  sfina<decltype(internal::bindInstanceProp<T, BaseClass, P>(std::declval<P BaseClass::*>(),
                                                             false))>
  instanceProperty(std::string name, P BaseClass::*ptr,
                   bool nothrow = kBindingNoThrowDefaultValue) {
    auto prop = internal::bindInstanceProp<T, BaseClass, P>(ptr, nothrow);
    insProperties_.push_back(typename InstanceDefine::PropertyDefine{
        std::move(name), std::move(prop.first), std::move(prop.second), {}});
    return thiz();
  }

  template <typename Container, typename G, typename S = InstanceSetterCallback>
  ClassDefineBuilder<T>& mapInstanceProperties(const Container& names, G&& getter, S&& setterCallback = nullptr,
           bool nothrow = internal::kBindingNoThrowDefaultValue) {
    for (auto& prop : names)
    {
        const auto getFunc = [getter, prop] (T* w)
        {
            using FunctionTraits = script::internal::FuncTrait<G>;
            using Return = typename FunctionTraits::ReturnType;

            return script::converter::Converter<Return>::toScript (getter (w, prop.data()));
        };

        const auto setter = [setterCallback, prop] (T* w, const script::Local<script::Value>& value)
        {
            using FunctionTraits = script::internal::FuncTrait<S>;
            using ArgTraits = script::internal::traits::TupleTrait<typename FunctionTraits::Arguments>;
            using ValueArg = std::decay_t<typename ArgTraits::template Arg<2>>; // Get the setter functions's expected type

            setterCallback (w, prop.data(), script::converter::Converter<ValueArg>::toCpp (value));
        };

        instanceProperty (std::string(prop), getFunc, setter);
    }

    return thiz();
  }
};

// specialize for void
template <>
class InstanceDefineBuilder<void> : public InstanceDefineBuilderState {
 protected:
  explicit InstanceDefineBuilder(ClassDefineBuilder<void>&) {}

 public:
  // nothing here
};

}  // namespace internal

template <typename T>
class ClassDefineBuilder : public internal::InstanceDefineBuilder<T> {
  static_assert(std::is_void_v<T> || std::is_convertible_v<T*, ScriptClass*>,
                "T must be subclass of ScriptClass, "
                "and can be void if no instance is required.");
  template <typename...>
  using sfina = ClassDefineBuilder<T>&;

  std::string className_{};
  std::string nameSpace_{};

  // static
  std::vector<internal::StaticDefine::FunctionDefine> functions_{};
  std::vector<internal::StaticDefine::PropertyDefine> properties{};

 public:
  explicit ClassDefineBuilder(std::string className)
      : internal::InstanceDefineBuilder<T>(*this), className_(std::move(className)) {}

  ClassDefineBuilder<T>& nameSpace(std::string nameSpace) {
    nameSpace_ = std::move(nameSpace);
    return *this;
  }

  ClassDefineBuilder<T>& function(std::string name, FunctionCallback func) {
    functions_.push_back(internal::StaticDefine::FunctionDefine{
        std::move(name), std::forward<FunctionCallback>(func), {}});
    return *this;
  }

  template <typename Func>
  sfina<decltype(internal::bindStaticFunc(std::declval<Func>(), false))> function(
      std::string name, Func func, bool nothrow = internal::kBindingNoThrowDefaultValue) {
    functions_.push_back(internal::StaticDefine::FunctionDefine{
        std::move(name), internal::bindStaticFunc(std::forward<Func>(func), nothrow), {}});
    return *this;
  }

  template <typename G, typename S = SetterCallback>
  sfina<decltype(internal::bindStaticGet(std::declval<G>(), false)),
        decltype(internal::bindStaticSet(std::declval<S>(), false))>
  property(std::string name, G&& getter, S&& setterCallback = nullptr,
           bool nothrow = internal::kBindingNoThrowDefaultValue) {
    properties.push_back(internal::StaticDefine::PropertyDefine{
        std::move(name),
        internal::bindStaticGet(std::forward<G>(getter), nothrow),
        internal::bindStaticSet(std::forward<S>(setterCallback), nothrow),
        {}});
    return *this;
  }

  template <typename S>
  sfina<decltype(internal::bindStaticSet(std::declval<S>(), false))> property(std::string name,
                                                                              S&& setterCallback) {
    properties.push_back(internal::StaticDefine::PropertyDefine{
        std::move(name), nullptr, internal::bindStaticSet(std::forward<S>(setterCallback)), {}});
    return *this;
  }

  template <typename P>
  sfina<decltype(internal::bindStaticProp(std::declval<P*>(), false))> property(
      std::string name, P* ptr, bool nothrow = internal::kBindingNoThrowDefaultValue) {
    auto prop = internal::bindStaticProp(ptr, nothrow);
    properties.push_back(internal::StaticDefine::PropertyDefine{
        std::move(name), std::move(prop.first), std::move(prop.second), {}});
    return *this;
  }

  ClassDefine<T> build() {
    using Instance = internal::InstanceDefineBuilder<T>;
    // fill trace name
    for (auto&& f : functions_) {
      f.traceName = className_ + "::" + f.name;
    }
    for (auto&& p : properties) {
      p.traceName = className_ + "::" + p.name;
    }
    for (auto&& f : Instance::insFunctions_) {
      f.traceName = className_ + "::" + f.name;
    }
    for (auto&& p : Instance::insProperties_) {
      p.traceName = className_ + "::" + p.name;
    }
    ClassDefine<T> define(std::move(className_), std::move(nameSpace_),
                          internal::StaticDefine{std::move(functions_), std::move(properties)},
                          internal::InstanceDefine{
                              std::move(Instance::constructor_), std::move(Instance::insFunctions_),
                              std::move(Instance::insProperties_), internal::sizeof_helper_v<T>});
    return define;
  }

  template <typename A>
  friend class ::script::WrappedClassDefineBuilder;
};

template <typename T>
inline ClassDefineBuilder<T> defineClass(std::string name) {
  return ClassDefineBuilder<T>(std::move(name));
}

class NativeRegister {
  void (*registerFunc_)(const void* def, ScriptEngine* engine);
#ifdef __cpp_rtti
  void (*visitFunc_)(const void* def, ClassDefineVisitor& visitor);
#endif

  template <typename T>
  explicit NativeRegister(const ClassDefine<T>* define) : registerFunc_(nullptr), define_(define) {
    registerFunc_ = [](const void* def, ScriptEngine* engine) {
      engine->registerNativeClass(*static_cast<const ClassDefine<T>*>(def));
    };
#ifdef __cpp_rtti
    visitFunc_ = [](const void* def, ClassDefineVisitor& visitor) {
      static_cast<const ClassDefine<T>*>(def)->visit(visitor);
    };
#endif
  }

  template <typename T>
  friend class ClassDefine;

 public:
  const void* define_;

  /**
   * register the wrapped class define to engine.
   * this is equivalent to call
   * \code
   * engine->registerNativeClass(wrappedDefine);
   * \endcode
   * @param engine
   */
  void registerNativeClass(ScriptEngine* engine) const { registerFunc_(define_, engine); }

#ifdef __cpp_rtti
  /**
   * You must enable rtti feature to use this api.
   * @param visitor
   */
  void visit(ClassDefineVisitor& visitor) const { visitFunc_(define_, visitor); }
#endif
};

template <typename T>
inline NativeRegister ClassDefine<T>::getNativeRegister() const {
  return NativeRegister(this);
}

#ifdef __cpp_rtti

template <typename T>
void script::ClassDefine<T>::visit(script::ClassDefineVisitor& visitor) const {
  internal::ClassDefineState::visit(visitor);
}

#else

template <typename T>
void script::ClassDefine<T>::visit(script::ClassDefineVisitor& visitor) const {
  static_assert(!std::is_same_v<T, T>, "ClassDefine::visit api requires rtti feature be enabled.");
}

#endif

template <typename... T>
inline FunctionCallback adaptOverLoadedFunction(T&&... functions) {
  return internal::adaptOverLoadedFunction(std::forward<T>(functions)...);
}

template <typename C, typename... T>
inline InstanceFunctionCallback adaptOverloadedInstanceFunction(T&&... functions) {
  return internal::adaptOverloadedInstanceFunction<C>(std::forward<T>(functions)...);
}

}  // namespace script
