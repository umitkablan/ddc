cmake_minimum_required(VERSION 3.14)
project(DDC VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address")
# set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(FETCHCONTENT_QUIET FALSE)

if(NOT EXISTS "${CMAKE_BINARY_DIR}/conan.cmake")
   message(STATUS "Downloading conan.cmake from https://github.com/conan-io/cmake-conan")
   file(DOWNLOAD "https://raw.githubusercontent.com/conan-io/cmake-conan/master/conan.cmake"
                  "${CMAKE_BINARY_DIR}/conan.cmake")
endif()
include(${CMAKE_BINARY_DIR}/conan.cmake)
# sync with conanfile.txt
conan_cmake_run(REQUIRES openssl/1.1.1l  BASIC_SETUP)
conan_cmake_run(REQUIRES libuv/1.42.0    BASIC_SETUP)
conan_cmake_run(REQUIRES hiredis/1.0.2   BASIC_SETUP)
conan_cmake_run(REQUIRES rapidjson/1.1.0 BASIC_SETUP)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)

include(FetchContent)
FetchContent_Declare(AMQP-CPP
  URL https://github.com/CopernicaMarketingSoftware/AMQP-CPP/archive/refs/tags/v4.3.15.zip
)
FetchContent_Declare(REDIS-PLUS-PLUS
  URL https://github.com/sewenew/redis-plus-plus/archive/refs/tags/1.3.3.tar.gz
)
FetchContent_Declare(CROWCPP
  URL https://github.com/CrowCpp/Crow/archive/refs/tags/v0.3+3.tar.gz
)

conan_global_flags()

set(OPENSSL_ROOT_DIR ${CONAN_OPENSSL_ROOT})
set(OPENSSL_USE_STATIC_LIBS True)

set(AMQP-CPP_LINUX_TCP ON )
FetchContent_MakeAvailable(AMQP-CPP)

set(HIREDIS_HEADER ${CONAN_HIREDIS_ROOT}/include)
set(HIREDIS_LIB ${CONAN_HIREDIS_ROOT}/lib)
set(REDIS_PLUS_PLUS_BUILD_TEST OFF)
FetchContent_MakeAvailable(REDIS-PLUS-PLUS)

set(BUILD_EXAMPLES OFF)
set(BUILD_TESTING OFF)
FetchContent_MakeAvailable(CROWCPP)


add_subdirectory(src)

