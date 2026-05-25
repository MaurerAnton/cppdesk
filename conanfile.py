from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout

class CppdeskConan(ConanFile):
    name = "cppdesk"
    version = "1.3.0"
    description = "C++ rewrite of RustDesk - cross-platform remote desktop"
    license = "AGPL-3.0"
    url = "https://github.com/MaurerAnton/cppdesk"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "with_tests": [True, False],
        "with_encryption": [True, False],
        "with_flutter": [True, False],
    }
    default_options = {
        "shared": False,
        "with_tests": True,
        "with_encryption": True,
        "with_flutter": True,
    }
    generators = "CMakeDeps", "CMakeToolchain"
    exports_sources = "CMakeLists.txt", "src/*", "include/*", "libs/*", "proto/*"

    def requirements(self):
        self.requires("protobuf/3.21.12")
        self.requires("nlohmann_json/3.11.3")
        self.requires("spdlog/1.13.0")
        self.requires("asio/1.28.2")
        self.requires("openssl/3.2.0")
        self.requires("libsodium/1.0.19")
        self.requires("sqlite3/3.45.0")
        if self.options.with_tests:
            self.requires("gtest/1.14.0")

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure(variables={
            "BUILD_TESTS": self.options.with_tests,
            "ENABLE_ENCRYPTION": self.options.with_encryption,
            "ENABLE_FLUTTER": self.options.with_flutter,
        })
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["cppdesk_common"]
