module;

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/task.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <fstream>
#endif

export module yspeech.resource_monitor;

import std;

namespace yspeech {

export struct ResourceUsage {
    double cpu_percent = 0.0;
    size_t memory_mb = 0;
    size_t peak_memory_mb = 0;
    
    std::string to_string() const {
        return std::format("CPU: {:.1f}%, Memory: {} MB (Peak: {} MB)",
            cpu_percent, memory_mb, peak_memory_mb);
    }
};

export class ResourceMonitor {
public:
    static ResourceUsage get_current() {
        ResourceUsage usage;
        
        usage.memory_mb = get_memory_usage_mb();
        usage.cpu_percent = get_cpu_percent();
        
        return usage;
    }
    
    static size_t get_memory_usage_mb() {
#ifdef __APPLE__
        task_basic_info info;
        mach_msg_type_number_t count = TASK_BASIC_INFO_COUNT;
        kern_return_t kr = task_info(mach_task_self(), TASK_BASIC_INFO,
                                      reinterpret_cast<task_info_t>(&info), &count);
        if (kr == KERN_SUCCESS) {
            return info.resident_size / (1024 * 1024);
        }
#elif defined(__linux__)
        std::ifstream file("/proc/self/status");
        std::string line;
        while (std::getline(file, line)) {
            if (line.find("VmRSS:") == 0) {
                size_t kb;
                std::sscanf(line.c_str(), "VmRSS: %zu kB", &kb);
                return kb / 1024;
            }
        }
#endif
        return 0;
    }
    
    static double get_cpu_percent() {
#ifdef __APPLE__
        task_thread_times_info thread_info;
        mach_msg_type_number_t count = TASK_THREAD_TIMES_INFO_COUNT;
        kern_return_t kr = task_info(mach_task_self(), TASK_THREAD_TIMES_INFO,
                                      reinterpret_cast<task_info_t>(&thread_info), &count);
        if (kr != KERN_SUCCESS) {
            return 0.0;
        }
        
        auto now = std::chrono::steady_clock::now();
        static auto last_time = now;
        static double last_user_time = 0.0;
        static double last_system_time = 0.0;
        
        double user_time = thread_info.user_time.seconds + thread_info.user_time.microseconds / 1000000.0;
        double system_time = thread_info.system_time.seconds + thread_info.system_time.microseconds / 1000000.0;
        
        auto elapsed = std::chrono::duration<double>(now - last_time).count();
        if (elapsed < 0.1) {
            return 0.0;
        }
        
        double user_delta = user_time - last_user_time;
        double system_delta = system_time - last_system_time;
        
        last_time = now;
        last_user_time = user_time;
        last_system_time = system_time;
        
        return ((user_delta + system_delta) / elapsed) * 100.0;
#elif defined(__linux__)
        struct rusage usage;
        getrusage(RUSAGE_SELF, &usage);
        
        auto now = std::chrono::steady_clock::now();
        static auto last_time = now;
        static double last_user_time = 0.0;
        static double last_system_time = 0.0;
        
        double user_time = usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1000000.0;
        double system_time = usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1000000.0;
        
        auto elapsed = std::chrono::duration<double>(now - last_time).count();
        if (elapsed < 0.1) {
            return 0.0;
        }
        
        double user_delta = user_time - last_user_time;
        double system_delta = system_time - last_system_time;
        
        last_time = now;
        last_user_time = user_time;
        last_system_time = system_time;
        
        return ((user_delta + system_delta) / elapsed) * 100.0;
#else
        return 0.0;
#endif
    }
    
    static void start_monitoring(int interval_ms = 100) {
        if (running_) return;
        
        running_ = true;
        cpu_samples_ = 0;
        cpu_sum_ = 0.0;
        peak_cpu_ = 0.0;
        
        monitor_thread_ = std::thread([interval_ms]() {
            while (running_) {
                auto usage = get_current();
                
                peak_memory_mb_.store(
                    std::max(peak_memory_mb_.load(), usage.memory_mb),
                    std::memory_order_relaxed
                );
                
                if (usage.cpu_percent > 0) {
                    cpu_sum_ += usage.cpu_percent;
                    cpu_samples_++;
                    double current_peak = peak_cpu_.load(std::memory_order_relaxed);
                    while (usage.cpu_percent > current_peak) {
                        if (peak_cpu_.compare_exchange_weak(current_peak, usage.cpu_percent,
                            std::memory_order_relaxed)) {
                            break;
                        }
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            }
        });
    }
    
    static void stop_monitoring() {
        running_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
    }
    
    static ResourceUsage get_peak() {
        ResourceUsage usage;
        usage.memory_mb = peak_memory_mb_.load(std::memory_order_relaxed);
        usage.peak_memory_mb = usage.memory_mb;
        usage.cpu_percent = get_avg_cpu();
        return usage;
    }
    
    static double get_avg_cpu() {
        if (cpu_samples_ == 0) return 0.0;
        return cpu_sum_ / cpu_samples_;
    }
    
    static double get_peak_cpu() {
        return peak_cpu_.load(std::memory_order_relaxed);
    }
    
    static void reset_peak() {
        peak_memory_mb_.store(0, std::memory_order_relaxed);
        peak_cpu_.store(0.0, std::memory_order_relaxed);
        cpu_samples_ = 0;
        cpu_sum_ = 0.0;
    }

private:
    static inline std::atomic<bool> running_{false};
    static inline std::atomic<size_t> peak_memory_mb_{0};
    static inline std::atomic<double> peak_cpu_{0.0};
    static inline std::atomic<size_t> cpu_samples_{0};
    static inline std::atomic<double> cpu_sum_{0.0};
    static inline std::thread monitor_thread_;
};

}
