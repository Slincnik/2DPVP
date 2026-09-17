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

Access и refresh token пока хранятся только в памяти процесса. Это намеренно:
перед persistent login нужно выбрать платформенное защищённое хранилище
(Windows Credential Manager, macOS Keychain, Secret Service на Linux), а не
записывать refresh token открытым текстом в конфигурационный файл.

`cpp-httplib` собирается с OpenSSL и поддерживает HTTPS. В production Gateway
должен публиковаться только через HTTPS. Проверка сертификатов Match Server в
MsQuic всё ещё отключена для локального self-signed сертификата и должна быть
включена перед внешним deployment.
