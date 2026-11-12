
#include "protocol.h"
#include <string.h>

/* ===============================
 * 1. CRC16/MODBUS
 * =============================== */

uint16_t proto_crc16_modbus(
    const uint8_t *data,
    size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];

        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }

    return crc;
}

/* ===============================
 * 2. 数据帧打包
 * =============================== */
/**
 * @brief 打包数据帧
 * @param cmd 命令字
 * @param seq 序号
 * @param data 数据指针
 * @param data_len 数据长度---决定数据帧的总长度
 * @param out 输出指针
 * @param out_capacity 输出缓冲区容量
 * @return int 打包后的字节数，-1 错误
 */
int proto_encode(
    uint8_t cmd,
    uint8_t seq,
    const uint8_t *data,
    uint8_t data_len,
    uint8_t *out,
    size_t out_capacity)
{
    if (out == NULL ||
        data_len > PROTO_MAX_DATA_LEN ||
        (data_len > 0 && data == NULL))
    {
        return -1;
    }

    uint8_t len = 2 + data_len;// 数据帧长度 2字节长度+数据长度

    size_t total_len = len + 5;

    if (out_capacity < total_len)
        return -1;

    /* 帧头 */
    out[0] = PROTO_SOF1;
    out[1] = PROTO_SOF2;

    /* 长度 */
    out[2] = len;

    /* 命令 */
    out[3] = cmd;

    /* 序号 */
    out[4] = seq;

    /* 数据区 */
    if (data_len > 0)
    {
        memcpy(&out[5], data, data_len);
    }

    /* CRC：从 LEN 到 DATA */
    uint16_t crc = proto_crc16_modbus(
        &out[2],
        len + 1
    );

    /* CRC 低字节在前 */
    out[3 + len] = crc & 0xFF;
    out[4 + len] = (crc >> 8) & 0xFF;

    return (int)total_len;
}

/* ===============================
 * 3. 初始化解析器
 * =============================== */
/**
 * @brief 重置解析器 内部的索引、预期总长度、状态等
 * @param parser 解析器指针
 */
static void parser_reset(proto_parser_t *parser)
{
    parser->index = 0;
    parser->expected_total = 0;
    parser->state = 0;
}

void proto_parser_init(proto_parser_t *parser)
{
    memset(parser, 0, sizeof(*parser));
}

/* ===============================
 * 4. 字节流解析
 * =============================== */
/**
 * @brief 输入一个字节，尝试解析一帧
 * @param parser 解析器指针--存的是接收到的未解析的字节流
 * @param byte 待解析的字节
 * @param out proto_frame_t 解析后的数据帧指针
 * @return int 0 成功，-1 错误，-2 数据帧长度错误，-3 解析器指针或数据帧指针为空
 */
int proto_parser_push(    proto_parser_t *parser,    uint8_t byte,    proto_frame_t *out)
{
    if (parser == NULL || out == NULL)
        return -3;

    switch (parser->state)// 解析器状态
    {
        /* 等待第一个帧头 */
        case 0:

            if (byte == PROTO_SOF1)
            {
                parser->buffer[0] = byte;
                parser->index = 1;// 记录当前字节在buffer中的索引（未写入的下一位），0是SOF1
                parser->state = 1;
            }

            break;

        /* 等待第二个帧头 */
        case 1:

            if (byte == PROTO_SOF2)
            {
                parser->buffer[1] = byte;
                parser->index = 2;// 记录当前字节在buffer中的索引，1是SOF2
                parser->state = 2;
            }
            else if (byte == PROTO_SOF1)
            {
                parser->buffer[0] = byte;
                parser->index = 1;
            }
            else
            {
                parser_reset(parser);
            }

            break;

        /* 读取长度 */
        case 2://等待长度字节  len

            if (byte < 2 ||
                byte > PROTO_MAX_BODY_LEN)
            {
                parser_reset(parser);

                if (byte == PROTO_SOF1)
                {
                    parser->buffer[0] = byte;
                    parser->index = 1;
                    parser->state = 1;
                }

                return -2;
            }

            parser->buffer[2] = byte;// 前两个是startofframe  第三个也就是这个byte=len

            parser->expected_total = byte + 5; // 数据帧总长度 2字节帧头+len+数据长度+2字节CRC

            parser->index = 3;//
            parser->state = 3;// 接收剩余数据和 CRC 状态 

            break;

        /* 接收剩余数据和 CRC */
        case 3:

            if (parser->index >= PROTO_MAX_FRAME_LEN)
            {
                parser_reset(parser);
                return -2;
            }

            parser->buffer[parser->index++] = byte;//持续接收数据，直到收到CRC字节

            // >=  检查是否接收完整数据帧
            if (parser->index >= parser->expected_total)
            {
                uint8_t len = parser->buffer[2];// 数据帧长度 len

                uint16_t crc_calc =
                    proto_crc16_modbus(
                        &parser->buffer[2],
                        len + 1
                    );// 计算CRC

                // 收到的CRC数据  低字节在前
                uint16_t crc_recv =
                    parser->buffer[parser->expected_total - 2] |
                    ((uint16_t)parser->buffer[parser->expected_total - 1] << 8);

                if (crc_calc != crc_recv)
                {
                    parser_reset(parser);
                    return -1;
                }

                /* CRC 正确，提取数据 */
                // 将parser未经过解析的字节流中的数据提取出来，赋值给out中
                out->cmd = parser->buffer[3];
                out->seq = parser->buffer[4];

                out->data_len = len - 2;//去掉cmd和seq字节

                /* 之前的parser解析器中只是将byte存储在parser中，没有解析数据 
                   后面这里则是将parser中的数据提取出来，赋值给out中
                */
                if (out->data_len > 0)
                {
                    memcpy(
                        out->data,
                        &parser->buffer[5],//2字节帧头+len+cmd+seq  -5字节跳过
                        out->data_len
                    );
                }

                parser_reset(parser);

                return 1;
            }

            break;

        default:

            parser_reset(parser);
            return -2;
    }

    return 0;
}
