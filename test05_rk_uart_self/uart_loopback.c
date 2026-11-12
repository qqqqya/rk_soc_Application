
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

// 配置 Linux UART
static int uart_config(int fd)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0)
    {
        perror("tcgetattr");
        return -1;
    }

    // 设置原始模式：不进行终端字符处理
    cfmakeraw(&tty);

    // 115200 波特率
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    // 8N1
    tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    tty.c_cflag |= CS8 | CLOCAL | CREAD;

    // 禁止硬件流控
#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
#endif

    // 接收超时：1秒
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    if (tcsetattr(fd, TCSANOW, &tty) < 0)
    {
        perror("tcsetattr");
        return -1;
    }

    // 清空收发缓冲区
    tcflush(fd, TCIOFLUSH);

    return 0;
}

// 完整发送数据，处理可能的部分写入
static int uart_write_all(int fd, const uint8_t *data, size_t len)
{
    size_t sent = 0;

    while (sent < len)
    {
        ssize_t n = write(fd, data + sent, len - sent);

        if (n > 0)
            sent += (size_t)n;
        else if (n < 0 && errno == EINTR)
            continue;
        else
            return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *device = "/dev/ttyS6";

    if (argc > 1)
        device = argv[1];

    // 1. 打开串口设备
    int fd = open(device, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror("open UART");
        return 1;
    }

    printf("UART device: %s\n", device);

    // 2. 配置串口
    if (uart_config(fd) < 0)
    {
        close(fd);
        return 1;
    }

    // 3. 准备发送数据
    const char tx_data[] = "RK3576 UART LOOPBACK 123\r\n";
    uint8_t rx_data[sizeof(tx_data)] = {0};

    size_t tx_len = sizeof(tx_data) - 1;

    // 4. 发送
    if (uart_write_all(fd, (const uint8_t *)tx_data, tx_len) < 0)
    {
        perror("write");
        close(fd);
        return 1;
    }

    // 等待发送完成
    tcdrain(fd);

    printf("TX: %s", tx_data);

    // 5. 接收
    size_t rx_len = 0;

    while (rx_len < tx_len)
    {
        ssize_t n = read(fd, rx_data + rx_len, tx_len - rx_len);

        if (n > 0)
        {
            rx_len += (size_t)n;
        }
        else if (n == 0)
        {
            printf("RX timeout: %zu/%zu bytes\n", rx_len, tx_len);
            break;
        }
        else if (errno == EINTR)
        {
            continue;
        }
        else
        {
            perror("read");
            break;
        }
    }

    // 6. 比较发送和接收数据
    printf("RX: %.*s", (int)rx_len, rx_data);

    if (rx_len == tx_len &&
        memcmp(tx_data, rx_data, tx_len) == 0)
    {
        printf("\nUART LOOPBACK PASS!\n");
    }
    else
    {
        printf("\nUART LOOPBACK FAIL!\n");
    }

    close(fd);

    return 0;
}
