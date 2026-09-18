# Authentication

## HTTP API

- `POST /api/v1/auth/register` — создаёт пользователя и выдаёт пару токенов.
- `POST /api/v1/auth/login` — проверяет login/password.
- `POST /api/v1/auth/refresh` — атомарно ротирует refresh token.
- `POST /api/v1/auth/logout` — отзывает переданный refresh token.
- `GET /api/v1/auth/me` — требует `Authorization: Bearer <accessToken>`.
- `GET /api/v1/profile` — требует `Authorization: Bearer <accessToken>` и
  возвращает публичные данные текущего пользователя и `played/wins/losses/draws`.

Login нормализуется в lowercase и должен соответствовать
`[a-z0-9_]{3,32}`. Пароль содержит 10–72 UTF-8 байт; верхняя граница задана
ограничением bcrypt. В базе хранится только bcrypt hash.

Access token — JWT HS256 со стандартными claims `iss`, `aud`, `sub`, `exp`,
`nbf`, `iat`, `jti` и custom claim `login`. TTL — 15 минут. Секрет задаётся
только через `JWT_SECRET` и должен содержать минимум 32 байта.

Refresh token — 256 случайных бит, закодированных base64url. В Postgres
хранится только SHA-256 hash. TTL — 30 дней. Refresh token одноразовый:
успешное обновление атомарно удаляет старый token и создаёт новый.

## Хранилище

Production wiring Gateway использует Postgres через `DATABASE_URL`. До запуска
Gateway нужно явно выполнить `make migrate`: команда применяет
`backend/migrations/*.up.sql` в лексикографическом порядке и записывает каждую
версию в `schema_migrations` в той же транзакции. Для legacy volumes без ledger
команда baseline-ит уже существующие `users`/`match_results`, поэтому её
безопасно запускать повторно и на существующем PostgreSQL volume; Gateway не выполняет миграции
автоматически. `MemoryStore` сохраняется только для unit-тестов.

Match token — отдельный credential с узким назначением. Access JWT нельзя
предъявлять напрямую игровому серверу: авторизованная очередь Gateway выдаёт
короткоживущий match ticket, привязанный к user ID, match ID и opponent ID.

## Internal match result delivery

Игровой клиент не имеет endpoint для записи результата. Match Server после
терминального authoritative snapshot делает `POST /internal/v1/matches/result`
в Gateway с заголовком `X-Internal-Match-Secret`. Gateway и Match Server должны
получить один `INTERNAL_MATCH_RESULT_SECRET` длиной минимум 32 байта. URL Gateway
для Match Server задаётся `GATEWAY_INTERNAL_URL` (по умолчанию
`http://localhost:8080`). Секрет не передаётся в логи и не включается в клиент.

Запись `match_results` уникальна по `match_id`: retry либо duplicate delivery
возвращает успешный ответ, но не меняет статистику второй раз.
