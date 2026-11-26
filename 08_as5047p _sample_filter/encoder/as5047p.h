#ifndef AS5047P_H
#define AS5047P_H

#include <stdint.h>

/* AS5047P寄存器地址 */
#define AS5047P_REG_NOP       0x0000U
#define AS5047P_REG_ERRFL     0x0001U
#define AS5047P_REG_DIAAGC    0x3FFCU
#define AS5047P_REG_MAG       0x3FFDU
#define AS5047P_REG_ANGLEUNC  0x3FFEU
#define AS5047P_REG_ANGLECOM  0x3FFFU

/**
 * @brief 读取AS5047P指定寄存器。
 * @param fd SPI设备文件描述符。
 * @param reg 目标寄存器地址。
 * @param data 保存低14位有效数据。
 * @return 0成功，-1传输失败，
 *         -2偶校验错误，-3芯片错误。
 */
int as5047p_read_register(int fd, uint16_t reg, uint16_t *data);

#endif