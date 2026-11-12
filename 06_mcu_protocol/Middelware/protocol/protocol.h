
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* ===============================
 * 1. 协议参数
 * =============================== */

#define PROTO_SOF1              0xAA
#define PROTO_SOF2              0x55    //帧头

#define PROTO_MAX_DATA_LEN      64      //最大数据长度
#define PROTO_MAX_BODY_LEN      (2 + PROTO_MAX_DATA_LEN)
#define PROTO_MAX_FRAME_LEN     (PROTO_MAX_BODY_LEN + 5)

/* ===============================
 * 2. 命令定义
 * =============================== */

#define CMD_PING                0x01      //Ping命令
#define CMD_PING_ACK            0x81      //Ping ACK命令

#define CMD_LED_SET             0x02      //LED设置命令
#define CMD_MOTOR_SET           0x03

/* ===============================
 * 3. 数据帧结构
 * =============================== */
// 解析完成的数据帧结构
typedef struct
{
    uint8_t cmd;                        //命令
    uint8_t seq;                        //序列号同步

    uint8_t data_len;                   //数据长度
    uint8_t data[PROTO_MAX_DATA_LEN];   //数据字段

} proto_frame_t;

/* ===============================
 * 4. 接收解析器
 * =============================== */
// 接收解析器结构--是接收的数据---用于解析接收的字节流，将字节流转换为数据帧
typedef struct
{
    uint8_t buffer[PROTO_MAX_FRAME_LEN];

    uint16_t index;
    uint16_t expected_total;

    uint8_t state;

} proto_parser_t;

/* ===============================
 * 5. 接口函数
 * =============================== */

/* CRC16 */
uint16_t proto_crc16_modbus(
    const uint8_t *data,
    size_t len
);

/* 数据帧打包 */
int proto_encode(
    uint8_t cmd,
    uint8_t seq,
    const uint8_t *data,
    uint8_t data_len,
    uint8_t *out,
    size_t out_capacity
);

/* 初始化解析器 */
void proto_parser_init(proto_parser_t *parser);

/* 输入一个字节，尝试解析一帧 */
int proto_parser_push(
    proto_parser_t *parser,
    uint8_t byte,
    proto_frame_t *out
);

#endif
