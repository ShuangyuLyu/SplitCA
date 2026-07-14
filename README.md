# *SplitCA*: An Effective Tool for Generating 4-wise Covering Arrays for Large-Scale Highly Configurable Systems

*SplitCA* is a novel and effective algorithm for solving large-scale 4-wise CCAG problems.
This repository provides the implementation of *SplitCA*, the testing instances used in the experiments, and the corresponding experimental results.

## Table of Contents

- [Artifact Evaluation](#artifact-evaluation)
  - [Getting Started](#getting-started)
  - [Representative Reproduction](#representative-reproduction)
  - [Full Reproduction](#full-reproduction)
  - [Running a Custom CNF](#running-a-custom-cnf)
  - [Optional: Using *SplitCA* with CombCT](#optional-using-splitca-with-combct)
  - [The Terminal User Interface](#the-terminal-user-interface)
- [How to Obtain *SplitCA*](#how-to-obtain-splitca)
- [Building *SplitCA* from Source Code](#building-splitca-from-source-code)
- [Instructions for Running *SplitCA*](#instructions-for-running-splitca)
- [Example Command for Running *SplitCA*](#example-command-for-running-splitca)
- [Implementation and Extension](#implementation-and-extension)
- [Testing Instances for Evaluating *SplitCA*](#testing-instances-for-evaluating-splitca)
- [Experimental Results](#experimental-results)

## Artifact Evaluation

This repository contains the artifact-evaluation material for:

> *SplitCA*: An Effective Tool for Generating 4-wise Covering Arrays for Large-Scale Highly Configurable Systems

The instructions below cover the containerized setup, smoke test, representative reproduction workflow, full reproduction workflow, and custom-input workflow. See [STATUS.md](STATUS.md) for the artifact status and [REQUIREMENTS.md](REQUIREMENTS.md) for the platform, software, and resource requirements.

### Getting Started

Users should be able to complete this section within 10 minutes on a machine with Docker or Podman. Building the image requires internet access because it downloads Ubuntu packages and CMake `FetchContent` dependencies. The prebuilt image can be loaded without network access, and the runtime workflows do not download data or dependencies.

From the repository root, build the container:

```sh
docker build --platform linux/amd64 -t splitca:latest .
```

Alternatively, load the prebuilt image without rebuilding it:

```sh
docker load -i artifacts/splitca_image.tar.gz
```

Run the smoke test:

```sh
./artifacts/scripts/smoke_test.sh
```

The smoke test runs *SplitCA* on `benchmarks/axtls.cnf` and checks the generated 4-wise covering array automatically. If Docker is unavailable but Podman is installed, replace `docker` with `podman` in the commands below and in any script you run:

```sh
podman run --rm splitca:latest ./test/test4.sh
```

### Representative Reproduction

This workflow runs on the 10 representative instances used in the paper:

```sh
./artifacts/scripts/run_representative.sh
```

The benchmark list is in [artifacts/representative.txt](artifacts/representative.txt). Each line is a benchmark basename, without the `.cnf` suffix. The default random seed is `1`; users can override it:

```sh
SEED=<any-positive-integer> ./artifacts/scripts/run_representative.sh
```

Outputs are written to `artifact_outputs/representative/`. Before adding a name to this list, verify that the corresponding `<name>.cnf` exists in `benchmarks/`; the list stores basenames without the `.cnf` suffix.

### Full Reproduction

The full workflow runs every `*.cnf` file in `benchmarks/` with the default *SplitCA* settings:

```sh
./artifacts/scripts/run_full.sh
```

This is a long-running workflow. The representative workflow is the recommended evaluator path for checking artifact functionality within a bounded time budget. Full-run outputs are written to `artifact_outputs/reproduction/`; each instance produces a `.csv` covering array and a `.log` file.

### Running a Custom CNF

*SplitCA* accepts a DIMACS CNF file. A custom instance does not need to be copied into the image or included in the repository: the usual workflow is to mount the host directory containing the instance into the prebuilt image.

If the prebuilt image has not been loaded yet, load it from the artifact archive:

```sh
docker load -i artifacts/splitca_image.tar.gz
```

Then create a host-side input/output directory and run the custom instance:

```sh
mkdir -p custom_benchmarks artifact_outputs
# Put custom.cnf, or any other DIMACS CNF file, in custom_benchmarks/.
docker run --rm \
  -v "$PWD/artifact_outputs:/workspace/artifact_outputs" \
  -v "$PWD/custom_benchmarks:/workspace/custom_benchmarks:ro" \
  splitca:latest ./build/bin/SplitCA \
  -t 4 -s 1 -o artifact_outputs/custom.csv custom_benchmarks/custom.cnf
```

The input path in the container is `/workspace/custom_benchmarks/custom.cnf`, while the generated result is written to the host directory `artifact_outputs/`. For another instance, change only the input filename and output filename. To run several new instances with the prebuilt image, repeat the `docker run` command for each file, or wrap the same command in a host-side loop. There is no need to rebuild the image unless the source code itself has changed.

The `-t 4` option requests the 4-wise covering array generated by *SplitCA*; it is also the program default, but it is shown explicitly here to make the command self-documenting. Use `--help` for all options:

```sh
docker run --rm splitca:latest ./build/bin/SplitCA --help
```

For Podman, replace `docker` with `podman` in the provided scripts or in these direct commands.

### Optional: Using *SplitCA* with CombCT

[CombCT](https://github.com/Bazoka13/CombCT) is another testing workflow that includes an `HSCA/` directory for generating t-wise covering arrays. Its current example uses `HSCA/example.cnf` and `HSCA/run.sh`; the script first creates an initial covering array with `SamplingCA` and then invokes `Generator` to produce `final.txt`. *SplitCA* can be used as an alternative covering-array generator for the same DIMACS-CNF-style input.

This is an optional interoperability example, not a prerequisite for artifact evaluation. The primary reuse path is the custom-CNF workflow above. Including this example is useful because it shows how *SplitCA* can serve as a generator inside another testing workflow, but it should not be read as a claim that the current CombCT pipeline has been fully integrated or experimentally validated with *SplitCA*.

The integration is intentionally not a drop-in replacement: CombCT's scripts and downstream test-generation code may assume the HSCA executable names, argument names, intermediate filenames, and exact covering-array format. The CombCT commands therefore need to be adapted to the desired *SplitCA* strength and output path. A typical prebuilt-image workflow is:

```sh
# Run from a directory containing CombCT/HSCA/example.cnf.
mkdir -p combct_outputs
docker run --rm \
  -v "$PWD/CombCT/HSCA:/workspace/combct_hsca:ro" \
  -v "$PWD/combct_outputs:/workspace/combct_outputs" \
  splitca:latest ./build/bin/SplitCA \
  -t 4 -s 1 \
  -o /workspace/combct_outputs/splitca_final.txt \
  /workspace/combct_hsca/example.cnf
```

To complete the integration, modify the CombCT/HSCA workflow so that the later CombCT step reads `splitca_final.txt` (or copy/rename it to the filename expected by that step, such as `final.txt`). If CombCT expects a different row/column encoding or metadata, add a small conversion step between *SplitCA* and CombCT. The CNF file must describe the same options and constraints in both tools, and `-t 4` should match the strength expected by the consuming CombCT workflow.

For a native build instead of the prebuilt image, the equivalent generator command is:

```sh
./build/bin/SplitCA -t 4 -s 1 \
  -o CombCT/HSCA/splitca_final.txt \
  CombCT/HSCA/example.cnf
```

The [CombCT repository](https://github.com/Bazoka13/CombCT) should be consulted for the downstream commands and any format assumptions in the current branch.

### The Terminal User Interface

*SplitCA* provides a terminal user interface (TUI) for interactive progress tracking. Because the TUI needs an interactive terminal, use `-it`:

```sh
docker run --rm -it splitca:latest ./build/bin/SplitCA benchmarks/axtls.cnf -t 4 --tui
```

The benchmark path can be replaced by any instance copied into the image or mounted into `/workspace`.

## How to Obtain *SplitCA*

*SplitCA* is [publicly available on GitHub](https://github.com/ShuangyuLyu/SplitCA). To obtain *SplitCA*, use `git clone` to get a local copy of the GitHub repository:

```sh
git clone https://github.com/ShuangyuLyu/SplitCA.git
```

## Building *SplitCA* from Source Code

*SplitCA* uses C++20. The recommended version of g++ is 13 or later. Alternatively, you may use clang++ 16 or later. Other compilers are not tested and may fail to compile.

Additionally, *SplitCA* uses [coprocessor](https://github.com/nmanthey/riss-solver) to simplify the input CNF instance, and uses [d4v2](https://github.com/crillab/d4v2) and [FastFMC](https://github.com/ShuangyuLyu/FastFMC) to convert CNF to d-DNNF for the **knowledge-compiled tuple validation technique**. For ease of use, we provide copies of these executables in `bin/` (these executables are currently only expected to run on Ubuntu 24.04).

> Note: The [coprocessor](https://github.com/nmanthey/riss-solver) is under [LGPL license](https://github.com/nmanthey/riss-solver/blob/master/LICENSE); the [d4v2](https://github.com/crillab/d4v2) is also under [LGPL license](https://github.com/crillab/d4v2/blob/main/LICENSE).

*SplitCA* uses [CMake](https://cmake.org) to configure the project, and requires CMake version 3.22 or later. The project must be compiled as C++20.
To configure *SplitCA*, use the following command:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20
```

> *SplitCA* has on four dependencies: [clipp](https://github.com/muellan/clipp) for cli arguments parsing, [minisat](https://github.com/niklasso/minisat.git) for low-level solver, [dbg_macro](https://github.com/sharkdp/dbg-macro) for debuging and [ftxui](https://github.com/ArthurSonzogni/FTXUI) for terminal user interface.
> CMake will automatically download and compile these dependencies. Users do not need to download manually.

If the version of your system default g++ is lower than required, you can manually specify the compiler with the following command:

```sh
# Do not forget to change `/usr/bin/gcc-13` and `/usr/bin/g++-13` to your real path for gcc and g++.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/usr/bin/gcc-13 -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 -DCMAKE_CXX_STANDARD=20
```

After configuration, compile *SplitCA* using the following command:

```sh
cmake --build build -t SplitCA
```

The binary executable file will be at `build/bin/SplitCA`.

## Instructions for Running *SplitCA*

The basic usage of *SplitCA* is as follows:
```
./build/bin/SplitCA <CNF-PATH> [-o <CA-PATH>] [--tui]
```

The required parameter is listed as follows:

| Parameter | Allowed Values | Description |
| - | - | - |
| `<CNF_PATH>` | path | the path of the cnf file of the input instance |

The optional parameters are listed as follows:

| Parameter | Allowed Values | Default Value | Description | 
| - | - | - | - |
| `-s` or `--seed` | integer | 1 | the random seed |
| `-o` or `--output` | path | -- | the path of the output CA file |
| `--threads` | integer | 32 | the number of maximum parallel threads |
| `--tui` | None | -- | enable TUI progress display |

The default strength is `4`. Use `-t 3` if a 3-wise covering array is needed for a smaller test run. Input files with a `.cnf` suffix are parsed as CNF; a `.nnf` file can be supplied when a precomputed smooth d-DNNF is available.

To learn more about the optional parameters of *SplitCA*, call *SplitCA* with the following command:

```sh
./build/bin/SplitCA -h
```

## Example Command for Running *SplitCA*

An example command for running *SplitCA* is:
```sh
./build/bin/SplitCA benchmarks/toybox.cnf -o toybox.ca --tui
```

The command above runs *SplitCA* with TUI enabled on the instance [benchmarks/toybox.cnf](benchmarks/toybox.cnf) and generates 4-wise CA in `toybox.ca`.

> This command may yield different results across runs because the random seed of *SplitCA* is not set by default.
> If you need a reproducible result, please set the random seed using `-s <seed>`.

> This command will not print the final CA because the output path of *SplitCA* is not set by default.
> If you need the CA constructed by *SplitCA*, please set the output path using `-o <path>`.

## Implementation and Extension

The main implementation of *SplitCA* is in the `src/` directory:

- `src/cli/` parses command-line options and input paths.
- `src/io/` parses DIMACS CNF and smooth d-DNNF files and performs the CNF-to-d-DNNF conversion used by tuple validation.
- `src/generate/` contains the 2-wise construction, expansion, optimization, and 4-wise partitioning logic.
- `src/tui/` contains the optional terminal progress interface.
- `3rd/minisat/` contains the low-level SAT solver used for sampling and validation.

To extend the framework, add a new algorithmic component in the relevant `src/` subdirectory, include its headers from the owning component, and add any new `.cpp` files under `src/`; CMake discovers source files recursively. Keep the command-line contract in `src/cli/args.cpp` and update this README when adding a new option. For a new external dependency, update `CMakeLists.txt`, `THIRD_PARTY_NOTICES.md`, and the container build instructions together.

### Adding an Instance

Additional instances use the standard DIMACS CNF format. A minimal file has a header and one clause per line:

```text
c optional comment
p cnf <number-of-variables> <number-of-clauses>
1 -2 0
2 3 -4 0
```

Variables are numbered from `1` through `<number-of-variables>`. A positive literal means that the corresponding option is enabled; a negative literal means that it is disabled. Each clause must end with `0`, and every valid configuration must satisfy every clause. Comments begin with `c` and are ignored. The header counts should match the actual variables and clauses. *SplitCA* treats each variable as a Boolean option; multi-valued options should be encoded using Boolean variables and CNF constraints, as in the bundled benchmarks.

For an additional instance under version control:

1. Add `<name>.cnf` to `benchmarks/`.
2. Run it directly, for example `./build/bin/SplitCA benchmarks/<name>.cnf -t 4 -s 1 -o <name>.ca`, or use [Running a Custom CNF](#running-a-custom-cnf) with the prebuilt image.
3. Validate the generated array with `tools/check_tuples` when the output format and strength are suitable for that checker.
4. Add the basename `<name>` to `artifacts/representative.txt` only if it should be part of the bounded representative workflow.
5. If the instance contributes to reported results, add or update the corresponding CSV under `experimental_results/` and document the provenance and resource requirements.

For a one-off instance, no repository change is required: follow [Running a Custom CNF](#running-a-custom-cnf) and mount the directory containing the `.cnf` file. The program can also accept a precomputed `.nnf` file alongside the CNF to avoid repeating the conversion step.

## Testing Instances for Evaluating *SplitCA*

The instances used in our experiments are placed in the `benchmarks/` directory. These instances are in DIMACS CNF format (i.e., Boolean formulas in Conjunctive Normal Form). The representative list used by the artifact scripts is maintained separately in `artifacts/representative.txt` so that the full benchmark collection and the bounded evaluation workflow can evolve independently.

## Experimental Results

The directory `experimental_results/` contains two `.csv` files that present the experimental results:

+ [Results_on_10_representative_instances/result_10.csv](experimental_results/Results_on_10_representative_instances/result_10.csv): Results of *SplitCA* on 10 representative instances.

+ [Results_on_large-scale_instances/result_large.csv](experimental_results/Results_on_large-scale_instances/result_large.csv): Results of *SplitCA* on large-scale instances with more than 1000 options. All the compititors fail to generate correct CA on these instances.

+ [Results_on_medium-scale_instances/result_medium.csv](experimental_results/Results_on_medium-scale_instances/result_medium.csv): Results of *SplitCA*, *HSCA* and *AutoCCAG* on medium-scale instances with less than 1000 options.
