# cppdesk Protocol Specification

## Wire Format

All messages use a binary framing protocol:
```
| Offset | Size | Field       | Description                    |
|--------|------|-------------|--------------------------------|
| 0      | 4    | magic       | 0x50454443 ("CPPD")            |
| 4      | 2    | version     | Protocol version (1)           |
| 6      | 2    | type        | Message type (see below)       |
| 8      | 4    | sequence    | Message sequence number        |
| 12     | 4    | length      | Payload length in bytes        |
| 16     | 8    | timestamp   | Unix timestamp in microseconds |
| 24     | N    | payload     | Message-specific data          |
```

## Message Types

### REGISTER_PEER (0)
Register a peer with the rendezvous server

### PUNCH_HOLE_REQUEST (1)
Request NAT hole punching to a peer

### PUNCH_HOLE_RESPONSE (2)
Response to hole punch request

### REQUEST_RELAY (3)
Request relay connection

### TEST_NAT (4)
Test NAT type (STUN-like)

### QUERY_ONLINE (5)
Query if a peer is online

### HEARTBEAT (6)
Keep-alive heartbeat

### REGISTER_PK (7)
Register public key

### PK_RESPONSE (8)
Public key registration response

### CONFIG_REQUEST (9)
Request server configuration

### CONFIG_RESPONSE (10)
Server configuration response

### SOFTWARE_UPDATE (11)
Software update notification

### ALIAS_UPDATE (12)
Update peer alias

### ADDRESS_BOOK (13)
Address book sync

### LOGIN (20)
Client login request

### LOGIN_RESPONSE (21)
Login response with permissions

### SWITCH_DISPLAY (22)
Switch to different display

### SWITCH_PERMISSION (23)
Change control permissions

### CLOSE_CONNECTION (24)
Close the connection

### VIDEO_FRAME (30)
Video frame data

### VIDEO_CODEC_CHANGE (31)
Change video codec

### VIDEO_QUALITY_CHANGE (32)
Change video quality

### AUDIO_FRAME (40)
Audio frame data

### AUDIO_CONFIG (41)
Audio configuration

### MOUSE_EVENT (50)
Mouse input event

### KEY_EVENT (51)
Keyboard input event

### CURSOR_DATA (52)
Cursor image data

### CURSOR_POSITION (53)
Cursor position update

### CURSOR_SHAPE (54)
Cursor shape change

### CLIPBOARD_TEXT (60)
Clipboard text data

### CLIPBOARD_FILE (61)
Clipboard file list

### CLIPBOARD_IMAGE (62)
Clipboard image data

### FILE_TRANSFER_REQUEST (70)
File transfer request

### FILE_TRANSFER_RESPONSE (71)
File transfer response

### FILE_CHUNK (72)
File data chunk

### FILE_DONE (73)
File transfer complete

### FILE_DIR (74)
Directory listing

### MISC (80)
Miscellaneous control message

### CHAT_MESSAGE (81)
Chat text message

### PRIVACY_MODE (82)
Privacy mode toggle

### PORT_FORWARD (83)
Port forwarding request

### WHITEBOARD (84)
Whiteboard data

### SUBSCRIBE_SERVICE (90)
Subscribe to a service

### UNSUBSCRIBE_SERVICE (91)
Unsubscribe from a service

### SERVICE_DATA (92)
Service-specific data

## Encryption

When encryption is enabled, each payload is encrypted with AES-256-GCM:
1. Generate random 12-byte nonce
2. Encrypt payload with derived session key
3. Prepend nonce to ciphertext
4. The 4-byte length field in the frame header refers to the encrypted size

## Key Exchange

1. Client generates X25519 ephemeral keypair
2. Client sends public key + Ed25519 signature of (ID + PK)
3. Server verifies signature, generates own X25519 keypair
4. Server sends public key
5. Both compute shared secret via X25519 ECDH
6. Session key = HKDF-SHA256(shared_secret, salt="cppdesk-v1")

## NAT Traversal

1. Client A connects to rendezvous server
2. Client A requests hole punch to Client B
3. Rendezvous server exchanges UDP endpoint info
4. Both clients send UDP packets to each other simultaneously
5. If UDP succeeds, direct connection established
6. If UDP fails after 3 attempts, fall back to TCP relay
7. Force relay option skips UDP hole punching entirely
