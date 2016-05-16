#pragma once

#include <cstddef>
#include <cstdint>

template <typename T>
class tabulation
{};

template <>
class tabulation<uint64_t>
{
public:
    tabulation() {}
    tabulation(const tabulation&) {}

    size_t operator()(uint64_t x) const {
        return
            t1[static_cast<uint8_t>(x)] ^
            t2[static_cast<uint8_t>(x>> 8)] ^
            t3[static_cast<uint8_t>(x>>16)] ^
            t4[static_cast<uint8_t>(x>>24)] ^
            t5[static_cast<uint8_t>(x>>32)] ^
            t6[static_cast<uint8_t>(x>>40)] ^
            t7[static_cast<uint8_t>(x>>48)] ^
            t8[static_cast<uint8_t>(x>>56)];
    }

private:
    static const size_t
        t1[256], t2[256], t3[256], t4[256],
        t5[256], t6[256], t7[256], t8[256];
};

