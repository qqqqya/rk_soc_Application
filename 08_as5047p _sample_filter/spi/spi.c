#define _POSIX_C_SOURCE 200809L

#include "spi.h"

#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>



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
void delay_us(unsigned int us){
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
int spi_config(int fd){
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
int spi_transfer16(int fd, uint16_t tx, uint16_t *rx){
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


