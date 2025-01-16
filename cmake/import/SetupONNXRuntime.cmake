# SetupONNXRuntime.cmake

# 封装下载和查找 ONNX Runtime 的逻辑
include(FetchContent)

function(setup_onnxruntime)
    # 解析参数
    set(options "")
    set(oneValueArgs VERSION PLATFORM)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(FETCHCONTENT_UPDATES_DISCONNECTED ${ARG_UPDATE})

    # 默认值
    if (NOT ARG_VERSION)
        set(ARG_VERSION "1.20.1")
    endif()
    if (NOT ARG_PLATFORM)
        set(ARG_PLATFORM "linux-x64-gpu")
    endif()
    
    if (NOT ARG_PKG_SUFFIX)
        set(ARG_PKG_SUFFIX "tgz")
    endif()

    set(onnxruntime_url "https://github.com/microsoft/onnxruntime/releases/download/v${ARG_VERSION}/onnxruntime-${ARG_PLATFORM}-${ARG_VERSION}.${ARG_PKG_SUFFIX}")
    message(STATUS "Downloading ONNX Runtime from ${onnxruntime_url}")

    # 下载预构建的 ONNX Runtime 二进制文件
    FetchContent_Declare(
        onnxruntime_binaries
        URL ${onnxruntime_url}
    )

    # 下载并解压
    FetchContent_MakeAvailable(onnxruntime_binaries)

    # 设置路径
    set(ONNXRUNTIME_ROOT "${onnxruntime_binaries_SOURCE_DIR}" CACHE PATH "Path to ONNX Runtime")

    # 查找头文件
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_c_api.h
              HINTS ${ONNXRUNTIME_ROOT}/include)

    # 查找库文件
    find_library(ONNXRUNTIME_LIBRARY
                 NAMES onnxruntime
                 HINTS ${ONNXRUNTIME_ROOT}/lib)

    # 检查是否找到
    if (NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
        message(FATAL_ERROR "ONNX Runtime not found. Please set ONNXRUNTIME_ROOT.")
    endif()

    # 输出找到的路径
    message(STATUS "Found ONNX Runtime include dir: ${ONNXRUNTIME_INCLUDE_DIR}")
    message(STATUS "Found ONNX Runtime library: ${ONNXRUNTIME_LIBRARY}")

    # 将结果存储在父作用域中
    set(ONNXRUNTIME_INCLUDE_DIR ${ONNXRUNTIME_INCLUDE_DIR} PARENT_SCOPE)
    set(ONNXRUNTIME_LIBRARY ${ONNXRUNTIME_LIBRARY} PARENT_SCOPE)

    include_directories(${ONNXRUNTIME_INCLUDE_DIR})
endfunction()