cmake_minimum_required(VERSION 3.10.0)
project(atcortex VERSION 0.1.0 LANGUAGES C)

# 递归查找目录下所有的 .c 文件
file(GLOB_RECURSE src "src/*.c")
add_executable(atcortex ${src})
target_include_directories(atcortex PRIVATE src/include )
