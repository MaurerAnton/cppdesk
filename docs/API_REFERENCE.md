# cppdesk API Reference

## Namespaces

### cppdesk::common

- `Config`
- `Resolution`
- `NatType`
- `ConnType`
- `PeerConfig`
- `VideoFrame`
- `AudioFrame`
- `MouseEvent`
- `KeyEvent`
- `CursorData`
- `ControlPermissions`
- `LoginResponse`
- `Stream`
- `MessageType`
- `ThumbnailData`

### cppdesk::common::crypto

- `KeyPair`
- `SignKeyPair`
- `generate_box_keypair`
- `generate_sign_keypair`
- `secretbox_encrypt`
- `secretbox_decrypt`
- `box_encrypt`
- `box_decrypt`
- `sign`
- `sign_verify`
- `sha256`
- `hmac_sha256`
- `random_bytes`
- `aes_gcm_encrypt`
- `aes_gcm_decrypt`
- `hash_password`
- `verify_password`
- `encode64`
- `decode64`
- `secure_compare`
- `derive_key`
- `generate_token`

### cppdesk::client

- `Client`
- `ClientInterface`
- `SessionManager`
- `FileManager`
- `LanDiscovery`
- `AudioDevice`
- `ScreenshotHelper`
- `ClientClipboardContext`
- `SessionInfo`
- `JobType`

### cppdesk::server

- `Server`
- `Service`
- `GenericService`
- `ConnInner`
- `Connection`
- `DisplayService`
- `VideoService`
- `AudioService`
- `InputService`
- `ClipboardService`
- `TerminalService`
- `PrinterService`
- `LoginFailureCheck`
- `ChildProcessTracker`
- `VirtualDisplayManager`

### cppdesk::rendezvous

- `RendezvousMediator`
- `RendezvousServer`
- `RelayServer`
- `RendezvousMessageType`

### cppdesk::platform

- `get_display_names`
- `current_resolution`
- `change_resolution`
- `get_cursor`
- `get_cursor_pos`
- `set_cursor_pos`
- `clip_cursor`
- `get_active_username`
- `is_installed`
- `is_wayland`
- `is_x11`
- `get_clipboard_text`
- `set_clipboard_text`
- `capture_screen`
- `simulate_mouse`
- `simulate_key`
- `simulate_text`
- `init`
- `cleanup`
- `WakeLock`
- `get_wakelock`

### scrap

- `TraitCapturer`
- `Decoder`
- `Encoder`
- `Recorder`
- `Camera`
- `ImageRgb`
- `ImageTexture`
- `Frame`
- `DisplayInfo`
- `CodecFormat`
- `ImageFormat`
- `create_capturer`
- `create_decoder`
- `create_encoder`
- `create_recorder`
- `create_camera`
- `convert_format`
- `convert_bgra_to_rgba`

### enigo

- `Enigo`
- `MouseControllable`
- `KeyboardControllable`
- `Key`
- `MouseButton`
- `MouseAxis`
- `DslParser`
- `DslToken`

### clipboard

- `PlatformClipboard`
- `ClipboardMonitor`
- `ClipboardSynchronizer`
- `CliprdrServiceContext`
- `CliprdrError`
- `ClipboardFile`
- `ProgressPercent`

### cppdesk::auth

- `TotpGenerator`
- `LoginAttemptTracker`
- `PasswordStrengthChecker`
- `TokenManager`

### cppdesk::network

- `KcpStream`
- `KcpReliableChannel`
- `LanDiscovery`

### cppdesk::codec::video

- `VideoEncoder`
- `VideoDecoder`
- `BitrateController`
- `CodecId`

### cppdesk::codec::audio

- `OpusEncoder`
- `OpusDecoder`
- `Resampler`
- `AudioMixer`
- `SilenceDetector`
- `EchoCanceller`
- `AudioDeviceManager`

### cppdesk::updater

- `UpdateChecker`
- `DownloadManager`
- `UpdateInstaller`
- `AutoUpdater`

### cppdesk::ffi

- `DartPortManager`
- `FfiSerializer`
- `CallbackManager`

### cppdesk::hbbs_http

- `HttpServer`
- `HbbsApi`
- `SyncManager`

### cppdesk::privacy

- `PrivacyModeManager`
- `ScreenBlanker`
- `PrivacyPolicy`

### cppdesk::whiteboard

- `Canvas`
- `DrawingTool`
- `StrokeEngine`
- `ShapeTool`

### cppdesk::port_forward

- `Tunnel`
- `TunnelManager`
- `TunnelConfig`

### cppdesk::ipc

- `IpcServer`
- `IpcClient`
- `IpcChannel`
- `IpcMessage`
