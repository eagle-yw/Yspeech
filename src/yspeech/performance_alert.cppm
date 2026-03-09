module;

export module yspeech.performance_alert;

import std;
import yspeech.types;

namespace yspeech {

export struct AlertRule {
    std::string id;
    std::string metric;
    double threshold;
    std::string comparison;
    std::string severity;
    
    static AlertRule rtf_high(double threshold = 1.0) {
        return AlertRule{
            .id = "rtf_high",
            .metric = "rtf",
            .threshold = threshold,
            .comparison = ">",
            .severity = "warning"
        };
    }
    
    static AlertRule memory_high(double threshold_mb = 500.0) {
        return AlertRule{
            .id = "memory_high",
            .metric = "peak_memory_mb",
            .threshold = threshold_mb,
            .comparison = ">",
            .severity = "warning"
        };
    }
    
    static AlertRule operator_slow(const std::string& op_id, double threshold_ms = 100.0) {
        return AlertRule{
            .id = "operator_slow_" + op_id,
            .metric = "operator_time_ms",
            .threshold = threshold_ms,
            .comparison = ">",
            .severity = "warning"
        };
    }
};

export class PerformanceAlerter {
public:
    using AlertCallback = std::function<void(const std::string& alert_id, const std::string& message)>;
    
    void add_rule(const AlertRule& rule) {
        rules_.push_back(rule);
    }
    
    void remove_rule(const std::string& rule_id) {
        rules_.erase(
            std::remove_if(rules_.begin(), rules_.end(),
                [&rule_id](const AlertRule& r) { return r.id == rule_id; }),
            rules_.end()
        );
    }
    
    void set_callback(AlertCallback callback) {
        callback_ = std::move(callback);
    }
    
    void check(const ProcessingStats& stats) {
        for (const auto& rule : rules_) {
            double value = get_metric_value(stats, rule.metric, rule.id);
            
            if (value == 0.0 && rule.metric != "rtf") {
                continue;
            }
            
            bool triggered = false;
            if (rule.comparison == ">") {
                triggered = value > rule.threshold;
            } else if (rule.comparison == "<") {
                triggered = value < rule.threshold;
            } else if (rule.comparison == "==") {
                triggered = std::abs(value - rule.threshold) < 0.0001;
            }
            
            if (triggered && callback_) {
                std::string message = std::format(
                    "[{}] {} = {:.2f} {} threshold {:.2f}",
                    rule.severity, rule.metric, value, rule.comparison, rule.threshold
                );
                callback_(rule.id, message);
            }
        }
    }
    
    void clear_rules() {
        rules_.clear();
    }
    
    const std::vector<AlertRule>& rules() const {
        return rules_;
    }

private:
    std::vector<AlertRule> rules_;
    AlertCallback callback_;
    
    double get_metric_value(const ProcessingStats& stats, 
                            const std::string& metric,
                            const std::string& rule_id) {
        if (metric == "rtf") {
            return stats.rtf;
        } else if (metric == "peak_memory_mb") {
            return stats.peak_memory_mb;
        } else if (metric == "avg_cpu_percent") {
            return stats.avg_cpu_percent;
        } else if (metric == "total_processing_time_ms") {
            return stats.total_processing_time_ms;
        } else if (metric == "operator_time_ms") {
            if (rule_id.find("operator_slow_") == 0) {
                std::string op_id = rule_id.substr(14);
                auto it = stats.operator_timings.find(op_id);
                if (it != stats.operator_timings.end()) {
                    return it->second.avg_time_ms;
                }
            }
        }
        return 0.0;
    }
};

}
