SHELL := /bin/sh

ROOT_DIR := $(abspath .)
CLIENT_BUILD_DIR ?= $(ROOT_DIR)/client/build
MSQUIC_VERSION ?= 2.5.7

# Prefer the Compose CLI plugin, but support the standalone command as well.
COMPOSE ?= $(shell \
	if docker compose version >/dev/null 2>&1; then printf 'docker compose'; \
	elif command -v docker-compose >/dev/null 2>&1; then printf 'docker-compose'; \
	fi)

DATABASE_URL ?= postgres://pvp_duel:local_only_password@localhost:5432/pvp_duel?sslmode=disable
JWT_SECRET ?= local-development-jwt-secret-change-me-123
MATCH_TICKET_SECRET ?= local-match-ticket-secret-change-me-123

.PHONY: all help proto client client-test server server-test \
        backend-build backend-test gateway matchserver smoke-quic \
        appimage appimage-native setup-msquic dev-cert db-up db-down db-reset clean

all: help

help:
	@printf '%s\n' \
	  'make client       Generate protobufs and build the C++ client' \
	  'make client-test  Build and run C++ tests' \
	  'make server       Build the Go backend' \
	  'make server-test  Run Go tests with the race detector' \
	  'make gateway      Build and run the Gateway' \
	  'make matchserver  Build and run the Match Server' \
	  'make proto        Generate Go and C++ protobuf code' \
	  'make db-up        Start local PostgreSQL'

# The client target owns its prerequisites: a fresh checkout only needs the
# tools listed in README.md and `make client`.
client: proto setup-msquic
	cmake -S client -B "$(CLIENT_BUILD_DIR)" $${CMAKE_ARGS:-}
	cmake --build "$(CLIENT_BUILD_DIR)" --parallel $${JOBS:-}

client-test: client
	ctest --test-dir "$(CLIENT_BUILD_DIR)" --output-on-failure

server: backend-build
server-test: backend-test

backend-build:
	cd backend && go build ./...

backend-test:
	cd backend && go test -race ./...

gateway:
	cd backend && mkdir -p bin && go build -o bin/gateway ./cmd/gateway && \
		DATABASE_URL='$(DATABASE_URL)' JWT_SECRET='$(JWT_SECRET)' \
		MATCH_TICKET_SECRET='$(MATCH_TICKET_SECRET)' exec ./bin/gateway

matchserver:
	cd backend && mkdir -p bin && go build -o bin/matchserver ./cmd/matchserver && \
		MATCH_TICKET_SECRET='$(MATCH_TICKET_SECRET)' exec ./bin/matchserver

smoke-quic:
	cd backend && MATCH_TICKET_SECRET='$(MATCH_TICKET_SECRET)' go run ./cmd/smokeclient

proto:
	buf generate

# MsQuic is deliberately bootstrapped here instead of through a Debian-only
# script. Homebrew provides a native library on both Intel and Apple Silicon;
# Linux uses the official package and detects the host architecture.
setup-msquic:
	@if [ "$$(uname -s)" = Darwin ]; then \
		command -v brew >/dev/null 2>&1 || { echo 'Homebrew is required for the client on macOS.' >&2; exit 1; }; \
		brew list --versions libmsquic >/dev/null 2>&1 || { echo 'Install the client prerequisites first: brew install cmake buf protobuf abseil libmsquic' >&2; exit 1; }; \
		echo 'Using Homebrew MsQuic'; \
	else \
		command -v dpkg-deb >/dev/null 2>&1 && command -v apt-get >/dev/null 2>&1 && command -v curl >/dev/null 2>&1 || { echo 'Linux client requires apt-get, curl and dpkg-deb.' >&2; exit 1; }; \
		arch=$$(dpkg --print-architecture 2>/dev/null || uname -m); \
		case "$$arch" in x86_64) arch=amd64;; aarch64) arch=arm64;; armv7l) arch=armhf;; amd64|arm64|armhf) ;; *) echo "Unsupported Linux architecture: $$arch" >&2; exit 1;; esac; \
		root='$(ROOT_DIR)'; dest="$$root/.deps/msquic"; \
		if [ -f "$$dest/include/msquic.h" ] && find "$$dest/usr/lib" -name 'libmsquic.so.2' -print -quit | grep -q .; then echo "Using cached MsQuic in $$dest"; exit 0; fi; \
		tmp=$$(mktemp -d); trap 'rm -rf "$$tmp"' EXIT; \
		mkdir -p "$$dest/include"; \
		if [ -r /etc/os-release ]; then . /etc/os-release; fi; \
		case "$${ID:-debian}" in ubuntu) base="https://packages.microsoft.com/ubuntu/$${VERSION_ID}/prod/pool/main/libm/libmsquic";; *) base="https://packages.microsoft.com/repos/microsoft-debian-trixie-prod/pool/main/libm/libmsquic";; esac; \
		url="$$base/libmsquic_$(MSQUIC_VERSION)_$$arch.deb"; \
		echo "Downloading MsQuic $(MSQUIC_VERSION) ($$arch)"; curl -fsSL "$$url" -o "$$tmp/libmsquic.deb"; dpkg-deb -x "$$tmp/libmsquic.deb" "$$dest"; \
		(cd "$$tmp" && apt-get download libxdp1 >/dev/null && dpkg-deb -x libxdp1_*.deb "$$dest"); \
		for header in msquic.h msquic_posix.h quic_sal_stub.h; do curl -fsSL "https://raw.githubusercontent.com/microsoft/msquic/v$(MSQUIC_VERSION)/src/inc/$$header" -o "$$dest/include/$$header"; done; \
		echo "MsQuic installed in $$dest"; \
	fi

appimage:
	GATEWAY_URL='$(GATEWAY_URL)' ./scripts/package-appimage-compatible.sh

appimage-native: setup-msquic
	GATEWAY_URL='$(GATEWAY_URL)' ./scripts/package-appimage.sh

db-up:
	$(COMPOSE) -p pvp-duel -f deploy/docker-compose.yml up -d postgres

db-down:
	$(COMPOSE) -p pvp-duel -f deploy/docker-compose.yml down

db-reset:
	$(COMPOSE) -p pvp-duel -f deploy/docker-compose.yml down -v
	$(COMPOSE) -p pvp-duel -f deploy/docker-compose.yml up -d postgres

dev-cert:
	mkdir -p deploy/certs
	openssl req -x509 -newkey rsa:2048 -nodes \
		-keyout deploy/certs/server.key -out deploy/certs/server.crt \
		-days 365 -subj '/CN=localhost' \
		-addext 'subjectAltName=DNS:localhost,IP:127.0.0.1'

clean:
	rm -rf "$(CLIENT_BUILD_DIR)" client/build-macos
