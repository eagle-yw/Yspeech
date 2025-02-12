export module yspeech.Context;

import std;

namespace yspeech {


export class Context {
public:
    Context() = default;
    ~Context() = default;
    auto getOpContext(std::string& op_name) -> std::any {
        auto it = op_contexts_.find(op_name);
        if (it == op_contexts_.end()) {
            throw std::out_of_range("Operation context not found for the given name.");
        }        
        return it->second;
    }
    auto opContext(std::string& op_name, std::any op_ctx) -> void {
        op_contexts_[op_name]=op_ctx;
    }
private:
    std::map<std::string, std::any> op_contexts_;
}; //class Context end;

} // namespace yspeech end;