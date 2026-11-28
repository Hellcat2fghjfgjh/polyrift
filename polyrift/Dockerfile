# Build stage: compile the server.
FROM gcc:13 AS build
WORKDIR /src
COPY Makefile ./
COPY src ./src
RUN make LDFLAGS="-static-libstdc++ -static-libgcc"

# Run stage: just the binary and the web client.
FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /src/polyrift ./polyrift
COPY public ./public
RUN useradd --system polyrift
USER polyrift
EXPOSE 3000
CMD ["./polyrift"]
