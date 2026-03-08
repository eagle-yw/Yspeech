#pragma once

#define YSPEECH_REGISTER_CAPABILITY(T) \
    static auto& _yspeech_reg_##T = ::yspeech::registered<T>
