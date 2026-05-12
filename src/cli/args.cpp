#include "cli/args.hpp"
#include <clipp.h>
#include <format>
#include <iostream>

void Argument::parse_path_extension(std::filesystem::path filename) {
    auto ext = filename.extension();
    if (ext == ".cnf") {
        if (!cnf_path.empty()) {
            std::cerr << "Warning: specified cnf file more than once.\n";
        }
        cnf_path = std::move(filename);
    } else if (ext == ".nnf") {
        if (!nnf_path.empty()) {
            std::cerr << "Warning: specified nnf file more than once.\n";
        }
        nnf_path = std::move(filename);
    } else {
        std::cerr << "Error: unknown option / input file: " << filename << "\n";
        std::cerr << "Error: unrecognized file extension: " << ext << std::endl;
        exit(1);
    }
}

std::unique_ptr<Argument> parse_args(int argc, char *argv[]) noexcept {
    auto arg = std::make_unique<Argument>();
    using namespace clipp;
    std::vector<std::string> files;
    clipp::group cli;
    auto print_help = [&cli, pname = argv[0]](int code = 0) {
        auto dfmt = doc_formatting{}.first_column(5).doc_column(25).indent_size(0);
        std::cerr << std::format("Usage: {} [options] <input files>...\n\n", pname);
        std::cerr << "Options:\n" << documentation(cli, dfmt) << std::endl;
        exit(code);
    };
    // clang-format off
    cli = (
        (   option("-h", "--help") % "Print help" >> print_help,
            option("-t", "--strength") % "Strength [required]"
                & value("t", arg->strength),
            option("-s", "--seed") % "Random seed [default=1]"
                & value("s", arg->seed),
            option("-o", "--output") % "Output CA file path"
                & value("path", arg->output_ca_path),
            option("--cnf") % "CNF file path [--cnf <.cnf>]"
                & value("path", arg->cnf_path),
            option("--nnf") % "smooth d-DNNF file path [--nnf <.nnf>]"
                & value("path", arg->nnf_path),
            option("--threads") % "multi-thread nums"
                & value("num", arg->thread_num),
            option("--lambda") % "lambda (aka. candidate set size)"
                & value("path", arg->candidate_set_size),
            ( option("--use_addition_tc").set(arg->use_addition_tc, true) 
            | option("--not_use_addition_tc").set(arg->use_addition_tc, false))
                % "(not) use addition testcases",
            option("--tui").set(arg->tui, true)
                % "Enable TUI progress display",
            any_other(files).label("input files")
                % "Input files with extension \".cnf\" or \".nnf\" "
                  "will be automatically parsed to cnf or smooth d-DNNF format. "
                  "Use \"--cnf\" or \"--nnf\" instead if you want to manual specify the format of input files."
        )
    ).repeatable(false);
    // clang-format on
    if (auto result = parse(argc, argv, cli)) {
        for (auto &file : files) {
            arg->parse_path_extension(file);
        }
        if (arg->cnf_path.empty() && arg->nnf_path.empty()) {
            std::cerr << "FATAL: no input file." << std::endl;
            exit(-1);
        }
        return arg;
    } else {
        if (result.begin() == result.end()) {
            print_help(1);
        }
        std::cerr << "Error when parsing command line arguments.\n"
                  << "Use \"--help\" to show man page." << std::endl;
        exit(1);
    }
}
