module;

#include <nlohmann/json.hpp>

export module yspeech.pipeline_config;

import std;

namespace yspeech {

namespace detail {

inline std::string value_to_string(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    } else if (value.is_number_integer()) {
        return std::to_string(value.get<int64_t>());
    } else if (value.is_number_float()) {
        return std::to_string(value.get<double>());
    } else if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    } else if (value.is_null()) {
        return "";
    }
    return value.dump();
}

inline std::string resolve_variables(const std::string& value, const nlohmann::json& global_props) {
    std::string result = value;
    std::regex pattern(R"(\$\{(\w+)\})");
    
    std::smatch match;
    while (std::regex_search(result, match, pattern)) {
        std::string var_name = match[1].str();
        std::string replacement;
        
        if (global_props.contains(var_name)) {
            replacement = value_to_string(global_props[var_name]);
        } else if (const char* env = std::getenv(var_name.c_str())) {
            replacement = env;
        }
        
        result.replace(match.position(), match.length(), replacement);
    }
    
    return result;
}

inline nlohmann::json resolve_json_variables(const nlohmann::json& j, const nlohmann::json& global_props) {
    nlohmann::json result = j;
    if (j.is_string()) {
        result = resolve_variables(j.get<std::string>(), global_props);
    } else if (j.is_object()) {
        for (auto& [key, val] : j.items()) {
            result[key] = resolve_json_variables(val, global_props);
        }
    } else if (j.is_array()) {
        for (size_t i = 0; i < j.size(); ++i) {
            result[i] = resolve_json_variables(j[i], global_props);
        }
    }
    return result;
}

struct ConfigValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

inline ConfigValidationResult validate_ops(const nlohmann::json& ops, const std::string& prefix = "") {
    ConfigValidationResult result;
    
    if (!ops.is_array()) {
        result.valid = false;
        result.errors.push_back(prefix + "Field 'ops' must be an array");
        return result;
    }
    
    if (ops.empty()) {
        result.warnings.push_back(prefix + "Field 'ops' is empty");
    }
    
    for (size_t i = 0; i < ops.size(); ++i) {
        const auto& op = ops[i];
        std::string op_prefix = prefix + "ops[" + std::to_string(i) + "]: ";
        
        if (!op.contains("id")) {
            result.valid = false;
            result.errors.push_back(op_prefix + "missing required field 'id'");
        } else if (!op["id"].is_string()) {
            result.valid = false;
            result.errors.push_back(op_prefix + "field 'id' must be a string");
        }
        
        if (!op.contains("name")) {
            result.valid = false;
            result.errors.push_back(op_prefix + "missing required field 'name'");
        } else if (!op["name"].is_string()) {
            result.valid = false;
            result.errors.push_back(op_prefix + "field 'name' must be a string");
        }
        
        if (op.contains("depends_on") && !op["depends_on"].is_array()) {
            result.valid = false;
            result.errors.push_back(op_prefix + "field 'depends_on' must be an array");
        }
        
        if (op.contains("params") && !op["params"].is_object()) {
            result.valid = false;
            result.errors.push_back(op_prefix + "field 'params' must be an object");
        }
        
        if (op.contains("capabilities") && !op["capabilities"].is_array()) {
            result.valid = false;
            result.errors.push_back(op_prefix + "field 'capabilities' must be an array");
        }
        
        if (op.contains("parallel") && !op["parallel"].is_boolean()) {
            result.valid = false;
            result.errors.push_back(op_prefix + "field 'parallel' must be a boolean");
        }
    }
    
    return result;
}

inline ConfigValidationResult validate_stage(const nlohmann::json& stage, size_t index) {
    ConfigValidationResult result;
    std::string prefix = "pipelines[" + std::to_string(index) + "]: ";
    
    if (!stage.contains("id")) {
        result.valid = false;
        result.errors.push_back(prefix + "missing required field 'id'");
    } else if (!stage["id"].is_string()) {
        result.valid = false;
        result.errors.push_back(prefix + "field 'id' must be a string");
    }
    
    if (!stage.contains("ops")) {
        result.valid = false;
        result.errors.push_back(prefix + "missing required field 'ops'");
    } else {
        auto ops_result = validate_ops(stage["ops"], prefix);
        if (!ops_result.valid) {
            result.valid = false;
            result.errors.insert(result.errors.end(), ops_result.errors.begin(), ops_result.errors.end());
        }
        result.warnings.insert(result.warnings.end(), ops_result.warnings.begin(), ops_result.warnings.end());
    }
    
    if (stage.contains("max_concurrency") && !stage["max_concurrency"].is_number_integer()) {
        result.valid = false;
        result.errors.push_back(prefix + "field 'max_concurrency' must be an integer");
    }
    
    if (stage.contains("input")) {
        if (!stage["input"].is_object()) {
            result.valid = false;
            result.errors.push_back(prefix + "field 'input' must be an object");
        } else {
            if (stage["input"].contains("key") && !stage["input"]["key"].is_string()) {
                result.valid = false;
                result.errors.push_back(prefix + "field 'input.key' must be a string");
            }
            if (stage["input"].contains("chunk_size") && !stage["input"]["chunk_size"].is_number_integer()) {
                result.valid = false;
                result.errors.push_back(prefix + "field 'input.chunk_size' must be an integer");
            }
        }
    }
    
    if (stage.contains("output")) {
        if (!stage["output"].is_object()) {
            result.valid = false;
            result.errors.push_back(prefix + "field 'output' must be an object");
        } else {
            if (stage["output"].contains("key") && !stage["output"]["key"].is_string()) {
                result.valid = false;
                result.errors.push_back(prefix + "field 'output.key' must be a string");
            }
        }
    }
    
    return result;
}

inline ConfigValidationResult validate_config(const nlohmann::json& config) {
    ConfigValidationResult result;
    
    if (config.contains("pipelines")) {
        if (!config["pipelines"].is_array()) {
            result.valid = false;
            result.errors.push_back("Field 'pipelines' must be an array");
            return result;
        }
        
        if (config["pipelines"].empty()) {
            result.valid = false;
            result.errors.push_back("Field 'pipelines' cannot be empty");
            return result;
        }
        
        for (size_t i = 0; i < config["pipelines"].size(); ++i) {
            auto stage_result = validate_stage(config["pipelines"][i], i);
            if (!stage_result.valid) {
                result.valid = false;
                result.errors.insert(result.errors.end(), stage_result.errors.begin(), stage_result.errors.end());
            }
            result.warnings.insert(result.warnings.end(), stage_result.warnings.begin(), stage_result.warnings.end());
        }
    } else {
        result.valid = false;
        result.errors.push_back("Config must contain 'pipelines' field");
    }
    
    if (config.contains("global")) {
        if (!config["global"].is_object()) {
            result.valid = false;
            result.errors.push_back("Field 'global' must be an object");
        } else {
            if (config["global"].contains("properties") && !config["global"]["properties"].is_object()) {
                result.valid = false;
                result.errors.push_back("Field 'global.properties' must be an object");
            }
            if (config["global"].contains("capabilities") && !config["global"]["capabilities"].is_array()) {
                result.valid = false;
                result.errors.push_back("Field 'global.capabilities' must be an array");
            }
        }
    }
    
    return result;
}

}

export struct StageInputConfig {
    std::string key;
    size_t chunk_size = 1600;
};

export struct StageOutputConfig {
    std::string key;
};

export struct OperatorConfig {
    std::string id;
    std::string name;
    nlohmann::json params;
    nlohmann::json capabilities;
    std::vector<std::string> depends_on;
    bool parallel = false;
    nlohmann::json error_handling;
};

export class PipelineStageConfig {
public:
    PipelineStageConfig() = default;
    
    static PipelineStageConfig from_json(const nlohmann::json& j, const nlohmann::json& global_props = {}) {
        PipelineStageConfig cfg;
        
        if (j.contains("id")) {
            cfg.id_ = j["id"].get<std::string>();
        }
        if (j.contains("name")) {
            cfg.name_ = j["name"].get<std::string>();
        }
        if (j.contains("max_concurrency")) {
            cfg.max_concurrency_ = j["max_concurrency"].get<size_t>();
        }
        
        if (j.contains("input")) {
            if (j["input"].contains("key")) {
                cfg.input_.key = j["input"]["key"].get<std::string>();
            }
            if (j["input"].contains("chunk_size")) {
                cfg.input_.chunk_size = j["input"]["chunk_size"].get<size_t>();
            }
        }
        
        if (j.contains("output")) {
            if (j["output"].contains("key")) {
                cfg.output_.key = j["output"]["key"].get<std::string>();
            }
        }
        
        if (j.contains("ops")) {
            for (const auto& op : j["ops"]) {
                OperatorConfig op_cfg;
                op_cfg.id = op["id"].get<std::string>();
                op_cfg.name = op["name"].get<std::string>();
                
                if (op.contains("params")) {
                    op_cfg.params = detail::resolve_json_variables(op["params"], global_props);
                }
                if (op.contains("capabilities")) {
                    op_cfg.capabilities = op["capabilities"];
                }
                if (op.contains("depends_on")) {
                    for (const auto& dep : op["depends_on"]) {
                        op_cfg.depends_on.push_back(dep.get<std::string>());
                    }
                }
                if (op.contains("parallel")) {
                    op_cfg.parallel = op["parallel"].get<bool>();
                }
                if (op.contains("error_handling")) {
                    op_cfg.error_handling = op["error_handling"];
                }
                
                cfg.ops_.push_back(std::move(op_cfg));
            }
        }
        
        return cfg;
    }
    
    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }
    size_t max_concurrency() const { return max_concurrency_; }
    const StageInputConfig& input() const { return input_; }
    const StageOutputConfig& output() const { return output_; }
    const std::vector<OperatorConfig>& ops() const { return ops_; }

private:
    std::string id_;
    std::string name_;
    size_t max_concurrency_ = 8;
    StageInputConfig input_;
    StageOutputConfig output_;
    std::vector<OperatorConfig> ops_;
};

export class PipelineConfig {
public:
    PipelineConfig() = default;

    static PipelineConfig from_file(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) {
            throw std::runtime_error("Failed to open config file: " + path);
        }
        
        nlohmann::json config;
        try {
            f >> config;
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Failed to parse config json: ") + e.what());
        }
        
        return from_json(config);
    }

    static PipelineConfig from_json(const nlohmann::json& config) {
        auto validation = detail::validate_config(config);
        if (!validation.valid) {
            std::string msg = "Config validation failed:\n";
            for (const auto& err : validation.errors) {
                msg += "  - " + err + "\n";
            }
            throw std::runtime_error(msg);
        }
        
        PipelineConfig cfg;
        
        if (config.contains("name")) {
            cfg.name_ = config["name"].get<std::string>();
        }
        if (config.contains("version")) {
            cfg.version_ = config["version"].get<std::string>();
        }
        
        if (config.contains("global")) {
            if (config["global"].contains("properties")) {
                cfg.properties_ = config["global"]["properties"];
            }
            if (config["global"].contains("capabilities")) {
                cfg.capabilities_ = config["global"]["capabilities"];
            }
        }

        if (config.contains("pipelines")) {
            for (const auto& stage : config["pipelines"]) {
                cfg.stages_.push_back(PipelineStageConfig::from_json(stage, cfg.properties_));
            }
        }
        
        return cfg;
    }

    const std::string& name() const { return name_; }
    const std::string& version() const { return version_; }
    const nlohmann::json& properties() const { return properties_; }
    const nlohmann::json& capabilities() const { return capabilities_; }
    const std::vector<PipelineStageConfig>& stages() const { return stages_; }
    
    bool is_single_stage() const { return stages_.size() == 1; }
    
    const PipelineStageConfig& stage(size_t index) const {
        return stages_.at(index);
    }
    
    size_t stage_count() const { return stages_.size(); }

    nlohmann::json resolve_params(const nlohmann::json& params) const {
        return detail::resolve_json_variables(params, properties_);
    }

    nlohmann::json resolve_capability_params(const nlohmann::json& params) const {
        return detail::resolve_json_variables(params, properties_);
    }

private:
    std::string name_;
    std::string version_;
    nlohmann::json properties_;
    nlohmann::json capabilities_;
    std::vector<PipelineStageConfig> stages_;
};

}
