# SplitCA ASE 2026 Artifact Package

This directory contains the artifact-evaluation material for:

> *SplitCA*: An Effective Tool for Generating 4-wise Covering Arrays for
> Large-Scale Highly Configurable Systems

## Getting Started

Users should be able to complete this section within 10 minutes on a machine
with Docker or Podman and internet access for the image build. The image build
downloads build-time dependencies through Ubuntu packages and CMake
`FetchContent`; the runtime scripts do not download data or dependencies.

From the repository root, build the container:

```sh
docker build --platform linux/amd64 -t splitca:latest .
```

Alternatively, we provide a prebuilt Docker image at
[artifacts/splitca_image.tar.gz](splitca_image.tar.gz). Users can load this
image without a network connection from the repository root:

```sh
docker load -i artifacts/splitca_image.tar.gz
```

Run the smoke test:

```sh
./artifacts/scripts/smoke_test.sh
```

The smoke test runs SplitCA on `benchmarks/axtls.cnf` and checks the generated
covering array automatically.

If Docker is unavailable but Podman is installed, replace `docker` with
`podman` in the commands above.

## Representative Reproduction

This workflow runs on the 10 representative instances introduced in the paper.

```sh
./artifacts/scripts/run_representative.sh
```

The benchmark list is in `artifacts/representative.txt`. The default random
seed is `1`; users can override it:

```sh
SEED=<any-positive-integer> ./artifacts/scripts/run_representative.sh
```

Outputs are written to `artifact_outputs/representative/`.

## Full Reproduction

The full workflow runs all CNF instances in `benchmarks/` using the
repository's `default.conf.json`:

```sh
./artifacts/scripts/run_full.sh
```

This is a long-running workflow. It is provided to make the full experimental
path explicit, but the representative workflow is the recommended evaluator
path for checking artifact functionality within a bounded time budget. The full
workflow uses the same output layout as the representative workflow, under
`artifact_outputs/reproduction/`.

## Running a Custom CNF

After building the container, a user can run SplitCA on any DIMACS CNF file
mounted into the container. For a file under the repository, the command shape
is:

```sh
docker run --rm \
  -v "$PWD/artifact_outputs:/workspace/artifact_outputs" \
  -v "$PWD/custom_benchmarks:/workspace/custom_benchmarks" \
  splitca:latest ./build/bin/SplitCA \
  -s 1 -o artifact_outputs/custom.csv custom_benchmarks/custom.cnf
```

For more information about the command-line interface of SplitCA, run the image
with `--help`:

```sh
docker run --rm splitca:latest ./build/bin/SplitCA --help
```

## The Terminal User Interface

*SplitCA* also provides a terminal user interface (TUI) for interactive progress
tracking. Because the TUI needs an interactive terminal, run the container with
`-it` when enabling this mode.

To launch *SplitCA* directly in TUI mode:

```sh
docker run --rm -it splitca:latest ./build/bin/SplitCA benchmarks/axtls.cnf --tui
```

The benchmark instance `benchmarks/axtls.cnf` can be replaced by any instance in
the `benchmarks/` directory or by any instance mounted into the container.

Alternatively, start an interactive shell inside the container:

```sh
docker run --rm -it splitca:latest
```

The prompt should look similar to `root@8701f934c92e:/workspace#`. From that
prompt, run:

```sh
./build/bin/SplitCA benchmarks/axtls.cnf --tui
```
