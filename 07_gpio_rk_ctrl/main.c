
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <gpiod.h>

/* ==============================
 * 1. GPIO 硬件配置
 * ============================== */

// 外接 LED：J11 Pin 33，GPIO3_B0
#define LED_CHIP        "/dev/gpiochip3"
#define LED_OFFSET      8

// 外部按键：J11 Pin 32，GPIO4_A0
#define KEY_CHIP        "/dev/gpiochip4"
#define KEY_OFFSET      0

// 板载 USER LED 属性文件
#define USER_TRIGGER    "/sys/class/leds/user/trigger"
#define USER_BRIGHTNESS "/sys/class/leds/user/brightness"

// 按键消抖时间，单位 ms
#define DEBOUNCE_MS     30

/* 程序运行标志，由 SIGINT 信号修改 */
static volatile sig_atomic_t g_running = 1;


/* ==============================
 * 2. 基础辅助函数
 * ============================== */

/**
 * @brief 处理退出信号，例如 Ctrl+C。
 * @param sig Linux 信号编号。
 * @return 无。
 *
 * 信号处理函数中只修改运行标志，
 * 不直接关闭 GPIO 或执行复杂操作。
 */
static void signal_handler(int sig){
    (void)sig;
    g_running = 0;
}


/**
 * @brief 向 sysfs 属性文件写入字符串。
 * @param path 属性文件路径。
 * @param value 需要写入的内容。
 * @return 成功返回0，失败返回-1。
 */
static int write_sysfs(const char *path, const char *value){
    FILE *fp = fopen(path, "w");

    if (fp == NULL){
        perror(path);
        return -1;
    }

    if (fputs(value, fp) == EOF){//和第一版类似 那里用的write函数
        perror("fputs");
        fclose(fp);
        return -1;
    }

    /* fclose 会刷新缓冲区，因此也需要检查 */
    if (fclose(fp) == EOF){
        perror("fclose");
        return -1;
    }

    return 0;
}


/**
 * @brief 同步控制外接 LED 和板载 USER LED。
 * @param led_line 已经申请的 GPIO 输出 line。
 * @param on 1表示点亮，0表示熄灭。
 * @return 成功返回0，失败返回-1。
 */
static int led_control(struct gpiod_line *led_line, int on){

    // 使用 libgpiod 控制外接 LED
    if (gpiod_line_set_value(led_line, on) < 0){
        perror("gpiod_line_set_value");
        return -1;
    }

    // 使用 LED class 控制板载 USER LED
    if (write_sysfs(USER_BRIGHTNESS, on ? "1\n" : "0\n") < 0){
        return -1;
    }

    return 0;
}


/* ==============================
 * 3. 按键软件消抖
 * ============================== */

/**
 * @brief 等待按键边沿停止，再读取稳定电平。
 * @param key_line 已申请双边沿事件的 GPIO line。
 * @return 0或1表示稳定电平，-1表示失败或退出。
 *
 * 每出现一次新的边沿，就重新等待30ms。
 * 连续30ms没有新事件，才读取当前电平。
 */
static int key_debounce(struct gpiod_line *key_line){

    struct timespec timeout = {
        .tv_sec = 0,
        .tv_nsec = DEBOUNCE_MS * 1000000L       //30ms 10^6
    };

    struct gpiod_line_event event;

    while (g_running){

        // 等待消抖期间是否又出现新的边沿---也就是等待30ms事件
        // 如果电平稳定 则是-30ms内不应该出现新的边沿事件，就认为电平稳定
        int ret = gpiod_line_event_wait(key_line, &timeout);

        if (ret == 0){            // 连续30ms没有新事件，认为电平稳定---在这里进行返回
            return gpiod_line_get_value(key_line);
        }

        if (ret < 0){
            if (errno == EINTR){
                continue;
            }

            perror("debounce wait");
            return -1;
        }

        // 仍然出现边沿，取走该事件---赋值给指针结构体 event

        if (gpiod_line_event_read(key_line, &event) < 0){
        // 读取事件后，会自动从事件队列中移除
        // 所以，后续等待30ms时，会自动跳过这个事件
        // 直到出现新的边沿事件
            perror("debounce read");
            return -1;
        }

        // 继续循环，重新等待完整的30ms
    }
    return -1;
}


/* ==============================
 * 4. 主程序
 * ============================== */

/**
 * @brief GPIO 输入输出综合实验。
 * @return 正常退出返回0，发生错误返回1。
 */
int main(void){

    struct gpiod_chip *led_chip = NULL;
    struct gpiod_chip *key_chip = NULL;

    struct gpiod_line *led_line = NULL;
    struct gpiod_line *key_line = NULL;

    struct gpiod_line_event event;
    //初始化变量--后面再进行赋值
    int last_value = -1;
    int current_value;
    int ret;
    int exit_code = 1;
    int led_requested = 0;
    int key_requested = 0;
    int user_ready = 0;

    // 注册 Ctrl+C 退出信号处理函数
    signal(SIGINT, signal_handler);///信号处理函数中只修改运行标志 g_running
    signal(SIGTERM, signal_handler);

    /* ---------- 4.1 打开 GPIO 控制器 ---------- */

    led_chip = gpiod_chip_open(LED_CHIP);
    key_chip = gpiod_chip_open(KEY_CHIP);//no byname V2新版本的

    if (led_chip == NULL || key_chip == NULL){
        perror("gpiod_chip_open");
        goto cleanup;
    }

    /* ---------- 4.2 获取 GPIO line ---------- */

    led_line = gpiod_chip_get_line(led_chip, LED_OFFSET);
    key_line = gpiod_chip_get_line(key_chip, KEY_OFFSET);

    if (led_line == NULL || key_line == NULL){
        perror("gpiod_chip_get_line");
        goto cleanup;
    }

    /* ---------- 4.3 申请输出 GPIO ---------- */

    // "rk-led" 是 consumer 名称-----io的模式申请了输出权限
    // 第三个参数0表示初始输出低电平
    if (gpiod_line_request_output(led_line, "rk-led", 0) < 0){
        perror("request LED output");
        goto cleanup;
    }

    led_requested = 1;

    /* ---------- 4.4 申请按键双边沿事件 ---------- */

    // 使用外部上拉电阻，监听上升沿和下降沿
    if (gpiod_line_request_both_edges_events(key_line, "rk-key") < 0){
        perror("request KEY events");
        goto cleanup;
    }

    key_requested = 1;

    /* ---------- 4.5 初始化板载 LED ---------- */

    // 关闭内核自动触发功能---trigger 填none
    if (write_sysfs(USER_TRIGGER, "none\n") < 0){//这里填heartbeat 怎么好像没有效果
        goto cleanup;
    }

    user_ready = 1;//用户userled-准备就绪

    printf("===== RK3576 GPIO EVENT DEMO =====\n");
    printf("LED : gpiochip3 offset 8\n");
    printf("KEY : gpiochip4 offset 0\n");
    printf("Press Ctrl+C to exit.\n");

    /* ---------- 4.6 获取按键初始状态 ---------- */

    current_value = gpiod_line_get_value(key_line);//获取按键初始状态
    printf("KEY initial value: %d\n", current_value);//////////
    if (current_value < 0){
        perror("get initial KEY value");
        goto cleanup;
    }

    last_value = current_value;

    // 按键低电平有效，因此需要取反      !current_value
    if (led_control(led_line, !current_value) < 0){
        goto cleanup;
    }

    printf("[INIT] KEY=%d, LED=%d\n",
           current_value, !current_value);

    /* ---------- 4.7 主事件循环 ---------- */

    while (g_running){

        // 最多等待250ms，方便检查退出标志
        struct timespec timeout = {
            .tv_sec = 0,
            .tv_nsec = 250000000L
        };

        ret = gpiod_line_event_wait(key_line, &timeout);/* 等待按键事件 250ms------------*/
        if (ret == 0){
            // 没有按键事件，继续等待  循环while  除非grunning标志位被handler修改
            continue;
        }
        if (ret < 0){//等待失败
            if (errno == EINTR){
                continue;
            }

            perror("gpiod_line_event_wait");
            goto cleanup;//如果等待失败，退出循环 清理资源
        }

        // 从内核事件队列中读取一个边沿事件
        if (gpiod_line_event_read(key_line, &event) < 0){
            perror("gpiod_line_event_read");
            goto cleanup;
        }

        if (event.event_type == GPIOD_LINE_EVENT_FALLING_EDGE){
            printf("[EDGE] Falling edge\n");
        }else if (event.event_type == GPIOD_LINE_EVENT_RISING_EDGE){
            printf("[EDGE] Rising edge\n");
        }

        // 执行软件消抖，取得稳定的按键电平---30ms内没有新的边沿事件
        current_value = key_debounce(key_line);

        if (!g_running){
            break;
        }

        if (current_value < 0){
            perror("key_debounce");
            goto cleanup;
        }

        // 也就是没有新的边沿事件，没有按键的变化
        // 如果消抖后的状态没有变化，不执行重复操作--后面的就直接跳过即可
        if (current_value == last_value){
            continue;
        }

        last_value = current_value;//获取到新的按键电平，更新上一次的按键电平

        // 低电平：按键按下
        if (current_value == 0){//低电平有效
            if (led_control(led_line, 1) < 0){
                goto cleanup;//写入失败，退出循环 清理资源
            }
            printf("[KEY] PRESSED  -> LED ON\n");
        }else{
            if (led_control(led_line, 0) < 0){
                goto cleanup;
            }
            printf("[KEY] RELEASED -> LED OFF\n");
        }
    }

    exit_code = 0;

    /* ---------- 4.8 统一释放资源 ---------- */

cleanup:

    printf("Releasing GPIO resources...\n");

    // 退出前关闭外接 LED
    if (led_requested){
        gpiod_line_set_value(led_line, 0);
        gpiod_line_release(led_line);
    }

    // 退出前关闭板载 USER LED
    if (user_ready){ // 如果用户userled-已经申请（也就是写入none--关闭内核自动触发功能）准备就绪
        write_sysfs(USER_BRIGHTNESS, "0\n");//亮度设置为0  但依然是手动trigger
    }

    if (key_requested){//if已经申请了按键事件，才释放资源
        gpiod_line_release(key_line);
    }

    if (led_chip != NULL){
        gpiod_chip_close(led_chip);
    }

    if (key_chip != NULL){
        gpiod_chip_close(key_chip);
    }

    printf("GPIO program exited.\n");

    return exit_code;
}
