export module yspeech.core:context;

import std;

namespace yspeech {

export class Context {
public:
    Context() = default;
    ~Context() = default;

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    template <typename T>
    void set(const std::string& key, T&& value) {
        std::unique_lock lock(mutex_);
        data_[key] = std::forward<T>(value);
    }

    template <typename T>
    T& get(const std::string& key) {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
            throw std::out_of_range(std::format("Context key not found: {}", key));
        }
        return std::any_cast<T&>(it->second);
    }
    
    bool contains(const std::string& key) const {
        std::shared_lock lock(mutex_);
        return data_.contains(key);
    }
    
    std::any get_any(const std::string& key) const {
        std::shared_lock lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) {
             return {};
        }
        return it->second;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::any> data_;
};

}
