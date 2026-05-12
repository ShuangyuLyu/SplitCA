#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

// Phase types reported by the Expandor/Optimizer to the TUI.
enum class TuiPhase : int {
    Idle,
    TupleValidation,
    Generation,
    Optimization,
    Done,
};

// Shared state between the Expandor/Optimizer (writers) and the TUI (reader).
// Atomic fields are written by worker threads and read by the TUI thread.
struct TuiState {
    // ── Timing ────────────────────────────────────────────────────────────────
    std::chrono::steady_clock::time_point start_time{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point phase_start_time{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point end_time;

    // ── Section 2: Stats ──────────────────────────────────────────────────────
    std::atomic<size_t> ca_size{0};

    // ── Section 3: Phase header ───────────────────────────────────────────────
    std::atomic<int>       strength{0};
    std::atomic<int>       partition{-1};   // -1 = unpartitioned; 0-4 = 4-wise part index
    std::atomic<int>       phase_type{static_cast<int>(TuiPhase::Idle)};

    // Tuple Validation data
    std::atomic<uint64_t> tv_all_tuples{0};          // total tuple combinations
    std::atomic<uint64_t> tv_valid{0};               // valid tuples found so far
    std::atomic<uint64_t> tv_uncovered{0};           // uncovered valid tuples
    std::atomic<uint64_t> tv_current_processed{0};   // tuples processed so far

    // Generation data
    std::atomic<size_t> gen_covered{0};            // covered tuples so far
    std::atomic<size_t> gen_all_uncovered{0};      // total uncovered at phase start
    std::atomic<size_t> gen_new_testcases{0};      // new test cases added

    // Optimization data
    std::atomic<size_t> opt_input_size{0};         // initial test case count being compressed
    std::atomic<size_t> opt_current_size{0};       // current (compressed) test case count

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    std::atomic<bool> done{false};

    // ── Log ring-buffer (immediate refresh on new log) ────────────────────────
    static constexpr size_t kMaxLogLines = 200;
    mutable std::mutex      log_mutex;
    std::deque<std::string> log_lines;

    // ── TUI refresh callback (set once by Tui::start()) ──────────────────────
    std::atomic<bool>     tui_active{false};
    std::function<void()> on_update;

    // ── Phase transition helpers ──────────────────────────────────────────────

    void begin_tuple_validation(int str, int part, uint64_t all_tuples) {
        strength.store(str);
        partition.store(part);
        tv_all_tuples.store(all_tuples);
        tv_valid.store(0);
        tv_uncovered.store(0);
        tv_current_processed.store(0);
        phase_start_time = std::chrono::steady_clock::now();
        phase_type.store(static_cast<int>(TuiPhase::TupleValidation));
        notify();
    }

    void begin_generation(int str, int part, uint64_t all_uncovered) {
        strength.store(str);
        partition.store(part);
        gen_covered.store(0);
        gen_all_uncovered.store(all_uncovered);
        gen_new_testcases.store(0);
        phase_start_time = std::chrono::steady_clock::now();
        phase_type.store(static_cast<int>(TuiPhase::Generation));
        notify();
    }

    void begin_optimization(int str, int part, size_t input_size) {
        strength.store(str);
        partition.store(part);
        opt_input_size.store(input_size);
        opt_current_size.store(input_size);
        phase_start_time = std::chrono::steady_clock::now();
        phase_type.store(static_cast<int>(TuiPhase::Optimization));
        notify();
    }

    void add_log(std::string line) {
        {
            std::lock_guard<std::mutex> lk(log_mutex);
            log_lines.push_back(std::move(line));
            while (log_lines.size() > kMaxLogLines)
                log_lines.pop_front();
        }
        notify();
    }

    // Post a redraw event to the TUI. Thread-safe; ignored when TUI is not running.
    void notify() {
        if (tui_active.load(std::memory_order_acquire) && on_update)
            on_update();
    }
};

