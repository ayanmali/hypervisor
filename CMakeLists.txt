cmake_minimum_required(VERSION 3.20)
project(cpp-hypervisor)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(cpp-hypervisor main.cpp)

target_link_libraries(cpp-hypervisor PRIVATE kvm)

install(TARGETS cpp-hypervisor DESTINATION bin)

install(FILES kvm.hpp DESTINATION include)