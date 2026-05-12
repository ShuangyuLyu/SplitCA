#include "tui.hpp"

#include <chrono>
#include <cstdio>
#include <format>
#include <string>
#include <thread>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#ifdef __linux__
#  include <unistd.h>
static double read_cpu_seconds() {
    FILE *f = std::fopen("/proc/self/stat", "r");
    if (!f) return 0.0;
    int pid; char comm[256]; char state;
    long ppid, pgrp, session, tty, tpgid;
    unsigned long flags, minflt, cminflt, majflt, cmajflt, utime, stime;
    if (std::fscanf(f, "%d %255s %c %ld %ld %ld %ld %ld %lu %lu %lu %lu %lu %lu %lu",
                &pid, comm, &state, &ppid, &pgrp, &session, &tty, &tpgid,
                &flags, &minflt, &cminflt, &majflt, &cmajflt, &utime, &stime) < 15) {
        std::fclose(f);
        return 0.0;
    }
    std::fclose(f);
    long tps = sysconf(_SC_CLK_TCK);
    return tps > 0 ? static_cast<double>(utime + stime) / tps : 0.0;
}
static size_t read_mem_rss_kb() {
    FILE *f = std::fopen("/proc/self/status", "r");
    if (!f) return 0;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        size_t rss = 0;
        if (std::sscanf(line, "VmRSS: %zu", &rss) == 1) {
            std::fclose(f);
            return rss;
        }
    }
    std::fclose(f);
    return 0;
}
#elif defined(__APPLE__)
#  include <sys/resource.h>
#  include <mach/mach.h>
static double read_cpu_seconds() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        return 0.0;
    long sec = usage.ru_utime.tv_sec + usage.ru_stime.tv_sec;
    long usec = usage.ru_utime.tv_usec + usage.ru_stime.tv_usec;
    return sec + usec / 1000000.0;
}
static size_t read_mem_rss_kb() {
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) != KERN_SUCCESS)
        return 0;
    return info.resident_size / 1024;
}
#else
static double read_cpu_seconds() { return 0.0; }
static size_t read_mem_rss_kb()  { return 0; }
#endif

// ── TuiStreambuf ──────────────────────────────────────────────────────────────

int TuiStreambuf::overflow(int c) {
    if (c == EOF) return c;
    char ch = static_cast<char>(c);
    if (ch == '\n') {
        state_.add_log(std::move(current_line_));
        current_line_.clear();
    } else {
        current_line_ += ch;
    }
    return c;
}

std::streamsize TuiStreambuf::xsputn(const char *s, std::streamsize n) {
    for (std::streamsize i = 0; i < n; ++i)
        overflow(static_cast<unsigned char>(s[i]));
    return n;
}

// ── helpers ───────────────────────────────────────────────────────────────────

// Strip ANSI escape sequences (CSI sequences: \033[ ... final_byte in 0x40-0x7E).
static std::pair<std::string, bool> strip_ansi(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    bool in_esc = false, escaped = false;
    for (unsigned char c : s) {
        if (in_esc) {
            if (c == 'm') in_esc = false;
        } else if (c == '\033') {
            in_esc = true;
            escaped = true;
        } else {
            out += static_cast<char>(c);
        }
    }
    return std::make_pair(std::move(out), escaped);
}

static std::string format_elapsed(std::chrono::steady_clock::duration d) {
    auto s = std::chrono::duration_cast<std::chrono::seconds>(d).count();
    if (s < 60)   return std::format("{}s", s);
    if (s < 3600) return std::format("{}m {:02d}s", s / 60, s % 60);
    return std::format("{}h {:02d}m {:02d}s", s / 3600, (s % 3600) / 60, s % 60);
}

static std::string format_mem(size_t kb) {
    if (kb == 0)          return "—";
    if (kb >= 1024 * 1024) return std::format("{:.1f} GB", kb / 1024.0 / 1024.0);
    if (kb >= 1024)        return std::format("{:.1f} MB", kb / 1024.0);
    return std::format("{} KB", kb);
}

// ── Tui ──────────────────────────────────────────────────────────────────────

void Tui::start() {
    auto fut = ready_.get_future();
    tui_thread_ = std::jthread([this](std::stop_token st) { run(std::move(st)); });
    fut.wait();
}

void Tui::stop() {
    if (tui_thread_.joinable()) {
        state_.end_time = std::chrono::steady_clock::now();
        state_.done.store(true, std::memory_order_release);
        state_.notify();
        tui_thread_.request_stop();
        tui_thread_.join();
    }
}

void Tui::wait() {
    if (tui_thread_.joinable()) {
        state_.end_time = std::chrono::steady_clock::now();
        state_.done.store(true, std::memory_order_release);
        state_.notify();
        // Do not request_stop, let the user exit manually via keypress.
        tui_thread_.join();
    }
}

void Tui::run(std::stop_token st) {
    using namespace ftxui;

    auto screen  = ScreenInteractive::Fullscreen();
    auto exit_fn = screen.ExitLoopClosure();

    state_.on_update = [&screen] { screen.PostEvent(Event::Custom); };
    state_.tui_active.store(true, std::memory_order_release);
    ready_.set_value();

    // ── periodic 1-second refresh ────────────────────────────────────────────
    std::jthread refresh_timer([&](std::stop_token rst) {
        while (!rst.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            screen.PostEvent(Event::Custom);
        }
    });

    // ── exit watcher ─────────────────────────────────────────────────────────
    std::jthread exit_watcher([&](std::stop_token ewst) {
        while (!ewst.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (st.stop_requested()) {
                screen.PostEvent(Event::Custom);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                exit_fn();
                return;
            }
        }
    });

    // ── system metrics state (cached, refreshed at most once per second) ─────
    double   cpu_last_s    = -1.0;
    auto     cpu_last_wall = std::chrono::steady_clock::now();
    double   cached_cpu    = 0.0;
    size_t   cached_mem_kb = 0;
    auto     last_sys_upd  = std::chrono::steady_clock::now() - std::chrono::seconds(2);

    // ── renderer ─────────────────────────────────────────────────────────────
    auto build_ui = [&]() -> Element {
        const auto now    = std::chrono::steady_clock::now();
        const auto phase  = static_cast<TuiPhase>(state_.phase_type.load(std::memory_order_acquire));
        const int  str    = state_.strength.load();
        const int  part   = state_.partition.load();
        const size_t ca   = state_.ca_size.load();
        const bool is_done = state_.done.load(std::memory_order_acquire);

        // ── elapsed time ─────────────────────────────────────────────────────
        auto elapsed_tp  = is_done ? state_.end_time : now;
        auto elapsed_str = format_elapsed(elapsed_tp - state_.start_time);

        // ── CPU / Memory (update at most 1/s) ─────────────────────────────────
        if (now - last_sys_upd >= std::chrono::seconds(1)) {
            double cur_cpu = read_cpu_seconds();
            if (cpu_last_s >= 0) {
                double dt_wall = std::chrono::duration<double>(now - cpu_last_wall).count();
                double dt_cpu  = cur_cpu - cpu_last_s;
                if (dt_wall > 0.01) cached_cpu = dt_cpu / dt_wall * 100.0;
            }
            cpu_last_s    = cur_cpu;
            cpu_last_wall = now;
            cached_mem_kb = read_mem_rss_kb();
            last_sys_upd  = now;
        }
        std::string cpu_str = cpu_last_s >= 0
            ? std::format("{:.1f}%", cached_cpu) : "—";
        std::string mem_str = format_mem(cached_mem_kb);

        // ── Section 1: Title ──────────────────────────────────────────────────
        Element title = text("SplitCA") | bold | center;

        // ── Section 2: Stats (4 lines) ────────────────────────────────────────
        auto color_ca = is_done ? Color::Green : Color::Cyan;
        Elements stats = {
            hbox({text(" CA Size  : "), text(std::to_string(ca)) | color(color_ca) | bold}),
            hbox({text(" Elapsed  : "), text(elapsed_str)}),
            hbox({text(" CPU      : "), text(cpu_str)}),
            hbox({text(" Memory   : "), text(mem_str)}),
        };

        // ── Section 3: Phase details ──────────────────────────────────────────
        std::string phase_title;
        if (is_done) {
            phase_title = "Done";
        } else if (str <= 0) {
            phase_title = "Initializing";
        } else {
            phase_title = std::to_string(str) + "-wise";
            if (part >= 0) phase_title += " part-" + std::to_string(part);
            switch (phase) {
                case TuiPhase::TupleValidation: phase_title += " Tuple Validation"; break;
                case TuiPhase::Generation:      phase_title += " Generation";       break;
                case TuiPhase::Optimization:    phase_title += " Optimization";     break;
                default: break;
            }
        }

        auto phase_elapsed_str = format_elapsed(now - state_.phase_start_time);

        Elements phase_rows;
        phase_rows.push_back(
            hbox({text(" "), text(phase_title) | bold |
                  color(is_done ? Color::Green : Color::Yellow)}));

        if (is_done) {
            phase_rows.clear();
            phase_rows.push_back(
                hbox({text(" "), text("Generation Finished") | bold | color(Color::Green)}));
            phase_rows.push_back(
                hbox({text("  Generated "), text(std::to_string(str) + "-wise CA size: ") | bold,
                      text(std::to_string(ca)) | color(Color::Green)}));
            phase_rows.push_back(
                hbox({text("  Total Time Used: "), text(elapsed_str)}));
            phase_rows.push_back(text(""));
            phase_rows.push_back(text("  Press Any Key to Exit") | dim);
        } else if (phase == TuiPhase::TupleValidation) {
            uint64_t all   = state_.tv_all_tuples.load();
            uint64_t proc  = state_.tv_current_processed.load();
            uint64_t valid = state_.tv_valid.load();
            uint64_t uncov = state_.tv_uncovered.load();
            phase_rows.push_back(hbox({
                text("  Phase Elapsed      : "),
                text(phase_elapsed_str),
            }));
            phase_rows.push_back(hbox({
                text("  All Tuples         : "),
                text(all > 0 ? std::to_string(all) : "—"),
            }));
            phase_rows.push_back(hbox({
                text("  Current   Checked  : "),
                text(proc > 0 ? std::to_string(proc) : "—") | color(Color::Cyan),
            }));
            phase_rows.push_back(hbox({
                text("  Current     Valid  : "),
                text(std::to_string(valid)) | color(Color::Cyan),
            }));
            phase_rows.push_back(hbox({
                text("  Current Uncovered  : "),
                text(std::to_string(uncov)) | color(Color::Cyan),
            }));
        } else if (phase == TuiPhase::Generation) {
            size_t covered    = state_.gen_covered.load();
            size_t all_uncov  = state_.gen_all_uncovered.load();
            size_t new_tcs    = state_.gen_new_testcases.load();
            phase_rows.push_back(hbox({
                text("  Phase Elapsed      : "),
                text(phase_elapsed_str),
            }));
            phase_rows.push_back(hbox({
                text("  Covered / Uncovered: "),
                text(std::format("{} / {}", covered, all_uncov))
                    | color(Color::Green),
            }));
            phase_rows.push_back(hbox({
                text("  New Testcases      : "),
                text(std::to_string(new_tcs)) | color(Color::Cyan),
            }));
        } else if (phase == TuiPhase::Optimization) {
            size_t input   = state_.opt_input_size.load();
            size_t current = state_.opt_current_size.load();
            phase_rows.push_back(hbox({
                text("  Phase Elapsed      : "),
                text(phase_elapsed_str),
            }));
            phase_rows.push_back(hbox({
                text("  Input Size         : "),
                text(std::to_string(input)),
            }));
            phase_rows.push_back(hbox({
                text("  Compressed to      : "),
                text(std::to_string(current)) | color(Color::Green) | bold,
            }));
        } else {
            phase_rows.push_back(text("  —") | color(Color::Default));
        }

        // ── Left panel: sections 1-3 ──────────────────────────────────────────
        Elements left_elems;
        left_elems.push_back(std::move(title));
        left_elems.push_back(separator());
        for (auto &e : stats)      left_elems.push_back(std::move(e));
        left_elems.push_back(separator());
        for (auto &e : phase_rows) left_elems.push_back(std::move(e));
        auto left_panel = vbox(std::move(left_elems)) | flex;

        // ── Right panel: logs ─────────────────────────────────────────────────
        Elements log_elems;
        {
            std::lock_guard<std::mutex> lk(state_.log_mutex);
            if (state_.log_lines.empty()) {
                log_elems.push_back(text("  (no output yet)") | color(Color::Default));
            } else {
                for (const auto &line : state_.log_lines) {
                    auto &&[txt, has_color] = strip_ansi(line);
                    auto &&color_func = has_color ? color(Color::Green) : color(Color::Default);
                    log_elems.push_back(text("  " + txt) | color_func);
                }
            }
        }
        auto right_panel = vbox({
            text(" Logs") | bold,
            separator(),
            vbox(std::move(log_elems)) | focusPositionRelative(0, 1) | frame | flex,
        }) | flex;

        // ── Assemble ──────────────────────────────────────────────────────────
        return hbox({left_panel, separator(), right_panel}) | border;
    };

    auto renderer = Renderer(build_ui);
    auto component = CatchEvent(renderer, [&](Event event) {
        if (state_.done.load(std::memory_order_acquire)) {
            if (event.is_character() || event == Event::Return || event == Event::Escape) {
                exit_fn();
                return true;
            }
        }
        return false;
    });
    screen.Loop(component);

    state_.tui_active.store(false, std::memory_order_release);
}

