# Third-Party Notices

This artifact includes or downloads the following third-party components.

## Components included in this repository

| Component | Location | Purpose | License note |
| --- | --- | --- | --- |
| MiniSat | `3rd/minisat/` | Low-level SAT solver library | See `3rd/minisat/LICENSE`. |
| coprocessor | `bin/coprocessor` | CNF simplification helper | LGPL; upstream: `https://github.com/nmanthey/riss-solver`. |
| d4v2 | `bin/d4v2` | d-DNNF compilation helper | LGPL; upstream: `https://github.com/crillab/d4v2`. |
| FastFMC | `bin/FastFMC` | d-DNNF smoothing/counting helper | Upstream: `https://github.com/ShuangyuLyu/FastFMC`. |

The helper executables are packaged to make review runs self-contained after
the container image is built.

## Components fetched by CMake during build

| Component | Purpose | Upstream |
| --- | --- | --- |
| clipp | Command-line argument parsing | `https://github.com/muellan/clipp` |
| FTXUI | Optional terminal UI | `https://github.com/ArthurSonzogni/FTXUI` |
| dbg-macro | Debug-only diagnostics | `https://github.com/sharkdp/dbg-macro` |

These dependencies are downloaded during CMake configuration when building from
source. They are already present in a prebuilt Docker/Podman image.
