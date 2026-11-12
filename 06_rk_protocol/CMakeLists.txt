cmake_minimum_required(VERSION 3.16)

# ==================================================
# 1. 项目基本信息
# ==================================================
set(APP_PROJECT_NAME "rk3576_uart_demo")
set(APP_EXE_NAME "protocol_rk")

project(${APP_PROJECT_NAME}
    VERSION 1.0.0
    LANGUAGES C
)

# ==================================================
# 2. C 语言配置
# ==================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# 生成 compile_commands.json，方便 VSCode 代码分析
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 默认使用 Debug
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# ==================================================
# 3. 编译选项
# ==================================================
add_executable(${APP_EXE_NAME}
    main.c
    protocol/protocol.c
)
# 头文件搜索路径，让 main.c 能找到 protocol.h
target_include_directories(${APP_EXE_NAME} PRIVATE
    protocol
)
# # Add sources to executable
# target_sources(${CMAKE_PROJECT_NAME} PRIVATE
#     # Add user sources here
#     protocol/protocol.c
# )


# GNU/Linux 特性宏，例如 cfmakeraw()
target_compile_definitions(${APP_EXE_NAME} PRIVATE
    _DEFAULT_SOURCE
)

target_compile_options(${APP_EXE_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Debug>:-g3>
)

# ==================================================
# 4. 链接选项
# ==================================================
option(RK_UART_STATIC_LINK "Static link executable" ON)

if(RK_UART_STATIC_LINK)
    target_link_options(${APP_EXE_NAME} PRIVATE
        -static
    )
endif()
