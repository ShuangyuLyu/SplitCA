#include "generate/expandor.hpp"
#include "generate/optimizer.hpp"
#include "tui/tui_state.hpp"
#include "util/dbg.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <format>
#include <future>
#include <functional>
#include <numeric>
#include <syncstream>
#include <thread>
#include <vector>

using namespace expandor;
using std::pair;
using std::string;

class GroupTaskPool {
 public:
    GroupTaskPool(size_t workers, int strength, TuiState *tui_state = nullptr)
        : workers_(workers),
          valid_nums_per_worker_(workers_, 0),
          uncovered_per_worker_(workers_, TupleVector(strength)),
          tui_state_(tui_state) {}

    void run(int nvar, int strength, auto &&task_fn)
        requires std::invocable<decltype(task_fn), combinadic_tuple const &, uint64_t &, TupleVector &>
    {
        constexpr uint64_t chunk_size = 64;
        std::atomic<uint64_t> next_task{0};
        static_assert(next_task.is_always_lock_free, "std::atomic<uint64_t> must be always lock free.");
        const auto task_count = combinadic_nCr(nvar, strength);
        std::vector<std::jthread> threads;
        threads.reserve(workers_);
        for (uint worker = 0; worker < workers_; ++worker) {
            threads.emplace_back([&, worker]() {
                while (true) {
                    auto begin = next_task.fetch_add(chunk_size, std::memory_order_relaxed);
                    if (begin >= task_count) break;
                    auto end = std::min(task_count, begin + chunk_size);
                    auto &valid_num = valid_nums_per_worker_[worker];
                    auto &uncovered_tuples = uncovered_per_worker_[worker];
                    auto processed = end - begin;
                    auto old_valid_num = valid_num;
                    auto old_uncovered_num = uncovered_tuples.size();
                    auto tuple = combinadic_decode(begin, strength, nvar);
                    for (; begin != end; ++begin, combinadic_next(tuple))
                        std::invoke(task_fn, tuple, valid_num, uncovered_tuples);
                    if (tui_state_) {
                        tui_state_->tv_current_processed.fetch_add(processed << strength, std::memory_order_relaxed);
                        tui_state_->tv_valid.fetch_add(valid_num - old_valid_num, std::memory_order_relaxed);
                        tui_state_->tv_uncovered.fetch_add(uncovered_tuples.size() - old_uncovered_num, std::memory_order_relaxed);
                    }
                }
            });
        }
    }

    uint64_t total_valid_nums() const {
        return std::accumulate(valid_nums_per_worker_.begin(), valid_nums_per_worker_.end(), uint64_t{0});
    }

    void append_uncovered_to(TupleVector &dst) const {
        for (const auto &local : uncovered_per_worker_)
            dst.extends(local);
    }

 private:
    size_t workers_;
    std::vector<uint64_t> valid_nums_per_worker_;
    std::vector<TupleVector> uncovered_per_worker_;
    TuiState *tui_state_;
};

Expandor::Expandor (const CNF *cnf, DDNNF *ddnnf, const Argument &arg)
    : cnf(cnf), ddnnf(ddnnf), seed(arg.seed),
    candidate_set_size(arg.candidate_set_size),
    thread_num(arg.thread_num),
    nvar(ddnnf->get_nvar()) {

    gen.seed(seed);

    strength = 2;

    use_addition_tc = arg.use_addition_tc;

    long long M = combinadic_nCr(nvar, strength - 1) << strength;
    covered_now_strength_bitmap.resize(M, BitSet(nvar));

    count_each_var_covered[0] = vector(nvar + 1, 0);
    count_each_var_covered[1] = vector(nvar + 1, 0);

    constexpr int kThreadSeedStep = 131;
    if (cnf != NULL) {
        cdcl_samplers.reserve(thread_num);
        for (int i = 0; i < thread_num; i++) {
            cdcl_samplers.emplace_back(std::make_unique<ExtMinisat::SamplingSolver>(
                nvar, cnf->get_clauses(), seed + i * kThreadSeedStep, true, 0));
        }
    }
}

Expandor::~Expandor () {
}

void Expandor::set_tui_state(TuiState *state) {
    tui_state_ = state;
}

std::vector<std::vector<int>> Expandor::sample_with_solver(const std::vector<int> &assumps, int sample_num) {
    if (cdcl_samplers.empty() || sample_num <= 0)
        return {};

    // build polarity probability vector from uncovered stats
    std::vector<std::pair<int, int>> prob(nvar, {0, 0});
    for (int j = 0; j < nvar; j++) {
        int c0 = count_each_var_uncovered[0].empty() ? 0 : count_each_var_uncovered[0][j + 1];
        int c1 = count_each_var_uncovered[1].empty() ? 0 : count_each_var_uncovered[1][j + 1];
        // set_prob: pair is (positive_weight, negative_weight)
        prob[j] = {c1 + 1, c0 + 1};
    }

    std::vector<std::vector<int>> ret(sample_num);
    std::atomic<bool> unsat = false;
    int workers = std::min(thread_num, sample_num);

    auto solve_range = [&](int worker_id, int begin, int end) {
        auto *solver = cdcl_samplers[worker_id].get();
        for (int s = begin; s < end && !unsat.load(std::memory_order_relaxed); s++) {
            solver->clear_assumptions();
            for (int lit : assumps)
                solver->add_assumption(lit);
            solver->set_prob(prob);

            std::vector<int> tc;
            solver->get_solution(tc);
            if (tc.empty()) { // UNSAT under assumptions
                unsat.store(true, std::memory_order_relaxed);
                return;
            }
            if ((int)tc.size() > nvar) tc.resize(nvar);
            if ((int)tc.size() < nvar) tc.resize(nvar, 0);
            ret[s] = std::move(tc);
        }
    };

    if (workers == 1) {
        solve_range(0, 0, sample_num);
    } else {
        std::vector<std::jthread> threads;
        threads.reserve(workers);
        int chunk = (sample_num + workers - 1) / workers;
        for (int i = 0; i < workers; i++) {
            int begin = i * chunk;
            int end = std::min(sample_num, begin + chunk);
            if (begin >= end) break;
            threads.emplace_back(solve_range, i, begin, end);
        }
    }

    if (unsat.load(std::memory_order_relaxed))
        return {};

    return ret;
}

std::vector<int> Expandor::sample_one_with_solver(const std::vector<int> &assumps) {
    auto samples = sample_with_solver(assumps, 1);
    if (samples.empty())
        return {};
    return samples.front();
}

void Expandor::Optimize(int stop_length, int left) {
    if (new_testcase.empty()) return;
    int part = (left == -1) ? -1 : (4 - left);
    size_t opt_input = new_testcase.size();
    if (tui_state_)
        tui_state_->begin_optimization(strength, part, opt_input);
    if (new_testcase.empty()) return;
    Optimizer optimizer(*this, stop_length, 90, left);
    if (tui_state_) optimizer.set_tui_state(tui_state_);
    optimizer.initialize();
    optimizer.search(std::chrono::seconds(30));
    optimizer.swap_testcase_out(*this);
    std::osyncstream(*log_stream_) << "optimize end" << std::endl;
    if (tui_state_)
        tui_state_->ca_size.store(old_testcase.size() + new_testcase.size());
}

int Expandor::get_gain (const vector<int> &tc) {
    if (tc.empty())
        return 0;

    if (strength == 2 && uncovered_tuples.size() > 1ll * nvar * (nvar - 1) / 2) {
        int gain = 0;
        for (int i = 0; i < nvar; i ++)
            for (int j = i + 1; j < nvar; j ++) {
                uint64_t state = TupleToIndex(1, nvar, t_tuple{tc[i] ? (i + 1) : (- i - 1)});
                if (! covered_now_strength_bitmap[state << 1 | tc[j]].get(j))
                    gain ++;
            }
        return gain;
    }
    int gain = 0;
    for (const auto &t : uncovered_tuples) {
        if (is_covered(tc, t))
            gain ++;
    }
    return gain;
}

int Expandor::generate_testcase() {
    const t_tuple &t1 = uncovered_tuples[gen() % uncovered_tuples.size()];
    std::vector<std::vector<int>> vec1;
    if (cnf != NULL && !cdcl_samplers.empty()) {
        vec1 = sample_with_solver(std::vector<int>(t1.v, t1.v + strength), candidate_set_size);
    } else {
        ddnnf->update_weight(count_each_var_uncovered[1], count_each_var_uncovered[0]);
        vec1 = ddnnf->sampling_with_assumps(std::span(t1.v, strength), candidate_set_size, gen);
    }
    if ((int)vec1.size() != candidate_set_size) {
        std::osyncstream(*log_stream_) << "c Sampling failed: expected " << candidate_set_size
                  << " candidates, got " << vec1.size() << ".\n";
        return -1;
    }
    std::move(vec1.begin(), vec1.end(), candidate_set.begin() + candidate_set_size);

    int gain_workers = std::min(thread_num, candidate_set_size * 2);
    auto compute_gain_range = [&](int begin, int end) {
        for (int i = begin; i < end; i++)
            gain[i] = std::make_pair(get_gain(candidate_set[i]), i);
    };
    if (gain_workers == 1) {
        compute_gain_range(0, candidate_set_size * 2);
    } else {
        std::vector<std::jthread> threads;
        threads.reserve(gain_workers);
        int chunk = (candidate_set_size * 2 + gain_workers - 1) / gain_workers;
        for (int i = 0; i < gain_workers; i++) {
            int begin = i * chunk;
            int end = std::min(candidate_set_size * 2, begin + chunk);
            if (begin >= end) break;
            threads.emplace_back(compute_gain_range, begin, end);
        }
    }
    
    std::sort(gain.begin(), gain.end());
    if (gain[2 * candidate_set_size - 1].first <= 0)
        return -1;
    
    vector<vector<int>> tmp;
    tmp.resize(candidate_set_size);
    for (int i = candidate_set_size; i < 2 * candidate_set_size; i++)
        tmp[i - candidate_set_size] = candidate_set[gain[i].second];
    for (int i = 0; i < candidate_set_size; i++)
        candidate_set[i] = tmp[i];
    return candidate_set_size - 1;
}

void Expandor::update_t_tuple_info(const vector<int> &testcase, bool setmap) {
    TupleVector tep(strength);
    for (const t_tuple &t : uncovered_tuples) {
        if (!is_covered(testcase, t)) {
            tep.push_back(t);
            continue;
        }

        for (int i = 0; i < strength; i++) {
            int pi = abs(t.v[i]) - 1, vi = t.v[i] > 0;
            count_each_var_uncovered[vi][pi + 1]--;
        }

        uint64_t state = TupleToIndex(strength - 1, nvar, t);
        int p = abs(t.v[strength - 1]) - 1, v = t.v[strength - 1] > 0;
        if (setmap && strength < 4) covered_now_strength_bitmap[state << 1 | v].set(p);
    }
    uncovered_tuples = std::move(tep);

    for (int i = 0; i < nvar; i ++)
        count_each_var_covered[testcase[i]][i + 1] ++;
}

int Expandor::update_1wise_info (const vector<int> &tc) {
    int gain = 0;
    for (int i = 0; i < nvar; i ++)
        if (count_each_var_uncovered[tc[i]][i + 1] > 0) {
            gain ++;
            count_each_var_uncovered[tc[i]][i + 1] --;
        }
    return gain;
}

int Expandor::get_gain_1wise (const vector<int> &tc) {
    int gain = 0;
    for (int i = 0; i < nvar; i ++)
        if (count_each_var_uncovered[tc[i]][i + 1] > 0)
            gain ++;
    return gain;
}

std::vector<int> Expandor::generate_testcase_1wise (int lit) {
    if (cnf != NULL && !cdcl_samplers.empty()) {
        candidate_set = sample_with_solver(std::vector<int>{lit}, candidate_set_size);
    } else {
        candidate_set = ddnnf->sampling_with_assumps(std::span(&lit, 1), candidate_set_size, gen, false);
    }
    if (candidate_set.empty() || candidate_set.front().empty())
        return {};

    for (int i = 0; i < candidate_set_size; i ++)
        gain[i] = std::make_pair(get_gain_1wise(candidate_set[i]), i);
    std::sort(gain.begin(), gain.end());
    if (gain[candidate_set_size - 1].first <= 0)
        return {};

    return candidate_set[gain[candidate_set_size - 1].second];
}

void Expandor::Generate_1wise_CA () {

    candidate_set.resize(candidate_set_size);
    gain.resize(candidate_set_size);

    std::vector<int> tc0;
    if (cnf != NULL && !cdcl_samplers.empty()) {
        tc0 = sample_one_with_solver({});
    } else {
        tc0 = ddnnf->sampling_random(1, gen).front();
    }
    if (tc0.empty())
        return;
    new_testcase.emplace_back(tc0);
    count_each_var_uncovered[0] = vector<int>(nvar + 1, 1);
    count_each_var_uncovered[1] = vector<int>(nvar + 1, 1);
    update_1wise_info(tc0);

    std::vector<int> order(nvar);
    std::iota(order.begin(), order.end(), 1);
    std::shuffle(order.begin(), order.end(), gen);
    for (int i : order) {

        if (!count_each_var_uncovered[1][i] && !count_each_var_uncovered[0][i])
            continue ;
        int lit = count_each_var_uncovered[1][i] ? i : -i;

        auto tc = generate_testcase_1wise(lit);
        if (tc.empty())
            continue ;
        int gain = update_1wise_info(tc);
        if (! gain) break ;
        // dbg(gain);
        new_testcase.emplace_back(tc);

        std::osyncstream(*log_stream_) << "\033[;32mc current 1-wise CA size: " << new_testcase.size()
                  << ", gain: " << gain << " \033[0m" << std::endl;
    }
    dbg(new_testcase.size());
}

void Expandor::Generate_2wise_CA () {

    candidate_set.resize(2 * candidate_set_size);
    gain.resize(2 * candidate_set_size);

    get_all_2wise_tuples();  // sets Tuple Validation phase internally

    for (const auto &tc : new_testcase)
        update_t_tuple_info(tc);
    std::osyncstream(*log_stream_) << "uncovered valid 2-wise tuples:" << uncovered_tuples.size() << std::endl;

    gen_tc_baseline_ = new_testcase.size();
    if (tui_state_)
        tui_state_->begin_generation(2, -1, uncovered_tuples.size());

    int old_size = old_testcase.size();
    for (int num_generated_testcase_ = (int)new_testcase.size() + 1;; num_generated_testcase_++) {
        if (uncovered_tuples.empty())
            break;
        int idx = generate_testcase();
        if (idx == -1) break;
        
        new_testcase.emplace_back(candidate_set[idx]);
        update_t_tuple_info(candidate_set[idx], true);

        std::osyncstream(*log_stream_) << "\033[;32mc current size: " << old_size + num_generated_testcase_
                  << ", current uncovered: " << uncovered_tuples.size() << " \033[0m" << std::endl;
        if (tui_state_) {
            size_t gen_all = tui_state_->gen_all_uncovered.load();
            tui_state_->gen_covered.store(
                gen_all > uncovered_tuples.size() ? gen_all - uncovered_tuples.size() : 0);
            tui_state_->gen_new_testcases.store(new_testcase.size() - gen_tc_baseline_);
            tui_state_->ca_size.store(old_size + num_generated_testcase_);
        }
    }

    replenish_testcase ();
}

void Expandor::replenish_testcase () {
    std::osyncstream(*log_stream_) << "uncovered_nums: " << uncovered_tuples.size() << "\n";
    std::osyncstream(*log_stream_) << "add new testcase: " << new_testcase.size() << std::endl;

    int old_size = old_testcase.size();
    while (!uncovered_tuples.empty()) {
        const auto &t = uncovered_tuples[0];
        std::vector<int> tc;
        if (cnf != NULL && !cdcl_samplers.empty()) {
            tc = sample_one_with_solver(std::vector(t.v, t.v + strength));
        } else {
            tc = ddnnf->sampling_with_assumps(std::vector(t.v, t.v + strength), 1, gen).front();
        }
        if (tc.empty())
            break;
        update_t_tuple_info(tc, true);
        new_testcase.push_back(std::move(tc));

        std::osyncstream(*log_stream_) << "\033[;32mc current size: " << old_size + new_testcase.size()
                  << ", current uncovered: " << uncovered_tuples.size() << " \033[0m" << std::endl;
        if (tui_state_) {
            size_t gen_all = tui_state_->gen_all_uncovered.load();
            tui_state_->gen_covered.store(
                gen_all > uncovered_tuples.size() ? gen_all - uncovered_tuples.size() : 0);
            tui_state_->gen_new_testcases.store(new_testcase.size() - gen_tc_baseline_);
            tui_state_->ca_size.store(old_size + new_testcase.size());
        }
    }

}

void Expandor::get_all_2wise_tuples () {

    tuples_U.clear();
    tuples_U.set_strength(2);
    count_each_var_uncovered[0] = vector<int>(nvar + 1, 0);
    count_each_var_uncovered[1] = vector<int>(nvar + 1, 0);

    auto total_combos = combinadic_nCr(nvar, 2) << strength;
    if (tui_state_)
        tui_state_->begin_tuple_validation(2, -1, total_combos);

    size_t processed = 0, valid_tuples_num = 0;
    for (int var1 = 1; var1 <= nvar; var1 ++) {
        for (int var2 = var1 + 1; var2 <= nvar; var2 ++) 
            for (int lit1 : {var1, -var1})
                for (int lit2 : {var2, -var2}) {
                    processed ++;
                    if (ddnnf->check_valid(std::array{lit1, lit2})) {
                        valid_tuples_num ++;
                        tuples_U.push_back(t_tuple{lit1,lit2});
                        count_each_var_uncovered[lit1 > 0][var1] ++;
                        count_each_var_uncovered[lit2 > 0][var2] ++;
                    }
                }
        if (tui_state_) {
            tui_state_->tv_current_processed.store(processed);
            tui_state_->tv_valid.store(valid_tuples_num);
            tui_state_->tv_uncovered.store(tuples_U.size());
        }
    }

    uncovered_tuples = tuples_U; // copy...
    if (tui_state_) {
        tui_state_->tv_current_processed.store(total_combos);
        tui_state_->tv_valid.store(valid_tuples_num);
        tui_state_->tv_uncovered.store(tuples_U.size());
    }
    std::osyncstream(*log_stream_) << "all valid 2-wise tuples:" << tuples_U.size() << std::endl;
}

void Expandor::reinit_ca() {

    const int left_end = nvar / 2;
    const int right_begin = left_end;
    const int right_cnt = nvar - right_begin;

    CA.clear(), CA_left.clear(), CA_right.clear();
    BitSet tmp(nvar), tmp_left(left_end), tmp_right(right_cnt);
    for (const auto &row : old_testcase) {
        for (int p = 0; auto c : row) {
            tmp[p] = c;
            if (p < left_end) tmp_left[p] = c;
            else tmp_right[p - right_begin] = c;
            p ++;
        }
        CA.push_back(tmp);
        CA_left.push_back(tmp_left);
        CA_right.push_back(tmp_right);
    }
    for (const auto &row : new_testcase) {
        for (int p = 0; auto c : row) {
            tmp[p] = c;
            if (p < left_end) tmp_left[p] = c;
            else tmp_right[p - right_begin] = c;
            p ++;
        }
        CA.push_back(tmp);
        CA_left.push_back(tmp_left);
        CA_right.push_back(tmp_right);
    }
}

void Expandor::set_covered_now_strength_bitmap(const std::vector<int> &ca_indices, const std::vector<BitSet> &ca_pool, int cols_strength, int nvar_local) {
    const int cols_size = cols_strength - 1;
    if (ca_indices.empty() || cols_size <= 0 || nvar_local <= 0) return;

    const combinadic_encode_type total_cols = combinadic_nCr(nvar_local, cols_size);
    if (total_cols == 0) return;

    const int workers = std::min<int>(thread_num, static_cast<int>(total_cols));
    if (workers == 1) {
        for (auto cols = combinadic_begin(cols_size); cols.back() < nvar_local; combinadic_next(cols)) {
            t_tuple tuple;
            for (int ca_index : ca_indices) {
                const auto &tc = ca_pool[ca_index];
                for (int i = 0; i < cols_size; ++i)
                    tuple.v[i] = tc.get(cols[i]) ? (cols[i] + 1) : -(cols[i] + 1);
                auto encode = TupleToIndex(cols_size, nvar_local, tuple);
                covered_now_strength_bitmap[encode << 1 | 1] |= tc;
                covered_now_strength_bitmap[encode << 1].or_not(tc);
            }
        }
        return;
    }

    std::vector<std::jthread> threads;
    threads.reserve(workers);
    const combinadic_encode_type chunk = (total_cols + workers - 1) / workers;

    for (int worker = 0; worker < workers; ++worker) {
        combinadic_encode_type begin = chunk * worker;
        combinadic_encode_type end = std::min(total_cols, begin + chunk);
        if (begin >= end) break;

        threads.emplace_back([this, &ca_indices, &ca_pool, cols_size, nvar_local, begin, end]() {
            auto cols = combinadic_decode(begin, cols_size, nvar_local - 1);

            t_tuple tuple;
            for (combinadic_encode_type idx = begin; idx < end; ++idx) {
                for (int ca_index : ca_indices) {
                    const auto &tc = ca_pool[ca_index];
                    for (int i = 0; i < cols_size; ++i)
                        tuple.v[i] = tc.get(cols[i]) ? (cols[i] + 1) : -(cols[i] + 1);
                    auto encode = TupleToIndex(cols_size, nvar_local, tuple);
                    covered_now_strength_bitmap[encode << 1 | 1] |= tc;
                    covered_now_strength_bitmap[encode << 1].or_not(tc);
                }

                if (idx + 1 < end) combinadic_next(cols);
            }
        });
    }
}

bool Expandor::check_part_invalid(const t_tuple &tp) const {
    for (int ban = 0; ban < strength; ban++) {
        t_tuple tmp;
        for (int i = 0, j = 0; i < strength; i++) {
            if (i == ban) continue;

            j++;
            if (j == strength - 1) {
                int p = abs(tp.v[i]) - 1, v = tp.v[i] > 0;
                uint64_t state = TupleToIndex(strength - 2, nvar, tmp);
                if (!covered_last_strength_bitmap[state << 1 | v].get(p)) return 1;
            } else
                tmp.v[j - 1] = tp.v[i];
        }
    }
    return 0;
}

bool Expandor::check_clauses_invalid(const t_tuple &tp) const {
    for (const auto &t : t_clauses) {
        bool flag = true;
        for (int i = 0; i < strength; i++)
            if (t.v[i] != -tp.v[i]) {
                flag = false;
                break;
            }
        if (flag) return 1;
    }
    return 0;
}

size_t Expandor::get_remaining_valid_tuples_4wise_group_left4(const std::vector<int> &all_ca_indices, int left_end) {
    constexpr int strength = 4;
    long long M = combinadic_nCr(left_end, strength - 1) << strength;
    covered_now_strength_bitmap = vector(M, BitSet(left_end));
    set_covered_now_strength_bitmap(all_ca_indices, CA_left, strength, left_end);

    GroupTaskPool pool(thread_num, strength, tui_state_);
    pool.run(left_end, 4, [this, left_end](combinadic_tuple_view vars, uint64_t &valid_nums, TupleVector &uncovered) {
        t_tuple tuple;
        for (int value = 0; value < 16; ++value) {
            for (int i = 0; i < 4; ++i)
                tuple.v[i] = (value & (1 << i)) ? (int)(vars[i] + 1) : -(int)(vars[i] + 1);
            uint64_t state = TupleToIndex(3, left_end, tuple);
            int p = abs(tuple.v[3]) - 1, v = tuple.v[3] > 0;
            if (covered_now_strength_bitmap[state << 1 | v].get(p)) {
                ++valid_nums;
                continue;
            }
            if (check_part_invalid(tuple) || check_clauses_invalid(tuple)) continue;
            if (ddnnf->check_valid<strength>(tuple)) {
                ++valid_nums;
                uncovered.push_back(tuple);
            }
        }
    });
    pool.append_uncovered_to(uncovered_tuples);
    return pool.total_valid_nums();
}

size_t Expandor::get_remaining_valid_tuples_4wise_group_left3(int left_end, int right_begin, int right_cnt) {
    vector<vector<int>> tc_index(right_cnt << 1);
    for (size_t i = 0; i < CA.size(); ++i) {
        const auto &tc = CA[i];
        for (int d = right_begin; d < nvar; ++d)
            tc_index[(d - right_begin) << 1 | tc[d]].push_back(i);
    }

    size_t valid_nums = 0;
    long long M = combinadic_nCr(left_end, 2) << 3;
    for (int d = right_begin; d < nvar; ++d) for (int dv : {0, 1}) {
        covered_now_strength_bitmap = vector(M, BitSet(left_end));
        set_covered_now_strength_bitmap(tc_index[(d - right_begin) << 1 | dv], CA_left, 3, left_end);

        GroupTaskPool pool(thread_num, 4, tui_state_);
        pool.run(left_end, 3, [this, left_end, v3 = dv ? (d + 1) : -(d + 1)](combinadic_tuple_view vars, uint64_t &valid_nums, TupleVector &uncovered) {
            t_tuple tuple;
            tuple.v[3] = v3;
            for (int value = 0; value < 8; ++value) {
                for (int i = 0; i < 3; ++i)
                    tuple.v[i] = (value & (1 << i)) ? (int)(vars[i] + 1) : -(int)(vars[i] + 1);

                uint64_t state = TupleToIndex(2, left_end, tuple);
                int p = abs(tuple.v[2]) - 1, v = tuple.v[2] > 0;
                if (covered_now_strength_bitmap[state << 1 | v].get(p)) {
                    ++valid_nums;
                    continue;
                }
                if (check_part_invalid(tuple) || check_clauses_invalid(tuple)) continue;
                if (ddnnf->check_valid<4>(tuple)) {
                    ++valid_nums;
                    uncovered.push_back(tuple);
                }
            }
        });
        valid_nums += pool.total_valid_nums();
        pool.append_uncovered_to(uncovered_tuples);
    }

    return valid_nums;
}

size_t Expandor::get_remaining_valid_tuples_4wise_group_left2(int left_end, int right_begin, int right_cnt) {
    vector<vector<int>> tc_index(right_cnt * right_cnt * 2);
    for (size_t i = 0; i < CA.size(); ++i) {
        const auto &tc = CA[i];
        for (int c = right_begin, pair_id = 0; c < nvar; ++c)
            for (int d = c + 1; d < nvar; ++d, ++pair_id)
                tc_index[pair_id << 2 | (tc[c] << 1) | tc[d]].push_back(i);
    }

    size_t valid_nums = 0;
    long long M = combinadic_nCr(left_end, 1) << 2;
    for (int c = right_begin, pair_id = 0; c < nvar; ++c)
        for (int d = c + 1; d < nvar; ++d, ++pair_id)
            for (int vc : {0, 1}) for (int vd : {0, 1}) {
                covered_now_strength_bitmap = vector(M, BitSet(left_end));
                set_covered_now_strength_bitmap(tc_index[pair_id << 2 | (vc << 1) | vd], CA_left, 2, left_end);

                GroupTaskPool pool(thread_num, 4, tui_state_);
                pool.run(left_end, 2, [this, left_end, c, d, vc, vd](combinadic_tuple_view vars, uint64_t &valid_nums, TupleVector &uncovered) {
                    t_tuple tuple;
                    tuple.v[2] = vc ? (c + 1) : -(c + 1);
                    tuple.v[3] = vd ? (d + 1) : -(d + 1);
                    for (int value = 0; value < 4; ++value) {
                        for (int i = 0; i < 2; ++i)
                            tuple.v[i] = (value & (1 << i)) ? (int)(vars[i] + 1) : -(int)(vars[i] + 1);

                        uint64_t state = TupleToIndex(1, left_end, tuple);
                        int p = abs(tuple.v[1]) - 1, v = tuple.v[1] > 0;
                        if (covered_now_strength_bitmap[state << 1 | v].get(p)) {
                            ++valid_nums;
                            continue;
                        }
                        if (check_part_invalid(tuple) || check_clauses_invalid(tuple)) continue;
                        if (ddnnf->check_valid<4>(tuple)) {
                            ++valid_nums;
                            uncovered.push_back(tuple);
                        }
                    }
                });
                valid_nums += pool.total_valid_nums();
                pool.append_uncovered_to(uncovered_tuples);
            }

    return valid_nums;
}

size_t Expandor::get_remaining_valid_tuples_4wise_group_left1(int left_end, int right_begin, int right_cnt) {
    vector<vector<int>> tc_index(left_end << 1);
    for (size_t i = 0; i < CA.size(); ++i) {
        const auto &tc = CA[i];
        for (int a = 0; a < left_end; ++a)
            tc_index[a << 1 | tc[a]].push_back(i);
    }

    size_t valid_nums = 0;
    long long M = combinadic_nCr(right_cnt, 2) << 3;
    for (int a = 0; a < left_end; ++a) for (int va : {0, 1}) {
        covered_now_strength_bitmap = vector(M, BitSet(right_cnt));
        set_covered_now_strength_bitmap(tc_index[a << 1 | va], CA_right, 3, right_cnt);

        GroupTaskPool pool(thread_num, 4, tui_state_);
        pool.run(right_cnt, 3, [this, right_begin, right_cnt, a, va](combinadic_tuple_view vars, uint64_t &valid_nums, TupleVector &uncovered) {
            t_tuple tuple, full_tuple;
            full_tuple.v[0] = va ? (a + 1) : -(a + 1);
            for (int value = 0; value < 8; ++value) {
                for (int i = 0; i < 3; ++i)
                    tuple.v[i] = (value & (1 << i)) ? (int)(vars[i] + 1) : -(int)(vars[i] + 1);

                uint64_t state = TupleToIndex(2, right_cnt, tuple);
                int p = abs(tuple.v[2]) - 1, v = tuple.v[2] > 0;
                if (covered_now_strength_bitmap[state << 1 | v].get(p)) {
                    ++valid_nums;
                    continue;
                }

                for (int i = 0; i < 3; ++i)
                    full_tuple.v[i + 1] = tuple.v[i] + (tuple.v[i] > 0 ? right_begin : -right_begin);
                if (check_part_invalid(full_tuple) || check_clauses_invalid(full_tuple)) continue;
                if (ddnnf->check_valid<4>(full_tuple)) {
                    ++valid_nums;
                    uncovered.push_back(full_tuple);
                }
            }
        });
        valid_nums += pool.total_valid_nums();
        pool.append_uncovered_to(uncovered_tuples);
    }

    return valid_nums;
}

size_t Expandor::get_remaining_valid_tuples_4wise_group_left0(const std::vector<int> &all_ca_indices, int right_begin, int right_cnt) {
    constexpr int strength = 4;
    long long M = combinadic_nCr(right_cnt, strength - 1) << strength;
    covered_now_strength_bitmap = vector(M, BitSet(right_cnt));
    set_covered_now_strength_bitmap(all_ca_indices, CA_right, strength, right_cnt);

    GroupTaskPool pool(thread_num, strength, tui_state_);
    pool.run(right_cnt, 4, [this, right_begin, right_cnt](combinadic_tuple_view vars, uint64_t &local_valid_nums, TupleVector &local_uncovered) {
        t_tuple tuple, full_tuple;
        for (int value = 0; value < 16; ++value) {
            for (int i = 0; i < 4; ++i)
                tuple.v[i] = (value & (1 << i)) ? (int)(vars[i] + 1) : -(int)(vars[i] + 1);
            uint64_t state = TupleToIndex(3, right_cnt, tuple);
            int p = abs(tuple.v[3]) - 1, v = tuple.v[3] > 0;
            if (covered_now_strength_bitmap[state << 1 | v].get(p)) {
                ++local_valid_nums;
                continue;
            }

            for (int i = 0; i < 4; ++i)
                full_tuple.v[i] = tuple.v[i] + (tuple.v[i] > 0 ? right_begin : -right_begin);
            if (check_part_invalid(full_tuple) || check_clauses_invalid(full_tuple)) continue;
            if (ddnnf->check_valid<strength>(full_tuple)) {
                ++local_valid_nums;
                local_uncovered.push_back(full_tuple);
            }
        }
    });
    pool.append_uncovered_to(uncovered_tuples);
    return pool.total_valid_nums();
}

size_t Expandor::get_remaining_valid_tuples_4wise_group(int left_half_vars) {
    assert(strength == 4);
    assert(0 <= left_half_vars && left_half_vars <= 4);
    const int left_end = nvar / 2;
    const int right_begin = left_end;
    const int right_cnt = nvar - right_begin;
    const int right_half_vars = 4 - left_half_vars;
    if (left_half_vars > left_end || right_half_vars > right_cnt) return 0;

    std::vector<int> all_ca_indices(CA.size());
    std::iota(all_ca_indices.begin(), all_ca_indices.end(), 0);

    if (left_half_vars == 4) return get_remaining_valid_tuples_4wise_group_left4(all_ca_indices, left_end);
    if (left_half_vars == 3) return get_remaining_valid_tuples_4wise_group_left3(left_end, right_begin, right_cnt);
    if (left_half_vars == 2) return get_remaining_valid_tuples_4wise_group_left2(left_end, right_begin, right_cnt);
    if (left_half_vars == 1) return get_remaining_valid_tuples_4wise_group_left1(left_end, right_begin, right_cnt);
    return get_remaining_valid_tuples_4wise_group_left0(all_ca_indices, right_begin, right_cnt);
}

size_t Expandor::get_remaining_valid_tuples_3wise() {
    assert(strength == 3);
    constexpr int strength = 3;
    size_t valid_nums = 0, processed = 0;
    for (auto cols = combinadic_begin(strength - 1); cols.back() < nvar - 1; combinadic_next(cols)) {
        for (int value = 0, mx_value = 1 << (strength - 1); value < mx_value; ++value) {
            t_tuple tuple;
            for (int i = 0; i < strength - 1; ++i)
                tuple.v[i] = (value & (1 << i)) ? (cols[i] + 1) : -(cols[i] + 1);
            auto encode = TupleToIndex(strength - 1, nvar, tuple);
            for (int last_col = cols.back() + 1; last_col < nvar; ++last_col) {
                for (int last : {0, 1}) {
                    processed ++;
                    tuple.v[strength - 1] = last ? (last_col + 1) : -(last_col + 1);
                    // check if already covered (by covered_now_strength_bitmap)
                    if (covered_now_strength_bitmap[encode << 1 | last].get(last_col)) {
                        ++valid_nums;
                        continue; // already covered...
                    }
                    // check if last-wise tuple invalid.
                    if (check_part_invalid(tuple) || check_clauses_invalid(tuple)) {
                        continue; // invalid
                    }
                    if (ddnnf->check_valid<strength>(tuple)) {
                        ++valid_nums;
                        uncovered_tuples.push_back(tuple);
                    }
                }
            }
        }
        if (tui_state_) {
            tui_state_->tv_current_processed.store(processed, std::memory_order_relaxed);
            tui_state_->tv_valid.store(valid_nums, std::memory_order_relaxed);
            tui_state_->tv_uncovered.store(uncovered_tuples.size(), std::memory_order_relaxed);
        }
    }
    return valid_nums;
}

void Expandor::refresh_4wise_covered(size_t begin_new_testcase) {
    assert(strength == 4);
    if (strength != 4) __builtin_unreachable();

    const int left_end = nvar / 2;
    const int right_begin = left_end;
    const int right_cnt = nvar - right_begin;

    BitSet bits(nvar), bits_left(left_end), bits_right(right_cnt);
    for (size_t i = begin_new_testcase; i < new_testcase.size(); ++i) {
        for (int p = 0; p < nvar; ++p) bits[p] = new_testcase[i][p];
        for (int p = 0; p < left_end; ++p) bits_left[p] = new_testcase[i][p];
        for (int p = 0; p < right_cnt; ++p) bits_right[p] = new_testcase[i][right_begin + p];
        
        CA.emplace_back(bits);
        CA_left.emplace_back(bits_left);
        CA_right.emplace_back(bits_right);
    }
}

void Expandor::expand_4wise_partitioned() {
    assert(strength == 4);
    constexpr int strength = 4;
    std::vector<std::future<std::vector<std::vector<int>>>> testcase_futures;

    size_t valid_nums = 0;
    // First pass snapshots all tuples uncovered by the incoming 3-wise CA (for optimizer target set).
    for (int left_half_vars : {4, 3, 2, 1, 0}) {
        assert(uncovered_tuples.empty());
        uncovered_tuples.set_strength(strength);

        const int left_nvar = nvar / 2;
        const int right_nvar = nvar - left_nvar;
        auto current_all_tuples_num =
            combinadic_nCr(left_nvar, left_half_vars) * combinadic_nCr(right_nvar, 4 - left_half_vars) << strength;

        int part_idx = 4 - left_half_vars;  // 4->0, 3->1, 2->2, 1->3, 0->4
        if (tui_state_)
            tui_state_->begin_tuple_validation(4, part_idx, current_all_tuples_num);

        auto current_valid_nums = get_remaining_valid_tuples_4wise_group(left_half_vars);
        covered_now_strength_bitmap.clear(); // release some memory
        valid_nums += current_valid_nums;

        std::osyncstream(*log_stream_) << std::format("partition left={}:\n", left_half_vars)
                  << std::format("uncovered valid 4-wise tuple nums: {}\n", uncovered_tuples.size())
                  << std::format("all valid 4-wise tuple nums: {}\n", current_valid_nums)
                  << std::format("invalid 4-wise tuple nums: {}\n", current_all_tuples_num - current_valid_nums)
                  << std::format("all tuples: {}", current_all_tuples_num)
                  << std::endl;
        size_t old_size = old_testcase.size();
        tuples_U = uncovered_tuples;

        count_each_var_covered[0] = vector(nvar + 1, 0);
        count_each_var_covered[1] = vector(nvar + 1, 0);
        count_each_var_uncovered[0] = vector(nvar + 1, 0);
        count_each_var_uncovered[1] = vector(nvar + 1, 0);
        for (const t_tuple &t : uncovered_tuples) {
            for (int k = 0; k < strength; k++) {
                int j = abs(t.v[k]) - 1, vj = t.v[k] > 0;
                count_each_var_uncovered[vj][j + 1]++;
            }
        }

        if (tui_state_)
            tui_state_->begin_generation(4, part_idx, uncovered_tuples.size());

        size_t gen_all = uncovered_tuples.size();
        assert(new_testcase.empty());
        for (int num_generated_testcase_ = 0;; num_generated_testcase_++) {
            if (uncovered_tuples.empty()) break;
            int idx = generate_testcase();
            if (idx == -1) break;

            new_testcase.push_back(candidate_set[idx]);
            update_t_tuple_info(candidate_set[idx], true);

            std::osyncstream(*log_stream_) << "\033[;32mc current size: " << old_size + num_generated_testcase_
                    << ", current uncovered: " << uncovered_tuples.size() << " \033[0m" << std::endl;
            if (tui_state_) {
                tui_state_->ca_size.store(old_size + num_generated_testcase_, std::memory_order_relaxed);
                tui_state_->gen_covered.store(gen_all - uncovered_tuples.size(), std::memory_order_relaxed);
                tui_state_->gen_new_testcases.store(new_testcase.size(), std::memory_order_relaxed);
                tui_state_->notify();
            }
        }
        replenish_testcase();

        assert(uncovered_tuples.empty());
        if (new_testcase.empty()) continue;
        if (! use_addition_tc) {
            uint new_testcase_size = new_testcase.size();
            auto *ls = log_stream_;
            std::packaged_task task([left_half_vars, new_testcase_size, ls](Optimizer optimizer) {
                optimizer.initialize();
                optimizer.search(std::chrono::hours(2));
                std::vector<std::vector<int>> testcase_out;
                optimizer.swap_testcase_out(testcase_out);
                std::osyncstream(*ls) << std::format(
                    "left #{}, optimizer end. new testcase size: {} -> {}",
                    left_half_vars, new_testcase_size, testcase_out.size()
                ) << std::endl;
                return testcase_out;
            });
            testcase_futures.push_back(task.get_future());
            std::thread(std::move(task), Optimizer(*this, 1000, 90, left_half_vars)).detach();
        } else {
            Optimize(50, left_half_vars); // compact...
            refresh_4wise_covered(0);
            old_testcase.insert(old_testcase.end(), new_testcase.begin(), new_testcase.end());
            new_testcase.clear();
        }
    }

    size_t num_combination_all_possible_ = combinadic_nCr(nvar, strength) << strength;
    size_t invalid_nums = num_combination_all_possible_ - valid_nums;
    std::osyncstream(*log_stream_) << "all valid " << strength << "-wise tuple nums: " << valid_nums << "\n";
    std::osyncstream(*log_stream_) << "invalid " << strength << "-wise tuple nums: " << invalid_nums << "\n";
    std::osyncstream(*log_stream_) << "all tuples: " << num_combination_all_possible_ << std::endl;

    for (auto &future : testcase_futures) {
        auto result = future.get();
        new_testcase.insert(new_testcase.end(),
            std::make_move_iterator(result.begin()),
            std::make_move_iterator(result.end()));
    }
}

void Expandor::Expand() {
    strength++;
    assert(strength == 3 || strength == 4);
    std::osyncstream(*log_stream_) << "start expand: " << strength << "\n";

    if (tui_state_)
        tui_state_->strength.store(strength);

    if (cnf != NULL) {
        t_clauses.clear();
        t_clauses.set_strength(strength);
        for (const vector<int> &c : cnf->get_clauses())
            if ((int)c.size() == strength) {
                t_tuple tep(c);
                t_clauses.push_back(tep);
            }
    }

    old_testcase.reserve(old_testcase.size() + new_testcase.size());
    for (vector<int> &tc : new_testcase) old_testcase.emplace_back(std::move(tc));
    new_testcase.clear();
    reinit_ca();

    auto num_combination_all_possible_ = combinadic_nCr(nvar, strength) << strength;
    std::osyncstream(*log_stream_) << strength << "-wise num_combination_all_possible_ = " << num_combination_all_possible_ << std::endl;

    long long M = combinadic_nCr(nvar, strength - 1) << strength;
    covered_last_strength_bitmap = std::move(covered_now_strength_bitmap);

    if (strength == 4) {
        expand_4wise_partitioned();
        std::osyncstream(*log_stream_) << std::format("c Expand CA to {} wise. size: {} -> {}.\n", strength,
                                 old_testcase.size(), old_testcase.size() + new_testcase.size());
        if (tui_state_)
            tui_state_->ca_size.store(old_testcase.size() + new_testcase.size());
        return;
    }

    covered_now_strength_bitmap = vector(M, BitSet(nvar));
    std::vector<int> all_ca_indices(CA.size());
    std::iota(all_ca_indices.begin(), all_ca_indices.end(), 0);
    set_covered_now_strength_bitmap(all_ca_indices, CA, strength, nvar);

    std::osyncstream(*log_stream_) << "begin to get remaining valid tuples" << std::endl;
    assert(uncovered_tuples.empty());
    uncovered_tuples.clear();
    uncovered_tuples.set_strength(strength);

    if (tui_state_)
        tui_state_->begin_tuple_validation(3, -1, num_combination_all_possible_);

    size_t valid_nums = get_remaining_valid_tuples_3wise();

    count_each_var_covered[0] = vector(nvar + 1, 0);
    count_each_var_covered[1] = vector(nvar + 1, 0);
    count_each_var_uncovered[0] = vector(nvar + 1, 0);
    count_each_var_uncovered[1] = vector(nvar + 1, 0);
    for (const t_tuple &t : uncovered_tuples) {
        for (int k = 0; k < strength; k++) {
            int j = abs(t.v[k]) - 1, vj = t.v[k] > 0;
            count_each_var_uncovered[vj][j + 1]++;
        }
    }

    tuples_U = uncovered_tuples;  // backup

    size_t uncovered_nums = uncovered_tuples.size();
    size_t covered_nums = valid_nums - uncovered_nums;
    size_t invalid_nums = num_combination_all_possible_ - valid_nums;
    std::osyncstream(*log_stream_) << "covered valid " << strength << "-wise tuple nums: " << covered_nums << "\n";
    std::osyncstream(*log_stream_) << "uncovered valid " << strength << "-wise tuple nums: " << uncovered_nums << "\n";
    std::osyncstream(*log_stream_) << "all valid " << strength << "-wise tuple nums: " << valid_nums << "\n";
    std::osyncstream(*log_stream_) << "invalid " << strength << "-wise tuple nums: " << invalid_nums << "\n";
    std::osyncstream(*log_stream_) << "all tuples: " << num_combination_all_possible_ << std::endl;
    if (tui_state_) {
        tui_state_->tv_current_processed.store(num_combination_all_possible_);
        tui_state_->tv_valid.store(valid_nums);
        tui_state_->tv_uncovered.store(uncovered_nums);
        tui_state_->ca_size.store(old_testcase.size());
        tui_state_->begin_generation(3, -1, uncovered_nums);
    }

    // Generate CA
    int old_size = old_testcase.size();
    for (int num_generated_testcase_ = new_testcase.size() + 1;; num_generated_testcase_++) {
        if (uncovered_tuples.empty())
            break;
        int idx = generate_testcase();
        if (idx == -1) break;
        
        new_testcase.emplace_back(candidate_set[idx]);
        update_t_tuple_info(candidate_set[idx], true);

        std::osyncstream(*log_stream_) << "\033[;32mc current size: " << old_size + num_generated_testcase_
                  << ", current uncovered: " << uncovered_tuples.size() << " \033[0m" << std::endl;
        if (tui_state_) {
            tui_state_->ca_size.store(old_size + num_generated_testcase_, std::memory_order_relaxed);
            tui_state_->gen_covered.store(uncovered_nums - uncovered_tuples.size(), std::memory_order_relaxed);
            tui_state_->gen_new_testcases.store(new_testcase.size(), std::memory_order_relaxed);
        }
    }

    replenish_testcase ();

    std::osyncstream(*log_stream_) << std::format("c Expand CA to {} wise. size: {} -> {}.\n", strength,
                             old_testcase.size(), old_testcase.size() + new_testcase.size());
}

void Expandor::output_to_file (const std::string &result_path) {
    if (result_path.empty()) {
        std::osyncstream(*log_stream_) << "warning: result_path is empty..." << std::endl;
        std::osyncstream(*log_stream_) << std::format("{}-wise CA size: {}", strength, old_testcase.size() + new_testcase.size()) << std::endl;
    }
    std::ofstream res_file(result_path);

    std::osyncstream(*log_stream_) << old_testcase.size() << "\n";
    for (const vector<int>& testcase: old_testcase) {
        for (int v = 0; v < nvar; v++)
            res_file << testcase[v] << " ";
        res_file << "\n";
    }
    std::osyncstream(*log_stream_) << new_testcase.size() << "\n";
    for (const vector<int>& testcase: new_testcase) {
        for (int v = 0; v < nvar; v++)
            res_file << testcase[v] << " ";
        res_file << "\n";
    }
    res_file.close();
    std::osyncstream(*log_stream_) << "c Testcase set saved in " << result_path << std::endl;
}
