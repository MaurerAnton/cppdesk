# Source files
set(CLIENT_SOURCES
  client/client.cpp
  client/io_loop.cpp
  client/helper.cpp
)

set(SERVER_SOURCES
  server/server.cpp
  server/services/display_service.cpp
  server/services/video_service.cpp
  server/services/audio_service.cpp
  server/services/input_service.cpp
  server/services/clipboard_service.cpp
  server/services/terminal_service.cpp
  server/services/printer_service.cpp
  server/connection.cpp
  server/video_qos.cpp
)

set(RENDEZVOUS_SOURCES
  rendezvous/rendezvous.cpp
)

set(IPC_SOURCES
  ipc/ipc.cpp
)

set(CLI_SOURCES
  cli/cli.cpp
)

set(UPDATER_SOURCES
  updater/updater.cpp
  updater/auto_update.cpp
)

set(CLIPBOARD_SOURCES
  clipboard/clipboard.cpp
)

set(LANG_SOURCES
  lang/locales.cpp
)

set(UI_SOURCES
  ui/legacy_ui.cpp
)

set(WHITEBOARD_SOURCES
  whiteboard/whiteboard.cpp
)

set(PRIVACY_SOURCES
  privacy/privacy_mode.cpp
)

set(AUTH_SOURCES
  auth/auth_2fa.cpp
)

set(NETWORK_SOURCES
  network/kcp_stream.cpp
  network/lan.cpp
)

set(CODEC_SOURCES
  codec/video_codec.cpp
  codec/audio_codec.cpp
)

set(FFI_SOURCES
  ffi/flutter_bridge.cpp
)

set(HBBS_HTTP_SOURCES
  hbbs_http/http_server.cpp
  hbbs_http/sync.cpp
  hbbs_http/api.cpp
)

# Platform-specific sources
if(PLATFORM_LINUX)
  list(APPEND PLATFORM_SOURCES
    platform/linux/platform_linux.cpp
    platform/linux/linux_desktop_manager.cpp
    platform/linux/gtk_sudo.cpp
    platform/linux/virtual_display_linux.cpp
    platform/linux/wayland.cpp
    platform/linux/uinput.cpp
    platform/linux/rdp_input.cpp
    platform/linux/dbus.cpp
  )
elseif(PLATFORM_WINDOWS)
  list(APPEND PLATFORM_SOURCES
    platform/windows/platform_windows.cpp
    platform/windows/win_device.cpp
    platform/windows/acl.cpp
    platform/windows/virtual_display_win.cpp
    platform/windows/portable_service.cpp
    platform/windows/printer_service.cpp
    platform/windows/terminal_helper.cpp
    platform/windows/rdp_input.cpp
  )
elseif(PLATFORM_MACOS)
  list(APPEND PLATFORM_SOURCES
    platform/macos/platform_macos.mm
    platform/macos/delegate.cpp
    platform/macos/virtual_display_mac.cpp
    platform/macos/terminal_helper.cpp
  )
elseif(PLATFORM_ANDROID)
  list(APPEND PLATFORM_SOURCES
    platform/android/platform_android.cpp
  )
endif()

# Main entry points
if(BUILD_CLIENT)
  add_executable(cppdesk_client
    main.cpp
    ${CLIENT_SOURCES}
    ${RENDEZVOUS_SOURCES}
    ${IPC_SOURCES}
    ${CLIPBOARD_SOURCES}
    ${LANG_SOURCES}
    ${UI_SOURCES}
    ${WHITEBOARD_SOURCES}
    ${PRIVACY_SOURCES}
    ${AUTH_SOURCES}
    ${NETWORK_SOURCES}
    ${CODEC_SOURCES}
    ${HBBS_HTTP_SOURCES}
    ${PLATFORM_SOURCES}
  )
  target_link_libraries(cppdesk_client PRIVATE
    cppdesk_common
    cppdesk_scrap
    cppdesk_enigo
    cppdesk_clipboard
    cppdesk_portable
    cppdesk_virtual_display
    cppdesk_remote_printer
    ${PLATFORM_LIBS}
  )
  if(ENABLE_ENCRYPTION)
    target_link_libraries(cppdesk_client PRIVATE sodium)
  endif()
endif()

if(BUILD_SERVER)
  add_executable(cppdesk_server
    server_main.cpp
    ${SERVER_SOURCES}
    ${RENDEZVOUS_SOURCES}
    ${IPC_SOURCES}
    ${LANG_SOURCES}
    ${PRIVACY_SOURCES}
    ${AUTH_SOURCES}
    ${NETWORK_SOURCES}
    ${CODEC_SOURCES}
    ${HBBS_HTTP_SOURCES}
    ${PLATFORM_SOURCES}
  )
  target_link_libraries(cppdesk_server PRIVATE
    cppdesk_common
    cppdesk_scrap
    cppdesk_enigo
    cppdesk_clipboard
    cppdesk_portable
    cppdesk_virtual_display
    cppdesk_remote_printer
    SQLite::SQLite3
    ${PLATFORM_LIBS}
  )
  if(ENABLE_ENCRYPTION)
    target_link_libraries(cppdesk_server PRIVATE sodium)
  endif()
endif()

if(BUILD_CLI)
  add_executable(cppdesk_cli
    cli_main.cpp
    ${CLI_SOURCES}
    ${RENDEZVOUS_SOURCES}
    ${LANG_SOURCES}
    ${PRIVACY_SOURCES}
  )
  target_link_libraries(cppdesk_cli PRIVATE
    cppdesk_common
    CLI11::CLI11
    ${PLATFORM_LIBS}
  )
endif()
