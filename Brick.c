// =============================================================================
/*
 * Brick - CDTV Joystick to IR Transmitter
 * Copyright (c) 2025 Korinel
 * SPDX-License-Identifier: MIT
 */
// =============================================================================

#include "pico/stdlib.h"     // gpio, sleep_ms, stdio_init_all, panic
#include "pico/time.h"       // repeating_timer_t, add_repeating_timer_us
#include "hardware/clocks.h" // clock_get_hz, clk_sys
#include "hardware/pwm.h"    // pwm APIs
#include "hardware/sync.h"   // __wfi()
#include <stdint.h>          // fixed-width integer types

// =============================================================================
// GPIO Pin Definitions
// =============================================================================

// GPIO Input Pin Definitions (Active LOW inputs)
#define JOY1_UP 2
#define JOY1_DOWN 3
#define JOY1_LEFT 4
#define JOY1_RIGHT 5
#define JOY1_FIRE2 6
#define JOY1_FIRE1 7

#define JOY2_UP 10
#define JOY2_DOWN 11
#define JOY2_LEFT 12
#define JOY2_RIGHT 13
#define JOY2_FIRE2 14
#define JOY2_FIRE1 15

// GPIO Output Pin Definitions (Active HIGH outputs)
#define LED_IR 16
#define LED_PICO 25

// =============================================================================
// IR Protocol Definitions
// =============================================================================

static const uint32_t ir_frequency_hz = 40000;          // 40 kHz carrier = 25us period
static const uint8_t ir_duty_cycle_percent = 33;        // 33% duty (CD1252 mouse is 33% too)
static const uint16_t data_mask = 0x0FFF;               // 12 bits valid data

/* CDTV IR joystick protocol timings (microseconds) */
static const uint32_t ir_hdr_mark_us = 1100;            // 44 * 25us
static const uint32_t ir_hdr_space_us = 375;            // 15 * 25us
static const uint32_t ir_zero_mark_us = 150;            //  6 * 25us
static const uint32_t ir_zero_space_us = 725;           // 29 * 25us
static const uint32_t ir_one_mark_us = 500;             // 20 * 25us
static const uint32_t ir_one_space_us = 375;            // 15 * 25us                
static const uint32_t ir_interframe_gap_us = 800;       // 32 * 25us

// =============================================================================
// GPIO and state definitions
// =============================================================================
// Read joystick GPIO pins in CDTV defined order
static const uint8_t gpio_pins[] = {
    JOY2_UP, JOY2_DOWN, JOY2_LEFT, JOY2_RIGHT, JOY2_FIRE1, JOY2_FIRE2,
    JOY1_UP, JOY1_DOWN, JOY1_LEFT, JOY1_RIGHT, JOY1_FIRE1, JOY1_FIRE2
   };

static const size_t num_gpio_pins = sizeof(gpio_pins) / sizeof(gpio_pins[0]);

// PWM state
static uint32_t pwm_slice_num = 0;
static uint32_t pwm_channel = 0;
static uint16_t pwm_level = 0;

// ISR / timer state
static volatile bool frame_pending = false; // set by timer IRQ, cleared in main loop
static const uint32_t max_idle_frames = 4;  // consecutive idle frames 
static uint32_t idle_frame_count = 0;       // counts consecutive idle frames

// Frame length in microseconds = ~24150 us
static const uint64_t ir_frame_period_us =
    (int64_t)ir_hdr_mark_us + ir_hdr_space_us +
    (13LL * (ir_zero_mark_us + ir_zero_space_us)) +
    (12LL * (ir_one_mark_us + ir_one_space_us)) +
    ir_interframe_gap_us;

static repeating_timer_t frame_timer; // frame timer handle

// =============================================================================
// Initialization
// =============================================================================
static void init_gpio(void)
{
    // Configure joystick inputs as pulled-up (active LOW)
    for (size_t i = 0; i < num_gpio_pins; ++i)
    {
        int pin = gpio_pins[i];
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_up(pin);
    }

    // Configure LED for feedback
    gpio_init(LED_PICO);
    gpio_set_dir(LED_PICO, GPIO_OUT);
    gpio_put(LED_PICO, 0);
}

static void init_pwm_for_ir(void)
{
    // configure pin for PWM and capture slice/channel
    gpio_set_function(LED_IR, GPIO_FUNC_PWM);
    pwm_slice_num = pwm_gpio_to_slice_num(LED_IR);
    pwm_channel = pwm_gpio_to_channel(LED_IR);

    // compute wrap (ticks per carrier period) and duty in ticks
    uint32_t sys_clk_freq = clock_get_hz(clk_sys);

    uint16_t pwm_wrap = (uint16_t)((sys_clk_freq / (ir_frequency_hz)) - 1);
    pwm_level = (pwm_wrap * ir_duty_cycle_percent) / 100; // IR_DUTY_CYCLE is the percentage

    // initialise PWM with required wrap and start it, keep output off
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, pwm_wrap);

    pwm_init(pwm_slice_num, &config, true);
    pwm_set_chan_level(pwm_slice_num, pwm_channel, 0);
}

// =============================================================================
// IR transmission logic
// =============================================================================
static inline void ir_emit(uint32_t mark_us, uint32_t space_us)
{
    // Turn carrier on for mark_us then off for space_us using PWM level
    pwm_set_chan_level(pwm_slice_num, pwm_channel, pwm_level);
    busy_wait_us_32(mark_us);

    pwm_set_chan_level(pwm_slice_num, pwm_channel, 0);
    busy_wait_us_32(space_us);
}

static void transmit_cdtv_frame(uint16_t joy_data)
{
    // Ensure only the lower 12 bits are used
    uint16_t check_bits = (uint16_t)(~joy_data) & data_mask;

    // Header
    ir_emit(ir_hdr_mark_us, ir_hdr_space_us);

    // Joystick identifier bit (0)
    ir_emit(ir_zero_mark_us, ir_zero_space_us);

    // Send 12 data bits (MSB first)
    for (size_t i = num_gpio_pins; i-- > 0;)
    {
        if (joy_data & (1u << (unsigned)i))
            ir_emit(ir_one_mark_us, ir_one_space_us);
        else
            ir_emit(ir_zero_mark_us, ir_zero_space_us);
    }

    // Send 12 inverted check bits (MSB first)
    for (size_t i = num_gpio_pins; i-- > 0;)
    {
        if (check_bits & (1u << (unsigned)i))
            ir_emit(ir_one_mark_us, ir_one_space_us);
        else
            ir_emit(ir_zero_mark_us, ir_zero_space_us);
    }

    // carrier already off; inter-frame gap is provided by the repeating timer
    // so do not send the footer here.
}

// =============================================================================
// Joystick input reading
// =============================================================================
static inline uint16_t read_joystick_inputs(void)
{
    uint16_t joystick_bits = 0;

    // Snapshot all GPIO inputs once (SIO->GPIO_IN), invert because inputs are active LOW
    uint32_t all_gpio_state = ~gpio_get_all();
    for (size_t i = 0; i < num_gpio_pins; ++i)
    {
        if (all_gpio_state & (1u << gpio_pins[i]))
            joystick_bits |= 1u << i;
    }
    return joystick_bits;
}

// =============================================================================
// Callbacks
// =============================================================================
static bool frame_timer_callback(repeating_timer_t *t)
{
    (void)t;
    frame_pending = true;
    return true;
}

// =============================================================================
// Main - Entry point
// =============================================================================
int main(void)
{
    init_gpio();
    init_pwm_for_ir();

    // Start repeating frame timer and check success
    if (!add_repeating_timer_us(-ir_frame_period_us, frame_timer_callback, NULL, &frame_timer))
    {
        panic("Failed to initialize frame timer");
    }

    // Flash LED to show initialization
    gpio_put(LED_PICO, 1);
    sleep_ms(500);
    gpio_put(LED_PICO, 0);

    while (true)
    {
        __wfi(); // wait for timer tick / IRQ

        if (frame_pending)
        {
            frame_pending = false;

            uint16_t joy_data = read_joystick_inputs();

            if (joy_data == 0)
            {
                ++idle_frame_count;
            }
            else
            {
                idle_frame_count = 0;
            }

            // if idle_threshold is reached do NOT transmit further idle frames
            if (idle_frame_count < max_idle_frames)
            {
                transmit_cdtv_frame(joy_data);
            }
        }
    }
}
// =============================================================================
// END OF FILE
// =============================================================================