# cppdesk Architecture

## Overview

cppdesk is a C++20 translation of RustDesk, organized into libraries and modules:

```
include/cppdesk/    Public headers
  common/           Config, crypto, protocol, stream, TLS
  client/           Client connection, IO loop, helper, screenshot
  server/           Server, services (audio/video/input/clipboard/terminal)
  rendezvous/       Rendezvous mediator, hbbs server, hbbr relay
  platform/         Platform abstractions (Windows/Linux/macOS/Android/iOS)
  ipc/              Inter-process communication
  plugin/           Plugin framework
  ui/               Legacy UI
  cli/              Command-line interface
  lang/             Localization (62 languages)
  updater/          Auto-update
  clipboard/        Clipboard sync
  privacy/          Privacy mode
  whiteboard/       Whiteboard collaboration
  auth/             Authentication (2FA, TOTP)
  network/          Networking (KCP, LAN)
  codec/            Video/audio codecs
  ffi/              Flutter FFI bridge
  hbbs_http/        HBBS HTTP server/API

libs/
  common/           Core library
  scrap/            Screen capture (DXGI, X11, Quartz)
  enigo/            Input simulation
  clipboard/        Clipboard management
  portable/         Portable app support
  virtual_display/  Virtual display driver
  remote_printer/   Remote printer

src/                Implementation files
flutter/            Flutter UI (Dart)
proto/              Protocol Buffers
tests/              Unit tests
```

## Data Flow

1. Client connects to rendezvous server to locate peer
2. NAT traversal via UDP hole punching or relay
3. TCP connection established with E2E encryption
4. Service negotiation: video, audio, clipboard, input
5. Data exchange with adaptive quality

## Key Protocols

- Rendezvous: custom binary protocol over TCP/UDP
- Peer connection: length-prefixed messages with encryption
- Video: raw/H264/H265/VP8/VP9/AV1 with adaptive bitrate
- Audio: Opus codec with silence detection
- Clipboard: text, file lists, images with deduplication
