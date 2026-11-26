
#ifndef ENCODER_CALC_H
#define ENCODER_CALC_H

#include <stdint.h>

/* AS5047P 分辨率：14 bit */
#define ENCODER_CPR 16384
// 滤波时间常数：0.2s
#define FILTER_TAU          0.2

/**
 * @brief 编码器状态结构体。
 *
 * raw_angle：本次单圈原始角度。
 * last_raw：上一次原始角度。
 * relative_counts：从第一次采样开始累计的相对位移。
 * last_time_ns：上一次采样时间，单位ns。
 * angle_deg：当前单圈角度，范围0～360度。
 * relative_deg：累计相对角度，可以超过360度。
 * rpm：当前估算转速，允许正负。
 * initialized：是否已经完成首次采样。
 */
typedef struct {
    uint16_t raw_angle;
    uint16_t last_raw;

    int64_t relative_counts;
    uint64_t last_time_ns;

    double angle_deg;
    double relative_deg;
    double rpm;

    int initialized;
} encoder_state_t;

/**
 * @brief 初始化编码器状态。
 * @param state 编码器状态结构体。
 * @return 无。
 */
void encoder_init(encoder_state_t *state);

/**
 * @brief 一阶低通滤波器。
 * @param input 本次原始转速。
 * @param last_output 上一次滤波结果。
 * @param dt_s 实际采样间隔，单位秒。
 * @return 本次滤波后的转速。
 */
double rpm_filter(double input, double last_output, double dt_s);


/**
 * @brief 更新编码器角度和转速。
 * @param state 编码器状态结构体。
 * @param raw 本次读取到的14位编码值。
 * @param now_ns 本次采样时间，单位ns。
 * @return 1表示计算完成；
 *         0表示首次采样初始化成功；
 *        -1表示参数错误；
 *        -2表示时间无效；
 *        -3表示恰好半圈，无法确定方向。
 */
int encoder_update(encoder_state_t *state, uint16_t raw, uint64_t now_ns);

#endif
