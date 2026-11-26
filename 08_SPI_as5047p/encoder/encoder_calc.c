
#include "encoder_calc.h"
#include <string.h>

/**
 * @brief 初始化编码器状态。
 * @param state 编码器状态结构体。
 * @return 无。
 */
void encoder_init(encoder_state_t *state){
    if (state == NULL){
        return;
    }

    // 清空全部状态变量
    memset(state, 0, sizeof(*state));
}

/**
 * @brief 根据相邻两次绝对角度计算位移和转速。
 * @param state 编码器状态结构体。
 * @param raw 本次14位编码值。
 * @param now_ns CLOCK_MONOTONIC时间戳，单位ns。
 * @return 1表示计算完成；0表示初始化；
 *         负数表示发生错误。
 *
 * @note 相邻两次采样间实际位移必须小于半圈，
 *       否则无法可靠判断跨零点方向。
 */
int encoder_update(encoder_state_t *state, uint16_t raw, uint64_t now_ns){
    if (state == NULL || raw >= ENCODER_CPR){
        return -1;
    }

    // 单圈角度转换
    double angle = (double)raw * 360.0 / ENCODER_CPR;

    /* 第一次采样：
     * 只建立初始角度和时间基准。
     * 由于没有上一次数据，无法计算转速。
     */
    if (!state->initialized){
        state->raw_angle = raw;
        state->last_raw = raw;
        state->last_time_ns = now_ns;

        state->angle_deg = angle;
        state->relative_deg = 0.0;
        state->relative_counts = 0;
        state->rpm = 0.0;

        state->initialized = 1;

        return 0;
    }

    // 计算两次采样间隔
    if (now_ns <= state->last_time_ns){
        return -2;
    }

    uint64_t dt_ns = now_ns - state->last_time_ns;

    // 首先计算未经修正的原始位移
    int32_t delta = (int32_t)raw - (int32_t)state->last_raw;

    // 恰好半圈时，方向无法唯一确定
    if (delta == ENCODER_CPR / 2 ||
        delta == -(ENCODER_CPR / 2)){
        return -3;
    }

    /* 跨零点修正：
     * 如果差值大于半圈，认为发生负向跨零点。
     * 如果差值小于负半圈，认为发生正向跨零点。
     */
    if (delta > ENCODER_CPR / 2){
        delta -= ENCODER_CPR;
    }else if (delta < -(ENCODER_CPR / 2)){
        delta += ENCODER_CPR;
    }

    // 累计相对位移，使用int64_t减少长期溢出风险
    state->relative_counts += delta;

    // 将累计位移转换为相对角度
    state->relative_deg =
        (double)state->relative_counts * 360.0 / ENCODER_CPR;

    /* RPM = 本次增量 / 每圈计数 × 60 / 实际采样秒数
     *
     * dt_ns的单位是纳秒，因此需要乘以1e9。
     */
    state->rpm =
        (double)delta * 60.0 * 1000000000.0 /
        ((double)ENCODER_CPR * (double)dt_ns);

    // 保存本次角度及时间，供下一次计算
    state->raw_angle = raw;
    state->last_raw = raw;
    state->last_time_ns = now_ns;
    state->angle_deg = angle;

    return 1;
}


#if 0 
#include <stdio.h>
#include <stdint.h>

void print_result(const char* desc, int ret, encoder_state_t *state) {
    printf("【测试场景】: %s\n", desc);
    printf("  返回值: %d\n", ret);
    if (ret > 0 || ret == 0) {
        printf("  当前角度: %.2f° | 累计位移: %d | 相对角度: %.2f° | 转速: %.2f RPM\n", 
               state->angle_deg, state->relative_counts, state->relative_deg, state->rpm);
    }
    printf("--------------------------------------------------\n");
}

int main() {
    encoder_state_t state;
    encoder_init(&state);
    
    // 模拟时间戳 (单位: ns)，假设每 10ms (10,000,000 ns) 采样一次
    uint64_t time_base = 1000000000ULL; // 1秒
    uint64_t dt = 10000000ULL;          // 10ms

    // 1. 第一次初始化采样 (raw = 1000)
    int ret = encoder_update(&state, 1000, time_base);
    print_result("第一次上电采样 (初始化)", ret, &state);

    // 2. 正常正向旋转 (raw 从 1000 增加到 1100)
    // 预期位移: +100, RPM = 100 * 60 / (16384 * 0.01) ≈ 36.62
    time_base += dt;
    ret = encoder_update(&state, 1100, time_base);
    print_result("正常正向转动 (+100 counts)", ret, &state);

    // 3. 正常反向旋转 (raw 从 1100 减少到 1050)
    // 预期位移: -50, RPM ≈ -18.31
    time_base += dt;
    ret = encoder_update(&state, 1050, time_base);
    print_result("正常反向转动 (-50 counts)", ret, &state);

    // 4. 正向跨越零点 (raw 从 16380 跳变到 10)
    // 14位最大是 16383。正向跨零实际位移 = 10 - 16380 + 16384 = +14
    time_base += dt;
    ret = encoder_update(&state, 16380, time_base); // 先挪到接近终点
    time_base += dt;
    ret = encoder_update(&state, 10, time_base);    // 再跨越零点
    print_result("正向跨越零点 (16380 -> 10)", ret, &state);

    // 5. 反向跨越零点 (raw 从 10 跳变到 16380)
    // 反向跨零实际位移 = 16380 - 10 - 16384 = -14
    time_base += dt;
    ret = encoder_update(&state, 16380, time_base); // 从 10 反向跨越到 16380
    print_result("反向跨越零点 (10 -> 16380)", ret, &state);

    // 6. 刚好半圈异常测试 (14位半圈是 8192)
    // 预期返回 -3 (无法判断方向)
    time_base += dt;
    ret = encoder_update(&state, 16380 + 8192, time_base); // 16380 + 8192 = 24572, 24572 % 16384 = 8188
    // 注意：上面的算式刚好差一点，为了精确测半圈，这里重置一下状态
    encoder_init(&state);
    encoder_update(&state, 0, time_base);
    time_base += dt;
    ret = encoder_update(&state, 8192, time_base);
    print_result("恰好半圈差异 (0 -> 8192) 【预期返回 -3】", ret, &state);

    // 7. 时间戳倒退测试
    // 预期返回 -2
    time_base -= 5000000ULL;
    ret = encoder_update(&state, 9000, time_base);
    print_result("时间戳倒退 【预期返回 -2】", ret, &state);

    // 8. 非法值测试 (大于等于 ENCODER_CPR)
    // 预期返回 -1
    ret = encoder_update(&state, 20000, time_base);
    print_result("非法值输入 (raw=20000) 【预期返回 -1】", ret, &state);

    return 0;
}
#endif
