cmake_minimum_required(VERSION 3.10)

project(bank CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)


find_package(Threads)
find_package(Boost 1.71 REQUIRED system)

set(NETWORKING_LIBS)

add_executable(bank-test doctest_main.cpp bank_test.cpp bank.cpp)
target_link_libraries(bank-test ${CMAKE_THREAD_LIBS_INIT})

add_executable(bank-server bank_server.cpp bank.cpp)
target_include_directories(bank-server PRIVATE ${Boost_INCLUDE_DIRS})
target_link_libraries(bank-server ${CMAKE_THREAD_LIBS_INIT} ${Boost_LIBRARIES} ${NETWORKING_LIBS})
