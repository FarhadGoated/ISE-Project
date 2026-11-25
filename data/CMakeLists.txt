cmake_minimum_required(VERSION 3.16)
project(Inc3Storage LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core)

add_library(checkout_storage STATIC CheckoutStorage.cpp CheckoutStorage.h)
target_link_libraries(checkout_storage PUBLIC Qt6::Core)

add_executable(storage_tests tests/storage_tests.cpp)
target_link_libraries(storage_tests PRIVATE checkout_storage)
