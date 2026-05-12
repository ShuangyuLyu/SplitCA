#pragma once

#include <cstring>
#include <iostream>
#include <ostream>
#include <vector>
#include <random>
#include <memory>
#include "io/cnf.hpp"
#include "io/ddnnf.hpp"
#include "util/bitset.hpp"
#include "util/combinadic.hpp"
#include "util/tuple_vector.hpp"
#include "cli/args.hpp"

#include <minisat_ext/BlackBoxSolver.h>
#include <minisat_ext/Ext.h>

struct TuiState;  // forward declaration

namespace expandor {
using std::vector;

class Expandor {

    friend class Optimizer;

 public:
    Expandor (const CNF *cnf, DDNNF *ddnnf, const Argument &arg);
    ~Expandor ();
    void Expand ();
    vector<vector<int>> GetFinalCA ();
    void Generate_1wise_CA ();
    void Generate_2wise_CA ();
    void output_to_file (const std::string &result_path);

    void Optimize(int stop_length, int left = -1);

    void set_tui_state(TuiState *state);
    void set_log_stream(std::ostream *stream) { log_stream_ = stream ? stream : &std::cout; }

  private:

    TuiState *tui_state_ = nullptr;
    std::ostream *log_stream_ = &std::cout;
    size_t gen_tc_baseline_ = 0;  // new_testcase.size() at start of current generation phase

    const CNF *cnf;
    DDNNF *ddnnf;
    int seed, strength;  
    int candidate_set_size = 100;
    int thread_num = 1;
    std::mt19937_64 gen;
    const int nvar;
    std::vector<std::unique_ptr<ExtMinisat::SamplingSolver>> cdcl_samplers;
    
    bool use_addition_tc = 1;

    vector<vector<int>> old_testcase;
    vector<vector<int>> new_testcase;
    vector<BitSet> covered_last_strength_bitmap;
    vector<BitSet> covered_now_strength_bitmap;
    vector<int> count_each_var_uncovered[2], count_each_var_covered[2];
    vector<vector<int>> candidate_set;
    vector<std::pair<int, int>> gain;
    TupleVector tuples_U, uncovered_tuples;

    void get_all_2wise_tuples ();
    int generate_testcase ();
    int get_gain (const vector<int> &tc);
    void update_t_tuple_info (const vector<int> &testcase, bool setmap = true);
    void replenish_testcase ();

    int update_1wise_info (const vector<int> &testcase);
    int get_gain_1wise (const vector<int> &tc);
    std::vector<int> generate_testcase_1wise (int lit);
    std::vector<std::vector<int>> sample_with_solver(const std::vector<int> &assumps, int sample_num);
    std::vector<int> sample_one_with_solver(const std::vector<int> &assumps);

    vector<BitSet> CA, CA_left, CA_right;
    TupleVector t_clauses;

    void reinit_ca(); // reinitialize CA
    bool check_part_invalid(const t_tuple &tp) const; // check if last-wise tuple invalid
    bool check_clauses_invalid(const t_tuple &tp) const; // quick check cnf clauses
    size_t get_remaining_valid_tuples_3wise(); // get remaing valid tuples (recalc tuples_U)
    size_t get_remaining_valid_tuples_4wise_group(int left_half_vars);
    size_t get_remaining_valid_tuples_4wise_group_left4(const std::vector<int> &all_ca_indices, int left_end);
    size_t get_remaining_valid_tuples_4wise_group_left3(int left_end, int right_begin, int right_cnt);
    size_t get_remaining_valid_tuples_4wise_group_left2(int left_end, int right_begin, int right_cnt);
    size_t get_remaining_valid_tuples_4wise_group_left1(int left_end, int right_begin, int right_cnt);
    size_t get_remaining_valid_tuples_4wise_group_left0(const std::vector<int> &all_ca_indices, int right_begin, int right_cnt);
    void expand_4wise_partitioned();
    void refresh_4wise_covered(size_t begin_new_testcase);
    void set_covered_now_strength_bitmap(const std::vector<int> &ca_indices, const std::vector<BitSet> &ca_pool, int cols_strength, int nvar_local); // covered_now_strength_bitmap
  
    uint64_t GetBase (int t, int n, const vector<int> &vec) const {
        if (t == 2) return (2ll * n - vec[0] - 1) * vec[0] / 2 + vec[1] - vec[0] - 1;
        long long res = combinadic_nCr(n, t) - combinadic_nCr(n - vec[0], t);
        vector<int> v(t - 1);
        for (int i = 0; i < t - 1; i++) v[i] = vec[i + 1] - vec[0] - 1;
        return res + GetBase(t - 1, n - vec[0] - 1, v);
    }
    uint64_t TupleToIndex (int strength, int nvar, const t_tuple &t) const {
        if (strength == 1) {
            int idx = abs(t.v[0]) - 1;
            return t.v[0] < 0 ? idx : nvar + idx;
        }

        vector<int> vec(strength);
        for (int i = 0; i < strength; i++) vec[i] = abs(t.v[i]) - 1;
        uint64_t base1 = GetBase(strength, nvar, vec);
        uint64_t base2 = 0;
        for (int i = 0; i < strength; i++) {
            int v = t.v[i] > 0;
            base2 |= v << (strength - i - 1);
        }
        return base2 * combinadic_nCr(nvar, strength) + base1;
    }

    bool is_covered (const vector<int> &tc, const t_tuple &t) const {
        for (int i = 0; i < strength; i++) {
            int pi = abs(t.v[i]) - 1, vi = t.v[i] > 0;
            if (tc[pi] != vi) return false;
        }
        return true;
    }
};

}
