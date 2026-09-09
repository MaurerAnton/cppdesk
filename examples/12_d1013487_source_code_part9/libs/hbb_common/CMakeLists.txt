# libs/hbb_common/CMakeLists.txt — translation of libs/hbb_common/Cargo.toml
# [package] + [dependencies] relevant to this step.
#
# Dependency mapping (this step):
#   protobuf/git   -> system protobuf, wired HERE (generated message structs)
#   tokio/tokio-util/futures(+util)/bytes/socket2 -> POSIX + transport step (06)
#   zstd           -> system libzstd, wired HERE (compress.cpp)
#   sodiumoxide    -> system libsodium (crypto step)
#   confy/dirs-next/directories-next -> config step (05, path scheme first)
#   serde/serde_derive/confy/filetime/rand/log/env_logger/anyhow/mac_address
#                    -> mapped per module as its owner step lands
#   protobuf-codegen-pure [build-dep] -> protoc, run once per .proto change;
#                    output checked in under protos/gen (see step 07 README)

# Generated protobuf structs (parity with libs/hbb_common/build.rs, which runs
# protobuf-codegen-pure on protos/*.proto). Regenerate with:
#   protoc --proto_path=protos --cpp_out=protos/gen protos/*.proto
# Requires protoc >= 3.x; system libprotobuf at build time (offline-safe).
find_path(PROTOBUF_INCLUDE_DIR NAMES google/protobuf/message.h)
find_library(PROTOBUF_LIBRARY NAMES protobuf)
if(NOT PROTOBUF_INCLUDE_DIR OR NOT PROTOBUF_LIBRARY)
    message(FATAL_ERROR "libprotobuf dev files not found (need message.h + libprotobuf)")
endif()

add_library(hbb_common STATIC
    src/addr_mangle.cpp
    src/util.cpp
    src/bytes_codec.cpp
    src/compress.cpp
    src/toml.cpp
    src/config.cpp
    src/tcp.cpp
    src/udp.cpp
    src/fs.cpp
    src/crypto.cpp
    protos/gen/message.pb.cc
    protos/gen/rendezvous.pb.cc
)
target_include_directories(hbb_common PUBLIC include protos/gen)
target_compile_features(hbb_common PUBLIC cxx_std_20)

# compress.cpp needs <zstd.h> + libzstd (upstream: zstd crate; system here so
# snapshots stay offline-buildable — no FetchContent).
find_path(ZSTD_INCLUDE_DIR NAMES zstd.h)
find_library(ZSTD_LIBRARY NAMES zstd)
if(NOT ZSTD_INCLUDE_DIR OR NOT ZSTD_LIBRARY)
    message(FATAL_ERROR
        "libzstd dev files not found (need zstd.h + libzstd); "
        "upstream carries zstd via cargo, later steps vendor it via the system package manager")
endif()
target_include_directories(hbb_common PRIVATE ${ZSTD_INCLUDE_DIR})
target_include_directories(hbb_common PRIVATE ${PROTOBUF_INCLUDE_DIR})
target_link_libraries(hbb_common PRIVATE ${ZSTD_LIBRARY} ${PROTOBUF_LIBRARY})

# crypto.cpp needs <sodium.h> + libsodium (upstream: sodiumoxide crate).
find_path(SODIUM_INCLUDE_DIR NAMES sodium.h)
find_library(SODIUM_LIBRARY NAMES sodium)
if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
    message(FATAL_ERROR "libsodium dev files not found (need sodium.h + libsodium)")
endif()
target_include_directories(hbb_common PRIVATE ${SODIUM_INCLUDE_DIR})
target_link_libraries(hbb_common PRIVATE ${SODIUM_LIBRARY})
