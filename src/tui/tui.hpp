#pragma once

#include "tui_state.hpp"

#include <future>
#include <streambuf>
#include <string>
#include <thread>

// ── TuiStreambuf ──────────────────────────────────────────────────────────────
// Used as the backing streambuf for a dedicated log ostream while the TUI is
// active.  Accumulated characters are forwarded line-by-line to TuiState's log
// buffer (and trigger a TUI redraw).  std::cout is intentionally left untouched
// so that FTXUI can continue to use it for terminal rendering.
class TuiStreambuf : public std::streambuf {
 public:
    TuiStreambuf(TuiState &state) : state_(state) {}

 protected:
    int overflow(int c) override;
    std::streamsize xsputn(const char *s, std::streamsize n) override;

 private:
    TuiState &state_;
    std::string current_line_;
};

// ── Tui ──────────────────────────────────────────────────────────────────────
// Runs an FTXUI ScreenInteractive in a background thread.
//
// Typical usage:
//   TuiState state;
//   TuiStreambuf tui_buf(state);
//   std::ostream log_stream(&tui_buf);
//   Tui tui(state);
//   tui.start();                    // blocks until TUI is ready and on_update is set
//   expandor.set_log_stream(&log_stream);   // Expandor writes to log_stream, not cout
//   /* run computation */
//   tui.stop();                     // wait for TUI thread to finish
class Tui {
 public:
    explicit Tui(TuiState &state) : state_(state) {}
    ~Tui() { stop(); }

    // Start the TUI in a background thread.  Blocks until the ScreenInteractive
    // is initialised and state_.on_update has been set.
    void start();

    // Signal the TUI to stop and wait for its thread to finish.
    void stop();

    // Signal that work is done and wait for the user to exit the TUI manually.
    void wait();

 private:
    TuiState &state_;
    std::promise<void> ready_;
    std::jthread tui_thread_;

    void run(std::stop_token st);
};
