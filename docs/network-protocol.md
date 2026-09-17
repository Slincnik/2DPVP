# Сетевой протокол MVP

## QUIC

- ALPN: `pvp-duel-v1`.
- Сервер: UDP `:4242` по умолчанию.
- Один двунаправленный reliable QUIC stream используется для аутентификации,
  `MatchReady`, `MatchStart` и `MatchEnd`.
- `PlayerInput` и `WorldSnapshot` передаются QUIC datagram без framing: граница
  datagram уже является границей одного protobuf payload.
- Серверный fixed tick работает независимо от входящих пакетов. Datagram лишь
  заменяет последний известный input; snapshot публикуется с частотой 30 Hz.

## Framing reliable stream

Каждое protobuf-сообщение имеет префикс из четырёх байт: размер payload как
беззнаковое 32-битное целое в network byte order (big endian). Максимальный
payload — 64 KiB.

Первое клиентское сообщение обязательно `ClientEnvelope.authenticate`. После
успешной аутентификации сервер отвечает `ServerEnvelope.ready`, затем
`ServerEnvelope.match_start` с исходным состоянием и параметрами времени.
Авторитетный room clock запускается только после успешной записи `MatchStart`
в stream обоих игроков, поэтому transport setup не расходует countdown.
Ошибка протокола возвращается как `ServerEnvelope.error`, после чего stream
закрывается. Тиковые `WorldSnapshot` после `ready` идут через datagram.
Терминальный `ServerEnvelope.match_end` содержит итоговый snapshot, победителя
и причину независимо от lossy snapshot-канала. После его отправки сервер
оставляет соединение открытым на короткий grace period и закрывает его штатно.

Match Server принимает только подписанные Gateway match tickets и объединяет
два ticket с одинаковым `matchId` и взаимными opponent claims. C++ клиент
использует MsQuic и отправляет ввод с частотой 30 Hz из независимого fixed-step
цикла. Проверка self-signed сертификата отключена только в development-клиенте.
Это временный адаптер Фазы 2, не production-аутентификация.
