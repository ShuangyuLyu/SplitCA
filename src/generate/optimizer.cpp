#include "optimizer.hpp"
#include <thread>
#include <syncstream>

#include "tui/tui_state.hpp"

using namespace expandor;
using std::make_pair;
using std::string;
using std::vector;

Optimizer::Optimizer(Expandor &expandor, int stop_length,
                     int forced_greedy_percent, int left)
    : stop_length(stop_length),
      saved_expandor(&expandor),
      left(left),
      forced_greedy_percent(forced_greedy_percent) {
    assert(! expandor.new_testcase.empty());
    
    gen.seed(expandor.gen());

    cnf = expandor.cnf;
    ddnnf = expandor.ddnnf;
    strength = expandor.strength;
    last_strength_testcases_size = expandor.old_testcase.size();
    testcases.swap(expandor.new_testcase);
    auto testcase_size = testcases.size();
    testcase_idx = testcase_size - 1;

    tuples_U = std::move(expandor.tuples_U);
}

void Optimizer::initialize() {
    rebind_pos_to_idx();

    const int nvar = ddnnf->get_nvar();
    if (cnf != NULL) {
        cdcl_solver = new CDCLSolver::Solver;
        cdcl_solver->read_clauses(nvar, cnf->get_clauses());
        cdcl_sampler = new ExtMinisat::SamplingSolver(nvar, cnf->get_clauses(), 1, true, 0);
    }
    else {
        cdcl_solver = nullptr;
        cdcl_sampler = nullptr;
    }

    out_stream() << "Optimizer init success" << std::endl;
}

std::osyncstream Optimizer::out_stream() const {
    std::osyncstream ostream(*saved_expandor->log_stream_);
    if (left != -1)
        ostream << "left = #" << left << ": ";
    return ostream;
}

void Optimizer::rebind_pos_to_idx() {
    int testcase_size = testcases.size(), tuples_nums = tuples_U.size();

    covered_tuples.clear();
    uncovered_tuples.clear();
    tuples_idx_to_pos.clear();
    covered_tuples.resize(tuples_nums);
    tuples_idx_to_pos.resize(tuples_nums);
    std::iota(covered_tuples.begin(), covered_tuples.end(), 0);
    std::iota(tuples_idx_to_pos.begin(), tuples_idx_to_pos.end(), 0);

    testcase_pos_to_idx.clear();
    testcase_idx_to_pos.clear();
    testcase_pos_to_idx.resize(testcase_size);
    testcase_idx_to_pos.resize(testcase_size);
    std::iota(testcase_pos_to_idx.begin(), testcase_pos_to_idx.end(), 0);
    std::iota(testcase_idx_to_pos.begin(), testcase_idx_to_pos.end(), 0);

    unique_covered_tuples.clear();
    tc_covered_tuples.clear();
    unique_covered_tuples.resize(testcase_size);
    tc_covered_tuples.resize(testcase_size);

    covered_times.clear();
    covered_testcases.clear();
    covered_times.resize(tuples_nums, 0);
    covered_testcases.resize(tuples_nums);

    auto thread_num = saved_expandor->thread_num;
    const int every = tuples_nums / thread_num;
    std::vector<std::thread> threads;
    vector<std::queue<std::pair<int, int>>> thread_covered_testcases(thread_num),
        thread_tc_covered_tuples(thread_num);
    for (uint id = 0; id < thread_num; ++id) {
        threads.emplace_back([&, id] {
            auto &my_covered_testcases = thread_covered_testcases[id];
            auto &my_tc_covered_tuples = thread_tc_covered_tuples[id];
            const int st = every * id + std::min<int>(id, tuples_nums % thread_num);
            const int ed = every * (id + 1) + std::min<int>(id + 1, tuples_nums % thread_num);
            for (int p = st; p < ed; p++) {
                const t_tuple &t = tuples_U[p];
                for (int pos = 0; pos < testcase_size; pos++) {
                    const auto &tc = testcases[pos];
                    if (is_covered(tc, t)) {
                        covered_times[p] += 1;
                        my_covered_testcases.emplace(p, testcase_pos_to_idx[pos]);
                        my_tc_covered_tuples.emplace(pos, p);
                    }
                }
            }
        });
    }
    for (auto &t : threads) t.join();
    threads.clear();
    for (auto &q : thread_covered_testcases) {
        while (!q.empty()) {
            auto [k, v] = q.front();
            q.pop();
            covered_testcases[k].insert(v, allocator_);
        }
    }
    for (auto &q : thread_tc_covered_tuples) {
        while (!q.empty()) {
            auto [k, v] = q.front();
            q.pop();
            tc_covered_tuples[k].insert(v, allocator_);
        }
    }
    thread_covered_testcases.clear();
    thread_tc_covered_tuples.clear();

    for (int p = 0; p < tuples_nums; p++) {
        if (covered_times[p] == 1) {
            int idx = covered_testcases[p].front();
            int pos = testcase_idx_to_pos[idx];
            unique_covered_tuples[pos].insert(p, allocator_);
        }
    }

    if (cnf != nullptr) {
        const int nvar = cnf->get_num_variables();
        const auto &pos_in_cls = cnf->get_pos_in_cls();
        const auto &neg_in_cls = cnf->get_neg_in_cls();
        clauses_cov.clear();
        clauses_cov.resize(testcase_size, vector<int>(cnf->get_num_clauses(), 0));
        for (int i = 0; i < testcase_size; i++) {
            const vector<int> &testcase = testcases[i];
            vector<int> &cur_clauses_cov = clauses_cov[i];
            for (int j = 0; j < nvar; j++) {
                const vector<int> &vec = (testcase[j] ? pos_in_cls[j + 1] : neg_in_cls[j + 1]);
                for (int x : vec) ++cur_clauses_cov[x];
            }
        }
    }

    greedy_limit = 0;
    last_greedy_time.clear();
    last_greedy_time.resize(testcase_size, -testcase_taboo - 1);
}


Optimizer::~Optimizer() {
    delete cdcl_solver;
    delete cdcl_sampler;
}

void Optimizer::uptate_unique_covered(int tcid) {
    int tcid_p = testcase_idx_to_pos[tcid];
    for (IntrusiveListNode *p = unique_covered_tuples[tcid_p].head, *q; p != NULL; p = q) {
        q = p->nxt;
        int tpid = p->val;
        if (covered_times[tpid] != 1) unique_covered_tuples[tcid_p].erase(p, allocator_);
    }
}

void Optimizer::update_covered_testcases(int tpid) {
    for (IntrusiveListNode *p = covered_testcases[tpid].head, *q; p != NULL; p = q) {
        q = p->nxt;
        int tcid = p->val;
        if (testcase_idx_to_pos[tcid] < 0) covered_testcases[tpid].erase(p, allocator_);
    }
}

int Optimizer::new_uncovered_tuples_after_remove_testcase(int tcid) {
    uptate_unique_covered(tcid);
    int tcid_p = testcase_idx_to_pos[tcid];
    return unique_covered_tuples[tcid_p].size();
}

int Optimizer::get_which_remove() {
    int besttc = -1, mini = 0;
    int testcase_size = testcases.size();
    for (int i = 0; i < testcase_size; ++i) {
        int idx = testcase_pos_to_idx[i];
        int res = new_uncovered_tuples_after_remove_testcase(idx);
        if (besttc == -1 || res < mini) {
            mini = res, besttc = i;
            if (mini == 0) break;
        }
    }
    return testcase_pos_to_idx[besttc];
}

void Optimizer::UpdateInfo_remove_testcase(int tcid_idx) {
    int tcid = testcase_idx_to_pos[tcid_idx];

    vector<int> break_pos, unique_id;
    for (IntrusiveListNode *p = tc_covered_tuples[tcid].head; p != NULL; p = p->nxt) {
        int tpid = p->val;
        covered_times[tpid]--;
        if (covered_times[tpid] == 0) {
            int pos = tuples_idx_to_pos[tpid];
            break_pos.emplace_back(pos);
            uncovered_tuples.emplace_back(tpid);
        } else if (covered_times[tpid] == 1)
            unique_id.emplace_back(tpid);
    }

    testcase_idx_to_pos[tcid_idx] = -1;
    for (int tpid : unique_id) {
        update_covered_testcases(tpid);
        int idx = covered_testcases[tpid].front();
        int pos = testcase_idx_to_pos[idx];
        unique_covered_tuples[pos].insert(tpid, allocator_);
    }

    sort(break_pos.begin(), break_pos.end());
    int break_num = break_pos.size();
    for (int i = break_num - 1; i >= 0; i--) {
        int pos = break_pos[i];
        if (pos != (int)covered_tuples.size() - 1) {
            int idx_new = covered_tuples.back();
            covered_tuples[pos] = idx_new;
            tuples_idx_to_pos[idx_new] = pos;
        }
        covered_tuples.pop_back();
    }

    int testcase_size = testcases.size();
    if (tcid != testcase_size - 1) {
        testcases[tcid] = std::move(testcases[testcase_size - 1]);
        last_greedy_time[tcid] = last_greedy_time[testcase_size - 1];
        if (cnf != NULL)
            clauses_cov[tcid] = std::move(clauses_cov[testcase_size - 1]);
        tc_covered_tuples[tcid] = std::move(tc_covered_tuples[testcase_size - 1]);
        unique_covered_tuples[tcid] = std::move(unique_covered_tuples[testcase_size - 1]);

        int idx = testcase_pos_to_idx[testcase_size - 1];
        testcase_idx_to_pos[idx] = tcid;
        testcase_pos_to_idx[tcid] = idx;
    }

    testcases.pop_back();
    last_greedy_time.pop_back();
    if (cnf != NULL)
        clauses_cov.pop_back();
    tc_covered_tuples.back().clear(allocator_);
    tc_covered_tuples.pop_back();
    unique_covered_tuples.back().clear(allocator_);
    unique_covered_tuples.pop_back();
}

void Optimizer::remove_testcase_greedily() {
    int idx = get_which_remove();
    UpdateInfo_remove_testcase(idx);
}

void Optimizer::change_bit(int v, int ad, const vector<int> &tc, vector<int> &cur_clauses_cov) {
    
    int vid = abs(v) - 1;
    int curbit = tc[vid], tt = v > 0;
    
    if (cnf != NULL) {
        const auto &pos_in_cls = cnf->get_pos_in_cls();
        const auto &neg_in_cls = cnf->get_neg_in_cls();
        if (curbit != tt) {
            const vector<int> &var_cov_old = (curbit ? pos_in_cls[vid + 1] : neg_in_cls[vid + 1]);
            const vector<int> &var_cov_new = (tt ? pos_in_cls[vid + 1] : neg_in_cls[vid + 1]);
            for (int cid : var_cov_new) cur_clauses_cov[cid] += ad;
            for (int cid : var_cov_old) cur_clauses_cov[cid] -= ad;
        }
    }
}

pair<bool, pair<int, int>> Optimizer::get_gain_for_forcetuple(int tcid, t_tuple chosen_tp) {
    const vector<int> &tc = testcases[tcid];
    
    vector<int> tc2 = tc;
    for (int i = 0; i < strength; i++) {
        tc2[abs(chosen_tp.v[i]) - 1] = (chosen_tp.v[i] > 0);
    }

    if (cnf != NULL) {
        const int nclauses = cnf->get_num_clauses();
        vector<int> &cur_clauses_cov = clauses_cov[tcid];
        for (int i = 0; i < strength; i++) {
            change_bit(chosen_tp.v[i], 1, tc, cur_clauses_cov);
        }
        bool has0 = false;
        for (int i = 0; i < nclauses; ++i) {
            if (cur_clauses_cov[i] == 0) {
                has0 = true;
                break;
            }
        }
        for (int i = 0; i < strength; i++) {
            change_bit(chosen_tp.v[i], -1, tc, cur_clauses_cov);
        }
        if (has0) return {false, {0, 0}};
    }

    else {  // TODO. ddnnf
        if (!ddnnf->check_valid_solution(tc2))
            return {false, {0, 0}};
    }

    auto res = get_gain_for_forcetestcase(tcid, tc2);
    return {true, res};
}

void Optimizer::forcetuple(int tid, t_tuple tp) {
    vector<int> tc2 = testcases[tid];

    for (int i = 0; i < strength; i++) {
        tc2[abs(tp.v[i]) - 1] = (tp.v[i] > 0);
    }

    forcetestcase(tid, tc2);
}

bool Optimizer::random_greedy_step() {
    int uncovered_cnt = uncovered_tuples.size();
    int picked_tuple = gen() % uncovered_cnt;

    int tpid = uncovered_tuples[picked_tuple];
    const t_tuple &tp = tuples_U[tpid];
    int besttcid = -1;
    long long maxi = 0;

    int testcase_size = testcases.size();
    for (int i = 0; i < testcase_size; i++) {
        if (greedy_limit - last_greedy_time[i] <= testcase_taboo) continue;

        auto res = get_gain_for_forcetuple(i, tp);
        if (res.first) {
            int net_gain = res.second.second - res.second.first;
            if (net_gain > maxi) besttcid = i, maxi = net_gain;
        }
    }

    if (besttcid != -1) {
        forcetuple(besttcid, tp);
        ++greedy_limit;
        last_greedy_time[besttcid] = greedy_limit;
        return true;
    }

    if (gen() % 100 < (uint)forced_greedy_percent) {
        return greedy_step_forced(tp);
    }

    return false;
}

bool Optimizer::greedy_step_forced(t_tuple tp) {
    const int nvar = ddnnf->get_nvar();
    int vid[mx_strength], bit[mx_strength];
    for (int i = 0; i < strength; i++) {
        vid[i] = abs(tp.v[i]) - 1;
        bit[i] = tp.v[i] > 0;
    }

    if (cnf != NULL && use_cdcl_solver) {
        cdcl_solver->clear_assumptions();
        for (int i = 0; i < strength; i++) {
            cdcl_solver->add_assumption(vid[i], bit[i]);
        }
    } else if (cnf != NULL) {
        cdcl_sampler->clear_assumptions();
        for (int i = 0; i < strength; i++) {
            cdcl_sampler->add_assumption(vid[i], bit[i]);
        }
    } else { // TODO.
        // nothing ?
    }

    int besttcid = -1;
    long long maxi = 0;
    vector<int> besttc2;

    vector<pair<int, int>> prob = vector<pair<int, int>>(nvar, make_pair(0, 0));

    int testcase_size = testcases.size();
    for (int i = 0; i < testcase_size; i++) {
        if (greedy_limit - last_greedy_time[i] <= testcase_taboo) continue;

        if (cnf != NULL && use_cdcl_solver) {
            for (int j = 0; j < nvar; j++) {
                cdcl_solver->set_polarity(j, testcases[i][j] == 0);
            }
        } else if (cnf != NULL) {
            for (int j = 0; j < nvar; j++) {
                if (testcases[i][j])
                    prob[j] = make_pair(1, 0);
                else
                    prob[j] = make_pair(0, 1);
            }
            cdcl_sampler->set_prob(prob);
        } else { // TODO. 
            std::vector<int> p_counts(ddnnf->get_nvar() + 1, 0), n_counts(ddnnf->get_nvar() + 1, 0);
            for (int j = 0; j < nvar; j++) {
                p_counts[j + 1] = (testcases[i][j] == 1);
                n_counts[j + 1] = (testcases[i][j] == 0);
            }
            ddnnf->update_weight(p_counts, n_counts);
        }

        vector<int> tc2 = vector<int>(nvar, 0);
        if (cnf != NULL && use_cdcl_solver) {
            bool ret = cdcl_solver->solve();
            search_nums++;
            if (!ret) {
                out_stream() << "c \033[1;31mError: SAT solve failing!\033[0m" << std::endl;
                return false;
            }
            cdcl_solver->get_solution(tc2);
        } else if (cnf != NULL) {
            cdcl_sampler->get_solution(tc2);
            search_nums++;
        } else { // TODO.
            tc2 = ddnnf->sampling_with_assumps(std::span(tp.v, strength), 1, gen).front();
        }

        auto res = get_gain_for_forcetestcase(i, tc2);

        int net_gain = res.second - res.first;
        if (besttcid == -1 || net_gain > maxi) {
            besttcid = i;
            besttc2 = tc2;
            maxi = net_gain;
        }
    }
    if (besttcid == -1) return false;

    forcetestcase(besttcid, besttc2);

    ++greedy_limit;
    last_greedy_time[besttcid] = greedy_limit;
    return true;
}

pair<int, int> Optimizer::get_gain_for_forcetestcase(int tcid, const vector<int> &tc2) {
    int tcid_idx = testcase_pos_to_idx[tcid];
    uptate_unique_covered(tcid_idx);
    int break_cnt = unique_covered_tuples[tcid].size();
    int gain_cnt = 0;

    for (IntrusiveListNode *p = unique_covered_tuples[tcid].head; p != NULL; p = p->nxt) {
        int tpid = p->val;
        const t_tuple &t = tuples_U[tpid];
        if (is_covered(tc2, t)) gain_cnt++;
    }

    for (int tpid : uncovered_tuples) {
        const t_tuple &t = tuples_U[tpid];
        if (is_covered(tc2, t)) gain_cnt++;
    }

    return {break_cnt, gain_cnt};
}

void Optimizer::forcetestcase(int tcid, const vector<int> &tc2) {
    int tcid_idx = testcase_pos_to_idx[tcid];

    testcase_idx_to_pos[tcid_idx] = -1;

    testcase_idx++;
    testcase_pos_to_idx[tcid] = testcase_idx;
    testcase_idx_to_pos.emplace_back(tcid);

    vector<int> break_pos, unique_id;

    for (IntrusiveListNode *p = tc_covered_tuples[tcid].head; p != NULL; p = p->nxt) {
        int tpid = p->val;
        covered_times[tpid]--;
        if (covered_times[tpid] == 0) {
            break_pos.emplace_back(tuples_idx_to_pos[tpid]);
            uncovered_tuples.emplace_back(tpid);
        }
        if (covered_times[tpid] == 1) unique_id.emplace_back(tpid);
    }

    sort(break_pos.begin(), break_pos.end());
    int break_num = break_pos.size();
    for (int i = break_num - 1; i >= 0; i--) {
        int p = break_pos[i];
        if (p != (int)covered_tuples.size()) {
            int idx_new = covered_tuples.back();
            covered_tuples[p] = idx_new;
            tuples_idx_to_pos[idx_new] = p;
        }
        covered_tuples.pop_back();
    }

    for (int tpid : unique_id) {
        update_covered_testcases(tpid);
        int idx = covered_testcases[tpid].front();
        int pos = testcase_idx_to_pos[idx];
        unique_covered_tuples[pos].insert(tpid, allocator_);
    }

    tc_covered_tuples[tcid].clear(allocator_);
    unique_covered_tuples[tcid].clear(allocator_);
    testcases[tcid] = tc2;

    for (int tpid : covered_tuples) {
        const t_tuple &t = tuples_U[tpid];
        if (is_covered(tc2, t)) {
            covered_times[tpid]++;
            tc_covered_tuples[tcid].insert(tpid, allocator_);
            covered_testcases[tpid].insert(testcase_idx, allocator_);
        }
    }

    break_pos.clear();
    int u_p = 0;
    for (int tpid : uncovered_tuples) {
        const t_tuple &t = tuples_U[tpid];
        if (is_covered(tc2, t)) {
            tuples_idx_to_pos[tpid] = covered_tuples.size();
            covered_tuples.emplace_back(tpid);
            covered_times[tpid] = 1;

            tc_covered_tuples[tcid].insert(tpid, allocator_);
            unique_covered_tuples[tcid].insert(tpid, allocator_);
            covered_testcases[tpid].insert(testcase_idx, allocator_);

            break_pos.emplace_back(u_p);
        }
        u_p++;
    }

    sort(break_pos.begin(), break_pos.end());
    break_num = break_pos.size();
    int uncovered_tuples_nums = uncovered_tuples.size();
    for (int i = break_num - 1; i >= 0; i--) {
        int p = break_pos[i];
        if (p != uncovered_tuples_nums)
            uncovered_tuples[p] = uncovered_tuples[uncovered_tuples_nums - 1];
        uncovered_tuples.pop_back();
        uncovered_tuples_nums--;
    }

    const int nvar = ddnnf->get_nvar();

    if (cnf != NULL) {
        const int nclauses = cnf->get_num_clauses();
        const auto &pos_in_cls = cnf->get_pos_in_cls();
        const auto &neg_in_cls = cnf->get_neg_in_cls();

        vector<int> &cur_clauses_cov = clauses_cov[tcid];
        cur_clauses_cov = vector<int>(nclauses, 0);
        for (int i = 0; i < nvar; i++) {
            const vector<int> &var = (tc2[i] ? pos_in_cls[i + 1] : neg_in_cls[i + 1]);
            for (int cid : var) cur_clauses_cov[cid]++;
        }
    }
}

bool Optimizer::check_for_flip(int tcid, int vid) {

    if (cnf != NULL) {
        const auto &pos_in_cls = cnf->get_pos_in_cls();
        const auto &neg_in_cls = cnf->get_neg_in_cls();

        const vector<int> &tc = testcases[tcid];
        int curbit = tc[vid];

        const vector<int> &var_cov_old = (curbit ? pos_in_cls[vid + 1] : neg_in_cls[vid + 1]);
        const vector<int> &var_cov_new = (curbit ? neg_in_cls[vid + 1] : pos_in_cls[vid + 1]);
        vector<int> &cur_clauses_cov = clauses_cov[tcid];

        bool has0 = true;
        for (int cid : var_cov_new) cur_clauses_cov[cid]++;
        for (int cid : var_cov_old) {
            cur_clauses_cov[cid]--;
            if (cur_clauses_cov[cid] == 0) has0 = false;
        }

        for (int cid : var_cov_new) --cur_clauses_cov[cid];
        for (int cid : var_cov_old) ++cur_clauses_cov[cid];

        return has0;
    }
    else { // TODO. ddnnf
        vector<int> &tc = testcases[tcid];
        tc[vid] ^= 1;
        bool valid = ddnnf->check_valid_solution(tc);
        tc[vid] ^= 1;
        return valid;
    }
}

void Optimizer::flip_bit(int tid, int vid) {
    vector<int> tc2 = testcases[tid];
    tc2[vid] ^= 1;
    forcetestcase(tid, tc2);
}

void Optimizer::random_step() {
    const int nvar = ddnnf->get_nvar();

    long long all_nums = testcases.size() * nvar;
    vector<int> flip_order;
    flip_order = vector<int>(all_nums, 0);
    std::iota(flip_order.begin(), flip_order.end(), 0);
    std::shuffle(flip_order.begin(), flip_order.end(), gen);

    for (int idx : flip_order) {
        int tid = idx / nvar, vid = idx % nvar;
        if (check_for_flip(tid, vid)) {
            flip_bit(tid, vid);
            break;
        }
    }
}

void Optimizer::search(std::chrono::milliseconds time) {
    auto begin_time = std::chrono::high_resolution_clock::now();
    cur_step = 0;
    int last_succ_step = 0;
    decltype(testcases) last_tc;  // a backup for testcases
    while ( last_succ_step < stop_length &&
           std::chrono::high_resolution_clock::now() - begin_time < time) {
        if (uncovered_tuples.empty()) {
            last_tc = testcases;
            out_stream() << "\033[;32mc current " << strength
                         << "-wise CA size: " << last_strength_testcases_size + testcases.size()
                         << ", step #" << cur_step << " \033[0m" << std::endl;
            remove_testcase_greedily();
            last_succ_step = 0;
            if (tui_state_) {
                tui_state_->opt_current_size.store(testcases.size());
                tui_state_->ca_size.store(
                    (size_t)last_strength_testcases_size + testcases.size());
            }
            continue;
        }
        cur_step++;
        last_succ_step++;

        int cyc = gen() % 100;
        if (cyc < 1 || !random_greedy_step()) {
            random_step();
        }
    }

    if (!uncovered_tuples.empty()) {
        testcases = last_tc;
    }
}

