# Builder: use distro Boost (fast). Other environments may still build Boost from source via CMake.
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    g++ \
    git \
    libargon2-dev \
    libboost-all-dev \
    libfmt-dev \
    libgmock-dev \
    libgtest-dev \
    libspdlog-dev \
    libsqlite3-dev \
    libsqlitecpp-dev \
    ninja-build \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY . .

# Skip FetchContent for fmt/spdlog/SQLiteCpp/GTest/argon2; use distro packages above.
RUN cmake -S . -B /build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DTESTS_ONLY=OFF \
        -DVOX_USE_SYSTEM_DEPS=ON \
    && cmake --build /build --target vox-server

FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libargon2-1 \
    libboost-chrono1.83.0 \
    libboost-date-time1.83.0 \
    libboost-filesystem1.83.0 \
    libboost-iostreams1.83.0 \
    libboost-json1.83.0 \
    libboost-locale1.83.0 \
    libboost-program-options1.83.0 \
    libboost-regex1.83.0 \
    libboost-serialization1.83.0 \
    libboost-system1.83.0 \
    libboost-test1.83.0 \
    libboost-thread1.83.0 \
    libboost-wave1.83.0 \
    libfmt9 \
    libspdlog1.12 \
    libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

# Avoid UID 1000: ubuntu:24.04 reserves it for the default "ubuntu" user.
RUN useradd --system --uid 10001 --home /data --shell /usr/sbin/nologin vox \
    && mkdir -p /data/blobs \
    && chown -R vox:vox /data

COPY --from=builder /build/bin/vox-server /usr/local/bin/vox-server
COPY deploy/docker-entrypoint.sh /docker-entrypoint.sh
RUN chmod +x /docker-entrypoint.sh

EXPOSE 8080
VOLUME ["/data"]

ENTRYPOINT ["/docker-entrypoint.sh"]
