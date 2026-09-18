# ADR-0003: модульная архитектура клиента и action-based gameplay

- **Статус:** Accepted
- **Дата:** 2026-09-18
- **Решение принимает:** владелец проекта
- **Связанные документы:**
  - [ADR-0001: клиентский 2D-фреймворк](0001-client-framework.md)
  - [ADR-0002: QUIC-библиотека C++ клиента](0002-cpp-quic-library.md)
  - [Правила MVP-дуэли](../mvp-duel-rules.md)
  - [Сетевой протокол MVP](../network-protocol.md)
  - [Roadmap](../roadmap.md)

## Контекст

Рабочий MVP уже позволяет зарегистрироваться, найти матч, подключиться по QUIC
и сыграть авторитетную 1v1-дуэль. Он намеренно оптимизирован под вертикальный
срез, а не под развитие игры.

Следующие пользовательские запросы меняют клиент и игровой протокол в разных
направлениях:

1. профиль пользователя и явный logout;
2. несколько арен и полноценные анимации персонажей;
3. dash;
4. более динамичная модель атак вместо бесконечного удержания `Space`;
5. переназначение клавиш.

В текущем состоянии эти изменения будут дорого вносить:

- `client/app/main.cpp` одновременно управляет окнами UI, auth, очередью,
  асинхронными HTTP/QUIC-операциями, игровым циклом, prediction, звуком и
  raylib-рендером;
- ввод снимается напрямую через raylib и сразу превращается в сетевой
  `PlayerInput`;
- `PlayerInput.attack` — один boolean для удерживаемой атаки; он не выражает
  одноразовый dash/удар и может потерять edge-нажатие в QUIC datagram;
- authoritative `PlayerState` не содержит состояние действия и его таймлайн,
  поэтому анимация выводится из косвенных признаков (например, изменения HP);
- правила Room представлены фиксированными константами и единственным
  attack-cooldown;
- у арены нет отдельной модели: сейчас есть только границы симуляции и
  примитивный рендер.

В то же время проект остаётся небольшой 1v1-игрой. Внедрение ECS, универсального
ability-фреймворка, отдельного игрового движка или микросервисов ради этого
инкремента создаст больше сложности, чем пользы.

## Требования

### Функциональные

- Показать отдельные экраны Main Menu, Profile, Queue, Match, Result и
  Settings; сохранить Login и восстановление auth-сессии.
- Дать пользователю logout и отображение профиля.
- Поддержать переназначаемые действия ввода без изменения игровой логики.
- Добавить dash как авторитетное действие с cooldown и предсказуемым
  клиентским UX.
- Заменить held-space combat на явные дискретные actions; первый набор —
  `LightAttack` и `Dash`.
- Дать анимации источник истины, синхронизированный с серверной симуляцией.
- Поддержать несколько арен: сначала разные визуальные темы при одинаковой
  геометрии, затем препятствия без расхождения prediction и сервера.
- Не менять принцип: сервер решает движение, коллизии, урон, cooldown и
  результат; клиент отправляет только input/action intent.

### Нефункциональные

- Render/UI thread не блокируется сетью, файловым I/O или secure storage.
- Все состояния, влияющие на результат матча, детерминированы на сервере и
  тестируются без QUIC.
- Новые поля protobuf добавляются wire-compatible способом; существующие
  числовые поля/enum значения не переиспользуются.
- Refresh token остаётся исключительно в protected credential storage.
- Обычные пользовательские настройки не содержат credentials и сохраняются
  атомарно.
- Архитектура должна быть пригодной для reconnect и новых actions в P2, но не
  реализует их заранее.

## Решение

### 1. Client shell отделяется от платформы, feature-слоёв и gameplay

Raylib остаётся тонким адаптером окна, ввода, аудио и 2D-отрисовки согласно
ADR-0001. Никакая доменная игровая логика не должна зависеть от raylib types
или `KEY_*` constants.

Целевая структура (возможны небольшие изменения имён, но не обязанностей):

```text
client/
  app/
    application.{h,cpp}          # lifecycle, переходы экранов, main loop
    screen.h
    screens/
      login_screen.*
      main_menu_screen.*
      profile_screen.*
      queue_screen.*
      match_screen.*
      result_screen.*
      settings_screen.*
  platform/
    raylib_input.*               # raylib key/mouse -> platform event
    raylib_renderer.*            # drawing, textures, sound
    settings_store.*             # обычный settings JSON, atomic write
  features/
    auth/                        # SessionManager и SecureStorage
    profile/                     # Profile service/model/UI adapter
    matchmaking/                 # queue use-case, не алгоритм MMR
  game/
    input/                       # Action, Binding, InputFrame, binding validation
    match/                       # MatchController и MatchModel
    prediction/                  # prediction/reconciliation движения и dash
    presentation/                # WorldView, animation state, VFX/SFX triggers
    arena/                       # ArenaCatalog, ArenaDescriptor, ArenaScene
  net/
    quic_client.*                # транспорт и потоки сообщений
  protocol/
    match_protocol_adapter.*     # protobuf <-> client domain model
```

Это логическая структура, а не требование к одному большому коммите. Код
следует переносить маленькими компилируемыми шагами. До удаления старого кода
поведение MVP не должно меняться.

#### Границы ответственности

| Слой | Владеет | Не делает |
|---|---|---|
| `app` | lifecycle, screen routing, запуск async use-cases | правилами боя, protobuf, raylib keycodes |
| `platform` | raylib, файлы настроек, текстуры/звук | сетевыми вызовами и авторитетной симуляцией |
| `features` | auth/profile/queue use-cases и их view-model | render loop и room rules |
| `game/input` | actions, bindings, edge/held input | HTTP, QUIC, raylib |
| `game/match` | orchestration input → network → model/presentation | рисованием UI и прямыми MsQuic callback |
| `game/presentation` | отображаемое состояние, animation/VFX/SFX | выбор победителя и нанесение урона |
| `net` | QUIC transport, thread-safe очередь входящих DTO | UI и game-rule decisions |
| `protocol` | маппинг generated protobuf в domain structs | бизнес-логику |

`main.cpp` после миграции только создаёт `Application`, платформенные адаптеры
и запускает его. Он не должен содержать gameplay state machine.

### 2. Screen state и use-cases

`Application` владеет одним текущим `Screen` и явно описывает допустимые
переходы:

```text
SessionRestore -> Login | MainMenu
Login          -> MainMenu
MainMenu       -> Profile | Queue | Settings | Login(logout)
Profile        -> MainMenu
Queue          -> MainMenu(cancel) | Match(connect)
Match          -> Result | MainMenu(network failure)
Result         -> MainMenu | Queue(rematch)
Settings       -> MainMenu
```

Каждый screen хранит только UI/view state. Он вызывает use-case (`SessionManager`,
`ProfileService`, `QueueService`, `MatchController`) и получает завершённый
result через очередь main thread. Screen не захватывает ссылки на уничтожаемые
UI-объекты в background lambda.

Существующий `SessionManager` является единственной точкой владения
`AuthSession`. Новый HTTP feature-код не принимает и не хранит raw access или
refresh token. Он вызывает authenticated executor/use-case SessionManager,
который выполняет один refresh и retry при `401`.

### 3. Action-based input и bindings

Ввод выражается доменными действиями, а не raylib keycodes:

```cpp
enum class Action {
    MoveUp,
    MoveDown,
    MoveLeft,
    MoveRight,
    Dash,
    LightAttack,
};

struct InputFrame {
    std::int8_t moveX;
    std::int8_t moveY;
    std::vector<Action> pressed; // edge только этого локального кадра
};
```

`RaylibInputAdapter` сопоставляет физические keyboard/mouse events с
платформенно-независимым `InputCode`; `InputBindings` сопоставляет `InputCode`
с `Action`; `InputSampler` формирует `InputFrame` для simulation tick.

Требования к bindings:

- настройки имеют defaults и version/schema version;
- нельзя назначить один и тот же `InputCode` двум взаимоисключающим actions;
- конфликт показывается до сохранения;
- reset-to-default доступен всегда;
- settings пишутся во временный файл и атомарно заменяют старый;
- refresh token, access token и другие credentials никогда не попадают в этот
  JSON;
- внутренние значения не зависят от raylib `KeyboardKey`, чтобы позже можно
  было добавить controller/gamepad без изменения gameplay.

Перемещение остаётся held-state. `Dash` и `LightAttack` являются edge actions.
Переход от render frames к simulation ticks должен не терять edge между тиками:
нажатие накапливается до потребления ближайшим fixed simulation tick.

### 4. Надёжная доставка одноразовых actions поверх datagram

QUIC datagram может быть потерян, продублирован или прийти не по порядку.
Поэтому dash/attack нельзя моделировать как простой boolean в одном пакете.

Протокол развивается добавлением новых protobuf полей (номера ниже
иллюстративны; фактические номера фиксируются до merge и никогда не меняются):

```proto
enum ActionType {
  ACTION_TYPE_UNSPECIFIED = 0;
  ACTION_TYPE_DASH = 1;
  ACTION_TYPE_LIGHT_ATTACK = 2;
}

message ActionCommand {
  uint32 sequence = 1;
  ActionType type = 2;
}

message PlayerInput {
  uint32 tick = 1;
  sint32 move_x = 2;
  sint32 move_y = 3;
  bool attack = 4 [deprecated = true];
  repeated ActionCommand pending_actions = 5; // max 8
}

message PlayerState {
  // Existing fields 1..7 remain unchanged.
  uint32 last_acked_action_sequence = 8;
  PlayerActionState action_state = 9;
  uint32 action_started_server_tick = 10;
  uint32 action_ticks_remaining = 11;
}
```

Также добавляется `PlayerActionState` как минимум с `IDLE`, `MOVE`, `DASH`,
`LIGHT_ATTACK_WINDUP`, `LIGHT_ATTACK_ACTIVE`, `LIGHT_ATTACK_RECOVERY`, `HIT`,
`KO`. `IDLE` должен быть нулевым enum value.

Алгоритм:

1. При edge action клиент увеличивает локальный `actionSequence` и помещает
   `ActionCommand` в небольшой ordered pending queue.
2. Каждый исходящий `PlayerInput` несёт все pending commands с sequence больше
   последнего ack из authoritative snapshot. Размер queue ограничен 8.
3. Room принимает command только один раз: sequence должен быть больше
   последнего обработанного sequence данного игрока.
4. В snapshot сервер публикует `last_acked_action_sequence` независимо от
   успешности ability. Rejected command считается обработанным и получает
   явную причины в telemetry/log при необходимости.
5. После ack клиент удаляет команды из queue.

Так command будет повторён в следующих datagrams до доставки, но никогда не
исполнится дважды. События не должны дублироваться через reliable stream для
самой симуляции. Reliable stream остаётся для `MatchStart`, `MatchEnd`,
protocol errors и будущих редких критичных событий.

Сервер обязан ограничить число pending commands, валидировать action enum,
отклонять sequence, не укладывающийся в разумное окно, и не принимать слишком
далёкие future input ticks. Более строгий input rate limit относится к P2,
но формат не должен мешать ему.

### 5. Authoritative combat ruleset

`Room` остаётся единственным владельцем симуляции и вызывается только его tick
loop. Новое состояние не превращается в generic spell framework.

Вместо глобального набора констант Room получает immutable `Ruleset` при
создании. Для первого ruleset достаточно структуры, подобной:

```go
type ActionDefinition struct {
    CooldownTicks uint32
    WindupTicks   uint32
    ActiveTicks   uint32
    RecoveryTicks uint32
    Damage        int32
    DashDistance  int32 // используется только для Dash
}

type Ruleset struct {
    TickRate          uint32
    MovementPerTick   int32
    Arena             ArenaDefinition
    Dash              ActionDefinition
    LightAttack       ActionDefinition
}
```

Точные поля выбираются по правилам дизайна, но обязательны следующие
инварианты:

- Room проверяет доступность action, cooldown, HP, phase и geometry;
- dash, position clamp и столкновения вычисляет сервер;
- attack hitbox активен только в active phase;
- damage одного тика применяется одновременно;
- все длительности выражены целым числом server ticks;
- snapshot полностью описывает текущую action phase, достаточную для позднего
  клиента и восстановления presentation после packet loss;
- правила не изменяются в середине конкретного матча.

`Room` не должен принимать `damage`, direction или новую position от клиента.
Клиент передаёт только move axis и action intent.

Первый динамичный combat design: один `LightAttack` по нажатию с
windup/active/recovery, плюс dash с cooldown. Удержание `Space` как
repeat-attack удаляется после миграции протокола и тестов. Новые атаки добавляют
конкретную `ActionType` и definition, а не абстрактный пользовательский скрипт.

### 6. Prediction и presentation

`MatchModel` содержит authoritative snapshot, локальный player ID, pending
input/actions и presentation-relevant state. `MatchController` — единственное
место, которое:

1. потребляет `InputFrame` на fixed tick;
2. создаёт network input;
3. применяет разрешённую локальную prediction;
4. получает DTO от `QuicClient`;
5. применяет authoritative snapshot и reconciliation;
6. уведомляет `MatchPresentation`.

Локально предсказываются только движение и dash, если клиент обладает той же
collision geometry и ruleset parameters, что и сервер. Урон, успешность атаки,
HP, cooldown и победитель не предсказываются как truth. До полной реализации
reconciliation dash может быть сначала instant visual anticipation с
authoritative movement из snapshot; нельзя выдавать непроверенный hit за факт.

`MatchPresentation` преобразует authoritative action state и server tick в
`AnimationState`. Он отвечает за sprite timeline, facing, blending,
последовательности hit/VFX/SFX и interpolation opponent. Он не читает входящие
QUIC сообщения и не изменяет `MatchModel`.

Минимальные animation states: `Idle`, `Run`, `Dash`, `AttackWindup`,
`AttackActive`, `AttackRecovery`, `Hit`, `KO`. Animation assets могут быть
табличными JSON/atlas-описаниями, но игровые hitbox/timing не читаются из
клиентских animation файлов: источник истины — Room ruleset.

### 7. Арены

Вводится стабильный `ArenaId` в `MatchStart` и `WorldSnapshot`/match metadata.
`ArenaCatalog` на клиенте связывает ID с background, декорацией, palette,
музыкой и visual bounds.

Этапы:

1. **Visual arenas:** несколько `ArenaId`, одна authoritative прямоугольная
   геометрия. Это безопасно для текущей prediction.
2. **Gameplay arenas:** `MatchStart` содержит versioned `ArenaDescriptor`:
   границы, spawn points и collision primitives. Server Room владеет
   authoritative descriptor, клиент использует полученную копию только для
   prediction/render.

Не разрешается иметь отдельные вручную продублированные препятствия в Go и C++:
это создаст desync. Если descriptor станет сложным, он должен быть protobuf
контрактом, а не двумя JSON-файлами с похожим содержимым.

### 8. Profile и backend границы

Профиль не требует MMR matchmaking. Следующий небольшой backend increment:

- запись terminal match result по `match_id` идемпотентно;
- базовая статистика пользователя;
- `GET /api/v1/profile`;
- client `ProfileService` и Profile screen.

Match Server сообщает результат только внутреннему Gateway endpoint/RPC;
игровой клиент не присылает результат. ELO/MMR и подбор по рейтингу можно
добавить позднее поверх той же истории матчей. Текущая local in-memory queue
остаётся способом тестировать gameplay до принятия дизайна боя.

## План миграции

Каждый этап должен оставлять repository в собираемом и тестируемом состоянии.

1. **Зафиксировать этот ADR и создать compile-only client skeleton.**
   Вынести `Application`, `Screen` и `platform` interfaces без изменения
   поведения. Добавить tests для screen transitions, где это не требует raylib.
2. **Перенести существующие screens/use-cases.**
   Login, session restore, logout, main menu, queue и result перестают жить в
   `main.cpp`. Сохраняется текущий protocol и бой.
3. **Input/settings.**
   Добавить domain `Action`, defaults, validation, atomic settings storage и
   Settings screen. Временно сопоставить actions со старым input format.
4. **Protocol and Room action migration.**
   Обновить `.proto`, regenerate Go/C++, добавить action acknowledgement,
   заменить boolean attack в Room на commands. Добавить deterministic room
   tests для loss/duplicate/reordering-command model, dash и phase/cooldown.
5. **Match controller and presentation.**
   Скрыть generated protobuf за adapter, вынести fixed tick orchestration из
   screen, внедрить animation state machine и effects.
6. **Visual arena catalog.**
   Добавить минимум две темы при одной geometry. Затем отдельно принять решение
   о `ArenaDescriptor` и препятствиях.
7. **Profile/results vertical slice.**
   Добавить DB/API/client profile, не меняя matchmaking policy.

Шаги 3–5 могут иметь отдельные ADR/дизайн-документы, если combat design (точные
frames, dash direction, stamina, cancel windows) ещё не согласован.

## Критерии готовности для агента-исполнителя

Агент не должен начинать все пункты одновременно. Для каждого выданного
инкремента нужны acceptance criteria.

### Архитектурный каркас

- `main.cpp` не содержит screen-specific UI, HTTP queue polling или gameplay
  simulation state.
- Существующий сценарий register/login → queue → match → result → rematch
  сохраняется.
- C++ build и существующие CTest проходят.

### Settings/input

- Default bindings эквивалентны старым (`WASD`, `Space`; dash имеет явный
  временный default).
- Смена binding применяется в следующем input frame и переживает restart.
- Invalid/duplicate binding не уничтожает последнее валидное settings file.
- Settings file не содержит token/password.

### Dash/actions

- Серверные unit-тесты проверяют: cooldown, невозможность dash в запрещённой
  phase, clamp/geometry, duplicate sequence, потерянный первый datagram и
  simultaneous interaction.
- Клиентский unit-тест проверяет, что command повторяется до ack и прекращает
  повторяться после ack.
- Два одинаковых commands не дают два dash/удара.
- Existing clients/protocol version не смешиваются молча: при несовместимом
  rollout используется новый ALPN/version либо явная protocol error.

### Presentation/arenas

- Анимация action начинается от server tick/state и корректно восстанавливается
  после пропущенного snapshot.
- VFX/SFX не изменяют game state и не требуют reliable delivery для
  корректности матча.
- Arena ID unknown to client приводит к controlled error/fallback, а не к
  undefined geometry.

### Profile

- API не принимает match result от клиента.
- Profile request использует SessionManager, включая refresh/retry.
- Unauthorized/expired session возвращает пользователя к Login без token leaks.

## Последствия

### Положительные

- Новые экраны, bindings, actions и visual content локализованы в собственных
  слоях.
- Gameplay UX развивается без ослабления authoritative модели.
- Datagram loss не делает dash/attack случайно ненадёжными.
- Анимации получают стабильный контракт, а не набор визуальных эвристик.
- Можно откладывать Redis/MMR и масштабирование, пока игра не доказала
  потребность в них.

### Отрицательные

- Увеличится количество маленьких C++ файлов и interfaces.
- Proto/Go/C++ должны меняться в одном коммите или через версионированный
  rollout; это требует дисциплины code generation.
- Action acknowledgement и prediction усложнят тесты.
- Полноценные gameplay arenas потребуют отдельного collision descriptor.

## Альтернативы

### Оставить код в `main.cpp`

Отклонено. Каждая новая feature будет менять один shared файл и смешивать UI,
transport и gameplay, что делает regressions неизбежными.

### Сразу внедрить ECS

Отклонено. Для двух бойцов ECS не решает текущие проблемы границ, protocol и
input, зато добавляет framework и data lifetime complexity.

### Оставить boolean для всех действий

Отклонено. Edge actions теряются в lossy datagram и не имеют естественной
семантики cooldown/ack/deduplication.

### Передавать attack/dash по reliable stream

Отклонено. Это создаёт зависимость gameplay responsiveness от stream ordering
и head-of-line delay. Повторяемые sequence commands поверх datagram надёжны
достаточно для intent, а серверный tick остаётся независимым.

### Хранить geometry арены двумя независимыми файлами на клиенте и сервере

Отклонено. Даже небольшой дрейф collision data ломает local prediction;
версионированный server-owned descriptor устраняет источник расхождения.

### Делать Redis/MMR matchmaking до client refactor

Отклонено. Это не улучшит основной пользовательский опыт боя и добавит
операционную сложность до фиксации combat design.

## Риски и меры

| Риск | Мера |
|---|---|
| Refactor меняет работающий MVP | маленькие этапы, snapshot/room regression tests, no-behaviour-change first commit |
| Action datagram flood | лимит pending queue, dedup sequence, будущий server rate limit |
| Prediction dash расходится с сервером | одна geometry/ruleset версия, reconciliation, сначала visual anticipation при необходимости |
| Несовместимость старого клиента | ALPN/protocol versioning и явное отклонение |
| Animation и ruleset drift | серверный tick/action state — источник таймлайна; art не определяет hitbox |
| Settings corruption | schema validation, temp file + atomic rename, defaults fallback |
| Token случайно попадает в settings | strict separation SettingsStore и SecureStorage, review/test scanning |

## Handoff для агента

Перед началом работы агент должен:

1. Прочитать этот ADR, ADR-0001/0002, `docs/network-protocol.md`,
   `docs/mvp-duel-rules.md`, текущие `client/app/main.cpp`, `client/net` и
   `backend/internal/room`.
2. Выбрать **один** этап миграции и сформулировать его без расширения scope.
3. Не менять ruleset/protobuf в задаче по чистому UI extraction.
4. Не добавлять ECS, generic ability scripting, Redis или MMR без отдельного
   принятого решения.
5. Для изменений протокола обновить source `.proto`, обе generated версии и
   tests; generated code не редактировать вручную.
6. Перед сдачей выполнить `make backend-test`, CMake build клиента и CTest;
   указать выполненные acceptance criteria и остаточные риски.

## Статус перехода

ADR становится **Accepted** после подтверждения владельцем. После этого
первым implementation task является этап 1: выделение client application/screen
skeleton без изменения network protocol и gameplay поведения.
