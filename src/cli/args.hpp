#pragma once
#include <filesystem>
#include <memory>
#include <string>

struct Argument {
    int strength = 4, seed = 1;
    std::string cnf_path, nnf_path;
    std::string output_ca_path;

    int thread_num = 32;
    int candidate_set_size = 100;

    bool use_addition_tc = true;

    bool tui = false;

    void parse_path_extension(std::filesystem::path filename);
};

std::unique_ptr<Argument> parse_args(int argc, char *argv[]) noexcept;
