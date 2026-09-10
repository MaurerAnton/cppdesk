#!/usr/bin/env python3
"""Apply the protobuf portability fixes to the frozen snapshots (07-13).

Two coupled problems on modern distros:

1. protobuf >= 22 links Abseil; a raw find_library() link drops the
   transitive DSOs and fails with "DSO missing from command line".
   Fixed by linking the CONFIG package through a shared
   cppdesk_protobuf interface target.

2. The checked-in protos/gen sources are pinned to the exact protoc that
   produced them (the generated header asserts PROTOBUF_VERSION equality),
   so they only build against that one protobuf release. Generate at build
   time with the local protoc instead (upstream build.rs parity); the
   checked-in copy stays as a fallback when protoc is missing.

Run from the repo root:  python3 tools/fix_snapshot_protobuf.py
"""
import glob
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SUB_OLD_FIND = """find_path(PROTOBUF_INCLUDE_DIR NAMES google/protobuf/message.h)
find_library(PROTOBUF_LIBRARY NAMES protobuf)
if(NOT PROTOBUF_INCLUDE_DIR OR NOT PROTOBUF_LIBRARY)
    message(FATAL_ERROR "libprotobuf dev files not found (need message.h + libprotobuf)")
endif()
"""

SUB_NEW_FIND = """# protobuf >= 22 links Abseil, whose transitive DSOs must appear on the
# link line; the CONFIG package target carries them. The top-level
# CMakeLists normally defines cppdesk_protobuf first — the fallback keeps
# this subdirectory standalone-buildable.
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
"""

SUB_GEN_BLOCK = """# Generated protobuf structs (upstream build.rs parity): generate at build
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

"""

ROOT_BLOCK = """# Protobuf: prefer the package config (protobuf >= 22 links Abseil, whose
# transitive DSOs must appear on the link line — a raw find_library() link
# fails with "DSO missing from command line"). The interface target below
# is inherited by libs/hbb_common.
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

"""


def patch_sub(path):
    text = open(path).read()
    if "PB_GEN_DIR" in text:
        return "already patched"
    if SUB_OLD_FIND in text:
        text = text.replace(SUB_OLD_FIND, SUB_NEW_FIND)
    if "add_library(hbb_common STATIC" not in text:
        return "add_library NOT FOUND"
    text = text.replace("add_library(hbb_common STATIC",
                        SUB_GEN_BLOCK + "add_library(hbb_common STATIC", 1)
    text = text.replace(
        "    protos/gen/message.pb.cc\n    protos/gen/rendezvous.pb.cc\n",
        "    ${PB_SRCS}\n")
    text = text.replace(
        "target_include_directories(hbb_common PUBLIC include protos/gen)",
        'target_include_directories(hbb_common PUBLIC include "${PB_INCLUDE_DIR}")')
    open(path, "w").write(text)
    return "patched"


def patch_root(path):
    text = open(path).read()
    changed = False
    if "add_subdirectory(libs/hbb_common)" in text and "cppdesk_protobuf" not in text:
        text = text.replace("add_subdirectory(libs/hbb_common)",
                            ROOT_BLOCK + "add_subdirectory(libs/hbb_common)", 1)
        text = text.replace("${PROTOBUF_LIBRARY}", "cppdesk_protobuf")
        changed = True
    line = "        libs/hbb_common/protos/gen\n"
    if line in text:
        text = text.replace(line, "")
        changed = True
    if changed:
        open(path, "w").write(text)
        return "patched"
    return "already patched"


def patch_tests(path):
    text = open(path).read()
    line = "        ../libs/hbb_common/protos/gen\n"
    if line in text:
        open(path, "w").write(text.replace(line, ""))
        return "patched"
    return "already patched"


def main():
    dirs = sorted(glob.glob(os.path.join(ROOT, "examples", "0[7-9]_*"))
                  + glob.glob(os.path.join(ROOT, "examples", "1[0-3]_*")))
    for d in dirs:
        sub = os.path.join(d, "libs", "hbb_common", "CMakeLists.txt")
        root = os.path.join(d, "CMakeLists.txt")
        tests = os.path.join(d, "tests", "CMakeLists.txt")
        if os.path.isfile(sub):
            print(os.path.relpath(sub, ROOT), "->", patch_sub(sub))
        if os.path.isfile(root):
            print(os.path.relpath(root, ROOT), "->", patch_root(root))
        if os.path.isfile(tests):
            print(os.path.relpath(tests, ROOT), "->", patch_tests(tests))


if __name__ == "__main__":
    main()
