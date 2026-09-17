# Authentication

## HTTP API

- `POST /api/v1/auth/register` — создаёт пользователя и выдаёт пару токенов.
- `POST /api/v1/auth/login` — проверяет login/password.
- `POST /api/v1/auth/refresh` — атомарно ротирует refresh token.
- `POST /api/v1/auth/logout` — отзывает переданный refresh token.
- `GET /api/v1/auth/me` — требует `Authorization: Bearer <accessToken>`.

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

Production wiring Gateway использует Postgres через `DATABASE_URL`. Миграции
лежат в `backend/migrations` и не запускаются приложением автоматически.
`MemoryStore` сохраняется только для unit-тестов.

Match token — отдельный credential с узким назначением. Access JWT нельзя
предъявлять напрямую игровому серверу: авторизованная очередь Gateway выдаёт
короткоживущий match ticket, привязанный к user ID, match ID и opponent ID.
