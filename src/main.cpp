#include "generate/expandor.hpp"
#include "cli/args.hpp"
#include "tui/tui.hpp"

#include <iostream>
#include <memory>
#include <signal.h>
#include <cstring>
using namespace std;

namespace {
// Set up TUI if requested
std::unique_ptr<TuiState>     tui_state;
std::unique_ptr<TuiStreambuf> tui_buf;
std::unique_ptr<std::ostream> tui_log_stream;
std::unique_ptr<Tui>          tui;

void HandleInterrupt(int sig) {
    if (tui) tui->stop();
    std::cout << "c" << endl;
    std::cout << "c caught signal... exiting" << std::endl;
    exit(-1);
}

void SetupSignalHandler() {
    signal(SIGTERM, HandleInterrupt);
    signal(SIGINT , HandleInterrupt);
    signal(SIGQUIT, HandleInterrupt);
    signal(SIGKILL, HandleInterrupt);
}
}

int main (int argc, char **argv) {
    SetupSignalHandler();
    auto argument = parse_args(argc, argv);
    auto cnf = argument->cnf_path.empty() ? nullptr : CNF::parse(argument->cnf_path);
    if (cnf) cnf->reduce_cnf();
    auto nnf = argument->nnf_path.empty() ? cnf->convert_to_ddnnf() : DDNNF::parse(argument->nnf_path);
    expandor::Expandor expandor(cnf.get(), nnf.get(), *argument);

    if (argument->tui) {
        tui_state = std::make_unique<TuiState>();
        expandor.set_tui_state(tui_state.get());
        tui = std::make_unique<Tui>(*tui_state);
        tui->start();  // blocks until TUI is ready and on_update is set
        // Point the expandor at a dedicated log stream backed by TuiStreambuf.
        // std::cout is intentionally left untouched so FTXUI can use it freely.
        tui_buf = std::make_unique<TuiStreambuf>(*tui_state);
        tui_log_stream = std::make_unique<std::ostream>(tui_buf.get());
        expandor.set_log_stream(tui_log_stream.get());
    }

    // expandor.Generate_1wise_CA();
    expandor.Generate_2wise_CA();
    if (argument->strength >= 3) {
        expandor.Expand();
        expandor.Optimize(100);
    }
    if (argument->strength >= 4) {
        expandor.Expand();
        // expandor.Optimize(100);
    }
    expandor.output_to_file(argument->output_ca_path);

    if (argument->tui) {
        tui->wait();
    }

    return 0;
}
