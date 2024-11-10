/*
 * SI5351 driver.
 *
 * Copyright (c) David Knell 2024.
 * Licensed under the CC-BY-NC 4.0 license - text at https://creativecommons.org/licenses/by-nc/4.0/legalcode.en
 * For all enquiries, please contact the author at david.knell@gmail.com
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SI5351_CLOCKS (8)
#define SI5351_PLLS (2)
#define SI5351_MIN_FREQ (8000)
#define SI5351_MAX_FREQ (150000000)
#define SI5351_VCO_MIN (600000000)
#define SI5351_VCO_MAX (900000000)
#define SI5351_CLOCK_ALL (-1)


// Register layout
enum {
  SI5351_REGISTER_0_DEVICE_STATUS = 0,
  SI5351_REGISTER_1_INTERRUPT_STATUS_STICKY = 1,
  SI5351_REGISTER_2_INTERRUPT_STATUS_MASK = 2,
  SI5351_REG_OE = 3,
  SI5351_REGISTER_9_OEB_PIN_ENABLE_CONTROL = 9,
  SI5351_REGISTER_15_PLL_INPUT_SOURCE = 15,
  SI5351_REG_CLK0_CONTROL = 16,
  SI5351_REG_CLK3_0_DISABLE_STATE = 24,
  SI5351_REG_CLK7_4_DISABLE_STATE = 25,
  SI5351_REG_MSN_PLL_BASE = 26,
  SI5351_REG_CLK_SYNTH_BASE = 42,
  SI5351_REG_PHASE_BASE = 165,
  SI5351_REG_CRYSTAL_INTERNAL_LOAD_CAPACITANCE = 183
};

typedef enum {
  SI5351_DISABLE_LOW = 0,
  SI5351_DISABLE_HIGH,
  SI5351_DISABLE_TRISTATE,
  SI5351_DISABLE_NEVER
} si5351_disable_t;

typedef enum {
  SI5351_PLL_A = 0,
  SI5351_PLL_B,
} si5351_PLL_t;

typedef enum {
  SI5351_CRYSTAL_LOAD_6PF = (1 << 6),
  SI5351_CRYSTAL_LOAD_8PF = (2 << 6),
  SI5351_CRYSTAL_LOAD_10PF = (3 << 6)
} si5351_crystal_load_t;

typedef enum {
  SI5351_CRYSTAL_FREQ_25MHZ = (25000000),
  SI5351_CRYSTAL_FREQ_27MHZ = (27000000)
} si5351_crystal_frequency_t;

typedef enum {
  SI5351_CLK_DRV_2MA = 0,
  SI5351_CLK_DRV_4MA = 1,
  SI5351_CLK_DRV_6MA = 2,
  SI5351_CLK_DRV_8MA = 3
} si5351_clock_drive_t;

typedef struct si5351_t {
  void *dev;
  uint32_t crystal_freq;
  si5351_crystal_load_t crystal_load;
  uint32_t freq[SI5351_CLOCKS];             // Clock frequency
  uint32_t phase[SI5351_CLOCKS];            // Clock phase
  si5351_PLL_t pll[SI5351_CLOCKS];           // Which PLL we use to derive this clock frequency
  uint8_t clk_pll[SI5351_PLLS];             // Which of the output clocks we use to derive this PLL frequency
  si5351_disable_t clk_dis[SI5351_CLOCKS];  // Output state when disabled
  uint8_t clk_invert[SI5351_CLOCKS];        // Invert clock output
  si5351_clock_drive_t clk_drive[SI5351_CLOCKS];  // Clock drive current
  uint32_t vco_freq[SI5351_PLLS];           // Calculated VCO frequency
  bool config;
  int (*write)(void *dev, uint8_t reg, uint8_t val);
  void (*log)(const char *fmt, ...);
} si5351_t;

void si5351_init(si5351_t *si5351, void *dev, uint32_t crystalFreq, si5351_crystal_load_t crystalLoad, 
                  int (*write)(void *dev, uint8_t reg, uint8_t val), void (*log)(const char *fmt, ...));
int si5351_set(si5351_t *si5351, uint8_t
 output, si5351_PLL_t pll, uint32_t freq, uint32_t phase, bool invert, bool pll_master);


