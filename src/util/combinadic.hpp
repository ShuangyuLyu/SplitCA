#pragma once

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>

using combinadic_encode_type = uint64_t;

// Split combinadic_nCr into two functions, one inline-able and another rarely called.
class combinadic_nCr_impl {
    static inline std::vector<std::vector<combinadic_encode_type>> table{{0}};

    static void calculate(uint32_t n) {
        while (n >= table.size()) {
            uint32_t depth = table.size();
            std::vector<combinadic_encode_type> row(depth + 1);
            const auto &last = table.back();
            row[0] = row[depth] = 1;
            for (uint32_t i = 1; i < depth; i++) {
                row[i] = last[i - 1] + last[i];
            }
            table.push_back(std::move(row));
        }
    }

 public:
    inline combinadic_encode_type operator()(uint32_t n, uint32_t r) const {
        if (n < r) [[unlikely]]
            return 0;
        if (n >= table.size()) [[unlikely]]
            calculate(n);
        return table[n][r];
    }
};
inline constexpr auto combinadic_nCr = combinadic_nCr_impl{};

using combinadic_tuple = std::vector<uint32_t>;
using combinadic_tuple_ref = std::span<uint32_t>;
using combinadic_tuple_view = std::span<const uint32_t>;

inline combinadic_tuple combinadic_begin(uint32_t size) {
    combinadic_tuple result(size);
    for (uint32_t i = 0; i < size; i++) {
        result[i] = i;
    }
    return result;
}

inline void combinadic_next(combinadic_tuple_ref tuple) {
    assert(!tuple.empty());
    uint32_t limit = tuple.size() - 1, ceiling = tuple[0];
    for (uint32_t i = 0; i < limit; ++i) {
        uint32_t entry = ceiling + 1;
        ceiling = tuple[i + 1];
        if (entry < ceiling) {
            tuple[i] = entry;
            return;
        }
        tuple[i] = i;
    }
    ++tuple[limit];
}

inline void combinadic_previous(combinadic_tuple_ref tuple) {
    assert(!tuple.empty());
    uint32_t limit = tuple.size();
    for (uint32_t i = 0; i < limit; ++i) {
        uint32_t entry = tuple[i];
        if (entry > i) {
            do {
                tuple[i] = --entry;
            } while (i-- > 0);
            return;
        }
    }
}

inline combinadic_encode_type combinadic_encode(combinadic_tuple_view tuple) {
    combinadic_encode_type result = 0;
    uint32_t limit = tuple.size();
    for (uint32_t i = 0; i < limit; ++i) {
        result += combinadic_nCr(tuple[i], i + 1);
    }
    return result;
}

inline combinadic_tuple combinadic_decode(combinadic_encode_type N, uint32_t k, uint32_t max) {
    combinadic_tuple result(k);
    for (int32_t i = k - 1; i >= 0; --i) {
        uint32_t L = i, R = max;
        while (L != R) {
            uint32_t mid = (L + R + 1) >> 1;
            if (combinadic_nCr(mid, i + 1) > N)
                R = mid - 1;
            else
                L = mid;
        }
        result[i] = L;
        N -= combinadic_nCr(L, i + 1);
    }
    return result;
}
