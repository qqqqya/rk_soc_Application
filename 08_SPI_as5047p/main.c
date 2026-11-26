


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

/* ========================================
 * 1. SPI 参数及 AS5047P 寄存器定义
 * ======================================== */

#define SPI_DEVICE          "/dev/spidev0.0"
#define SPI_SPEED_HZ        500000U
#define SPI_BITS            8

#define AS5047P_REG_NOP       0x0000U
#define AS5047P_REG_ERRFL     0x0001U
#define AS5047P_REG_DIAAGC    0x3FFCU
#define AS5047P_REG_MAG       0x3FFDU
#define AS5047P_REG_ANGLEUNC  0x3FFEU
#define AS5047P_REG_ANGLECOM  0x3FFFU

// Bit14：读取命令或响应错误标志
#define AS5047P_READ_BIT      0x4000U
#define AS5047P_ERROR_BIT     0x4000U

// Bit15：偶校验位
#define AS5047P_PARITY_BIT    0x8000U

// 用于提取低14位有效数据
#define AS5047P_DATA_MASK     0x3FFFU

#define AS5047P_RESOLUTION    16384.0


/* ========================================
 * 2. 基础延时函数
 * ======================================== */

/**
 * @brief Linux 微秒级延时。
 * @param us 延时时间，单位微秒。
 * @return 无。
 *
 * 普通 Linux 调度不能保证精确的微秒级实时性，
 * 本实验只用于保证两次SPI传输之间存在间隔。
 */
static void delay_us(unsigned int us){
    struct timespec ts;

    ts.tv_sec = us / 1000000U;
    ts.tv_nsec = (long)(us % 1000000U) * 1000L;

    while (nanosleep(&ts, &ts) < 0 && errno == EINTR){
        // 如果睡眠被信号打断，继续剩余时间
    }
}


/* ========================================
 * 3. Linux SPI 基础驱动接口
 * ======================================== */

/**
 * @brief 配置 Linux SPI 设备。
 * @param fd 已打开的SPI设备文件描述符。
 * @return 成功返回0，失败返回-1。
 */
static int spi_config(int fd){
    uint8_t mode = SPI_MODE_1;
    uint8_t bits = SPI_BITS;

    // 0表示MSB First，高位先发送
    uint8_t lsb_first = 0;

    uint32_t speed = SPI_SPEED_HZ;

    // 设置SPI模式：CPOL=0、CPHA=1
    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0){
        perror("SPI set mode");
        return -1;
    }

    // 设置位传输顺序
    if (ioctl(fd, SPI_IOC_WR_LSB_FIRST, &lsb_first) < 0){
        perror("SPI set bit order");
        return -1;
    }

    // 设置控制器传输字宽为8位
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0){
        perror("SPI set bits");
        return -1;
    }

    // 设置SPI最大时钟频率
    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0){
        perror("SPI set speed");
        return -1;
    }

    // 回读驱动配置，方便检查
    if (ioctl(fd, SPI_IOC_RD_MODE, &mode) < 0 ||
        ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed) < 0){
        perror("SPI read config");
        return -1;
    }

    printf("SPI Mode  : %u\n", (unsigned)mode);
    printf("SPI Bits  : %u\n", (unsigned)bits);
    printf("SPI Speed : %u Hz\n", (unsigned)speed);

    return 0;
}


/**
 * @brief 执行一次16位SPI全双工传输。
 * @param fd SPI设备文件描述符。
 * @param tx 待发送的16位数据。
 * @param rx 保存接收到的16位数据。
 * @return 成功返回0，失败返回-1。
 *
 * 将16位数据拆成两个8位字节发送，
 * 高字节在前，低字节在后。
 *
 * 一次SPI_IOC_MESSAGE包含两个字节，
 * 正常情况下CS在整个16位帧期间保持有效。
 */
static int spi_transfer16(int fd, uint16_t tx, uint16_t *rx){
    uint8_t tx_buf[2];
    uint8_t rx_buf[2] = {0};

    // 明确发送字节顺序，避免CPU大小端影响---MSB First
    tx_buf[0] = (uint8_t)(tx >> 8);
    tx_buf[1] = (uint8_t)(tx & 0xFFU);

    // 初始化SPI传输描述结构体
    struct spi_ioc_transfer transfer = {
        .tx_buf = (uintptr_t)tx_buf,
        .rx_buf = (uintptr_t)rx_buf,
        .len = sizeof(tx_buf),
        .speed_hz = SPI_SPEED_HZ,
        .bits_per_word = SPI_BITS,
        .cs_change = 0
    };

    // 同时发送MOSI数据和接收MISO数据
    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &transfer);

    if (ret < 0){
        perror("SPI transfer");
        return -1;
    }

    // 正常情况下应完成2字节传输
    if (ret != (int)sizeof(tx_buf)){
        fprintf(stderr, "SPI transfer length error: %d\n", ret);
        return -1;
    }

    // 将接收到的两个字节重新组合成16位数据
    *rx = ((uint16_t)rx_buf[0] << 8) | rx_buf[1];

    return 0;
}


/* ========================================
 * 4. AS5047P 协议处理
 * ======================================== */

/**
 * @brief 计算16位数据中1的数量是否为奇数。
 * @param value 待检查的数据。
 * @return 奇数个1返回1，偶数个1返回0。
 */
static uint8_t as5047p_parity(uint16_t value){
    uint8_t parity = 0;

    for (int i = 0; i < 16; i++){
        parity ^= (uint8_t)(value & 0x01U);     // 逐位异或，统计1的数量
        value >>= 1;
    }

    return parity;
}


/**
 * @brief 生成AS5047P寄存器读取命令。
 * @param reg 低14位寄存器地址。
 * @return 带有偶校验位的16位读取命令。
 */
static uint16_t as5047p_read_command(uint16_t reg){
    // 保留14位地址，并设置Bit14为读取
    uint16_t cmd = (reg & AS5047P_DATA_MASK)
                   | AS5047P_READ_BIT;

    // 如果低15位包含奇数个1，则Bit15置1
    if (as5047p_parity(cmd) != 0){
        cmd |= AS5047P_PARITY_BIT;
    }

    return cmd;
}


/**
 * @brief 读取AS5047P指定寄存器。
 * @param fd SPI设备文件描述符。
 * @param reg 寄存器地址。
 * @param data 保存读取到的14位有效数据。
 * @return 0成功，-1传输失败，
 *         -2偶校验错误，-3芯片报告错误。
 */
static int as5047p_read_register(int fd, uint16_t reg, uint16_t *data){
    uint16_t ignored = 0;
    uint16_t response = 0;

    // 生成目标寄存器读取命令
    uint16_t cmd = as5047p_read_command(reg);

    // 生成读取NOP命令，用于产生下一帧时钟
    uint16_t nop = as5047p_read_command(AS5047P_REG_NOP);

    /* 第一次SPI传输：
     * 发送读取命令。
     * 此时返回的是此前命令的响应，不使用它。
     */
    if (spi_transfer16(fd, cmd, &ignored) < 0){
        return -1;
    }

    // 两次独立传输之间留出片选高电平时间
    delay_us(10);

    /* 第二次SPI传输：
     * 发送NOP，接收目标寄存器的数据。---dummy数据，产生时序，接收真实数据
     */
    if (spi_transfer16(fd, nop, &response) < 0){
        return -1;
    }

    // 检查整个响应帧的偶校验
    if (as5047p_parity(response) != 0){
        printf("[ERROR] Parity error: 0x%04X\n",
               (unsigned)response);
        return -2;
    }

    // 检查Bit14错误标志
    if (response & AS5047P_ERROR_BIT){
        printf("[ERROR] EF set: 0x%04X\n",
               (unsigned)response);
        return -3;
    }

    // 去掉Bit15校验位和Bit14错误位
    *data = response & AS5047P_DATA_MASK;

    return 0;
}


/**
 * @brief 将14位编码器原始数据转换成角度。
 * @param raw 原始角度数据，范围0~16383。
 * @return 角度值，单位degree。
 */
static double as5047p_raw_to_degree(uint16_t raw){
    return (double)raw * 360.0 / AS5047P_RESOLUTION;
}


/* ========================================
 * 5. 主程序
 * ======================================== */

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
                uint64_t now_ns =
                    (uint64_t)ts.tv_sec * 1000000000ULL +
                    (uint64_t)ts.tv_nsec;//将时间转换为纳秒级 s+ns

                // 更新编码器位移与速度
                int result = encoder_update(&encoder, raw, now_ns);//根据时间进行角度更新

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
        // 每隔100ms读取一次，便于手动转动磁铁
        delay_us(1000000);
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
