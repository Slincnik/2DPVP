# Matchmaking MVP

## HTTP flow

Все queue endpoints требуют access JWT:

```text
POST   /api/v1/queue/join
GET    /api/v1/queue/status
DELETE /api/v1/queue
```

`join` идемпотентен. Первый игрок получает `waiting`; после входа второго оба
получают через `status`:

```json
{
  "status": "matched",
  "matchId": "uuid",
  "opponentId": "uuid",
  "serverAddr": "localhost:4242",
  "matchTicket": "eyJ...",
  "ticketExpiresAt": "..."
}
```

Очередь пока находится в памяти Gateway, работает FIFO и удаляет ожидание через
5 минут. Polling `status` раз в 1–2 секунды достаточно для MVP. Клиент вызывает
`DELETE /api/v1/queue` после надёжного `MatchEnd`, чтобы удалить свой завершённый
`matched` result перед повторным `join`; запись соперника независима.

## Match tickets

Gateway подписывает отдельный JWT HS256 с audience Match Server. Ticket живёт
60 секунд и содержит `matchId`, `sub` (user ID), `opponentId`, `jti` и
стандартные временные claims. Для подписи используется отдельный
`MATCH_TICKET_SECRET`, не access-JWT secret.

Match Server проверяет подпись, срок, issuer/audience, соответствие `sub`
заявленному `playerId` и взаимную привязку обоих соперников. После подключения
пары `matchId` помечается использованным, поэтому replay ticket отклоняется.

Для production с несколькими инстансами очередь и consumed-ticket state нужно
перенести в Redis либо создавать room через внутренний RPC конкретного Match
Server. Текущая реализация рассчитана на один Gateway и один Match Server.
