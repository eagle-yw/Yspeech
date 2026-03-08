module;

#include <glog/logging.h>
#include <source_location>
#include <chrono>
#include <ctime>

export module yspeech.log;

import std;

namespace yspeech {

export inline void log_init(const char* program_name) {
    google::InitGoogleLogging(program_name);
}

export inline void log_shutdown() {
    google::ShutdownGoogleLogging();
}

namespace detail {

inline std::string format_location(const std::source_location& loc) {
    std::string file = loc.file_name();
    if (auto pos = file.find_last_of("/\\"); pos != std::string::npos) {
        file = file.substr(pos + 1);
    }
    return std::format("{}:{}", file, loc.line());
}

inline std::string format_time() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::tm tm = *std::localtime(&time);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::format("{}.{}", buf, ms.count());
}

}

export struct FmtLoc {
    std::string_view fmt;
    std::source_location loc;
    
    FmtLoc(const char* fmt, 
           std::source_location loc = std::source_location::current())
        : fmt(fmt), loc(loc) {}
};

export void log_debug(FmtLoc fmt_loc, auto&&... args) {
    DLOG(INFO) << std::format("[{}] [DEBUG] [{}] {}", 
                              detail::format_time(),
                              detail::format_location(fmt_loc.loc), 
                              std::vformat(fmt_loc.fmt, std::make_format_args(args...)));
}

export void log_info(FmtLoc fmt_loc, auto&&... args) {
    LOG(INFO) << std::format("[{}] [INFO] [{}] {}", 
                            detail::format_time(),
                            detail::format_location(fmt_loc.loc), 
                            std::vformat(fmt_loc.fmt, std::make_format_args(args...)));
}

export void log_warn(FmtLoc fmt_loc, auto&&... args) {
    LOG(WARNING) << std::format("[{}] [WARN] [{}] {}", 
                                detail::format_time(),
                                detail::format_location(fmt_loc.loc), 
                                std::vformat(fmt_loc.fmt, std::make_format_args(args...)));
}

export void log_error(FmtLoc fmt_loc, auto&&... args) {
    LOG(ERROR) << std::format("[{}] [ERROR] [{}] {}", 
                              detail::format_time(),
                              detail::format_location(fmt_loc.loc), 
                              std::vformat(fmt_loc.fmt, std::make_format_args(args...)));
}

}
