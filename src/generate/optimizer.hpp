#pragma once
#include "expandor.hpp"

#include "util/intrusive_list.h"

struct TuiState;  // forward declaration

namespace expandor {
using std::pair;

class Optimizer {
    Optimizer() = default;
    void rebind_pos_to_idx();

 public:
    Optimizer(Expandor &expandor, int stop_length, int forced_greedy_percent = 90, int left = -1);
    ~Optimizer();
    Optimizer(const Optimizer &) = delete;
    Optimizer(Optimizer &&) noexcept = default;

    void initialize();
    void search(std::chrono::milliseconds time);
    void swap_testcase_out(Expandor &expandor) {
        expandor.new_testcase.swap(testcases);
    }
    void swap_testcase_out(std::vector<std::vector<int>> &out) {
        out.swap(testcases);
    }

    void set_tui_state(TuiState *ts) { tui_state_ = ts; }

 private:
    TuiState *tui_state_ = nullptr;

    std::mt19937_64 gen;
    int stop_length = 1000;
    int strength;

    const Expandor *saved_expandor;

    int last_strength_testcases_size = 0;
    int left;
    vector<vector<int>> testcases;
    const CNF *cnf;
    DDNNF *ddnnf;

    TupleVector tuples_U;
    vector<int> covered_times;

    vector<int> covered_tuples;
    vector<int> uncovered_tuples;

    vector<vector<int>> clauses_cov;

    int greedy_limit;
    int testcase_taboo = 10;
    int forced_greedy_percent = 10;
    vector<int> last_greedy_time;

    bool use_cdcl_solver = true;
    CDCLSolver::Solver *cdcl_solver = nullptr;
    ExtMinisat::SamplingSolver *cdcl_sampler = nullptr;

    // Clear the vector of lists would leak the memory intentionally.
    // By using allocator_, free the allocator_ will free all the memory the lists used.
    IntrusiveList::Allocator allocator_;
    vector<IntrusiveList> unique_covered_tuples;
    vector<IntrusiveList> tc_covered_tuples;
    vector<IntrusiveList> covered_testcases;

    vector<int> testcase_pos_to_idx;
    vector<int> testcase_idx_to_pos;
    vector<int> tuples_idx_to_pos;

    int testcase_idx;
    int cur_step;

    long long search_nums = 0;

    bool is_covered(const vector<int> &tc, const t_tuple &t) const {
        for (int i = 0; i < strength; i++) {
            int pi = abs(t.v[i]) - 1, vi = t.v[i] > 0;
            if (tc[pi] != vi) return false;
        }
        return true;
    }

    int new_uncovered_tuples_after_remove_testcase(int tcid);
    int get_which_remove();
    void UpdateInfo_remove_testcase(int tcid);
    void remove_testcase_greedily();
    pair<bool, pair<int, int>> get_gain_for_forcetuple(int tcid, t_tuple chosen_tp);
    bool random_greedy_step();
    void random_step();
    void forcetuple(int tid, t_tuple tp);
    bool greedy_step_forced(t_tuple tp);
    void change_bit(int v, int ad, const vector<int> &tc, vector<int> &cur_clauses_cov);
    pair<int, int> get_gain_for_forcetestcase(int tcid, const vector<int> &tc2);
    void forcetestcase(int tcid, const vector<int> &tc2);
    void flip_bit(int tid, int vid);
    bool check_for_flip(int tcid, int vid);
    void uptate_unique_covered(int tcid);
    void update_covered_testcases(int tpid);

    std::osyncstream out_stream() const;
};

}  // namespace expandor::scalable
