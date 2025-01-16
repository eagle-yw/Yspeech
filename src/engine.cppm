export module yspeech.Engine;

import std;

import yspeech.Common.Types;
import yspeech.Context;
import yspeech.Operator;
import yspeech.Operator.Vad;

namespace yspeech {
export class Engine {
public:
    Engine() {
        std::cout << "Engine created" << std::endl;
        operators_.emplace_back(OpVad());
    }

    ~Engine() {
        std::cout << "Engine destroyed" << std::endl;
    }

    auto run() -> void {
        std::cout << "Engine running" << std::endl;
        for(auto& op : operators_){
            auto context = Context();
            op.process(context);
        }
    }

    
private:
    std::vector<OperatorIface> operators_;
};

}
