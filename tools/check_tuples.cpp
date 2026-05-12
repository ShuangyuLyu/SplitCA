#include "util/bitset.hpp"
#include "util/combinadic.hpp"

#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cout << std::format("usage: {} <ca> <strength>\n", argv[0]);
        exit(1);
    }
    std::ifstream fca(argv[1]);
    int strength = atoi(argv[2]);

    std::vector<std::vector<int>> testcases;
    for (std::string buffer; std::getline(fca, buffer);) {
        std::istringstream ss(std::move(buffer));
        std::vector<int> tc;
        for (int x; ss >> x;)
            tc.push_back(x);
        testcases.push_back(std::move(tc));
    }
    std::vector<BitSet> bitsets;
    bitsets.reserve(testcases.size());
    for (const auto &tc : testcases) {
        BitSet bs(tc.size());
        for (int i = 0; i < tc.size(); ++i)
            bs[i] = tc[i];
        bitsets.push_back(std::move(bs));
    }

    const int nvar = testcases.front().size();
    const uint64_t M = combinadic_nCr(nvar, strength - 1) << (strength - 1);
    std::vector<BitSet> covered[2];
    covered[0].resize(M, BitSet(nvar));
    covered[1].resize(M, BitSet(nvar));

    for (int tci = 0; tci < testcases.size(); ++tci) {
        for (auto cols = combinadic_begin(strength - 1); cols.back() < nvar - 1;
             combinadic_next(cols)) {
            uint val = 0;
            for (auto col : cols)
                val = val << 1 | testcases[tci][col];
            auto offset = combinadic_encode(cols) << (strength - 1) | val;
            covered[0][offset].or_not(bitsets[tci]);
            covered[1][offset] |= (bitsets[tci]);
        }
    }

    uint64_t tuples_num = 0;
    for (auto cols = combinadic_begin(strength - 1); cols.back() < nvar - 1;
         combinadic_next(cols)) {
        for (uint val = 0, mx_val = 1 << (strength - 1); val < mx_val; ++val) {
            auto offset = combinadic_encode(cols) << (strength - 1) | val;
            for (int v = cols.back() + 1; v < nvar; ++v) {
                tuples_num += covered[0][offset][v];
                tuples_num += covered[1][offset][v];
            }
        }
    }
    std::cout << tuples_num << std::endl;
    return 0;
}
