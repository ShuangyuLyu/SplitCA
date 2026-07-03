# *SplitCA*: An Effective Tool for Generating 4-wise Covering Arrays for Large-Scale Highly Configurable Systems

*SplitCA* is a novel and effective algorithm for solving large-scale 4-wise CCAG problems.
This repository provides the implementation of *SplitCA*, the testing instances used in the experiments, and the corresponding experimental results.

## Artifact Evaluation

The ASE 2026 artifact package instructions are in [artifacts/README.md](artifacts/README.md). They include the containerized setup, smoke test, reduced reproduction workflow, full reproduction workflow.

## How to Obtain *SplitCA*

*SplitCA* is [publicly available on GitHub](https://github.com/ShuangyuLyu/SplitCA). To obtain *SplitCA*, use `git clone` to get a local copy of the GitHub repository:

```sh
git clone https://github.com/ShuangyuLyu/SplitCA.git
```

## Building *SplitCA* from Source Code

*SplitCA* uses C++20. The recommended version of g++ is 12.0 or later. Alternatively, you may use clang++ 16.0 or later. Other compilers are not tested and may fail to compile.

Additionally, *SplitCA* uses [coprocessor](https://github.com/nmanthey/riss-solver) to simplify the input CNF instance, and uses [d4v2](https://github.com/crillab/d4v2) and [FastFMC](https://github.com/ShuangyuLyu/FastFMC) to convert CNF to d-DNNF for the **knowledge-compiled tuple validation technique**. For ease of use, we provide copies of these executables in `bin/` (these executables are currently only expected to run on Ubuntu 24.04).

> Note: The [coprocessor](https://github.com/nmanthey/riss-solver) is under [LGPL license](https://github.com/nmanthey/riss-solver/blob/master/LICENSE); the [d4v2](https://github.com/crillab/d4v2) is also under [LGPL license](https://github.com/crillab/d4v2/blob/main/LICENSE).

*SplitCA* uses [CMake](https://cmake.org) to configure the project, and requires CMake version 3.22 or later.  
To configure *SplitCA*, use the following command:

```sh
cmake -DCMAKE_BUILD_TYPE=Release -B build .
```

> *SplitCA* has on four dependencies: [clipp](https://github.com/muellan/clipp) for cli arguments parsing, [minisat](https://github.com/niklasso/minisat.git) for low-level solver, [dbg_macro](https://github.com/sharkdp/dbg-macro) for debuging and [ftxui](https://github.com/ArthurSonzogni/FTXUI) for terminal user interface.
> CMake will automatically download and compile these dependencies. Users do not need to download manually.

If the version of your system default g++ is lower than required, you can manually specify the compiler with the following command:

```sh
# Do not forget to change `/usr/bin/gcc-12` and `/usr/bin/g++-12` to your real path for gcc and g++.
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=/usr/bin/gcc-12 -DCMAKE_CXX_COMPILER=/usr/bin/g++-12 -B build .
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

## Implementation of *SplitCA*

The main implementation of *SplitCA* is in the `src/` directory.

## Testing Instances for Evaluating *SplitCA*

The instances used in our experiments are placed in the `benchmarks/` directory. These instances are in CNF format (i.e., Boolean formulas in Conjunctive Normal Form).

## Experimental Results

The directory `experimental_results/` contains two `.csv` files that present the experimental results:

+ [Results_on_10_representative_instances/result_10.csv](experimental_results/Results_on_10_representative_instances/result_10.csv): Results of *SplitCA* on 10 representative instances.

+ [Results_on_large-scale_instances/result_large.csv](experimental_results/Results_on_large-scale_instances/result_large.csv): Results of *SplitCA* on large-scale instances with more than 1000 options. All the compititors fail to generate correct CA on these instances.

+ [Results_on_medium-scale_instances/result_medium.csv](experimental_results/Results_on_medium-scale_instances/result_medium.csv): Results of *SplitCA*, *HSCA* and *AutoCCAG* on medium-scale instances with less than 1000 options.
