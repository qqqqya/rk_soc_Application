#include "as5047p.h"
#include "spi.h"

#include <stdio.h>

/* 仅供AS5047P协议模块内部使用 */
#define AS5047P_READ_BIT      0x4000U
#define AS5047P_ERROR_BIT     0x4000U
#define AS5047P_PARITY_BIT    0x8000U
#define AS5047P_DATA_MASK     0x3FFFU



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
int as5047p_read_register(int fd, uint16_t reg, uint16_t *data){
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










