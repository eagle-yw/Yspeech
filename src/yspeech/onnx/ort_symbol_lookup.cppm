module;

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#include <dlfcn.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

export module yspeech.onnx.ort_symbol_lookup;

export namespace yspeech::ort_detail {

inline auto lookup_symbol(const char* symbol_name) noexcept -> void* {
#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    return dlsym(RTLD_DEFAULT, symbol_name);
#elif defined(_WIN32)
    const HMODULE modules[] = {
        GetModuleHandleA(nullptr),
        GetModuleHandleA("onnxruntime.dll"),
        GetModuleHandleA("onnxruntime_providers_shared.dll"),
        GetModuleHandleA("onnxruntime_providers_coreml.dll")
    };
    for (const auto module : modules) {
        if (module == nullptr) {
            continue;
        }
        if (auto* proc = GetProcAddress(module, symbol_name); proc != nullptr) {
            return reinterpret_cast<void*>(proc);
        }
    }
    return nullptr;
#else
    (void)symbol_name;
    return nullptr;
#endif
}

template <typename Fn>
auto lookup_symbol_fn(const char* symbol_name) noexcept -> Fn {
    return reinterpret_cast<Fn>(lookup_symbol(symbol_name));
}

} // namespace yspeech::ort_detail
