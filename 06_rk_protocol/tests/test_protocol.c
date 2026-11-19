/*
 * Lesson 4.4.1：UART 协议解析器单元测试
 *
 * 说明：
 * 1. 只调用 protocol.h / protocol.c，不打开 /dev/ttyS6，也不依赖 STM32。
 * 2. 使用 proto_encode() 生成真实有效的帧，再按不同方式送入解析器。
 * 3. 函数定义遵循「返回类型 函数名(全部参数){」的书写风格。
 * 4. 兼容之前约定的接口：proto_encode / proto_parser_init / proto_parser_push。
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "protocol.h"

/* 保存测试结果，方便最后统计通过与失败数量。 */
typedef struct {
    unsigned passed;
    unsigned failed;
} test_stats_t;

/* 断言式检查：打印每一项验证结果，但不会在首次失败后直接退出。 */
static void check_result(test_stats_t *stats, const char *name, int condition){
    if (condition) {
        stats->passed++;
        printf("[PASS] %s\n", name);
    } else {
        stats->failed++;
        printf("[FAIL] %s\n", name);
    }
}

/* 打印完整帧的十六进制字节，方便和串口抓包结果对照。 */
static void print_hex(const uint8_t *bytes, size_t length){
    for (size_t i = 0; i < length; i++) {
        printf("%02X%s", bytes[i], (i + 1 == length) ? "\n" : " ");
    }
}

/*
 * 打包、、构造 PING 帧：CMD=0x01、SEQ=传入值、DATA="HELLO"。
 * 返回值为完整帧字节数；返回负数说明打包失败。
 */
static int make_ping(uint8_t seq, uint8_t *buffer, size_t capacity){
    const uint8_t payload[] = {'H', 'E', 'L', 'L', 'O'};
    return proto_encode(CMD_PING, seq, payload, (uint8_t)sizeof(payload), buffer, capacity);
}

/* 检查解码后的消息字段是否与约定的 PING 请求完全一致。 */
static int is_ping(const proto_frame_t *frame, uint8_t seq){
    const uint8_t expected[] = {'H', 'E', 'L', 'L', 'O'};
    return frame->cmd == CMD_PING &&
           frame->seq == seq &&
           frame->data_len == sizeof(expected) &&
           memcmp(frame->data, expected, sizeof(expected)) == 0;
}

/*
 * 就是直接进行解析 parser 按字节模拟 UART 接收：每输入 1 字节就调用一次解析器。
 * 统计量---frames / errors 是输出参数，用来统计成功完整帧与错误帧数量。
 * out 保存最近一次成功解析的完整帧。
 */
static void feed_bytes(proto_parser_t *parser, const uint8_t *data, size_t length, proto_frame_t *out, unsigned *frames, unsigned *errors){
    for (size_t i = 0; i < length; i++) {
        int result = proto_parser_push(parser, data[i], out);// 解析一个字节
        if (result == 1) {
            (*frames)++;
        } else if (result < 0) {
            (*errors)++;
        }
    }
}

/* 测试 1：一帧一次性输入，验证常规解析功能。 */
static void test_complete_frame(test_stats_t *stats){
    uint8_t wire[PROTO_MAX_FRAME_LEN] = {0};
    int length = make_ping(1, wire, sizeof(wire));      //seq传入1
    // makeping中给数组 wire 赋值，返回值为打包后的字节数
    check_result(stats, "正常帧打包成功", length > 0);
    if (length <= 0) return;

    printf("完整帧 HEX: ");
    print_hex(wire, (size_t)length);

    proto_parser_t parser;
    proto_frame_t result = {0};
    unsigned frames = 0, errors = 0;
    proto_parser_init(&parser);
    feed_bytes(&parser, wire, (size_t)length, &result, &frames, &errors);

    check_result(stats, "T.1完整帧只解析成功一次", frames == 1);
    check_result(stats, "完整帧没有解析错误", errors == 0);
    check_result(stats, "完整帧 CMD/SEQ/DATA 正确", is_ping(&result, 1));//判断解析之后的帧是否与预期一致
}

/* 测试 2：把一帧切为 3、4、剩余字节三段，模拟拆包。 */
static void test_fragmented_frame(test_stats_t *stats){
    uint8_t wire[PROTO_MAX_FRAME_LEN] = {0};
    int length = make_ping(2, wire, sizeof(wire));//seq传入2 
    if (length <= 7) {// hello+7  如果按理来说应该是合成帧要加+7
        check_result(stats, "拆包测试打包成功", 0);
        return;
    }

    proto_parser_t parser;
    proto_frame_t result = {0};
    unsigned frames = 0, errors = 0;
    proto_parser_init(&parser);

    feed_bytes(&parser, wire, 3, &result, &frames, &errors);//  内部解析 parser  3byte
    check_result(stats, "T.2---拆包第一段不能提前形成完整帧", frames == 0);

    feed_bytes(&parser, wire + 3, 4, &result, &frames, &errors);
    check_result(stats, "拆包第二段不能提前形成完整帧", frames == 0);

    feed_bytes(&parser, wire + 7, (size_t)length - 7, &result, &frames, &errors);//解析数据的时候只有到CRC才会return1 
    check_result(stats, "拆包第三段完成后恰好得到一帧", frames == 1 && errors == 0);
    check_result(stats, "拆包后消息内容一致  seq=2", is_ping(&result, 2));
}

/* 测试 3：连续输入三帧，不插入空闲间隔，模拟粘包。 */
static void test_concatenated_frames(test_stats_t *stats){
    uint8_t stream[3 * PROTO_MAX_FRAME_LEN] = {0};///////三倍的帧长
    size_t stream_length = 0;
    for (uint8_t seq = 1; seq <= 3; seq++) {
        // 打包三帧 
        int n = make_ping(seq, stream + stream_length, sizeof(stream) - stream_length);//213-0  213-12
        if (n <= 0) {
            check_result(stats, "粘包测试打包成功", 0);
            return;
        }
        stream_length += (size_t)n;// 累加打包后的字节数  +12  +12  +12
    }

    proto_parser_t parser;
    proto_frame_t result = {0};
    unsigned frames = 0, errors = 0;
    int sequence_ok = 1;
    proto_parser_init(&parser);

    for (size_t i = 0; i < stream_length; i++) {
        int ret = proto_parser_push(&parser, stream[i], &result);// 解析一个字节
        if (ret == 1) {
            frames++;
            if (frames > 3 || !is_ping(&result, (uint8_t)frames)) sequence_ok = 0;
        } else if (ret < 0) {
            errors++;
        }
    }

    check_result(stats, "三帧连续输入后解析出三帧", frames == 3 && errors == 0);
    check_result(stats, "粘包情况下 SEQ=1/2/3 顺序正确", sequence_ok);
}

/* 测试 4：破坏 DATA 而不更新 CRC，验证拒绝错误帧和后续恢复。 */
static void test_crc_error_and_recovery(test_stats_t *stats){
    uint8_t bad[PROTO_MAX_FRAME_LEN] = {0};
    uint8_t good[PROTO_MAX_FRAME_LEN] = {0};
    int bad_length = make_ping(4, bad, sizeof(bad));
    int good_length = make_ping(5, good, sizeof(good));
    if (bad_length <= 5 || good_length <= 0) {
        check_result(stats, "CRC 错误测试打包成功", 0);
        return;
    }
    bad[5] ^= 0x01;  /* 修改第一个 DATA 字节；CRC 保持打包时的原值。 */

    proto_parser_t parser;
    proto_frame_t result = {0};
    unsigned frames = 0, errors = 0;
    proto_parser_init(&parser);

    feed_bytes(&parser, bad, (size_t)bad_length, &result, &frames, &errors);    //解析出来错误帧 error++
    check_result(stats, "CRC 错误帧不被接受", frames == 0 && errors == 1);  

    feed_bytes(&parser, good, (size_t)good_length, &result, &frames, &errors);  //解析出来正常帧 frames++
    check_result(stats, "CRC 错误后可解析下一帧", frames == 1 && is_ping(&result, 5));
}

/* 测试 5：先注入非法长度，再发送有效帧，检查状态机复位。 */
static void test_invalid_length_and_recovery(test_stats_t *stats){
    const uint8_t malformed[] = {PROTO_SOF1, PROTO_SOF2, 0xFF};
    uint8_t good[PROTO_MAX_FRAME_LEN] = {0};
    int length = make_ping(6, good, sizeof(good));
    if (length <= 0) {
        check_result(stats, "长度错误测试打包成功", 0);
        return;
    }

    proto_parser_t parser;
    proto_frame_t result = {0};
    unsigned frames = 0, errors = 0;
    proto_parser_init(&parser);

    feed_bytes(&parser, malformed, sizeof(malformed), &result, &frames, &errors);//解析出来  只有三个数据--还有个非法长度ff
    check_result(stats, "非法 LEN 能被拒绝", frames == 0 && errors == 1);

    feed_bytes(&parser, good, (size_t)length, &result, &frames, &errors);
    check_result(stats, "非法 LEN 后可恢复接收", frames == 1 && is_ping(&result, 6));
}

/* 测试 6：验证 64 字节合法最大负载，以及 65 字节被拒绝。 */
static void test_payload_boundary(test_stats_t *stats){
    uint8_t payload[PROTO_MAX_DATA_LEN + 1] = {0};//65字节  超过最大
    uint8_t wire[PROTO_MAX_FRAME_LEN] = {0};
    for (size_t i = 0; i < sizeof(payload); i++) payload[i] = (uint8_t)i;//填充数据

    //测试64字节数据正常编码
    int length = proto_encode(CMD_PING, 7, payload, PROTO_MAX_DATA_LEN, wire, sizeof(wire));//payload 但是长度64Byte
    check_result(stats, "64 字节 DATA 能正常编码", length == PROTO_MAX_FRAME_LEN);

    if (length > 0) {
        proto_parser_t parser;
        proto_frame_t result = {0};
        unsigned frames = 0, errors = 0;
        proto_parser_init(&parser);
        feed_bytes(&parser, wire, (size_t)length, &result, &frames, &errors);//解析到good  wire数组里面
        check_result(stats, "64 字节 DATA 可完整解析", frames == 1 && errors == 0 &&
                     result.data_len == PROTO_MAX_DATA_LEN &&
                     memcmp(result.data, payload, PROTO_MAX_DATA_LEN) == 0);
    }

    //测试65字节数据被拒绝
    int oversized = proto_encode(CMD_PING, 8, payload, PROTO_MAX_DATA_LEN + 1, wire, sizeof(wire));
    check_result(stats, "65 字节 DATA 会被拒绝", oversized < 0);
}

/* 所有测试汇总；退出码非零代表存在失败，便于脚本或 CI 判断。 */
int main(void){
    test_stats_t stats = {0};
    puts("=== Lesson 4.4.1 Protocol Parser Tests ===");
    test_complete_frame(&stats);    //测试完整帧解析成功
    //------------
    test_fragmented_frame(&stats);    //测试分帧解析成功
    test_concatenated_frames(&stats);    //测试粘包解析成功
    //------------
    // test_crc_error_and_recovery(&stats);
    // test_invalid_length_and_recovery(&stats);    //测试长度错误解析成功
    // test_payload_boundary(&stats);
    printf("\nSUMMARY: %u passed, %u failed\n", stats.passed, stats.failed);
    return stats.failed == 0 ? 0 : 1;
}
