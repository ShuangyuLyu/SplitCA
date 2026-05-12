#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <fstream>
#include <memory>
#include <queue>
#include <random>
#include <ranges>
#include <span>
#include <stack>
#include <unordered_map>
#include <vector>
#include <iostream>

#include "util/tuple_vector.hpp"

class DDNNF {
    friend class CNF;
    DDNNF() = default;

 public:
    enum NodeType { AND, OR, LIT };
    static inline char to_char(NodeType ty) {
        constexpr char chs[] = {'A', 'O', 'L'};
        return chs[static_cast<int>(ty)];
    }

 private:
    struct Node {
        std::unordered_map<int, int> vars;
        std::vector<int> tar;
        std::vector<int> from;
        NodeType ty;
        int var_at(int v) const {
            if (auto it = vars.find(v); it != vars.end()) {
                return it->second;
            } else {
                return -1;
            }
        }
    };
    static inline const std::vector<int> empty_tar{};

    int set_covered(int strength, int node_id, std::span<const int> tuple) {
        auto &node = nodes[node_id];
        if (node.ty == LIT) {
            assert(tuple.size() == 1);
            return tuple.front() == node.tar.front();
        }
        else if (node.ty == OR) {
            int val = 0;
            for (auto t : node.tar)
                val |= set_covered(strength, t, tuple);
            if (val && tuple.size() == strength)
                covered[node_id] ++;
            return val;
        }
        else if (node.ty == AND) {
            std::unordered_map<int, std::vector<int>> children;
            for (auto lit : tuple) {
                int child = node.var_at(var_of(lit));
                if (child == -1) continue;
                children[child].push_back(lit);
            }
            int val = 1;
            for (auto &[c, t] : children)
                val &= set_covered(strength, c, t);
            if (val && tuple.size() == strength)
                covered[node_id] ++;
            return val;
        }
        else
            __builtin_unreachable();
    }

    template <size_t N>
    bool check_valid_impl(int node_id, const std::array<int, N> &tuple) const {
        if (node_id == -1) return true;
        auto &tar = nodes[node_id].tar;
        switch (nodes[node_id].ty) {
        case LIT: {
            if (tuple[0] == tar.front()) return true;
            if (tuple[0] == -tar.front()) return false;
            return true;
        }
        case OR: {
            for (auto t : tar)
                if (check_valid_impl(t, tuple)) return true;
            return false;
        }
        case AND: {
            auto &node = nodes[node_id];
            if constexpr (N == 1) {
                int child = node.var_at(var_of(tuple[0]));
                return check_valid_impl(child, tuple);
            } else if constexpr (N == 2) {
                int c1 = node.var_at(var_of(tuple[0])),
                    c2 = node.var_at(var_of(tuple[1]));
                if (c1 == c2) {
                    return check_valid_impl(c1, tuple);
                } else {
                    std::array<int, 1> t1{tuple[0]};
                    std::array<int, 1> t2{tuple[1]};
                    return check_valid_impl(c1, t1) && check_valid_impl(c2, t2);
                }
            } else if constexpr (N == 3) {
                auto [v1, v2, v3] = tuple;
                int c1 = node.var_at(var_of(v1)),
                    c2 = node.var_at(var_of(v2)),
                    c3 = node.var_at(var_of(v3));
                if (c1 == c2 && c2 == c3) {
                    return check_valid_impl(c1, tuple);
                } else if (c1 == c2) {
                    std::array<int, 2> t12{v1, v2};
                    std::array<int, 1> t3{v3};
                    return check_valid_impl(c1, t12) && check_valid_impl(c3, t3);
                } else if (c1 == c3) {
                    std::array<int, 2> t13{v1, v3};
                    std::array<int, 1> t2{v2};
                    return check_valid_impl(c1, t13) && check_valid_impl(c2, t2);
                } else if (c2 == c3) {
                    std::array<int, 1> t1{v1};
                    std::array<int, 2> t23{v2, v3};
                    return check_valid_impl(c1, t1) && check_valid_impl(c2, t23);
                } else {
                    std::array<int, 1> t1{v1}, t2{v2}, t3{v3};
                    return check_valid_impl(c1, t1) && check_valid_impl(c2, t2) &&
                           check_valid_impl(c3, t3);
                }
            } else if constexpr (N == 4) {
                auto [v1, v2, v3, v4] = tuple;
                int c1 = node.var_at(var_of(v1)),
                    c2 = node.var_at(var_of(v2)),
                    c3 = node.var_at(var_of(v3)),
                    c4 = node.var_at(var_of(v4));
                using std::swap;
                if (c1 > c2) swap(v1, v2), swap(c1, c2);
                if (c2 > c3) swap(v2, v3), swap(c2, c3);
                if (c3 > c4) swap(v3, v4), swap(c3, c4);
                if (c1 > c2) swap(v1, v2), swap(c1, c2);
                if (c2 > c3) swap(v2, v3), swap(c2, c3);
                if (c1 > c2) swap(v1, v2), swap(c1, c2);
                if (c1 == c2) {
                    if (c2 == c3) {
                        if (c3 == c4) {
                            return check_valid_impl(c1, tuple);
                        } else {
                            return check_valid_impl(c1, std::array{v1, v2, v3})
                                && check_valid_impl(c4, std::array{v4});
                        }
                    } else {
                        if (c3 == c4) {
                            return check_valid_impl(c1, std::array{v1, v2})
                                && check_valid_impl(c3, std::array{v3, v4});
                        } else {
                            return check_valid_impl(c1, std::array{v1, v2})
                                && check_valid_impl(c3, std::array{v3})
                                && check_valid_impl(c4, std::array{v4});
                        }
                    }
                } else {
                    if (c2 == c3) {
                        if (c3 == c4) {
                            return check_valid_impl(c1, std::array{v1})
                                && check_valid_impl(c2, std::array{v2, v3, v4});
                        } else {
                            return check_valid_impl(c1, std::array{v1})
                                && check_valid_impl(c2, std::array{v2, v3})
                                && check_valid_impl(c4, std::array{v4});
                        }
                    } else {
                        if (c3 == c4) {
                            return check_valid_impl(c1, std::array{v1})
                                && check_valid_impl(c2, std::array{v2})
                                && check_valid_impl(c3, std::array{v3, v4});
                        } else {
                            return check_valid_impl(c1, std::array{v1})
                                && check_valid_impl(c2, std::array{v2})
                                && check_valid_impl(c3, std::array{v3})
                                && check_valid_impl(c4, std::array{v4});
                        }
                    }
                }
            } else {
                static_assert(N >= 1 && N <= 4);
            }
            break;
        }
        default: __builtin_unreachable();
        }
    }

    void update_leaf_weight (const std::vector<int> &p_counts, 
                             const std::vector<int> &n_counts) {
        
        weight.resize(nodes.size());
        
        [[maybe_unused]] const auto dqtanh = [](double x) {
            constexpr double k = 5;
            return (tanh(k * x) / tanh(k) + 1) / 2;
        };
        for (int v = 1; v <= nvar; v ++) {
            int p = p_counts[v] + 5, q = n_counts[v] + 5;
            int total = p + q;
            assert (total > 0);
            if (~lit[0][v]) weight[lit[0][v]] = 1.0 * q / total; // dqtanh(1.0 * q / total - bias[lit[0][v]]);
            if (~lit[1][v]) weight[lit[1][v]] = 1.0 * p / total; // dqtanh(1.0 * p / total - bias[lit[1][v]]);
        }
    }

    std::vector<std::vector<int>> get_samples_mat (int samples, const std::vector<std::vector<int>> &sol,
                                                   std::mt19937_64 &gen, bool output = false) const {
        std::vector<std::vector<int>> samples_mat(samples, std::vector<int>(nvar, -1));
        for (int i = 1; i <= nvar; i++) {
            
            if (~lit[0][i])
                for (int id : sol[lit[0][i]])
                    samples_mat[id][i - 1] = 0;
            
            if (~lit[1][i])
                for (int id : sol[lit[1][i]])
                    samples_mat[id][i - 1] = 1;
            
            if (output)
                std::cout << sol[lit[0][i]].size() << "/" 
                          << (sol[lit[0][i]].size() + sol[lit[1][i]].size()) << ' ';
        }
        if (output) std::cout << std::endl;

        for ([[maybe_unused]] auto &tc : samples_mat) {
            for (int i = 0; i < nvar; i ++)
                assert(tc[i] != -1);
        }
        return samples_mat;
    }

    std::vector<int> split_int_non_negative (int S, int n, std::mt19937_64 &gen) const {
        std::uniform_int_distribution<int> dis(0, S);
        std::vector<int> cut_points;
        for (int i = 0; i < n - 1; ++i) {
            cut_points.push_back(dis(gen));
        }
        cut_points.push_back(0);
        cut_points.push_back(S);
        std::sort(cut_points.begin(), cut_points.end());
        std::vector<int> parts;
        for (int i = 0; i < n; ++i) {
            parts.push_back(cut_points[i + 1] - cut_points[i]);
        }
        return parts;
    }

    std::vector<int> split (int x, int samples, std::mt19937_64 &gen, bool is_from = false) {
        const auto &nodex = nodes[x];
        const auto &array_y = is_from ? nodex.from : nodex.tar;
        std::vector<int> contri(array_y.size());
        auto w = array_y | std::views::transform([&](int y) { return is_from ? weight[y] / nodes[y].vars.size() : weight[y]; });
        std::discrete_distribution<> dist(w.begin(), w.end());

        for (int s = 0; s < samples; s ++) {
            int random_idx = dist(gen);
            contri[random_idx] ++;
        }
        return contri;
    }

    void pushdown (int x, int f, std::mt19937_64 &gen) {
        static std::stack<std::pair<int, int>> pushdown_stack;
        flow[x] += f;
        assert(flow[x] >= 0);
        const auto &nodex = nodes[x];

        if (nodex.tar.empty() || f == 0) return;
        pushdown_stack.push({x, f});
        if (nodex.ty == AND) {
            for (int y : nodex.tar) contribute[x][y] += f, pushdown(y, f, gen);
        } else if (nodex.ty == OR) {
            if (f > 0) {
                // auto parts = split_int_non_negative(std::abs(f), nodex.tar.size(), gen);
                auto parts = split(x, std::abs(f), gen);
                int i = 0;
                for (int y : nodex.tar) {
                    int local_f = parts[i ++];
                    contribute[x][y] += local_f, pushdown(y, local_f, gen);
                }
            } else {
                std::vector<int> ord(nodex.tar.size());
                std::iota(ord.begin(), ord.end(), 0);
                std::shuffle(ord.begin(), ord.end(), gen);
                int F = -f;
                for (int id : ord) {
                    int y = nodex.tar[id];
                    if (F <= 0) break;
                    if (contribute[x][y] == 0) continue;
                    if (F > contribute[x][y]) {
                        int local_f = contribute[x][y];
                        F -= local_f;
                        contribute[x][y] = 0;
                        pushdown(y, -local_f, gen);
                    } else {
                        contribute[x][y] -= F;
                        pushdown(y, -F, gen);
                        F = 0;
                    }
                }
            }
        }
        pushdown_stack.pop();
    }

    void modify (int x, int f, std::mt19937_64 &gen) {
        if (f == 0) return ;
        struct Flow { int u, f, v; };
        std::queue<Flow> q;
        q.emplace(x, f, -1);

        while (! q.empty()) {
            auto [x, f, child] = q.front(); q.pop();
            // dbg(x, flow.size());
            flow[x] += f;
            assert(flow[x] >= 0);
            if (f == 0) continue;

            const auto &nodex = nodes[x];
            if (child != -1 && nodex.ty == AND) {
                for (int y : nodex.tar) {
                    if (y == child) continue ;
                    contribute[x][y] += f;
                    pushdown(y, f, gen);
                }
            }
            if (nodex.from.empty())
                continue;
            if (f > 0) {
                // auto parts = split_int_non_negative(std::abs(f), nodex.from.size(), gen);
                auto parts = split(x, std::abs(f), gen, true);
                int i = 0;
                for (int y : nodex.from) {
                    int local_f = parts[i ++];
                    if (local_f == 0) continue;
                    contribute[y][x] += local_f;
                    q.emplace(y, local_f, x);
                }
            } else {
                std::vector<int> ord(nodex.from.size());
                std::iota(ord.begin(), ord.end(), 0);
                std::shuffle(ord.begin(), ord.end(), gen);
                int F = -f;
                for (int id : ord) {
                    int y = nodex.from[id];
                    if (F <= 0) break;
                    if (contribute[y][x] == 0) continue;
                    if (F > contribute[y][x]) {
                        int local_f = contribute[y][x];
                        F -= local_f;
                        contribute[y][x] = 0;
                        q.emplace(y, -local_f, x);
                    } else {
                        contribute[y][x] -= F;
                        q.emplace(y, -F, x);
                        F = 0;
                    }
                }
                assert (F == 0);
            }
        }
        return ;
    }

    auto sample_flow (int samples, std::mt19937_64 &gen) {
        std::vector<std::vector<int>> sol(nodes.size());
        for (int i = 0; i < samples; i ++)
            sol[root].push_back(i);
        for (int x : topsort_order) {
            const auto &node = nodes[x];
            if (sol[x].empty())
                continue;
            if (node.ty == AND) {
                for (int y : node.tar) {
                    for (int id : sol[x])
                        sol[y].emplace_back(id);
                }
            }
            else if (node.ty == OR) {
                if (!node.tar.empty()) {
                    std::vector<int> w;
                    [[maybe_unused]] int sumw = 0;
                    for (int y : node.tar) {
                        w.push_back(contribute[x][y]);
                        sumw += contribute[x][y];
                    }
                    assert(sumw > 0 && sumw == flow[x]);
                    std::discrete_distribution<int> dist(w.begin(), w.end());
                    for (int id : sol[x]) {
                        int random_idx = dist(gen);
                        assert(w[random_idx] != 0);
                        sol[node.tar[random_idx]].push_back(id);
                    }
                }
            }
        }
        return get_samples_mat(samples, sol, gen);
    }

 public:
    static inline std::string_view d4v2_path = "bin/d4v2";
    static inline std::string_view fastfmc_path = "bin/FastFMC";

    static uint var_of(int lit) { return std::abs(lit) - 1; }
    static int mk_lit(uint var, bool neg) { return neg ? -(int)(var + 1) : (int)(var + 1); }

    static std::unique_ptr<DDNNF> parse(std::istream &ddnnf, int nvar = 0);
    static std::unique_ptr<DDNNF> parse(const std::string &ddnnf_file, int nvar = 0) {
        std::ifstream ddnnf(ddnnf_file);
        if (!ddnnf.is_open())
            throw std::runtime_error("Failed to open smooth d-DNNF file.");
        return parse(ddnnf, nvar);
    }
    void print(std::ostream &ddnnf) const;
    void print(const std::string &ddnnf_file) const {
        std::ofstream ddnnf(ddnnf_file);
        if (!ddnnf.is_open()) {
            throw std::runtime_error("Failed to open smooth d-DNNF file for writing.");
        }
        print(ddnnf);
    }

    NodeType get_nodetype(int node_id) const { return nodes[node_id].ty; }
    const std::vector<int> &get_targets(int node_id) const {
        return nodes[node_id].ty == LIT ? empty_tar : nodes[node_id].tar;
    }
    int get_literal(int node_id) const {
        assert(nodes[node_id].ty == LIT);
        return nodes[node_id].tar.front();
    }
    const auto &get_vars(int node_id) const { return nodes[node_id].vars; }
    int get_nvar() const { return nvar; }

    bool check_valid_solution (const std::vector<int> &tc);

    template <size_t N>
        requires(N >= 1 && N <= 4)
    bool check_valid(const std::array<int, N> &tuple) const {
        return check_valid_impl(root, tuple);
    }

    void update_weight (const std::vector<int> &p_counts, const std::vector<int> &n_counts);

    std::vector<std::vector<int>> sampling (int samples, std::mt19937_64 &gen, bool output = false) const;
    std::vector<std::vector<int>> sampling_random (int samples, std::mt19937_64 &gen, bool output = false) const;

    std::vector<std::vector<int>> sampling_leaf_to_root (
        int samples, std::mt19937_64 &gen, const std::vector<int> &p_counts, const std::vector<int> &n_counts);

    std::vector<std::vector<int>> sampling_with_tuples (
        int samples, std::mt19937_64 &gen, const TupleVector &tuples);

    std::vector<std::vector<int>> sampling_with_assumps (
        std::span<const int> lits, int samples, std::mt19937_64 &gen, bool use_weight = true) const;
    
    std::vector<int> get_a_best_solution (
        int strength, std::mt19937_64 &gen, const TupleVector &tuples);

 private:
    constexpr static double MINUS_INF = -1e15;
    int nvar, root;
    std::vector<Node> nodes;

    std::vector<double> weight;
    std::vector<double> bias;
    std::vector<int> lit[2];

    void calc_bias();
    void check_ddnnf_valid() const;

    std::vector<int> topsort_order;
    std::vector<int> rev_order;

    std::vector<int> flow;
    std::vector<std::unordered_map<int, int>> contribute;

    std::unordered_map<int, int> covered;
};
