# Сетевой протокол MVP

## QUIC

- ALPN: `pvp-duel-v2`. Клиенты `v1` намеренно не соединяются с action-based
  room и не смешиваются с новым ruleset.
- Сервер: UDP `:4242` по умолчанию.
- Один двунаправленный reliable QUIC stream используется для аутентификации,
  `MatchReady`, `MatchStart` и `MatchEnd`.
- `PlayerInput` и `WorldSnapshot` передаются QUIC datagram без framing: граница
  datagram уже является границей одного protobuf payload.
- Серверный fixed tick работает независимо от входящих пакетов. Datagram лишь
  заменяет последний известный movement input; snapshot публикуется с частотой
  30 Hz.

## Gameplay input v2

`PlayerInput.move_x/move_y` остаются held-state. Одноразовые действия передаются
как `pending_actions` (не более 8 команд):

- `ACTION_TYPE_DASH`;
- `ACTION_TYPE_LIGHT_ATTACK`.

Каждая команда имеет монотонный `sequence`. Клиент повторяет все pending-команды
в последующих datagram, пока authoritative `PlayerState.last_acked_action_sequence`
не подтвердит их обработку. Сервер сортирует и дедуплицирует команды, принимает
sequence только в ограниченном окне (64 значения после последнего ack) и
подтверждает также команды, отклонённые из-за cooldown, phase или неизвестного
type. Пакет несёт максимум 8 команд; input tick не может продвинуться более чем
на 120 относительно последнего принятого tick.

Поле `PlayerInput.attack = 4` сохранено только для wire compatibility, помечено
deprecated и симуляцией v2 не читается.

`PlayerState.action_state`, `action_started_server_tick` и
`action_ticks_remaining` являются authoritative timeline для клиента. Datagram
loss не требует восстановления visual event history: новый snapshot полностью
описывает текущую phase. Клиент предсказывает только held-перемещение. Dash
показывается после authoritative snapshot: текущий `MatchStart` ещё не передаёт
версионированные geometry/ruleset параметры, поэтому локальная dash prediction
до появления такого контракта намеренно отключена.

## Visual arena metadata

`MatchStart.arena_id` и `WorldSnapshot.arena_id` фиксируют визуальную тему на
весь матч. Сейчас сервер детерминированно выбирает `neon_rooftop` или
`ember_foundry`; обе темы используют одну и ту же authoritative прямоугольную
геометрию и отличаются только palette/background/decor/music metadata клиента.
`MatchStart.arena_id` совпадает с ID начального и последующих snapshots.

Поля добавлены wire-compatible, поэтому ALPN остаётся `pvp-duel-v2`. Клиент,
который получает пустой (от старого v2 сервера) или неизвестный ID, использует
контролируемый default visual fallback. Fallback не меняет collision/prediction
geometry. Препятствия и `ArenaDescriptor` в эту версию протокола не входят.

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
