add_library(cppdesk_common STATIC
  src/config.cpp
  src/protocol.cpp
  src/crypto.cpp
  src/stream.cpp
  src/addr.cpp
  src/logging.cpp
  src/version.cpp
  src/error.cpp
  src/timer.cpp
  src/fs.cpp
  src/tls.cpp
)
target_include_directories(cppdesk_common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(cppdesk_common PUBLIC
  protobuf::libprotobuf
  nlohmann_json::nlohmann_json
  OpenSSL::SSL OpenSSL::Crypto
  spdlog::spdlog
  asio
)
if(ENABLE_ENCRYPTION)
  target_link_libraries(cppdesk_common PUBLIC sodium)
  target_compile_definitions(cppdesk_common PUBLIC HAS_LIBSODIUM)
endif()
