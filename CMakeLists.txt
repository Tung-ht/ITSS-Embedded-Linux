cmake_minimum_required(VERSION 3.10)
project(ITSS_Embedded_Project C)

set(CMAKE_C_STANDARD 11)

add_executable(server server.c)
add_executable(client client.c)
