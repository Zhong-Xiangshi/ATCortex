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
)

target_include_directories(ATCortex 
    PUBLIC include
    PRIVATE .
)

# 继承 Zephyr 的所有编译 flag (CFLAGS)
target_link_libraries(ATCortex PRIVATE zephyr_interface)