#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

/* ================== 宏定义 ================== */
#define SPI_DEVICE "/dev/spidev0.0"

// 寄存器地址 (已包含读写位与奇偶校验位)
#define AS5047P_REG_ERRFL     0x4001     /* 错误标志寄存器 */
#define AS5047P_REG_DIAAGC    0xFFFC     /* 诊断与增益寄存器 */
#define AS5047P_REG_ANGLEUNC  0x7FFE     /* 未补偿角度寄存器 */
#define AS5047P_REG_ANGLECOM  0xFFFF     /* 已补偿角度寄存器 (推荐使用) */

int spi_fd = -1;

/* ================== 底层 SPI 通信 ================== */

// 初始化 SPI 设备 (模式 1)
int AS5047P_Init(void) {
    spi_fd = open(SPI_DEVICE, O_RDWR);///spidev0.0总线0  通道0
    if (spi_fd < 0) {
        perror("[ERROR] Failed to open SPI device");
        return -1;
    }

    uint8_t mode = SPI_MODE_1; // AS5047P 标准通信要求 CPOL=0, CPHA=1
    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {    //mode 1 低电平 下降沿有效
        perror("[ERROR] Failed to set SPI mode");
        return -1;
    }
    return 0;
}

// 严谨的两帧读取逻辑
uint16_t as5047p_read_register(uint16_t reg_addr) {
    // 【第一帧】：发送目标寄存器地址
    uint8_t tx1[2] = { (reg_addr >> 8) & 0xFF, reg_addr & 0xFF }; // MSB 先行
    uint8_t rx1[2] = { 0 };
    struct spi_ioc_transfer tr1 = {
        .tx_buf = (unsigned long)tx1,//16 bit 数据--目标寄存器地址
        .rx_buf = (unsigned long)rx1,
        .len = 2,
        .delay_usecs = 1,     // 保证 CS 能够拉高           
        .speed_hz = 5000000,  // 5MHz
        .bits_per_word = 8,
    }; 
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr1);//发送  执行包含一个 transfer 的 SPI 消息
    //spi 发送接收是同步的，  所以说的是执行一个spi消息
    
    // 【第二帧】：发送哑元 (0x0000) 并接收真实数据----dummy数据发送产生时序---并接收sensor真实数据
    uint8_t tx2[2] = { 0x00, 0x00 };
    uint8_t rx2[2] = { 0 };
    struct spi_ioc_transfer tr2 = {
        .tx_buf = (unsigned long)tx2,
        .rx_buf = (unsigned long)rx2,
        .len = 2,
        .delay_usecs = 1,
        .speed_hz = 5000000,
        .bits_per_word = 8,
    };
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr2);//（本质上其实是接收--发送的是dummy数据）发送  执行包含一个 transfer 的 SPI 消息

    // 拼接接收到的数据
    uint16_t rx_data = (rx2[0] << 8) | rx2[1];
    
    // 屏蔽最高两位的状态位/校验位，返回低 14 位纯数据 (0 ~ 16383)
    return rx_data & 0x3FFF;
}

/* ================== 主程序 ================== */

int main(void) {
    if (AS5047P_Init() != 0) {
        return -1;
    }

    printf("Starting AS5047P Diagnostic Monitor...\n");
    printf("======================================\n");

    while(1) {
        // 1. 读取并解析错误标志寄存器 (ERRFL)
        uint16_t errfl = as5047p_read_register(AS5047P_REG_ERRFL);
        uint8_t parity_err  = (errfl & 0x0004) >> 2;
        uint8_t invcmd_err  = (errfl & 0x0002) >> 1;
        uint8_t framing_err = (errfl & 0x0001);
        
            // 时刻读取错误标志寄存器，检查是否有错误
        printf("[ERRFL] Raw: 0x%04X | ParityErr: %d, InvCmd: %d, FramingErr: %d\n",
               errfl, parity_err, invcmd_err, framing_err);

        // 2. 读取并解析诊断与增益寄存器 (DIAAGC)
        uint16_t diaagc = as5047p_read_register(AS5047P_REG_DIAAGC);
        uint8_t agc_value = diaagc & 0x00FF;         // 自动增益控制值 (0-255)
        uint8_t magl      = (diaagc >> 11) & 0x01;   // 1代表磁场太弱 (距离太远)
        uint8_t magh      = (diaagc >> 10) & 0x01;   // 1代表磁场太强 (距离太近)

        printf("[DIAAGC] Raw: 0x%04X | AGC: %3d | MAGL(Weak): %d, MAGH(Strong): %d\n",
               diaagc, agc_value, magl, magh);

        // 3. 读取并转换未补偿角度 (ANGLEUNC)
        uint16_t angleUNC = as5047p_read_register(AS5047P_REG_ANGLEUNC);
        float deg_unc = ((float)angleUNC / 16384.0f) * 360.0f;
        printf("[ANGLE_UNC] Raw: %5d | Deg: %6.2f°\n", angleUNC, deg_unc);

        // 4. 读取并转换已补偿角度 (ANGLECOM) - 实际应用中推荐看这个
        uint16_t angleCOM = as5047p_read_register(AS5047P_REG_ANGLECOM);
        float deg_com = ((float)angleCOM / 16384.0f) * 360.0f;
        printf("[ANGLE_COM] Raw: %5d | Deg: %6.2f°\n", angleCOM, deg_com);

        printf("--------------------------------------\n");

        // 每隔 0.5 秒刷新一次，方便你转动磁铁观察数值变化
        usleep(500000); 
    }

    close(spi_fd);
    return 0;
}
