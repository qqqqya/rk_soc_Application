#ifndef SPI_H
#define SPI_H

#include <stdint.h>
#define SPI_DEVICE      "/dev/spidev0.0"
#define SPI_SPEED_HZ    500000U
#define SPI_BITS        8
/**
 * @brief 配置SPI模式、字宽、位序及传输速度。
 * @param fd SPI设备文件描述符。
 * @return 成功返回0，失败返回-1。
 */
int spi_config(int fd);

/**
 * @brief 执行一次16位SPI全双工传输。
 * @param fd SPI设备文件描述符。
 * @param tx 待发送的16位数据。
 * @param rx 接收数据的存储地址。
 * @return 成功返回0，失败返回-1。
 */
int spi_transfer16(int fd, uint16_t tx, uint16_t *rx);

/**
 * @brief 微秒级休眠。
 * @param us 休眠时间，单位微秒。
 * @return 无。
 */
void delay_us(unsigned int us);

#endif