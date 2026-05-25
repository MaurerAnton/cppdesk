# cppdesk API Reference

## Common Library

### Config
```cpp
auto& cfg = cppdesk::common::Config::instance();
cfg.set_password("secret");
cfg.set_option("key", "value");
auto id = cfg.get_id();
```

### Crypto
```cpp
auto hash = cppdesk::common::crypto::sha256("data");
auto kp = cppdesk::common::crypto::generate_box_keypair();
auto enc = cppdesk::common::crypto::box_encrypt(data, len, nonce, their_pk, my_sk);
```

### Protocol
```cpp
cppdesk::common::MouseEvent ev;
ev.x = 100; ev.y = 200;
ev.mask = MouseEvent::BUTTON_LEFT | MouseEvent::TYPE_DOWN;

cppdesk::common::VideoFrame frame;
frame.width = 1920; frame.height = 1080;
frame.codec = 1; // H264
```

## Client
```cpp
cppdesk::client::Client client;
client.start("peer_id", "key", "token", cppdesk::common::ConnType::DEFAULT_CONN, iface);
client.send_mouse_event(ev);
client.send_clipboard_text("hello");
```

## Server
```cpp
auto srv = cppdesk::server::Server::create();
auto conn = std::make_shared<cppdesk::server::ConnInner>(id, stream, addr);
srv->add_connection(conn);
```

## Platform
```cpp
cppdesk::platform::init();
auto displays = cppdesk::platform::get_display_names();
auto pos = cppdesk::platform::get_cursor_pos();
cppdesk::platform::simulate_key(65, true); // Press 'A'
```
