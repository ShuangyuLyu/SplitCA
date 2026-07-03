FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash \
        build-essential \
        ca-certificates \
        cmake \
        coreutils \
        git \
        time \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . /workspace

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Wno-dev \
    && cmake --build build --target SplitCA check_tuples -j"$(nproc)"

FROM ubuntu:24.04

WORKDIR /workspace

COPY benchmarks /workspace/benchmarks
COPY bin /workspace/bin
COPY test /workspace/test
COPY --from=builder /workspace/build/bin /workspace/build/bin

RUN chmod +x bin/* test/*.sh

CMD [ "/bin/bash" ]
