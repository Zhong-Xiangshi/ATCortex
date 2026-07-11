cmake_minimum_required(VERSION 3.16)
project(ATCortex C)

add_library(ATCortex STATIC)

target_sources(ATCortex PRIVATE
    ATCortex.c
    interface.c
    ring_buffer.c
    slist.c
    extern_msg_handle.c
    urc_handle.c
    send_msg_handle.c
    recv_data_handle.c
    stack.c
)

target_include_directories(ATCortex 
    PUBLIC include
    PRIVATE .
)