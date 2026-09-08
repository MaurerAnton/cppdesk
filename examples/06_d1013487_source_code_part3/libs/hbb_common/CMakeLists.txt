# libs/hbb_common/CMakeLists.txt — translation of libs/hbb_common/Cargo.toml
# [package] + [dependencies] relevant to this step.
#
# Dependency mapping (this step):
#   (none external for the core: std-only + system libzstd, see below)
# Deferred to later steps, with the C++ plan:
#   protobuf/git   -> system protobuf once message structs are ported (protos step)
#   tokio/tokio-util/futures(+util)/bytes/socket2 -> POSIX + transport step (06)
#   zstd           -> system libzstd, wired HERE (compress.cpp)
#   sodiumoxide    -> system libsodium (crypto step)
#   confy/dirs-next/directories-next -> config step (05, path scheme first)
#   serde/serde_derive/confy/filetime/rand/log/env_logger/anyhow/mac_address
#                    -> mapped per module as its owner step lands
#   protobuf-codegen-pure [build-dep] -> protoc at protos step

add_library(hbb_common STATIC
    src/addr_mangle.cpp
    src/util.cpp
    src/bytes_codec.cpp
    src/compress.cpp
    src/toml.cpp
    src/config.cpp
    src/tcp.cpp
    src/udp.cpp
)
target_include_directories(hbb_common PUBLIC include)
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
target_link_libraries(hbb_common PRIVATE ${ZSTD_LIBRARY})
