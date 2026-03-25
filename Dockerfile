FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential \
       cmake \
       libssl-dev \
       pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVIGILANT_AUTO_SYMLINK_GLOBAL=OFF \
    && cmake --build build --config Release -j"$(nproc)"

FROM debian:bookworm-slim AS runtime

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       ca-certificates \
       libssl3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/build/vigilant /usr/local/bin/vigilant

RUN mkdir -p /etc/vigilant/services

EXPOSE 9000 9001
VOLUME ["/etc/vigilant/services"]

ENTRYPOINT ["/usr/local/bin/vigilant"]
CMD ["server", "-d", "/etc/vigilant/services", "-p", "9000", "-dash", "9001"]
