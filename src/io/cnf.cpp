#include "io/cnf.hpp"
#include "io/ddnnf.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <sstream>
#include <unistd.h>

/// run command in bash and redirect output to std::cout
inline int exec_drop_output(std::string command) {
    command += " 1>/dev/null 2>&1";
    FILE *pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error(std::format("popen error: {}", errno));
    }
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) {
        std::cout << buf;
    }
    return pclose(pipe);
}

void CNF::reset_cnf_path() {
    if (delete_cnf_file_ && !cnf_file_path_.empty()) {
        std::filesystem::remove(cnf_file_path_);
    }
    cnf_file_path_.clear();
    delete_cnf_file_ = false;
}

CNF::~CNF() {
    if (delete_cnf_file_ && !cnf_file_path_.empty()) {
        std::filesystem::remove(cnf_file_path_);
    }
}

CNF::CNF(const CNF &other)
    : cnf_file_path_(""),
      delete_cnf_file_(false),
      num_variables_(other.num_variables_),
      num_cared_(other.num_cared_),
      clauses_(other.clauses_),
      pos_in_cls_(other.pos_in_cls_),
      neg_in_cls_(other.neg_in_cls_),
      group_info_(other.group_info_) {}

CNF::CNF(CNF &&other) noexcept
    : cnf_file_path_(std::move(other.cnf_file_path_)),
      delete_cnf_file_(other.delete_cnf_file_),
      num_variables_(other.num_variables_),
      num_cared_(other.num_cared_),
      clauses_(std::move(other.clauses_)),
      pos_in_cls_(std::move(other.pos_in_cls_)),
      neg_in_cls_(std::move(other.neg_in_cls_)),
      group_info_(std::move(other.group_info_)) {
    other.cnf_file_path_ = "";
    other.delete_cnf_file_ = false;
}

CNF &CNF::operator=(const CNF &other) {
    if (this == &other) return *this;
    if (delete_cnf_file_ && !cnf_file_path_.empty()) {
        std::filesystem::remove(cnf_file_path_);
    }
    cnf_file_path_ = "";
    delete_cnf_file_ = false;
    num_variables_ = other.num_variables_;
    num_cared_ = other.num_cared_;
    clauses_ = other.clauses_;
    pos_in_cls_ = other.pos_in_cls_;
    neg_in_cls_ = other.neg_in_cls_;
    group_info_ = other.group_info_;
    return *this;
}

CNF &CNF::operator=(CNF &&other) noexcept {
    if (this == &other) return *this;
    if (delete_cnf_file_ && !cnf_file_path_.empty()) {
        std::filesystem::remove(cnf_file_path_);
    }
    cnf_file_path_ = std::move(other.cnf_file_path_);
    delete_cnf_file_ = other.delete_cnf_file_;
    num_variables_ = other.num_variables_;
    num_cared_ = other.num_cared_;
    clauses_ = std::move(other.clauses_);
    pos_in_cls_ = std::move(other.pos_in_cls_);
    neg_in_cls_ = std::move(other.neg_in_cls_);
    group_info_ = std::move(other.group_info_);
    other.cnf_file_path_ = "";
    other.delete_cnf_file_ = false;
    return *this;
}

auto CNF::parse(std::istream &cnf) -> std::unique_ptr<CNF> {
    std::unique_ptr<CNF> result(new CNF());
    unsigned num_clauses = 0;
    std::string line;
    while (std::getline(cnf, line)) {
        if (line.empty() || line[0] == 'c') continue;  // Skip comments and empty lines
        if (line[0] == 'p') {
            std::istringstream iss(line);
            std::string type;
            iss >> type >> type >> result->num_variables_ >> num_clauses;
            result->num_cared_ = result->num_variables_;
            result->clauses_.reserve(num_clauses);
            assert(type == "cnf" && "Expected CNF format");
            continue;
        }
        std::istringstream iss(line);
        std::vector<int> clause;
        int literal;
        while (iss >> literal) {
            if (literal == 0) break;  // End of clause
            clause.push_back(literal);
        }
        result->clauses_.push_back(std::move(clause));
    }
    result->calc_cnf_info();
    return result;
}

void CNF::print(std::ostream &cnf) const {
    cnf << "p cnf " << num_variables_ << " " << get_num_clauses() << "\n";
    for (const auto &clause : clauses_) {
        for (auto lit : clause) {
            cnf << lit << " ";
        }
        cnf << "0\n";  // End of clause
    }
}

static std::string create_tmpfile() {
    std::string result = "/tmp/cnfXXXXXX";
    int fd = mkstemp(result.data());
    if (fd == -1) {
        throw std::runtime_error("Failed to create temporary file for CNF");
    }
    close(fd);  // Close the file descriptor, we just need the name
    return result;
}

void CNF::reduce_cnf() {
    if (coprocessor_path.empty()) return;
    if (!std::filesystem::exists(coprocessor_path)) {
        std::cout << std::format(
            "c Warning: coprocessor_path `{}` not exists!", coprocessor_path);
        return;
    }
    auto &before_file = get_cnf_file_path();
    std::string after_file = create_tmpfile();
    std::string cmd =
        std::format("{} -enabled_cp3 -up -subsimp -no-bve -no-bce -no-dense -dimacs={} {} 2>&1",
                    coprocessor_path, after_file, before_file);
    exec_drop_output(std::move(cmd));
    auto reduced_cnf = CNF::parse(after_file);
    reduced_cnf->delete_cnf_file_ = true;
    if (num_variables_ != reduced_cnf->num_variables_) {
        std::cout << std::format(
            "c Warning: coprocessor: num variables mismatch ({} vs {})",
            num_variables_, reduced_cnf->num_variables_);
        return;
    }
    *this = std::move(*reduced_cnf);
}

const std::string &CNF::get_cnf_file_path() {
    if (cnf_file_path_.empty()) {
        cnf_file_path_ = create_tmpfile();
        delete_cnf_file_ = true;
        print(cnf_file_path_);
    }
    return cnf_file_path_;
}

void CNF::calc_cnf_info() {
    pos_in_cls_.resize(num_variables_ + 1);
    neg_in_cls_.resize(num_variables_ + 1);
    for (size_t idx = 0; auto &cl : clauses_) {
        for (auto lit : cl) {
            if (lit < 0) {
                neg_in_cls_[-lit].push_back(idx);
            } else {
                pos_in_cls_[lit].push_back(idx);
            }
        }
        ++idx;
    }
}

std::unique_ptr<DDNNF> CNF::convert_to_ddnnf() const {
    if (!std::filesystem::exists(DDNNF::d4v2_path))
        throw std::runtime_error("d4v2 not found");
    if (!std::filesystem::exists(DDNNF::fastfmc_path))
        throw std::runtime_error("FastFMC not found");
    auto cnf_path = const_cast<CNF *>(this)->get_cnf_file_path();
    auto tmpfile = create_tmpfile();
    auto cmd1 = std::format("{} -i {} -m ddnnf-compiler --dump-ddnnf {}", DDNNF::d4v2_path, cnf_path, tmpfile);
    int ret1 = exec_drop_output(std::move(cmd1));
    if (ret1 != 0) throw std::runtime_error("Error while running d4v2");
    auto cmd2 = std::format("{0} {1} --save-ddnnf {1}", DDNNF::fastfmc_path, tmpfile);
    int ret2 = exec_drop_output(std::move(cmd2));
    if (ret2 != 0) throw std::runtime_error("Error while running FastFMC");
    std::filesystem::remove(tmpfile);
    auto ddnf_filename = tmpfile + "-saved.nnf";
    auto result = DDNNF::parse(ddnf_filename, num_variables_);
    std::filesystem::remove(ddnf_filename);
    if (result->nvar != num_variables_)
        throw std::runtime_error("smooth d-DNNF file broken");
    return result;
}
