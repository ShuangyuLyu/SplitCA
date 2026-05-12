#pragma once

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

class DDNNF;

class CNF {
    CNF() = default;

    void reset_cnf_path();

 public:
    static inline std::string_view coprocessor_path = "bin/coprocessor";

    ~CNF();
    CNF(const CNF &);
    CNF(CNF &&) noexcept;
    CNF &operator=(const CNF &);
    CNF &operator=(CNF &&) noexcept;

    static auto parse(std::istream &cnf) -> std::unique_ptr<CNF>;
    static auto parse(const std::string &cnf_file) -> std::unique_ptr<CNF> {
        std::ifstream cnf(cnf_file);
        if (!cnf.is_open()) {
            throw std::runtime_error("Failed to open CNF file.");
        }
        auto result = parse(cnf);
        result->cnf_file_path_ = cnf_file;
        return result;
    }
    void print(std::ostream &cnf) const;
    void print(const std::string &cnf_file) const {
        std::ofstream cnf(cnf_file);
        if (!cnf.is_open()) {
            throw std::runtime_error("Failed to open CNF file for writing.");
        }
        print(cnf);
    }
    void reduce_cnf();

    bool validate(std::span<const int> row) const {
        return std::ranges::all_of(clauses_, [&](const auto &cl) {
            return std::ranges::any_of(cl, [&](auto lit) {
                int opt = std::abs(lit) - 1;
                return lit < 0 ? ~row[opt] & 1 : row[opt] & 1;
            });
        });
    }

    /// MARK: Basic Information

    const std::string &get_cnf_file_path();
    unsigned get_num_variables() const { return num_variables_; }
    unsigned get_num_cared() const { return num_cared_; }
    unsigned get_num_clauses() const { return clauses_.size(); }
    const auto &get_clauses() const { return clauses_; }
    const auto &get_pos_in_cls() const { return pos_in_cls_; }
    const auto &get_neg_in_cls() const { return neg_in_cls_; }
    const auto &get_group_info() const { return group_info_; }

    /// MARK: Modification

    void add_clause(std::vector<int> vec) {
        reset_cnf_path();
        int idx = clauses_.size();
        for (auto const &lit : vec) {
            if (lit < 0) {
                neg_in_cls_[-lit].push_back(idx);
            } else {
                pos_in_cls_[lit].push_back(idx);
            }
        }
        clauses_.push_back(std::move(vec));
    }

    void add_variables(unsigned size = 1) {
        reset_cnf_path();
        num_variables_ += size;
        pos_in_cls_.resize(pos_in_cls_.size() + size);
        neg_in_cls_.resize(neg_in_cls_.size() + size);
    }

    /// Convert
    std::unique_ptr<DDNNF> convert_to_ddnnf() const;

 private:
    std::string cnf_file_path_;
    bool delete_cnf_file_ = false;
    unsigned num_variables_ = 0;
    unsigned num_cared_ = 0;
    std::vector<std::vector<int>> clauses_;

    void calc_cnf_info();
    std::vector<std::vector<int>> pos_in_cls_;
    std::vector<std::vector<int>> neg_in_cls_;
    std::vector<std::vector<int>> group_info_;
};
