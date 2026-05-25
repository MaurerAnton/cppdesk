FROM ubuntu:24.04 AS builder
RUN apt-get update && apt-get install -y \
    cmake ninja-build g++-13 pkg-config \
    libgtk-3-dev libx11-dev libxfixes-dev libxrandr-dev \
    libpulse-dev libssl-dev libsqlite3-dev \
    git ca-certificates
WORKDIR /build
COPY . .
RUN cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13
RUN cmake --build build --parallel $(nproc)

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y \
    libgtk-3-0 libx11-6 libxfixes3 libxrandr2 \
    libpulse0 libssl3 libsqlite3-0
COPY --from=builder /build/build/cppdesk_client /usr/bin/cppdesk
COPY --from=builder /build/build/cppdesk_server /usr/bin/cppdesk_server
COPY --from=builder /build/build/cppdesk_cli /usr/bin/cppdesk_cli
ENTRYPOINT ["/usr/bin/cppdesk"]
