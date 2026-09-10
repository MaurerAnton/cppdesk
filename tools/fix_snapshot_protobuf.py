#!/usr/bin/env python3
"""Apply the protobuf/Abseil CMake fix to the frozen snapshots (07-13).

protobuf >= 22 links Abseil; the raw find_library() link drops the
transitive DSOs and fails with "DSO missing from command line". The live
root was fixed by using the protobuf CONFIG package through a shared
interface target (cppdesk_protobuf). This script applies the identical
wiring to the frozen snapshots so the README's "each snapshot builds
standalone" promise holds on modern distros.

Run from the repo root:  python3 tools/fix_snapshot_protobuf.py
"""
import glob
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SUB_OLD = """find_path(PROTOBUF_INCLUDE_DIR NAMES google/protobuf/message.h)
find_library(PROTOBUF_LIBRARY NAMES protobuf)
if(NOT PROTOBUF_INCLUDE_DIR OR NOT PROTOBUF_LIBRARY)
    message(FATAL_ERROR "libprotobuf dev files not found (need message.h + libprotobuf)")
endif()
"""

SUB_NEW = """# protobuf >= 22 links Abseil, whose transitive DSOs must appear on the
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


def patch(path, is_sub):
    text = open(path).read()
    if "cppdesk_protobuf" in text:
        return "already patched"
    if is_sub:
        if SUB_OLD not in text:
            return "SUB BLOCK NOT FOUND"
        text = text.replace(SUB_OLD, SUB_NEW)
        text = text.replace(
            "target_include_directories(hbb_common PRIVATE ${PROTOBUF_INCLUDE_DIR})\n", "")
        text = text.replace(
            "target_link_libraries(hbb_common PRIVATE ${ZSTD_LIBRARY} ${PROTOBUF_LIBRARY})",
            "target_link_libraries(hbb_common PRIVATE ${ZSTD_LIBRARY} cppdesk_protobuf)")
    else:
        marker = "add_subdirectory(libs/hbb_common)"
        if marker not in text:
            return "add_subdirectory NOT FOUND"
        text = text.replace(marker, ROOT_BLOCK + marker, 1)
        text = text.replace("${PROTOBUF_LIBRARY}", "cppdesk_protobuf")
    open(path, "w").write(text)
    return "patched"


def main():
    dirs = sorted(glob.glob(os.path.join(ROOT, "examples", "0[7-9]_*"))
                  + glob.glob(os.path.join(ROOT, "examples", "1[0-3]_*")))
    for d in dirs:
        for rel, is_sub in (("CMakeLists.txt", False),
                            (os.path.join("libs", "hbb_common", "CMakeLists.txt"), True)):
            p = os.path.join(d, rel)
            if os.path.isfile(p):
                print(os.path.relpath(p, ROOT), "->", patch(p, is_sub))


if __name__ == "__main__":
    main()
