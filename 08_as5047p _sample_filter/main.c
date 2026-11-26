


#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>


#include "encoder_calc.h"
#include "spi.h"
#include "as5047p.h"

/* ========================================
 * 1. SPI 参数及 AS5047P 寄存器定义
 * ======================================== */

// #define SPI_DEVICE          "/dev/spidev0.0"
// #define SPI_SPEED_HZ        500000U
// #define SPI_BITS            8

// #define AS5047P_REG_NOP       0x0000U
// #define AS5047P_REG_ERRFL     0x0001U
// #define AS5047P_REG_DIAAGC    0x3FFCU
// #define AS5047P_REG_MAG       0x3FFDU
// #define AS5047P_REG_ANGLEUNC  0x3FFEU
// #define AS5047P_REG_ANGLECOM  0x3FFFU

// // Bit14：读取命令或响应错误标志
// #define AS5047P_READ_BIT      0x4000U
// #define AS5047P_ERROR_BIT     0x4000U

// // Bit15：偶校验位
// #define AS5047P_PARITY_BIT    0x8000U

// // 用于提取低14位有效数据
// #define AS5047P_DATA_MASK     0x3FFFU

// #define AS5047P_RESOLUTION    16384.0



// /**
//  * @brief 将14位编码器原始数据转换成角度。
//  * @param raw 原始角度数据，范围0~16383。
//  * @return 角度值，单位degree。
//  */
// static double as5047p_raw_to_degree(uint16_t raw){
//     return (double)raw * 360.0 / AS5047P_RESOLUTION;
// }


/* ========================================
 * 5. 主程序
 * ======================================== */
// #define SAMPLE_US    1000000U   //1000 000  1000ms
#define SAMPLE_US    100000U   //1000 000  100ms
// #define SAMPLE_US    10000U   //10 000  10ms
/**
 * @brief AS5047P SPI角度读取实验。
 * @param argc 命令行参数数量。
 * @param argv 命令行参数数组。
 * @return 正常结束返回0，初始化失败返回1。
 */
int main(int argc, char *argv[]){
    const char *device = SPI_DEVICE;

    // 支持运行时指定不同SPI设备
    if (argc > 1){
        device = argv[1];
    }

    printf("===== AS5047P SPI DEMO =====\n");
    printf("SPI Device: %s\n", device);

    // 第一步：打开SPI字符设备
    int fd = open(device, O_RDWR);

    if (fd < 0){
        perror("open SPI");
        return 1;
    }

    // 第二步：配置SPI模式、速度和数据宽度
    if (spi_config(fd) < 0){
        close(fd);
        return 1;
    }

    // 留出传感器初始化时间
    delay_us(100000);

    /************************************************* */
        double dt_ms = 0.0;
        double filtered_rpm = 0.0;
        encoder_state_t encoder;
    encoder_init(&encoder);

    while(1){    // 读取本次编码器原始角度
        uint16_t raw = 0;

        // 这里使用前面经过奇偶校验和EF检查的读取接口
        int read_status = as5047p_read_register(
            fd,
            AS5047P_REG_ANGLECOM,
            &raw
        );

        if (read_status == 0){
            // 获取单调时钟
            struct timespec ts;

            if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0){//使用单调时钟
                uint64_t now_ns =       // 变量
                    (uint64_t)ts.tv_sec * 1000000000ULL +
                    (uint64_t)ts.tv_nsec;//将时间转换为纳秒级 s+ns


                // 获取上一次有效采样时间
                uint64_t last_ns = encoder.last_time_ns;

                // 更新编码器位移与速度
                int result = encoder_update(&encoder, raw, now_ns);//根据时间进行角度更新
            


        // 每隔100ms读取一次，便于手动转动磁铁
        delay_us(SAMPLE_US);

                /** --------------------*/
                // 计算实际采样间隔，单位ms
                if (encoder.initialized && now_ns > last_ns){
                    dt_ms = (double)(now_ns - last_ns) / 1000000.0;
                }
                printf("DT=%8.3f ms\n", dt_ms);



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
            printf("FILTERED RPM=%8.3f RPM\n", filtered_rpm);
        }


                if (result >= 0){
                    printf("RAW=%5u  ANGLE=%8.3f  "
                        "RELATIVE=%9.3f  RPM=%8.3f\n",
                        (unsigned)encoder.raw_angle,
                        encoder.angle_deg,
                        encoder.relative_deg,
                        encoder.rpm);
                }else{
                    printf("Encoder calculation error: %d\n", result);
                    }
                }
        }
    }

#if 0
    // 第三步：读取一次诊断寄存器
    uint16_t diag = 0;

    if (as5047p_read_register(fd, AS5047P_REG_DIAAGC, &diag) == 0){
        printf("DIAAGC = 0x%04X\n", (unsigned)diag);
        printf("AGC    = %u\n", (unsigned)(diag & 0xFFU));
        printf("LF     = %u\n", (unsigned)((diag >> 8) & 1U));
        printf("COF    = %u\n", (unsigned)((diag >> 9) & 1U));
        printf("MAGH   = %u\n", (unsigned)((diag >> 10) & 1U));
        printf("MAGL   = %u\n", (unsigned)((diag >> 11) & 1U));
    }

    printf("\nStart reading angle...\n");

    // 第四步：循环读取角度100次
    for (int i = 0; i < 100; i++){
        uint16_t raw_angle = 0;

        int ret = as5047p_read_register(
            fd,
            AS5047P_REG_ANGLECOM,
            &raw_angle
        );

        if (ret == 0){
            double degree = as5047p_raw_to_degree(raw_angle);

            printf("[%03d] RAW=%5u HEX=0x%04X ANGLE=%8.3f deg\n",
                   i + 1,
                   (unsigned)raw_angle,
                   (unsigned)raw_angle,
                   degree);
        }else{
            printf("[%03d] Read failed: %d\n", i + 1, ret);
        }

        // 每隔100ms读取一次，便于手动转动磁铁
        delay_us(100000);
    }
#endif
    // 第五步：释放SPI设备文件描述符
    close(fd);

    printf("AS5047P experiment finished.\n");

    return 0;
}
