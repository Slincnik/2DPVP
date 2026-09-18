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

CI и автоматическое обновление Linux-клиента описаны в
[`docs/client-updates.md`](docs/client-updates.md). Релиз создаётся тегом
`vX.Y.Z` и публикует AppImage в GitHub Releases.

## Быстрый старт

```bash
make backend-test
make db-up
make migrate # обязательная операция перед первым запуском Gateway на существующем volume
make gateway
# либо Match Server:
make dev-cert
make matchserver
# health: http://localhost:8081/healthz, QUIC: udp://localhost:4242
# в другом терминале — полный smoke test протокола:
make smoke-quic
```

Сборка клиента (генерация protobuf и подготовка MsQuic выполняются автоматически):

```bash
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

### Сборка клиента на macOS / Apple Silicon

На Mac с M1/M2/M3 нужен нативный ARM64 build toolchain. Установи Xcode
Command Line Tools и Homebrew, затем зависимости:

```bash
xcode-select --install  # если ещё не установлен
brew install buf cmake protobuf abseil libmsquic
make client
```

Готовый бинарник будет в `client/build/pvp_duel_client`. Gateway можно
переопределить при запуске:

```bash
GATEWAY_URL=http://192.168.88.72:8080 ./client/build/pvp_duel_client
```

CMake автоматически находит Homebrew в `/opt/homebrew` (Apple Silicon) или
`/usr/local` (Intel). На macOS сертификат Match Server пока намеренно не
проверяется так же, как в development Linux-клиенте.

### Переносимый Linux-клиент (AppImage)

Собрать x86_64 AppImage с адресом Gateway по умолчанию:

```bash
GATEWAY_URL=http://192.168.1.10:8080 make appimage
```

Готовый файл появится в `dist/PvPDuel-x86_64.AppImage`. Его можно передать
пользователю и запустить без установки MsQuic/protobuf:

```bash
chmod +x PvPDuel-x86_64.AppImage
./PvPDuel-x86_64.AppImage
```

Адрес сервера при необходимости переопределяется при запуске через
`GATEWAY_URL`. Основная цель собирает клиент в контейнере Ubuntu 22.04, скачивает закреплённую
версию `linuxdeploy`, проверяет её SHA-256 и упаковывает динамические
зависимости. Поэтому AppImage совместим с glibc 2.35 и запускается на Ubuntu
22.04 и более новых версиях. Для сохранения refresh token на Linux во время
сборки требуется `libsecret-1-dev`, а во время запуска — доступный freedesktop
Secret Service/keyring (например, GNOME Keyring или KWallet). Keyring daemon не
входит в AppImage: если Secret Service недоступен, вход работает только до
завершения текущего запуска, а persistence корректно отключается без записи
credentials в settings или обычный файл.

Для быстрой сборки непосредственно на текущей системе существует `make
appimage-native`; он также требует libsecret development files и завершает
конфигурацию с ошибкой при их отсутствии. Такой артефакт может требовать более
новую glibc. Если в системе недоступен FUSE, AppImage можно запустить с
`--appimage-extract-and-run`.

Миграции не выполняются Gateway автоматически: до запуска Gateway в каждом
новом deployment или на существующем PostgreSQL volume нужно выполнить
`make migrate`. Команда ведёт таблицу `schema_migrations`, поэтому повторный
запуск безопасен и применяет `000002_match_results` один раз. Для volumes,
созданных до появления migration ledger, она распознаёт существующие таблицы
`users`/`match_results` и baseline-ит соответствующие первоначальные миграции,
не переигрывая их.

Локальный `make gateway` использует development-значения `DATABASE_URL` и
`JWT_SECRET` из Makefile; для Gateway и Match Server также нужно задать один
`INTERNAL_MATCH_RESULT_SECRET` не короче 32 байт. Match Server использует
`GATEWAY_INTERNAL_URL` (по умолчанию `http://localhost:8080`) для защищённой
передачи terminal match results. В deployment эти значения обязательно нужно
переопределить. Описание endpoints и модели безопасности: [`docs/auth.md`](docs/auth.md) и
[`docs/matchmaking.md`](docs/matchmaking.md).

## Инструменты

- Go 1.25+
- `buf` или `protoc` с плагинами `protoc-gen-go` и `protoc-gen-go-grpc`
  для `make proto`
- CMake 3.24+, C++20-компилятор и OpenSSL development headers для клиента
- Linux-клиенту дополнительно нужны `curl`, `dpkg-deb` и доступ `apt-get download` к пакету `libxdp1`; MsQuic подготавливается автоматически через `make client`

Module path сейчас `github.com/dprishchepa/2d-pvp-duel`; поменяйте его до
первого внешнего релиза, если URL репозитория будет другим.
