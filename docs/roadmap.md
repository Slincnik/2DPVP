# Roadmap: 2D Realtime PvP Duel

## Текущее состояние

Рабочий вертикальный срез уже существует:

```text
register/login
→ JWT
→ matchmaking queue
→ signed match tickets
→ QUIC connection
→ authoritative fixed-tick room
→ datagram input/snapshots
→ client prediction/interpolation
```

Backend, C++ клиент и сквозной сценарий двух игроков работают локально.

---

## P0 — законченный игровой цикл MVP

### Lifecycle матча

- [ ] Разнести игроков по разным стартовым позициям.
- [ ] Ввести состояния `waiting → countdown → active → finished`.
- [ ] Добавить обратный отсчёт перед началом матча.
- [ ] Запретить движение и атаки до состояния `active`.
- [ ] Добавить ограничение времени матча.
- [ ] Определить победителя по HP или оставшемуся HP после таймаута.
- [ ] Надёжно доставлять `MatchStart` и `MatchEnd` через QUIC stream.
- [ ] После результата закрывать room с небольшим grace period.
- [ ] Добавить возврат клиента в меню.
- [ ] Разрешить повторный поиск матча без перезапуска клиента.

### Disconnect

- [ ] Обнаруживать разрыв соединения каждого игрока.
- [ ] Определить правила технического поражения.
- [ ] Добавить короткий reconnect grace period.
- [ ] Не оставлять room/goroutine после ухода игроков.
- [ ] Показывать клиенту причину завершения матча.

### Базовый gameplay UX

- [ ] Отображать countdown, таймер, HP и результат.
- [ ] Добавить визуальное состояние атаки.
- [ ] Добавить эффект попадания.
- [ ] Зафиксировать направленные атаки и реальные хитбоксы.
- [ ] Добавить базовые звуки.

---

## P1 — результаты, профиль и рейтинг

### PostgreSQL

- [ ] Добавить таблицу истории матчей.
- [ ] Хранить участников, победителя, причину завершения и длительность.
- [ ] Гарантировать идемпотентную запись результата по `match_id`.
- [ ] Добавить рейтинг/MMR пользователя.
- [ ] Обновлять рейтинг транзакционно вместе с результатом матча.
- [ ] Добавить периодическую очистку истёкших refresh tokens.

### Backend API

- [ ] `GET /api/v1/profile`.
- [ ] `GET /api/v1/matches` с cursor pagination.
- [ ] `GET /api/v1/leaderboard`.
- [ ] Добавить internal endpoint/RPC для подтверждения результата Match Server.
- [ ] Не принимать результат матча от игрового клиента.

### Matchmaking

- [ ] Подбирать соперников по диапазону рейтинга.
- [ ] Расширять допустимый диапазон по мере ожидания.
- [ ] Добавить timeout ожидания и понятный статус очереди.
- [ ] Добавить cancel queue в UI клиента.

---

## P1 — клиентская auth-сессия

- [ ] Автоматически обновлять access token через refresh token.
- [ ] Повторять исходный HTTP-запрос после успешного refresh.
- [ ] Добавить logout и отзыв refresh token.
- [ ] Выбрать защищённое persistent storage:
  - Windows Credential Manager;
  - macOS Keychain;
  - Secret Service/libsecret на Linux.
- [ ] Не хранить refresh token открытым текстом.
- [ ] Восстанавливать пользовательскую сессию при старте клиента.
- [ ] Добавить отдельные экраны профиля, очереди и результата.

---

## P1 — безопасность перед внешним deployment

- [ ] Включить проверку сертификата Match Server в MsQuic.
- [ ] Убрать `QUIC_CREDENTIAL_FLAG_NO_CERTIFICATE_VALIDATION`.
- [ ] Публиковать Gateway только через HTTPS.
- [ ] Настроить доверенные CA/certificate pinning при необходимости.
- [ ] Разделить development и production конфигурации.
- [ ] Хранить JWT и match-ticket secrets в secret manager.
- [ ] Добавить ротацию ключей с `kid`.
- [ ] Добавить rate limiting для register/login/refresh/queue.
- [ ] Ограничить число активных сессий и refresh tokens пользователя.
- [ ] Обнаруживать повторное использование уже ротированного refresh token.
- [ ] При refresh-token replay отзывать всё семейство токенов.
- [ ] Добавить security headers и строгую конфигурацию reverse proxy.
- [ ] Проверить максимальные размеры всех HTTP, stream и datagram сообщений.

---

## P2 — устойчивость сети

- [ ] Добавить reconnect к активному матчу.
- [ ] Привязать reconnect token к пользователю и `match_id`.
- [ ] Восстанавливать prediction buffer после reconnect.
- [ ] Добавить sequence/ack метрики для datagram loss.
- [ ] Проверить поведение при packet loss, duplication и reordering.
- [ ] Проверить игру при latency 50/100/200 ms и jitter.
- [ ] Добавить rate limit входящих игровых inputs.
- [ ] Запрещать слишком далёкие будущие input ticks.
- [ ] Добавить timeout отсутствия inputs/AFK detection.
- [ ] Дублировать критичные события только через reliable stream.

---

## P2 — масштабирование backend

### Matchmaking и presence

- [ ] Перенести очередь из памяти Gateway в Redis.
- [ ] Хранить presence и active match mapping.
- [ ] Сделать операции join/pair/cancel атомарными.
- [ ] Поддержать несколько экземпляров Gateway.

### Match Server fleet

- [ ] Добавить регистрацию и heartbeat Match Server.
- [ ] Выбирать сервер по региону, нагрузке и доступности.
- [ ] Создавать room через внутренний gRPC/RPC.
- [ ] Включать server ID/address в match ticket.
- [ ] Перенести consumed-ticket state в Redis либо конкретный Match Server.
- [ ] Добавить graceful draining сервера перед deployment.
- [ ] Не назначать новые матчи на draining instance.

### MatchManager

- [ ] Хранить активные rooms по `match_id`.
- [ ] Добавить лимиты rooms и подключений.
- [ ] Добавить защиту от повторного подключения третьего клиента.
- [ ] Гарантировать очистку room, channels и goroutines.

---

## P2 — observability

- [ ] Добавить structured logging через `slog`.
- [ ] Добавить request ID и match ID во все связанные логи.
- [ ] Не логировать пароли, JWT, refresh tokens и match tickets.
- [ ] Добавить Prometheus metrics:
  - HTTP latency/error rate;
  - размер очереди и время ожидания;
  - число активных rooms/players;
  - tick duration и missed ticks;
  - datagram loss/receive rate;
  - match duration и disconnect rate.
- [ ] Добавить health/readiness проверки PostgreSQL и Redis.
- [ ] Добавить OpenTelemetry tracing между Gateway и Match Server.
- [ ] Настроить алерты на tick overruns, auth errors и рост disconnect rate.

---

## P2 — тестирование

- [ ] Integration tests auth repository с настоящим PostgreSQL.
- [ ] Integration tests migrations up/down.
- [ ] Полный тест `register → queue → QUIC → result → rating`.
- [ ] Тест одновременного входа множества игроков в очередь.
- [ ] Race/leak tests MatchManager и room lifecycle.
- [ ] Fuzz tests protobuf framing и HTTP JSON parsing.
- [ ] Network simulation tests с loss/jitter/reordering.
- [ ] Проверка истечения JWT, refresh token и match ticket.
- [ ] Проверка replay access/refresh/match credentials.
- [ ] Soak test множества последовательных матчей.
- [ ] Load test Gateway и Match Server.

---

## P2 — CI/CD и deployment

- [ ] Добавить Dockerfile для Gateway.
- [ ] Добавить Dockerfile для Match Server.
- [ ] Добавить production Docker Compose или Kubernetes manifests.
- [ ] Добавить GitHub Actions jobs:
  - Go format/vet/test/race;
  - CMake build/CTest;
  - protobuf generation check;
  - migration validation;
  - dependency/security scanning.
- [ ] Проверять, что generated protobuf не устарел.
- [ ] Добавить version/build commit в бинарники.
- [ ] Настроить reproducible release builds клиента.
- [ ] Настроить backup и restore PostgreSQL.

---

## P3 — игровой контент после MVP

- [ ] Несколько арен.
- [ ] Несколько персонажей/наборов способностей.
- [ ] Dash, jump или другие механики после фиксации базового combat design.
- [ ] Spectator mode.
- [ ] Replay на основе input stream.
- [ ] Chat/social events через reliable stream.
- [ ] Friends, invites и private lobby.
- [ ] Региональный matchmaking.
- [ ] Сезоны и рейтинговые награды.

---

## Рекомендуемый порядок ближайших работ

1. Lifecycle матча и стартовые позиции.
2. Disconnect/cleanup и возврат в меню.
3. Запись результата в PostgreSQL.
4. ELO/MMR и profile endpoints.
5. Автоматический refresh/logout на клиенте.
6. Проверка сертификата Match Server и HTTPS Gateway.
7. Redis и масштабирование только после устойчивого single-instance MVP.
