#include "ddnnf.hpp"
#include "util/dbg.hpp"
#include <algorithm>
#include <cassert>
#include <format>
#include <queue>
#include <ranges>
#include <unordered_set>
#include <vector>

std::unique_ptr<DDNNF> DDNNF::parse(std::istream &is, int nvar) {
    std::unique_ptr<DDNNF> ddnnf(new DDNNF());
    std::string c;
    is >> c;
    assert(c == "nnf");
    int N;
    is >> N >> ddnnf->nvar /* ignore one integer */ >> ddnnf->nvar;
    nvar = std::max(ddnnf->nvar, nvar);

    ddnnf->root = N - 1;
    ddnnf->nodes.reserve(N);

    ddnnf->lit[0] = std::vector<int>(nvar + 1, -1);
    ddnnf->lit[1] = std::vector<int>(nvar + 1, -1);

    const auto assign_lit_node = [&ddnnf](Node &node, int l) {
        node.ty = LIT;
        node.tar.push_back(l);
        node.vars.emplace(var_of(l), -1);
        assert(ddnnf->lit[l > 0][abs(l)] == -1);
        ddnnf->lit[l > 0][abs(l)] = ddnnf->nodes.size() - 1;
    };
    while (is >> c) {
        auto &node = ddnnf->nodes.emplace_back();
        if (c == "L") {
            int l;
            is >> l;
            assign_lit_node(node, l);
        } else if (c == "A") {
            int ch_num;
            is >> ch_num;
            node.ty = AND;
            for (int i = 0, ch; i < ch_num; i++)
                is >> ch, node.tar.push_back(ch);
        } else if (c == "O") {
            int tmp;
            is >> tmp;
            assert(tmp == 0);
            int ch_num;
            is >> ch_num;
            node.ty = OR;
            for (int i = 0, ch; i < ch_num; i++)
                is >> ch, node.tar.push_back(ch);
        } else {
            throw std::runtime_error(std::format("unknown node type: {}", c));
        }
    }

    auto &root_node = ddnnf->nodes[ddnnf->root];
    if (root_node.ty != AND && ddnnf->nvar < nvar) {
        auto &node = ddnnf->nodes.emplace_back();
        node.ty = AND;
        node.tar.push_back(ddnnf->root);
        ddnnf->root = ddnnf->nodes.size() - 1;
    }
    for (auto &root = ddnnf->nodes[ddnnf->root]; ddnnf->nvar < nvar; ) {
        dbg(ddnnf->nvar, nvar);
        assert(root.ty != LIT);
        int vv = ddnnf->nvar++;
        int n1 = ddnnf->nodes.size();
        {
            int l = mk_lit(vv, false);
            auto &node = ddnnf->nodes.emplace_back();
            assign_lit_node(node, l);
        }
        int n2 = ddnnf->nodes.size();
        {
            int l = mk_lit(vv, true);
            auto &node = ddnnf->nodes.emplace_back();
            assign_lit_node(node, l);
        }
        int n3 = ddnnf->nodes.size();
        {
            auto &node = ddnnf->nodes.emplace_back();
            node.ty = OR;
            node.tar.push_back(n1);
            node.tar.push_back(n2);
        }
        // Add to root...
        root.tar.push_back(n3);
    }
    ddnnf->nodes.shrink_to_fit();

    std::vector<int> degrees(ddnnf->nodes.size());
    std::queue<int> queue;
    ddnnf->rev_order.clear();
    for (size_t idx = 0; auto &node : ddnnf->nodes) {
        if (node.ty == LIT)
            queue.push(idx);
        else {
            degrees[idx] = node.tar.size();
            for (int x : node.tar) ddnnf->nodes[x].from.push_back(idx);
        }
        ++idx;
    }
    while (!queue.empty()) {
        int u = queue.front();
        ddnnf->rev_order.emplace_back(u);
        queue.pop();
        for (auto p : ddnnf->nodes[u].from) {
            for (auto [v, _] : ddnnf->nodes[u].vars) ddnnf->nodes[p].vars.emplace(v, u);
            if (--degrees[p] == 0) queue.push(p);
        }
    }

    ddnnf->topsort_order = ddnnf->rev_order;
    std::reverse(ddnnf->topsort_order.begin(), ddnnf->topsort_order.end());
    
    ddnnf->calc_bias();
#ifndef NDEBUG
    ddnnf->check_ddnnf_valid();
#endif
    return ddnnf;
}

void DDNNF::calc_bias() {
    bias.clear();
    bias.resize(nodes.size(), .0);
    bias[root] = 1;
    for (int x : topsort_order) {
        const auto &nodex = nodes[x];
        if (nodex.ty == AND) {
            for (int y : nodex.tar)
                bias[y] += bias[x];
        } else if (nodex.ty == OR) {
            size_t sz = nodex.tar.size();
            for (int y : nodex.tar)
                bias[y] += bias[x] / sz;
        }
    }
}

bool DDNNF::check_valid_solution (const std::vector<int> &tc) {
    std::vector<uint8_t> flag(nodes.size());
    for (int i = 0; i < nvar; i ++) {
        if (~lit[tc[i]][i + 1]) flag[lit[tc[i]][i + 1]] = true;
        if (~lit[tc[i] ^ 1][i + 1]) flag[lit[tc[i] ^ 1][i + 1]] = false;
    }
    for (int x : rev_order) {
        const auto &nodex = nodes[x];
        if (nodex.ty == LIT)
            continue ;
        else if (nodex.ty == OR) {
            flag[x] = false;
            for (int y : nodex.tar)
                flag[x] |= flag[y];
        }
        else if (nodex.ty == AND) {
            flag[x] = true;
            for (int y : nodex.tar)
                flag[x] &= flag[y];
        }
    }
    return flag[root];
}

std::vector<std::vector<int>> DDNNF::sampling_random (int samples, std::mt19937_64 &gen, bool output) const {
    std::vector<std::vector<int>> sol(nodes.size());
    for (int i = 0; i < samples; i ++)
        sol[root].push_back(i);

    for (int x : topsort_order) {
        const auto &node = nodes[x];
        if (sol[x].empty())
            continue;
        if (node.ty == AND) {
            for (int y : node.tar)
                for (int id : sol[x])
                    sol[y].emplace_back(id);
        }
        else if (node.ty == OR) {
            if (!node.tar.empty()) {
                for (int id : sol[x]) {
                    int random_idx = gen() % node.tar.size();
                    sol[node.tar[random_idx]].push_back(id);
                }
            }
        }
    }
    return get_samples_mat(samples, sol, gen, output);
}

std::vector<std::vector<int>> DDNNF::sampling_leaf_to_root (
    int samples, std::mt19937_64 &gen, const std::vector<int> &p_counts, const std::vector<int> &n_counts) {
    
    flow = std::vector<int>(nodes.size(), 0);
    contribute.clear();
    contribute.resize(nodes.size());

    std::vector<int> order(nvar);
    std::iota(order.begin(), order.end(), 1);
    // std::shuffle(order.begin(), order.end(), gen);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        int da = std::abs(p_counts[a] - n_counts[a]);
        int db = std::abs(n_counts[b] - p_counts[b]); // 等价于 abs(p_counts[b] - n_counts[b])
        return da > db;   // 从大到小
    });

    // int t = nvar * 0.05;
    int t = nvar;
    for (int v : order) {
        if ((--t) <= 0) break ;
        int p = p_counts[v], q = n_counts[v];
        if (p + q == 0 || lit[0][v] == -1 || lit[1][v] == -1)
            continue ;
        
        int f = samples * 1.0 * p / (p + q);
        modify(lit[1][v], f, gen);
        modify(lit[1][v], f, gen);

        // if (flow[root] == 0) {
        //     if (~lit[1][v])
        //         modify(lit[1][v], f, gen);
        //     else
        //         modify(lit[0][v], samples, gen);

        //     if (~lit[0][v]) modify(lit[0][v], samples - f, gen);
        //     else modify(lit[1][v], samples, gen);
        // }
        // else if ((~lit[1][v]) && (~lit[0][v]) && flow[lit[1][v]] < f * 0.8) {
        //     modify(lit[1][v], f - flow[lit[1][v]], gen);
        //     modify(lit[0][v], (- f + flow[lit[1][v]]) / 2, gen);
        // }
        // else if ((~lit[1][v]) && (~lit[0][v]) && flow[lit[1][v]] > f * 1.5) {
        //     modify(lit[1][v], (f - flow[lit[1][v]]) / 2, gen);
        //     modify(lit[0][v], - f + flow[lit[1][v]], gen);
        // }
    }

    return sample_flow(samples, gen);
}

std::vector<std::vector<int>> DDNNF::sampling_with_tuples (int samples, std::mt19937_64 &gen, const TupleVector &tuples) {
    int strength = tuples.get_strength();
    flow = std::vector<int>(nodes.size(), 0);
    contribute.clear();
    contribute.resize(nodes.size());

    if (tuples.size() < 1000) {
        for (const t_tuple &t : tuples) {
            for (int i = 0; i < strength; i ++)
                modify(lit[t.v[i] > 0][abs(t.v[i])], 10, gen);
        }
    }
    else {
        for (int times = 0; times < 100; times ++) {
            const auto &t = tuples[gen() % tuples.size()];
            for (int i = 0; i < strength; i ++)
                modify(lit[t.v[i] > 0][abs(t.v[i])], 10, gen);
        }
    }

    int index = gen() % tuples.size();
    const auto &t = tuples[index];
    dbg(t.v);
    for (int i = 0; i < strength; i ++) {
        int lit_node = lit[t.v[i] > 0][abs(t.v[i])];
        int neg_lit_node = lit[t.v[i] < 0][abs(t.v[i])];
        if (flow[neg_lit_node] > 0) {
            // dbg(neg_lit_node, flow[neg_lit_node]);
            int f = flow[neg_lit_node];
            modify(lit_node, f, gen);
            modify(neg_lit_node, -f, gen);
            dbg(lit_node, neg_lit_node);
            dbg(flow[lit_node], flow[neg_lit_node]);
            for ([[maybe_unused]] int y : nodes[neg_lit_node].from)
                assert(contribute[y][neg_lit_node] == 0);
        }
    }
    
    const auto vec = sample_flow(samples, gen);
    
    for (const auto &tc : vec)
        for (int i = 0; i < strength; i ++) {
            if (tc[abs(t.v[i]) - 1] != (t.v[i] > 0))
                dbg(t.v[i], tc[abs(t.v[i]) - 1]);
            assert(tc[abs(t.v[i]) - 1] == (t.v[i] > 0));
        }
        
    return vec;
}

std::vector<std::vector<int>> DDNNF::sampling_with_assumps (
    std::span<const int> lits, int samples, std::mt19937_64 &gen, bool use_weight) const {
    std::vector<int> assump_status(nodes.size(), -1);
    for (int l : lits) {
        if (lit[l > 0][abs(l)] == -1) return {}; // invalid...
        assump_status[lit[l > 0][abs(l)]] = 1;
        if (int id = lit[l < 0][abs(l)]; ~id)
            assump_status[id] = 0;
    }
    for (int x : rev_order) {
        const auto &node = nodes[x];
        if (node.ty == AND) {
            assump_status[x] = 1;
            for (auto y : node.tar)
                if (assump_status[y] == 0) {
                    assump_status[x] = 0;
                    break;
                } else if (assump_status[y] == -1) {
                    assump_status[x] = -1;
                }
        } else if (node.ty == OR) {
            assump_status[x] = 0;
            for (auto y : node.tar)
                if (assump_status[y] == 1) {
                    assump_status[x] = 1;
                    break;
                } else if (assump_status[y] == -1) {
                    assump_status[x] = -1;
                }
        }
    }
    if (assump_status[root] == 0) return {}; // invalid...
    std::vector<std::vector<int>> solutions(samples, std::vector<int>(nvar));
    std::vector<std::vector<int>> node_sols(nodes.size());

    for (int i = 0; i < samples; ++i)
        node_sols[root].push_back(i);
    for (int x : topsort_order) {
        const auto &node = nodes[x];
        if (node_sols[x].empty()) continue;
        if (node.ty == LIT) {
            int lit = get_literal(x);
            for (int id : node_sols[x]) {
                solutions[id][var_of(lit)] = lit > 0;
            }
        } else if (node.ty == AND) {
            for (int y : node.tar)
                for (int id : node_sols[x])
                    node_sols[y].push_back(id);
        } else if (node.ty == OR) {
            std::vector<double> w;
            if (use_weight) {
                for (int y : node.tar) {
                    if (assump_status[y] == 1) {
                        for (int id : node_sols[x])
                            node_sols[y].push_back(id);
                        goto next_loop;
                    } else if (assump_status[y] == 0) {
                        w.push_back(0.0);
                    } else {
                        w.push_back(weight[y]);
                    }
                }
            } else {
                for (int y : node.tar) {
                    if (assump_status[y] == 1) {
                        for (int id : node_sols[x])
                            node_sols[y].push_back(id);
                        goto next_loop;
                    } else if (assump_status[y] == 0) {
                        w.push_back(0.0);
                    } else {
                        w.push_back(1.0);
                    }
                }
                // w = std::vector<double>(node.tar.size(), 1);
            }
            std::discrete_distribution<> dist(w.begin(), w.end());
            for (int id : node_sols[x])
                node_sols[node.tar[dist(gen)]].push_back(id);
        } else {
            __builtin_unreachable();
        }
        next_loop:;
    }

    return solutions;
}

void DDNNF::update_weight (const std::vector<int> &p_counts, const std::vector<int> &n_counts) {
    update_leaf_weight(p_counts, n_counts);
    for (int x : rev_order) {
        const auto &node = nodes[x];
        if (node.ty == LIT);
        else if (node.ty == OR) {
            weight[x] = 0.0;
            for (int y : node.tar)
                weight[x] += weight[y];
            weight[x] /= node.tar.size();
        }
        else {
            weight[x] = 0.0;
            for (int y : node.tar)
                weight[x] += weight[y];
        }
    }
}

void DDNNF::check_ddnnf_valid() const {
    for (size_t node_idx = 0; auto &node : nodes) {
        switch (node.ty) {
        case AND: {
            std::unordered_set<int> child_lits;
            for (int c : node.tar) {
                for (auto [l, _] : nodes[c].vars) {
                    if (child_lits.contains(l)) {
                        throw std::runtime_error(std::format("AND error at: {}", node_idx));
                    }
                    child_lits.insert(l);
                }
            }
            assert(child_lits.size() == node.vars.size());
        } break;
        case OR: {
            constexpr auto get_key = [](auto &&pair) { return pair.first; };
            auto _my_lits = node.vars | std::views::transform(get_key);
            std::unordered_set my_lits(_my_lits.begin(), _my_lits.end());
            for (int c : node.tar) {
                auto _child_lits = nodes[c].vars | std::views::transform(get_key);
                std::unordered_set child_lits(_child_lits.begin(), _child_lits.end());
                if (my_lits != child_lits) {
                    throw std::runtime_error(std::format("OR error at: {}", node_idx));
                }
            }
        } break;
        case LIT: {
            if (node.vars.size() != 1) {
                throw std::runtime_error(std::format("LIT error", node_idx));
            }
        } break;
        }
        ++node_idx;
    }
}

void DDNNF::print(std::ostream &os) const {
    os << std::format("nnf {} {} {}\n", nodes.size(), 0, nvar);
    for (auto &node : nodes) {
        switch (node.ty) {
        case LIT: os << "L " << node.tar.front() << '\n'; break;
        case AND:
            os << "A " << node.tar.size();
            for (auto t : node.tar) os << ' ' << t;
            os << '\n';
            break;
        case OR:
            os << "O 0 " << node.tar.size();
            for (auto t : node.tar) os << ' ' << t;
            os << '\n';
            break;
        default: __builtin_unreachable();
        }
    }
}

std::vector<int> DDNNF::get_a_best_solution (
        int strength, std::mt19937_64 &gen, const TupleVector &tuples) {
    
    covered.clear();

    int max_times = 1000;
    if (tuples.size() > max_times) {
        while (max_times --) {
            const auto &t = tuples[gen() % tuples.size()];
            set_covered(strength, root, std::span(t.v,strength));
            assert(covered[root] > 0);
        }
    }
    else {
        for (const auto &t : tuples)
            set_covered(strength, root, std::span(t.v,strength));
    }
    dbg(covered[root]);

    std::vector<int> sol(nvar, -1);

    std::queue<int> q;
    q.push(root);
    while (! q.empty()) {
        int x = q.front(); q.pop();
        const auto &nodex = nodes[x];
        if (nodex.ty == LIT) {
            int lit = get_literal(x);
            sol[var_of(lit)] = lit > 0;
        }
        else if (nodex.ty == OR) {
            int mx_covered = -1;
            std::vector<int> choices;
            for (int y : nodex.tar) {
                if (covered[y] > mx_covered) {
                    mx_covered = covered[y];
                    choices.clear(), choices.push_back(y);
                }
                else if (covered[y] == mx_covered)
                    choices.push_back(y);
            }
            assert(!choices.empty());
            int y = choices[gen() % choices.size()];
            q.push(y);
        }
        else if (nodex.ty == AND) {
            for (int y : nodex.tar)
                q.push(y);
        }
    }

    for ([[maybe_unused]] int v : sol)
        assert(~v);

    return sol;
}