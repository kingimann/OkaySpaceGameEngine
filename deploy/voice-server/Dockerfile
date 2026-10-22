# OkaySpace voice server (the UDP relay calls and multiplayer go through).
# Build from the repository root:
#   docker build -f deploy/voice-server/Dockerfile -t okayspace-voice .
#   docker run -d --restart unless-stopped -p 45100:45100/udp okayspace-voice
FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -DCMAKE_BUILD_TYPE=Release \
        -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_TESTS=OFF -DOKAY_BUILD_LAUNCHER=OFF \
        -DOKAY_USE_SODIUM=OFF \
 && cmake --build /build --target okayspace-relay -j"$(nproc)"

FROM debian:bookworm-slim
COPY --from=build /build/bin/okayspace-relay /usr/local/bin/okayspace-relay
RUN useradd --system --no-create-home okay
USER okay
EXPOSE 45100/udp
ENTRYPOINT ["/usr/local/bin/okayspace-relay"]
CMD ["45100"]
