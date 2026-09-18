# C++ client flow

Клиент теперь управляет полным переходом между транспортами:

```text
Login/Register
    → access + refresh token in memory
    → POST /queue/join
    → GET /queue/status every second
    → matched {userId, serverAddr, matchTicket}
    → QUIC authentication
    → fixed-tick game loop
```

HTTP запросы и QUIC handshake выполняются через `std::async`, поэтому render
loop raylib не блокируется. Gateway URL по умолчанию —
`http://localhost:8080`; его можно изменить переменной `GATEWAY_URL`.

Access token хранится только в памяти. Refresh token сохраняется только в
защищённом системном credential store: Secret Service/libsecret на Linux,
Windows Credential Manager на Windows и macOS Keychain на macOS. Токен никогда не записывается в конфиг или иной
обычный файл. Если secure storage недоступен, клиент продолжает работу только
с сессией в памяти и явно предупреждает, что она не будет восстановлена.

При запуске клиент асинхронно читает refresh token и немедленно ротирует его
через `/auth/refresh`. Каждый авторизованный queue-запрос при единственном
`401` выполняет refresh и повторяет исходный запрос один раз. Logout отзывает
refresh token на Gateway и всегда удаляет локальную credential, даже если
Gateway недоступен.

Для Linux persistent login требует runtime и development-пакет `libsecret-1`
(например, `libsecret-1-0 libsecret-1-dev` в Debian/Ubuntu). CMake включает
Secret Service автоматически, когда `pkg-config` обнаруживает пакет.

`cpp-httplib` собирается с OpenSSL и поддерживает HTTPS. В production Gateway
должен публиковаться только через HTTPS. Проверка сертификатов Match Server в
MsQuic всё ещё отключена для локального self-signed сертификата и должна быть
включена перед внешним deployment.
