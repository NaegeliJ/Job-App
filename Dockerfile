FROM debian:trixie-slim AS builder
RUN apt-get update && apt-get install -y cmake make g++ libcurl4-openssl-dev zlib1g-dev && rm -rf /var/lib/apt/lists/*
ARG VERSION=unknown
WORKDIR /src
COPY . .
RUN cmake -B build -DAPP_VERSION=${VERSION} && cmake --build build --parallel

FROM debian:trixie-slim
RUN apt-get update && apt-get install -y libcurl4t64 curl zlib1g && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=builder /src/build/Job_App .
COPY frontend/ frontend/
COPY config/system_prompt.txt config/system_prompt.txt
COPY config/user_profile_template.md config/user_profile_template.md
COPY config/onboarding_prompt.txt config/onboarding_prompt.txt
COPY config/import_prompt.txt config/import_prompt.txt
RUN mkdir -p data
EXPOSE 8080
CMD ["./Job_App"]
