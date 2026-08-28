#ifndef LUNAR_TSL2591_H
#define LUNAR_TSL2591_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"

#define TSL2591_FILTER_SIZE 15u

typedef struct {
    i2c_inst_t *i2c;
    uint sda_pin;
    uint scl_pin;
    uint8_t gain_index;
    uint8_t integration_index;
    bool detected;
    bool reading_valid;
    float latest_lux;
    float filtered_lux;
    float samples[TSL2591_FILTER_SIZE];
    uint8_t sample_count;
    uint8_t sample_head;
    uint16_t raw_full;
    uint16_t raw_ir;
    uint32_t last_attempt_ms;
    uint32_t last_success_ms;
    uint32_t error_count;
} tsl2591_t;

void tsl2591_init(tsl2591_t *sensor, i2c_inst_t *i2c, uint sda_pin, uint scl_pin);
void tsl2591_task(tsl2591_t *sensor, uint32_t now_ms);
bool tsl2591_has_fresh_reading(const tsl2591_t *sensor, uint32_t now_ms);
float tsl2591_lux(const tsl2591_t *sensor);
uint32_t tsl2591_reading_age_ms(const tsl2591_t *sensor, uint32_t now_ms);
uint16_t tsl2591_gain_multiplier(const tsl2591_t *sensor);

#endif
