
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <poll.h>

#include "protocol.h"

const uint8_t payload[] = "HELLO-RK\r\n";

uint8_t tx_frame[PROTO_MAX_FRAME_LEN];

//// 配置串口：115200，8N1，无流控
static int uart_config(int fd)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0)
        return -1;

    cfmakeraw(&tty);

    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);

    tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE);

#ifdef CRTSCTS
    tty.c_cflag &= ~CRTSCTS;
#endif

    tty.c_cflag |= CS8 | CLOCAL | CREAD;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) < 0)
        return -1;

    tcflush(fd, TCIOFLUSH);

    return 0;
}

// 完整发送，处理部分写入
static int uart_write_all(int fd,
                          const uint8_t *data,
                          size_t len)
{
    size_t sent = 0;

    while (sent < len)
    {
        ssize_t n = write(fd, data + sent, len - sent);
        //根据fd进行写入uart  fd是串口设备的文件描述符
        // 处理部分写入
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

    printf("UART device: %s\n", device);

    // 1. 打开串口设备
    int fd = open(device, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror("open UART");
        return 1;
    }

    // 2. 配置串口--115200，8N1，无流控
    if (uart_config(fd) < 0)
    {
        perror("uart_config");
        close(fd);
        return 1;
    }

    // 3. 准备发送数据
    const char tx[] = "PING_FROM_RK\r\n";
    const size_t tx_len = sizeof(tx) - 1;

    tcflush(fd, TCIFLUSH);// 清空接收缓冲区

    printf("TX: %s", tx);

    // 4. 发送数据

    int tx_len = proto_encode(
        CMD_PING,
        1,
        payload,    // 数据
        sizeof(payload) - 1,
        tx_frame,
        sizeof(tx_frame)
    );// 编码数据  生成数据帧

    if (tx_len < 0)
    {
        perror("proto_encode");
        return 1;
    }

    if (uart_write_all(
            fd,
            tx_frame,
            (size_t)tx_len) < 0)
    {
        perror("UART write");
        return 1;
    }

    tcdrain(fd);
    // if (uart_write_all(fd,
    //                    (const uint8_t *)tx,
    //                    tx_len) < 0)// 发送数据 根据serial设备 fd 进行文件写入数据
    // {
    //     perror("write");
    //     close(fd);
    //     return 1;
    // }

    // tcdrain(fd);// 等待发送完成--drain 疏通之后 才写入完成

    // 5. 等待 STM32 回显
    uint8_t rx[128];
    size_t received = 0;// 接收到的数据数量

    while (received < tx_len)// 未接收完成
    {
        struct pollfd pfd = {
            .fd = fd,           // 只监听串口设备  s6
            .events = POLLIN    // 监听可读事件         
        };

        int ret = poll(&pfd, 1, 2000);// 监听串口设备  s6  2s超时
        //poll 告诉我们有数据可以读取（针对的是fd 事件为in  Poll INput）

        if (ret == 0)
        {
            printf("RX timeout: %zu/%zu bytes\n",
                   received, tx_len);
            break;
        }

        if (ret < 0)
        {
            if (errno == EINTR)
                continue;

            perror("poll");
            break;
        }

        //poll 返回值大于0 有了pollin event 表示有数据可以读取--才进行读取操作
        ssize_t n = read(fd,
                         rx + received,
                         sizeof(rx) - received);// 从串口设备读取数据

        if (n > 0)
            received += (size_t)n;      //读取数据数量增加
    }

    // 6. 打印接收到的数据
    printf("RX: ");
    fwrite(rx, 1, received, stdout);

    if (received != tx_len ||
        memcmp(tx, rx, tx_len) != 0)
    {
        printf("\nUART COMMUNICATION FAIL!\n");
        close(fd);
        return 1;
    }
    printf("\nUART COMMUNICATION PASS!\n");
    close(fd);// 关闭串口设备

    return 0;
}

