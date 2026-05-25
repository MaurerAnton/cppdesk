# cmake/FindLibSodium.cmake
# Find libsodium for encryption support
find_path(SODIUM_INCLUDE_DIR sodium.h)
find_library(SODIUM_LIBRARY NAMES sodium libsodium)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Sodium DEFAULT_MSG SODIUM_LIBRARY SODIUM_INCLUDE_DIR)
if(SODIUM_FOUND)
    set(SODIUM_LIBRARIES ${SODIUM_LIBRARY})
    set(SODIUM_INCLUDE_DIRS ${SODIUM_INCLUDE_DIR})
    if(NOT TARGET Sodium::Sodium)
        add_library(Sodium::Sodium UNKNOWN IMPORTED)
        set_target_properties(Sodium::Sodium PROPERTIES
            IMPORTED_LOCATION "${SODIUM_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SODIUM_INCLUDE_DIR}")
    endif()
endif()

# cmake/CompilerWarnings.cmake
function(set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /w14242 /w14254 /w14263 /w14265 /w14287 /we4289 /w14296 /w14311 /w14545 /w14546 /w14547 /w14549 /w14555 /w14619 /w14640 /w14826 /w14905 /w14906 /w14928)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion -Wnull-dereference -Wdouble-promotion -Wformat=2)
    endif()
endfunction()

# cmake/Sanitizers.cmake
function(enable_sanitizers target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

# cmake/InstallRules.cmake
include(GNUInstallDirs)
install(TARGETS cppdesk_client cppdesk_server cppdesk_cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES res/cppdesk.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES res/cppdesk.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
