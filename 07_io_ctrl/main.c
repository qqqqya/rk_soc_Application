
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>


// #include <stdio.h>
// #include <string.h>
#include <stdlib.h>

#include <time.h>
#include <gpiod.h>
/**
 * @brief 向 Linux sysfs 属性文件（userled）写入字符串。
 * @param path 属性文件的完整路径。
 * @param value 要写入的字符串，例如 "1\n"。
 * @return 成功返回0，失败返回-1。
 *
 * @note sysfs 属性由内核驱动提供，并非普通磁盘文件。
 *       write() 会触发对应驱动的属性设置处理。
 */
static int write_sysfs(const char *path, const char *value){
    //写入一次--执行一次完整的 open & close 操作
    // 以只写方式打开 LED 属性文件
    int fd = open(path, O_WRONLY);

    if (fd < 0){
        perror("open sysfs");
        return -1;
    }

    // 计算需要写入的字符数量
    size_t len = strlen(value);

    // 写入指定的亮度或触发方式
    ssize_t written = write(fd, value, len);

    if (written != (ssize_t)len){
        perror("write sysfs");//写入失败
        close(fd);
        return -1;
    }

    // 关闭文件描述符
    close(fd);

    return 0;
}

/**
 * @brief 控制 RK3576 板载 USER LED 闪烁。
 * @param void 无。
 * @return 成功返回0，失败返回1。
 */
int main(void){
    //板载 USER LED 属性文件
    const char *trigger = "/sys/class/leds/user/trigger";
    const char *brightness = "/sys/class/leds/user/brightness";

    // 第一步：关闭 LED 自动触发功能
    if (write_sysfs(trigger, "none\n") < 0){//写入none(event)关闭自动触发功能
        return 1;
    }

    // 第二步：循环闪烁5次
    for (int i = 0; i < 3; i++){
        // 设置亮度为1，点亮LED
        if (write_sysfs(brightness, "1\n") < 0){//写入1点亮LED
            return 1;
        }

        printf("LED ON  %d\n", i + 1);
        sleep(1);

        // 设置亮度为0，熄灭LED
        if (write_sysfs(brightness, "0\n") < 0){
            return 1;
        }
//平时终端中可以cat /sys/class/leds/user/brightness 查看当前亮度
        printf("LED OFF %d\n", i + 1);
        sleep(1);
    }

    printf("GPIO LED experiment finished.\n");



    const char *chipname = "/dev/gpiochip4";
    unsigned int line_num = 0;
    const char *consumer = "edge-detector";//consumer 消费者名称

    // 1. 打开GPIO芯片                _by_name
    struct gpiod_chip *chip = gpiod_chip_open(chipname);
    if (!chip) {
        perror("gpiod_chip_open_by_name");//byname 舍弃
        return EXIT_FAILURE;
    }

    // 2. 获取指定的GPIO线
    struct gpiod_line *line = gpiod_chip_get_line(chip, line_num);

        // 3. 请求监听“双沿”（上升沿和下降沿）事件
    // 这一步会将引脚自动配置为输入模式
    if (gpiod_line_request_both_edges_events(line, consumer) < 0) {
        perror("gpiod_line_request_both_edges_events");
        gpiod_chip_close(chip);
        return EXIT_FAILURE;
    }

     // 4. 循环等待并读取事件
    while (1) {
        // 等待事件发生（这里设置超时为NULL，即无限等待）
        if (gpiod_line_event_wait(line, NULL) < 0) {
            perror("gpiod_line_event_wait");
            break;
        }

        struct gpiod_line_event event;
        // 读取事件信息
        if (gpiod_line_event_read(line, &event) < 0) {//读取指定line的事件
            perror("gpiod_line_event_read");
            break;
        }

        // 5. 根据事件类型进行判断和打印
        const char *edge_type_str;
        if (event.event_type == GPIOD_LINE_EVENT_RISING_EDGE) {
            edge_type_str = "00-上升沿 (Rising Edge)";
        } else if (event.event_type == GPIOD_LINE_EVENT_FALLING_EDGE) {
            edge_type_str = "下降沿 (Falling Edge)";
        } else {
            edge_type_str = "未知事件";
        }
    
        printf("%s\n",  edge_type_str);
    }

    return 0;
}
