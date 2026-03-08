module;

#include <nlohmann/json.hpp>

export module yspeech.error;

import std;

namespace yspeech {

export enum class ErrorLevel {
    Info,
    Warning,
    Error,
    Fatal
};

export enum class ErrorCode {
    Success = 0,
    Unknown = 1,
    
    InvalidConfig = 100,
    ConfigFileNotFound = 101,
    ConfigParseError = 102,
    ConfigValidationError = 103,
    
    OperatorNotFound = 200,
    OperatorInitFailed = 201,
    OperatorProcessFailed = 202,
    OperatorDeinitFailed = 203,
    OperatorTimeout = 204,
    
    ResourceNotFound = 300,
    ResourceLoadFailed = 301,
    ResourceReleaseFailed = 302,
    ResourceExhausted = 303,
    
    Timeout = 400,
    OperationTimeout = 401,
    
    NetworkError = 500,
    ConnectionFailed = 501,
    ConnectionReset = 502
};

export inline std::string error_code_to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::Success: return "Success";
        case ErrorCode::Unknown: return "Unknown";
        case ErrorCode::InvalidConfig: return "InvalidConfig";
        case ErrorCode::ConfigFileNotFound: return "ConfigFileNotFound";
        case ErrorCode::ConfigParseError: return "ConfigParseError";
        case ErrorCode::ConfigValidationError: return "ConfigValidationError";
        case ErrorCode::OperatorNotFound: return "OperatorNotFound";
        case ErrorCode::OperatorInitFailed: return "OperatorInitFailed";
        case ErrorCode::OperatorProcessFailed: return "OperatorProcessFailed";
        case ErrorCode::OperatorDeinitFailed: return "OperatorDeinitFailed";
        case ErrorCode::OperatorTimeout: return "OperatorTimeout";
        case ErrorCode::ResourceNotFound: return "ResourceNotFound";
        case ErrorCode::ResourceLoadFailed: return "ResourceLoadFailed";
        case ErrorCode::ResourceReleaseFailed: return "ResourceReleaseFailed";
        case ErrorCode::ResourceExhausted: return "ResourceExhausted";
        case ErrorCode::Timeout: return "Timeout";
        case ErrorCode::OperationTimeout: return "OperationTimeout";
        case ErrorCode::NetworkError: return "NetworkError";
        case ErrorCode::ConnectionFailed: return "ConnectionFailed";
        case ErrorCode::ConnectionReset: return "ConnectionReset";
        default: return "UnknownErrorCode";
    }
}

export inline std::string error_level_to_string(ErrorLevel level) {
    switch (level) {
        case ErrorLevel::Info: return "Info";
        case ErrorLevel::Warning: return "Warning";
        case ErrorLevel::Error: return "Error";
        case ErrorLevel::Fatal: return "Fatal";
        default: return "Unknown";
    }
}

export struct Error {
    std::string source;
    std::string component;
    std::string message;
    ErrorCode code = ErrorCode::Unknown;
    ErrorLevel level = ErrorLevel::Error;
    int attempt = 0;
    bool recovered = false;
    std::chrono::system_clock::time_point timestamp;
    nlohmann::json metadata;
    
    std::string to_string() const {
        auto time_t = std::chrono::system_clock::to_time_t(timestamp);
        std::tm tm = *std::localtime(&time_t);
        char time_buf[32];
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm);
        
        std::string metadata_str;
        if (!metadata.is_null()) {
            metadata_str = std::format(" [{}]", metadata.dump());
        }
        
        return std::format("[{}] {} [{}] {}({}): {}{}{}",
            time_buf,
            error_level_to_string(level),
            component,
            source,
            error_code_to_string(code),
            message,
            attempt > 0 ? std::format(" (attempt {})", attempt) : "",
            recovered ? " [RECOVERED]" : ""
        );
    }
    
    nlohmann::json to_json() const {
        auto time_t = std::chrono::system_clock::to_time_t(timestamp);
        std::tm tm = *std::localtime(&time_t);
        char time_buf[32];
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", &tm);
        
        return nlohmann::json{
            {"source", source},
            {"component", component},
            {"message", message},
            {"code", error_code_to_string(code)},
            {"level", error_level_to_string(level)},
            {"attempt", attempt},
            {"recovered", recovered},
            {"timestamp", time_buf},
            {"metadata", metadata}
        };
    }
};

}
