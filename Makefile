DATABASE_URL ?= postgres://pvp_duel:local_only_password@localhost:5432/pvp_duel?sslmode=disable
JWT_SECRET ?= local-development-jwt-secret-change-me-123
MATCH_TICKET_SECRET ?= local-match-ticket-secret-change-me-123

.PHONY: backend-build backend-test gateway matchserver smoke-quic proto client client-test client-macos appimage appimage-native setup-msquic dev-cert db-up db-down db-reset

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

client:
	cmake -S client -B client/build
	cmake --build client/build

client-test: client
	ctest --test-dir client/build --output-on-failure

client-macos:
	brew install buf cmake protobuf abseil libmsquic
	make proto
	cmake -S client -B client/build-macos -DCMAKE_BUILD_TYPE=Release
	cmake --build client/build-macos --target pvp_duel_client --parallel

appimage:
	GATEWAY_URL='$(GATEWAY_URL)' ./scripts/package-appimage-compatible.sh

appimage-native:
	GATEWAY_URL='$(GATEWAY_URL)' ./scripts/package-appimage.sh

setup-msquic:
	./scripts/setup-msquic.sh

db-up:
	docker compose -p pvp-duel -f deploy/docker-compose.yml up -d postgres

db-down:
	docker compose -p pvp-duel -f deploy/docker-compose.yml down

db-reset:
	docker compose -p pvp-duel -f deploy/docker-compose.yml down -v
	docker compose -p pvp-duel -f deploy/docker-compose.yml up -d postgres

dev-cert:
	mkdir -p deploy/certs
	openssl req -x509 -newkey rsa:2048 -nodes \
		-keyout deploy/certs/server.key \
		-out deploy/certs/server.crt \
		-days 365 -subj '/CN=localhost' \
		-addext 'subjectAltName=DNS:localhost,IP:127.0.0.1'
