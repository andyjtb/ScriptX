cmake_minimum_required(VERSION 3.23)

target_sources(ScriptX PRIVATE

        ${CMAKE_CURRENT_LIST_DIR}/HermesEngine.cc
        ${CMAKE_CURRENT_LIST_DIR}/HermesEngine-Native.cc
        ${CMAKE_CURRENT_LIST_DIR}/HermesEngine.h
        ${CMAKE_CURRENT_LIST_DIR}/HermesException.cc
        ${CMAKE_CURRENT_LIST_DIR}/HermesLocalReference.cc
        ${CMAKE_CURRENT_LIST_DIR}/HermesNative.cc
        ${CMAKE_CURRENT_LIST_DIR}/HermesNative.hpp
        ${CMAKE_CURRENT_LIST_DIR}/HermesReference.hpp
        ${CMAKE_CURRENT_LIST_DIR}/HermesScope.cc
        ${CMAKE_CURRENT_LIST_DIR}/HermesTypedArrayApi.cc
        ${CMAKE_CURRENT_LIST_DIR}/HermesUtils.cc
        ${CMAKE_CURRENT_LIST_DIR}/HermesValue.cc
)

