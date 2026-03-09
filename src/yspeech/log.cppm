module;

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/fmt.h>

export module yspeech.log;

import std;

namespace yspeech {

export enum class LogLevel {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    None = 4
};

export inline LogLevel current_log_level = LogLevel::Info;

export inline void set_log_level(LogLevel level) {
    current_log_level = level;
    auto logger = spdlog::default_logger();
    if (!logger) return;
    
    switch (level) {
        case LogLevel::Debug:
            logger->set_level(spdlog::level::debug);
            break;
        case LogLevel::Info:
            logger->set_level(spdlog::level::info);
            break;
        case LogLevel::Warn:
            logger->set_level(spdlog::level::warn);
            break;
        case LogLevel::Error:
            logger->set_level(spdlog::level::err);
            break;
        case LogLevel::None:
            logger->set_level(spdlog::level::off);
            break;
    }
}

export inline LogLevel get_log_level() {
    return current_log_level;
}

export inline void log_init(const char* program_name) {
    static std::once_flag init_flag;
    std::call_once(init_flag, []{
        auto console = spdlog::stdout_color_mt("yspeech");
        spdlog::set_default_logger(console);
        console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");
        set_log_level(LogLevel::Info);
    });
}

export inline void log_shutdown() {
    spdlog::shutdown();
}

export struct FmtLoc {
    std::string_view fmt;
    std::source_location loc;
    
    FmtLoc(const char* fmt, 
           std::source_location loc = std::source_location::current())
        : fmt(fmt), loc(loc) {}
};

namespace detail {

inline std::string format_loc(const std::source_location& loc) {
    std::string file = loc.file_name();
    if (auto pos = file.find_last_of("/\\"); pos != std::string::npos) {
        file = file.substr(pos + 1);
    }
    return std::format("[{}:{}]", file, loc.line());
}

template<typename... Args>
std::string format_msg(FmtLoc fmt_loc, Args&&... args) {
    try {
        return std::format("{} {}", format_loc(fmt_loc.loc), 
            std::vformat(fmt_loc.fmt, std::make_format_args(args...)));
    } catch (...) {
        return std::format("{} {}", format_loc(fmt_loc.loc), fmt_loc.fmt);
    }
}

}

export void log_debug(FmtLoc fmt_loc, auto&&... args) {
    if (current_log_level > LogLevel::Debug) return;
    spdlog::debug("{}", detail::format_msg(fmt_loc, std::forward<decltype(args)>(args)...));
}

export void log_info(FmtLoc fmt_loc, auto&&... args) {
    if (current_log_level > LogLevel::Info) return;
    spdlog::info("{}", detail::format_msg(fmt_loc, std::forward<decltype(args)>(args)...));
}

export void log_warn(FmtLoc fmt_loc, auto&&... args) {
    if (current_log_level > LogLevel::Warn) return;
    spdlog::warn("{}", detail::format_msg(fmt_loc, std::forward<decltype(args)>(args)...));
}

export void log_error(FmtLoc fmt_loc, auto&&... args) {
    if (current_log_level > LogLevel::Error) return;
    spdlog::error("{}", detail::format_msg(fmt_loc, std::forward<decltype(args)>(args)...));
}

}
