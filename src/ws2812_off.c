#include "ws2812_off.h"

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "ws2812.pio.h"

void ws2812_drive_black(uint pin) {
    PIO pio = pio0;
    const uint state_machine = 0u;
    const uint offset = pio_add_program(pio, &ws2812_program);

    pio_sm_config config = ws2812_program_get_default_config(offset);
    sm_config_set_sideset_pins(&config, pin);
    sm_config_set_out_shift(&config, false, true, 24u);
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);

    const int cycles_per_bit = ws2812_T1 + ws2812_T2 + ws2812_T3;
    const float divider = (float)clock_get_hz(clk_sys) / (800000.0f * cycles_per_bit);
    sm_config_set_clkdiv(&config, divider);

    pio_gpio_init(pio, pin);
    pio_sm_set_consecutive_pindirs(pio, state_machine, pin, 1u, true);
    pio_sm_init(pio, state_machine, offset, &config);
    pio_sm_set_enabled(pio, state_machine, true);
    pio_sm_put_blocking(pio, state_machine, 0u);
    sleep_us(100u);
}
