# Сетевой протокол MVP

## QUIC

- ALPN: `pvp-duel-v1`.
- Сервер: UDP `:4242` по умолчанию.
- Один двунаправленный reliable QUIC stream используется для аутентификации,
  `MatchReady` и будущих критичных событий.
- `PlayerInput` и `WorldSnapshot` передаются QUIC datagram без framing: граница
  datagram уже является границей одного protobuf payload.
- Серверный fixed tick работает независимо от входящих пакетов. Datagram лишь
  заменяет последний известный input; snapshot публикуется с частотой 30 Hz.

## Framing reliable stream

Каждое protobuf-сообщение имеет префикс из четырёх байт: размер payload как
беззнаковое 32-битное целое в network byte order (big endian). Максимальный
payload — 64 KiB.

Первое клиентское сообщение обязательно `ClientEnvelope.authenticate`. После
успешной аутентификации сервер отвечает `ServerEnvelope.ready`. Ошибка
протокола возвращается как `ServerEnvelope.error`, после чего stream
закрывается. Тиковый обмен после `ready` идёт через datagram. Финальный
snapshot дополнительно дублируется через reliable stream перед закрытием
матча, чтобы потеря datagram не скрыла результат.

Match Server принимает только подписанные Gateway match tickets и объединяет
два ticket с одинаковым `matchId` и взаимными opponent claims. C++ клиент
использует MsQuic и отправляет ввод с частотой 30 Hz из независимого fixed-step
цикла. Проверка self-signed сертификата отключена только в development-клиенте.
Это временный адаптер Фазы 2, не production-аутентификация.
