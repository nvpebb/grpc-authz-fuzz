from conan import ConanFile
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout


class GrpcAuthzFuzzConan(ConanFile):
    name = "grpc-authz-fuzz"
    version = "0.1.0"
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"
    def requirements(self):
        self.requires("grpc/1.72.0")
        self.requires("gtest/1.16.0")
        self.requires("yaml-cpp/0.8.0")
        self.requires("cli11/2.4.2")
        self.requires("spdlog/1.16.0")

    def build_requirements(self):
        self.tool_requires("grpc/1.72.0")

    def layout(self):
        cmake_layout(self)

    def configure(self):
        # drop unused options noise for an application package
        pass

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.variables["CMAKE_CXX_STANDARD"] = "20"
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
