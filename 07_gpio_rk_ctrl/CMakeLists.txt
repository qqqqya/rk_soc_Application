cmake_minimum_required(VERSION 3.16)

# ==================================================
# 1. 项目基本信息
# ==================================================
set(APP_PROJECT_NAME "rk3576_uart_demo")
set(APP_EXE_NAME "gpio_rk_ctrl")

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
add_executable(${APP_EXE_NAME})

# # # 新增一个独立的协议测试程序
#     add_executable(protocol_test
#         # main.c############################
#         tests/test_protocol.c   #内部有main函数的入口 就不用再添加main.c
#         protocol/protocol.c
#     )

#     # 为测试程序添加协议头文件搜索路径
#     target_include_directories(protocol_test PRIVATE
#         ${CMAKE_CURRENT_SOURCE_DIR}/protocol
#     )


# Add sources to executable
target_sources(${APP_EXE_NAME} PRIVATE
    # Add user sources here
    main.c
    protocol/protocol.c
)
# 头文件搜索路径，让 main.c 能找到 protocol.h
target_include_directories(${APP_EXE_NAME} PRIVATE
    protocol
)


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
# option(RK_UART_STATIC_LINK "Static link executable" ON)

# target_link_directories(${APP_EXE_NAME} PRIVATE /usr/lib/aarch64-linux-gnu)

# if(RK_UART_STATIC_LINK)
#     target_link_options(${APP_EXE_NAME} PRIVATE
#         -static
#     )
# endif()

        # # ==================================================
        # # 4. ARM64 libgpiod 配置
        # # ==================================================

        # # libgpiod 开发库的位置
        # set(GPIOD_ROOT "/home/tony/rk3576-deps/gpiod")

        # # 头文件搜索路径
        # target_include_directories(${APP_EXE_NAME} PRIVATE
        #     "${GPIOD_ROOT}/include"
        # )

        # # 指定 ARM64 动态库的完整路径
        # target_link_libraries(${APP_EXE_NAME} PRIVATE
        #     "${GPIOD_ROOT}/lib/libgpiod.so"
        # )

        # # 本次实验先关闭静态链接
        # option(RK_UART_STATIC_LINK "Static link executable" OFF)

        # if(RK_UART_STATIC_LINK)
        #     target_link_options(${APP_EXE_NAME} PRIVATE -static)
        # endif()


# ==============================
# 3. 第三方 libgpiod 库
# ==============================

set(GPIOD_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/gpiod"
)

# 添加第三方头文件搜索路径
target_include_directories(${APP_EXE_NAME} PRIVATE
    ${GPIOD_ROOT}/include
)
# # 指定 ARM64 动态库的完整路径
# target_link_libraries(${APP_EXE_NAME} PRIVATE
#     "${GPIOD_ROOT}/lib/libgpiod.so"
# )

# 链接 ARM64 静态库    .a 文件
target_link_libraries(${APP_EXE_NAME} PRIVATE
    ${GPIOD_ROOT}/lib/libgpiod.a
)

# ==============================
# 4. 编译选项
# ==============================

target_compile_options(${APP_EXE_NAME} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    $<$<CONFIG:Debug>:-O0>
    $<$<CONFIG:Debug>:-g3>
)

# 整个可执行程序采用静态链接
target_link_options(${APP_EXE_NAME} PRIVATE
    -static
)