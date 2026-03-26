# Sessions, context, cookies

This chapter maps to:

- `include/bsrvcore/session/context.h`
- `include/bsrvcore/session/attribute.h`
- `include/bsrvcore/connection/server/server_set_cookie.h`
- parts of `include/bsrvcore/core/http_server.h` and `include/bsrvcore/connection/server/http_server_task.h`

## Context

`bsrvcore::Context` is a **thread-safe key/value bag**.
It is used for request data (or session data) that you want to share across code.

- `SetAttribute(key, value)`
- `GetAttribute(key)`
- `HasAttribute(key)`

Attributes are stored as `std::shared_ptr<bsrvcore::Attribute>`.

If you want a small typed attribute, `CloneableAttribute<T>` is an easy base class.

## Sessions

Server-side sessions are managed by `HttpServer`.
Each session is a `Context` behind the scenes.

- Set default session timeout: `HttpServer::SetDefaultSessionTimeout(ms)`
- Enable background cleanup: `HttpServer::SetSessionCleaner(true)`

Inside a handler:

```cpp
const std::string& session_id = task->GetSessionId();
auto session = task->GetSession();
if (session) {
  // session is a Context
}
```

`GetSessionId()` returns an existing session id from cookies, or creates one and adds a `Set-Cookie` header.

In other words: the first request can create a session, later requests can reuse it.

You can change timeout per session:

- `task->SetSessionTimeout(ms)`
- `server->SetSessionTimeout(session_id, ms)`

## Cookies

Read a request cookie:

```cpp
const std::string& v = task->GetCookie("name");
```

Add a response cookie:

```cpp
bsrvcore::ServerSetCookie c;
// configure fields on c
task->AddCookie(std::move(c));
```

Note: `AddCookie()` only adds a `Set-Cookie` entry to the response. The browser (or client) decides whether to store it.

Next: [Logging](logging.md).
