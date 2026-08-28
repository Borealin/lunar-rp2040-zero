#include "tsl2591.h"

#include <math.h>
#include <string.h>

#include "hardware/gpio.h"

#define TSL2591_ADDRESS 0x29u
#define TSL2591_COMMAND 0xA0u

#define TSL2591_REG_ENABLE 0x00u
#define TSL2591_REG_CONTROL 0x01u
#define TSL2591_REG_DEVICE_ID 0x12u
#define TSL2591_REG_CHAN0_LOW 0x14u
#define TSL2591_REG_CHAN1_LOW 0x16u

#define TSL2591_ENABLE_POWER_ON 0x01u
#define TSL2591_ENABLE_ALS 0x02u
#define TSL2591_EXPECTED_ID 0x50u

#define TSL2591_SAMPLE_PERIOD_MS 1000u
#define TSL2591_STALE_AFTER_MS 5000u
#define TSL2591_LOW_COUNT 200u
#define TSL2591_HIGH_COUNT 60000u
#define TSL2591_LUX_DF 408.0f
#define TSL2591_I2C_TIMEOUT_US 10000u

static const uint16_t gain_multipliers[] = {1u, 25u, 428u, 9876u};
static const uint8_t gain_register_values[] = {0x00u, 0x10u, 0x20u, 0x30u};

static bool write_reg(const tsl2591_t *sensor, uint8_t reg, uint8_t value) {
    const uint8_t data[] = {(uint8_t)(TSL2591_COMMAND | reg), value};
    return i2c_write_timeout_us(sensor->i2c, TSL2591_ADDRESS, data, sizeof(data), false,
                                TSL2591_I2C_TIMEOUT_US) == (int)sizeof(data);
}

static bool read_reg8(const tsl2591_t *sensor, uint8_t reg, uint8_t *value) {
    const uint8_t command = (uint8_t)(TSL2591_COMMAND | reg);
    if (i2c_write_timeout_us(sensor->i2c, TSL2591_ADDRESS, &command, 1, true,
                             TSL2591_I2C_TIMEOUT_US) != 1) {
        return false;
    }
    return i2c_read_timeout_us(sensor->i2c, TSL2591_ADDRESS, value, 1, false,
                               TSL2591_I2C_TIMEOUT_US) == 1;
}

static bool read_reg16(const tsl2591_t *sensor, uint8_t reg, uint16_t *value) {
    const uint8_t command = (uint8_t)(TSL2591_COMMAND | reg);
    uint8_t data[2];
    if (i2c_write_timeout_us(sensor->i2c, TSL2591_ADDRESS, &command, 1, true,
                             TSL2591_I2C_TIMEOUT_US) != 1) {
        return false;
    }
    if (i2c_read_timeout_us(sensor->i2c, TSL2591_ADDRESS, data, sizeof(data), false,
                            TSL2591_I2C_TIMEOUT_US) != (int)sizeof(data)) {
        return false;
    }
    *value = (uint16_t)data[0] | ((uint16_t)data[1] << 8u);
    return true;
}

static bool apply_configuration(tsl2591_t *sensor) {
    const uint8_t control =
        (uint8_t)(sensor->integration_index | gain_register_values[sensor->gain_index]);
    return write_reg(sensor, TSL2591_REG_ENABLE, 0u) &&
           write_reg(sensor, TSL2591_REG_CONTROL, control) &&
           write_reg(sensor, TSL2591_REG_ENABLE,
                     TSL2591_ENABLE_POWER_ON | TSL2591_ENABLE_ALS);
}

static bool detect_and_configure(tsl2591_t *sensor) {
    uint8_t id = 0u;
    if (!read_reg8(sensor, TSL2591_REG_DEVICE_ID, &id) || id != TSL2591_EXPECTED_ID) {
        sensor->detected = false;
        return false;
    }
    sensor->detected = apply_configuration(sensor);
    return sensor->detected;
}

static float calculate_lux(const tsl2591_t *sensor, uint16_t full, uint16_t ir) {
    if (full == 0u) {
        return 0.0f;
    }
    if (full == UINT16_MAX || ir == UINT16_MAX || ir > full) {
        return NAN;
    }

    const float integration_ms = 100.0f * (float)(sensor->integration_index + 1u);
    const float gain = (float)gain_multipliers[sensor->gain_index];
    const float counts_per_lux = (integration_ms * gain) / TSL2591_LUX_DF;
    const float ratio = (float)ir / (float)full;
    const float lux = ((float)full - (float)ir) * (1.0f - ratio) / counts_per_lux;

    return isfinite(lux) && lux >= 0.0f ? lux : NAN;
}

static void push_sample(tsl2591_t *sensor, float lux) {
    sensor->samples[sensor->sample_head] = lux;
    sensor->sample_head = (uint8_t)((sensor->sample_head + 1u) % TSL2591_FILTER_SIZE);
    if (sensor->sample_count < TSL2591_FILTER_SIZE) {
        sensor->sample_count++;
    }

    float sum = 0.0f;
    for (uint8_t i = 0u; i < sensor->sample_count; ++i) {
        sum += sensor->samples[i];
    }
    sensor->latest_lux = lux;
    sensor->filtered_lux = sum / (float)sensor->sample_count;
    sensor->reading_valid = true;
}

void tsl2591_init(tsl2591_t *sensor, i2c_inst_t *i2c, uint sda_pin, uint scl_pin) {
    memset(sensor, 0, sizeof(*sensor));
    sensor->i2c = i2c;
    sensor->sda_pin = sda_pin;
    sensor->scl_pin = scl_pin;
    sensor->gain_index = 1u;
    sensor->integration_index = 1u;

    i2c_init(i2c, 100000u);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    if (!detect_and_configure(sensor)) {
        sensor->error_count++;
    }
}

void tsl2591_task(tsl2591_t *sensor, uint32_t now_ms) {
    if ((uint32_t)(now_ms - sensor->last_attempt_ms) < TSL2591_SAMPLE_PERIOD_MS) {
        return;
    }
    sensor->last_attempt_ms = now_ms;

    if (!sensor->detected && !detect_and_configure(sensor)) {
        sensor->error_count++;
        sensor->reading_valid = false;
        return;
    }

    uint16_t full = 0u;
    uint16_t ir = 0u;
    if (!read_reg16(sensor, TSL2591_REG_CHAN0_LOW, &full) ||
        !read_reg16(sensor, TSL2591_REG_CHAN1_LOW, &ir)) {
        sensor->error_count++;
        sensor->detected = false;
        if ((uint32_t)(now_ms - sensor->last_success_ms) > TSL2591_STALE_AFTER_MS) {
            sensor->reading_valid = false;
        }
        return;
    }

    sensor->raw_full = full;
    sensor->raw_ir = ir;

    if ((full >= TSL2591_HIGH_COUNT || ir >= TSL2591_HIGH_COUNT) && sensor->gain_index > 0u) {
        sensor->gain_index--;
        if (!apply_configuration(sensor)) {
            sensor->detected = false;
            sensor->error_count++;
        }
        return;
    }

    if (full < TSL2591_LOW_COUNT && sensor->gain_index + 1u <
                                         (sizeof(gain_multipliers) /
                                          sizeof(gain_multipliers[0]))) {
        sensor->gain_index++;
        if (!apply_configuration(sensor)) {
            sensor->detected = false;
            sensor->error_count++;
        }
        return;
    }

    const float lux = calculate_lux(sensor, full, ir);
    if (!isfinite(lux)) {
        sensor->error_count++;
        return;
    }

    push_sample(sensor, lux);
    sensor->last_success_ms = now_ms;
}

bool tsl2591_has_fresh_reading(const tsl2591_t *sensor, uint32_t now_ms) {
    return sensor->detected && sensor->reading_valid &&
           (uint32_t)(now_ms - sensor->last_success_ms) <= TSL2591_STALE_AFTER_MS;
}

float tsl2591_lux(const tsl2591_t *sensor) {
    return sensor->filtered_lux;
}

uint32_t tsl2591_reading_age_ms(const tsl2591_t *sensor, uint32_t now_ms) {
    if (!sensor->reading_valid) {
        return UINT32_MAX;
    }
    return (uint32_t)(now_ms - sensor->last_success_ms);
}

uint16_t tsl2591_gain_multiplier(const tsl2591_t *sensor) {
    return gain_multipliers[sensor->gain_index];
}
