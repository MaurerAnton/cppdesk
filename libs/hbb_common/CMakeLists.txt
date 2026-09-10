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
#
# The top-level CMakeLists defines cppdesk_protobuf (CONFIG package when
# available, raw paths otherwise). The CONFIG path is required on
# protobuf >= 22 because Abseil's DSOs must be linked explicitly. The
# fallback keeps standalone builds of this subdirectory working.
if(NOT TARGET cppdesk_protobuf)
    find_package(Protobuf CONFIG QUIET)
    add_library(cppdesk_protobuf INTERFACE)
    if(TARGET protobuf::libprotobuf)
        target_link_libraries(cppdesk_protobuf INTERFACE protobuf::libprotobuf)
    else()
        find_path(PROTOBUF_INCLUDE_DIR NAMES google/protobuf/message.h)
        find_library(PROTOBUF_LIBRARY NAMES protobuf)
        if(NOT PROTOBUF_INCLUDE_DIR OR NOT PROTOBUF_LIBRARY)
            message(FATAL_ERROR "libprotobuf dev files not found (need message.h + libprotobuf)")
        endif()
        target_include_directories(cppdesk_protobuf INTERFACE ${PROTOBUF_INCLUDE_DIR})
        target_link_libraries(cppdesk_protobuf INTERFACE ${PROTOBUF_LIBRARY})
    endif()
endif()

# Generated protobuf structs (upstream build.rs parity): generate at build
# time with the local protoc. The checked-in protos/gen copies are pinned to
# the exact protoc that produced them (PROTOBUF_VERSION equality check in the
# generated headers) and fail on other distro versions — they remain only as
# a fallback for systems without protoc.
if(TARGET protobuf::protoc)
    set(PROTOC_CMD protobuf::protoc)
elseif(Protobuf_PROTOC_EXECUTABLE)
    set(PROTOC_CMD "${Protobuf_PROTOC_EXECUTABLE}")
else()
    find_program(PROTOC_CMD NAMES protoc)
endif()

set(PB_PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/protos")
set(PB_PROTOS "${PB_PROTO_DIR}/message.proto" "${PB_PROTO_DIR}/rendezvous.proto")
set(PB_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/protos_gen")
if(PROTOC_CMD)
    file(MAKE_DIRECTORY "${PB_GEN_DIR}")
    set(PB_SRCS "${PB_GEN_DIR}/message.pb.cc" "${PB_GEN_DIR}/rendezvous.pb.cc")
    add_custom_command(
        OUTPUT ${PB_SRCS}
        COMMAND ${PROTOC_CMD} --proto_path=${PB_PROTO_DIR}
                --cpp_out=${PB_GEN_DIR} ${PB_PROTOS}
        DEPENDS ${PB_PROTOS}
        COMMENT "Generating protobuf sources"
        VERBATIM)
    set(PB_INCLUDE_DIR "${PB_GEN_DIR}")
else()
    set(PB_SRCS protos/gen/message.pb.cc protos/gen/rendezvous.pb.cc)
    set(PB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/protos/gen")
    message(WARNING "protoc not found — using checked-in protos/gen (version-locked)")
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
    ${PB_SRCS}
)
target_include_directories(hbb_common PUBLIC include "${PB_INCLUDE_DIR}")
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
target_link_libraries(hbb_common PRIVATE ${ZSTD_LIBRARY} cppdesk_protobuf)

# crypto.cpp needs <sodium.h> + libsodium (upstream: sodiumoxide crate).
find_path(SODIUM_INCLUDE_DIR NAMES sodium.h)
find_library(SODIUM_LIBRARY NAMES sodium)
if(NOT SODIUM_INCLUDE_DIR OR NOT SODIUM_LIBRARY)
    message(FATAL_ERROR "libsodium dev files not found (need sodium.h + libsodium)")
endif()
target_include_directories(hbb_common PRIVATE ${SODIUM_INCLUDE_DIR})
target_link_libraries(hbb_common PRIVATE ${SODIUM_LIBRARY})
