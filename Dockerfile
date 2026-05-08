FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y cmake make g++ libcurl4-openssl-dev && rm -rf /var/lib/apt/lists/*
ARG VERSION=unknown
WORKDIR /src
COPY . .
RUN cmake -B build -DAPP_VERSION=${VERSION} && cmake --build build --parallel

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y libcurl4 curl && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /src/build/Job_App .
COPY frontend/ frontend/
COPY config/system_prompt.txt config/system_prompt.txt
COPY config/user_profile_template.md config/user_profile_template.md
RUN mkdir -p data
EXPOSE 8080
CMD ["./Job_App"]
