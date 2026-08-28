#ifndef LUNAR_SERIAL_PROTOCOL_H
#define LUNAR_SERIAL_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "tsl2591.h"

void serial_protocol_init(void);
void serial_protocol_task(const tsl2591_t *sensor, uint32_t now_ms);
bool serial_protocol_bootloader_due(uint32_t now_ms);

#endif
