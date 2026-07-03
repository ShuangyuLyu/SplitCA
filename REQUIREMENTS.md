SplitCA ASE 2026 Artifact Requirements
======================================

Recommended evaluation environment
----------------------------------

Architecture:
  - x86_64 / amd64

Container or VM:
  - Docker 24+ or Podman 4+ on an x86_64 host
  - On ARM hosts, build and run with --platform linux/amd64
  - Ubuntu 24.04 base image, as specified by Dockerfile

Host hardware:
  - CPU: 32 cores or more recommended
  - Memory: 256 GB RAM minimum, 512 GB or more recommended for larger experiments
  - Storage: 5 GB free space for source, build products, dependencies, and
    reproduction outputs
  - GPU: not required
  - Network: only required during Docker image construction or native CMake
    dependency download; not required during smoke tests or reproduction scripts

Container software installed by Dockerfile:
  - Ubuntu 24.04
  - GCC/G++ toolchain with C++20 support
  - CMake 3.22 or newer
  - Bash, coreutils, ca-certificates, time, git
  - zlib development headers for MiniSat

Artifact-specific binaries:
  - bin/coprocessor: Linux x86_64 ELF helper executable
  - bin/d4v2: Linux x86_64 ELF helper executable
  - bin/FastFMC: Linux x86_64 ELF helper executable

Because these helper binaries are Linux x86_64 executables, the fully executable
artifact should be evaluated in the provided container or an equivalent Linux
x86_64 VM. Native macOS or Windows builds may compile SplitCA itself, but they
cannot run the packaged helper binaries without Linux compatibility support.

Native Ubuntu 24.04 requirements
--------------------------------

For reviewers who do not use Docker/Podman:

  sudo apt-get update
  sudo apt-get install -y build-essential cmake ca-certificates zlib1g-dev

Then build with:

  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --target SplitCA check_tuples -j"$(nproc)"

Long-running experiments
------------------------

The smoke and reduced reproduction scripts are intended for artifact review and
should complete quickly on commodity hardware. Full reproduction of all large
benchmark entries may take many hours and should be run selectively or on a
dedicated machine.
