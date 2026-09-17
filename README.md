# 2D PvP Duel

Монорепо для realtime 2D-дуэли 1v1: C++ клиент и авторитетный Go backend.

## Текущий инкремент

- Go Gateway: bcrypt login/password, JWT access tokens и rotating refresh tokens.
- PostgreSQL users/refresh-token storage с версионированной миграцией.
- Изолированная детерминированная модель комнаты с fixed tick и unit-тестами.
- QUIC: reliable stream для auth/control и datagram для input/snapshot.
- Client-side prediction и reconciliation локального игрока.
- Protobuf-схема как единый контракт клиента и backend.
- C++ client skeleton с raylib. Решение зафиксировано в
  [`docs/adr/0001-client-framework.md`](docs/adr/0001-client-framework.md).

Полная целевая схема: [`arch-2d-pvp-duel (1).md`](arch-2d-pvp-duel%20(1).md).

## Быстрый старт

```bash
make backend-test
make db-up
make gateway
# либо Match Server:
make dev-cert
make matchserver
# health: http://localhost:8081/healthz, QUIC: udp://localhost:4242
# в другом терминале — полный smoke test протокола:
make smoke-quic
```

Сборка клиента:

```bash
make setup-msquic  # один раз: локально скачивает официальный MsQuic 2.5.7
make proto
make client
```

Запусти Gateway, Match Server и два экземпляра клиента. В каждом окне создай
отдельного пользователя, нажми **Find match** — клиенты автоматически опросят
очередь и подключатся к QUIC комнате по полученному ticket:

```bash
./client/build/pvp_duel_client
./client/build/pvp_duel_client
```

Gateway URL можно переопределить через `GATEWAY_URL`. Управление в игре:
`WASD`, атака — `Space`. Подробности: [`docs/client-flow.md`](docs/client-flow.md).
Зависимости MsQuic хранятся в `.deps/` и не попадают в Git.

Локальный `make gateway` использует development-значения `DATABASE_URL` и
`JWT_SECRET` из Makefile; в deployment их обязательно нужно переопределить.
Описание endpoints и модели безопасности: [`docs/auth.md`](docs/auth.md) и
[`docs/matchmaking.md`](docs/matchmaking.md).

## Инструменты

- Go 1.25+
- `buf` или `protoc` с плагинами `protoc-gen-go` и `protoc-gen-go-grpc`
  для `make proto`
- CMake 3.24+, C++20-компилятор и OpenSSL development headers для клиента
- `curl`, `dpkg-deb` и `apt-get download` для `make setup-msquic`

Module path сейчас `github.com/dprishchepa/2d-pvp-duel`; поменяйте его до
первого внешнего релиза, если URL репозитория будет другим.
