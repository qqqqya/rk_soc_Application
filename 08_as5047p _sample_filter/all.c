

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#include "spi.h"
#include "as5047p.h"
#include "encoder_calc.h"

/* ========================================
 * 1. 实验参数配置
 * ======================================== */

#define SPI_DEVICE          "/dev/spidev0.0"

// 采样休眠时间：100ms
#define SAMPLE_US           100000U

// 滤波时间常数：0.2s
#define FILTER_TAU          0.2

// 最大有效采样间隔：250ms
// 本次实验假设实际转速不超过100RPM
#define MAX_VALID_GAP_NS    250000000ULL

// 总采样次数，100ms下约运行30秒
#define SAMPLE_COUNT        300

// 每隔多少次有效采样打印一次
#define PRINT_EVERY         1

// 程序运行标志
static volatile sig_atomic_t g_running = 1;


/* ========================================
 * 2. 数据统计结构体
 * ======================================== */

typedef struct {
    unsigned int total;         // 总采样请求次数
    unsigned int success;       // 有效采样次数
    unsigned int spi_error;     // SPI传输错误次数
    unsigned int parity_error;  // 偶校验错误次数
    unsigned int ef_error;      // 编码器EF错误次数
} sample_stats_t;


/* ========================================
 * 3. 基础辅助函数
 * ======================================== */

/**
 * @brief 处理Ctrl+C及程序终止信号。
 * @param sig 信号编号。
 * @return 无。
 */
static void signal_handler(int sig){
    (void)sig;

    // 信号处理函数只修改运行标志
    g_running = 0;
}


/**
 * @brief 获取Linux单调时钟。
 * @param now_ns 用于保存纳秒时间戳的指针。
 * @return 成功返回0，失败返回-1。
 */
static int get_time_ns(uint64_t *now_ns){
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0){
        perror("clock_gettime");
        return -1;
    }

    *now_ns = (uint64_t)ts.tv_sec * 1000000000ULL
              + (uint64_t)ts.tv_nsec;

    return 0;
}


/**
 * @brief 一阶低通滤波器。
 * @param input 本次原始转速。
 * @param last_output 上一次滤波结果。
 * @param dt_s 实际采样间隔，单位秒。
 * @return 本次滤波后的转速。
 */
static double rpm_filter(double input, double last_output, double dt_s){
    // 根据实际采样周期计算滤波系数
    double alpha = dt_s / (FILTER_TAU + dt_s);

    // 当前输入和历史输出进行加权
    return alpha * input + (1.0 - alpha) * last_output;
}


/**
 * @brief 检查AS5047P启动时的磁场诊断状态。
 * @param fd SPI设备文件描述符。
 * @return 0表示诊断正常，-1表示读取或诊断异常。
 */
static int encoder_check_diag(int fd){
    uint16_t diag = 0;

    if (as5047p_read_register(fd, AS5047P_REG_DIAAGC, &diag) != 0){
        fprintf(stderr, "Read DIAAGC failed.\n");
        return -1;
    }

    unsigned int agc  = diag & 0xFFU;
    unsigned int lf   = (diag >> 8) & 1U;
    unsigned int cof  = (diag >> 9) & 1U;
    unsigned int magh = (diag >> 10) & 1U;
    unsigned int magl = (diag >> 11) & 1U;

    printf("DIAAGC=0x%04X AGC=%u LF=%u COF=%u MAGH=%u MAGL=%u\n",
           (unsigned)diag, agc, lf, cof, magh, magl);

    // 磁场或内部诊断异常时，不进入正式采样实验
    if (!lf || cof || magh || magl){
        fprintf(stderr, "Encoder diagnostic abnormal.\n");
        return -1;
    }

    return 0;
}


/* ========================================
 * 4. 应用层主程序
 * ======================================== */

/**
 * @brief AS5047P模块化采样与转速计算实验。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 * @return 正常退出返回0，发生错误返回1。
 */
int main(int argc, char *argv[]){
    const char *device = SPI_DEVICE;

    if (argc > 1){
        device = argv[1];
    }

    // 注册退出信号
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 初始化编码器算法状态
    encoder_state_t encoder;
    encoder_init(&encoder);

    // 初始化统计结构体
    sample_stats_t stats = {0};

    double filtered_rpm = 0.0;
    int exit_code = 0;

    /* ---------- 第一步：打开SPI设备 ---------- */

    int fd = open(device, O_RDWR);

    if (fd < 0){
        perror("open SPI");
        return 1;
    }

    /* ---------- 第二步：配置SPI ---------- */

    if (spi_config(fd) < 0){
        close(fd);
        return 1;
    }

    // 等待传感器初始化完成
    delay_us(100000);

    /* ---------- 第三步：检查磁场状态 ---------- */

    if (encoder_check_diag(fd) < 0){
        close(fd);
        return 1;
    }

    printf("===== AS5047P MODULE DEMO =====\n");
    printf("Device: %s\n", device);
    printf("Sample delay: %u us\n", SAMPLE_US);
    printf("Press Ctrl+C to exit.\n\n");

    /* ---------- 第四步：循环采样 ---------- */

    for (int i = 0; i < SAMPLE_COUNT && g_running; i++){

        uint16_t raw = 0;
        uint64_t now_ns = 0;
        double dt_ms = 0.0;

        stats.total++;

        // 读取14位角度寄存器
        int status = as5047p_read_register(fd, AS5047P_REG_ANGLECOM, &raw);

        // 获取本次读取完成后的近似采样时间
        if (get_time_ns(&now_ns) < 0){
            exit_code = 1;
            break;
        }

        /* 检查距离上一次有效采样的时间。
         * 无论本次读取成功还是失败，都需要检查。
         */
        if (encoder.initialized){

            if (now_ns <= encoder.last_time_ns){
                fprintf(stderr, "Invalid sample time.\n");
                exit_code = 1;
                break;
            }

            uint64_t dt_ns = now_ns - encoder.last_time_ns;

            dt_ms = (double)dt_ns / 1000000.0;

            /* 时间间隔过长时，多圈角度可能失去连续性。
             * 本次教学实验直接停止，不继续输出不可靠位置。
             */
            if (dt_ns > MAX_VALID_GAP_NS){
                fprintf(stderr, "Sample gap too large: %.3f ms\n", dt_ms);
                exit_code = 1;
                break;
            }
        }

        /* ---------- 第五步：判断读取结果 ---------- */

        if (status != 0){

            if (status == -1){
                stats.spi_error++;
            }else if (status == -2){
                stats.parity_error++;
            }else if (status == -3){
                stats.ef_error++;
            }

            // 丢弃本次错误数据，不更新角度状态
            delay_us(SAMPLE_US);
            continue;
        }

        /* ---------- 第六步：更新角度与转速 ---------- */

        int result = encoder_update(&encoder, raw, now_ns);

        if (result < 0){
            fprintf(stderr, "Encoder update failed: %d\n", result);
            exit_code = 1;
            break;
        }

        stats.success++;

        /* result=0：第一次采样，只建立初始状态。
         * result=1：已具备两个采样点，可以计算转速。
         */
        if (result == 0){

            filtered_rpm = 0.0;

        }else{

            // 将实际采样周期转换为秒
            double dt_s = dt_ms / 1000.0;

            // 对原始RPM进行一阶低通滤波
            filtered_rpm = rpm_filter(
                encoder.rpm,
                filtered_rpm,
                dt_s
            );
        }

        /* ---------- 第七步：打印数据 ---------- */

        if (stats.success == 1 ||
            stats.success % PRINT_EVERY == 0){

            printf("RAW=%5u ANGLE=%8.3f REL=%9.3f "
                   "RPM=%8.3f FILTER=%8.3f DT=%7.3f ms\n",
                   (unsigned)encoder.raw_angle,
                   encoder.angle_deg,
                   encoder.relative_deg,
                   encoder.rpm,
                   filtered_rpm,
                   dt_ms);
        }

        // 等待下一次采样
        delay_us(SAMPLE_US);
    }

    /* ---------- 第八步：统计实验结果 ---------- */

    printf("\n===== SAMPLE SUMMARY =====\n");

    printf("Total        : %u\n", stats.total);
    printf("Success      : %u\n", stats.success);
    printf("SPI error    : %u\n", stats.spi_error);
    printf("Parity error : %u\n", stats.parity_error);
    printf("EF error     : %u\n", stats.ef_error);

    if (stats.total > 0){

        double success_rate =
            (double)stats.success * 100.0 / stats.total;

        printf("Success rate : %.2f%%\n", success_rate);
    }

    // 正常释放文件描述符
    close(fd);

    printf("SPI program exited.\n");

    return exit_code;
}
