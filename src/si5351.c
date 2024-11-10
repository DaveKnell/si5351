/*
 * SI5351 driver.
 *
 * Copyright (c) David Knell 2024.
 * Licensed under the CC-BY-NC 4.0 license - text at https://creativecommons.org/licenses/by-nc/4.0/legalcode.en
 * For all enquiries, please contact the author at david.knell@gmail.com
 */

#include "../include/si5351.h"
#include <math.h>
#include <string.h>

#define LOG(fmt, ...) if (si5351->log != NULL) si5351->log(fmt, ##__VA_ARGS__)

// Rather nice algorithm to calculate the closest fractional approximation to a real number
// given contstraints on the denominator.
// This is used to calculate the PLL multiplier and dividers for the Si5351

static void farey_fraction(float f, uint32_t max_denominator, uint32_t *num, uint32_t *den)
{
    if (f <= 0 || f >= 1 || max_denominator <= 1)
    {
        *num = 0;
        *den = 1;
        return;
    }

    uint32_t a = 0, b = 1, c = 1, d = 1;
    while (1) {
        uint32_t mediant_num = a + c;
        uint32_t mediant_den = b + d;
        if (mediant_den > max_denominator) {
            break;
        }
        if (f < ((float) mediant_num)/mediant_den) {
            c = mediant_num;
            d = mediant_den;
        } else {
            a = mediant_num;
            b = mediant_den;
        }
    }   

    if (fabs(f - ((float) a)/b) < fabs(f - ((float) c)/d)) {
        *num = a;
        *den = b;
    } else {
        *num = c;
        *den = d;
    }
}

static void si5351_calc_multisynth(si5351_t *si5351, uint32_t f1, uint32_t f2, uint32_t *pll)
{
    LOG("MS: %ld %ld", f1, f2);
    // Calculate the ref->PLL nultiplier and divider
    uint32_t pll_mult = f1 / f2;
    uint32_t pll_num;
    uint32_t pll_den;
    farey_fraction(((float)(f1) / f2) - pll_mult , 1048575, &pll_num, &pll_den);

    LOG("F1: %ld, F2: %ld, PLL Mult: %ld, Num: %ld, Den: %ld", f1, f2, pll_mult, pll_num, pll_den);

    // Calculate PLL parameters
    pll[0] = (pll_mult << 7) + ((128 * pll_num) / pll_den) - 512;
    pll[1] = (pll_num << 7) - (pll_den * ((128 * pll_num) / pll_den));
    pll[2] = pll_den;
    LOG("Multisynth parameters: %08lx %08lx %08lx", pll[0], pll[1], pll[2]);
}

// Configure SI5351 
static int si5351_configure(si5351_t *si5351)
{
    // Check if we're in config mode..
    if (!si5351->config) {
        return 0;
    }

    // Set output disable state
    si5351->write(si5351->dev, SI5351_REG_CLK3_0_DISABLE_STATE, 
        (si5351->clk_dis[3] << 6) | (si5351->clk_dis[2] << 4) | (si5351->clk_dis[1] << 2) | si5351->clk_dis[0]);
    si5351->write(si5351->dev, SI5351_REG_CLK7_4_DISABLE_STATE, 
        (si5351->clk_dis[7] << 6) | (si5351->clk_dis[6] << 4) | (si5351->clk_dis[5] << 2) | si5351->clk_dis[4]);

    // Disable outputs
    si5351->write(si5351->dev, SI5351_REG_OE, 0xFF);

    for (int i=0; i<SI5351_CLOCKS; i++) {
        si5351->write(si5351->dev, SI5351_REG_CLK0_CONTROL + i, 0x80);
    }

    // Set crystal load capacitance
    si5351->write(si5351->dev, SI5351_REG_CRYSTAL_INTERNAL_LOAD_CAPACITANCE, 0x48 | si5351->crystal_load);
    
    // Calculate VCO frequencies
    for (int i=0; i<SI5351_PLLS; i++) {

        // Calculate a sensible VCO frequency - even multiple of target, in the range 600-900MHz
        if (si5351->clk_pll[i] >= SI5351_CLOCKS) {
            LOG("Clock %d out of range for PLL %d", si5351->clk_pll[i], i);
            return -1;
        }
        uint32_t freq = si5351->freq[si5351->clk_pll[i]];
        if ((freq < SI5351_MIN_FREQ) || (freq > SI5351_MAX_FREQ)) {
            LOG("Frequency %lu out of range", freq);
            return -1;
        } 

        uint8_t vco_ri = 0;
        // Get a value for R which gives a pre-R frequency of at least 500kHz - AN619
        while (freq < 500000) {
            freq *= 2;
            vco_ri++;
        }   

        uint32_t omd_div = (uint32_t)((600000000.0/freq) + 3) & ~1;
        if ((omd_div < 8) || (omd_div > 2047)) {
            LOG("Calculated OMD %ld out of range", omd_div);
            return -1;
        }

        uint32_t vco_freq = freq * omd_div;   
        if ((vco_freq < SI5351_VCO_MIN) || (vco_freq > SI5351_VCO_MAX)) {
            LOG("Calculated VCO frequency %lu out of eange", vco_freq);
            return -1;
        }
        si5351->vco_freq[i] = vco_freq;

        uint32_t pll[4];
        si5351_calc_multisynth(si5351, vco_freq, si5351->crystal_freq, pll);

        // Write to PLL registers
        si5351->write(si5351->dev, SI5351_REG_MSN_PLL_BASE + (i * 8), (pll[2] >> 8) & 0xFF);
        si5351->write(si5351->dev, SI5351_REG_MSN_PLL_BASE + (i * 8) + 1, pll[2] & 0xFF);
        si5351->write(si5351->dev, SI5351_REG_MSN_PLL_BASE + (i * 8) + 2, (pll[0] >> 16) & 0x03);
        si5351->write(si5351->dev, SI5351_REG_MSN_PLL_BASE + (i * 8) + 3, (pll[0] >> 8) & 0xFF);
        si5351->write(si5351->dev, SI5351_REG_MSN_PLL_BASE + (i * 8) + 4, pll[0] & 0xFF);
        si5351->write(si5351->dev, SI5351_REG_MSN_PLL_BASE + (i * 8) + 5, (((pll[2] >> 16) & 0x0F) << 4) | ((pll[1] >> 16) & 0x0f));
        si5351->write(si5351->dev, SI5351_REG_MSN_PLL_BASE + (i * 8) + 6, (pll[1] >> 8) & 0xFF);
        si5351->write(si5351->dev, SI5351_REG_MSN_PLL_BASE + (i * 8) + 7, pll[1] & 0xFF);
    }

    // Set clock frequencies
    for (int i=0; i<SI5351_CLOCKS; i++) {
        LOG("Clock %d freq %ld", i, si5351->freq[i]);
        if (si5351->freq[i] == 0) continue;

        if (si5351->pll[i] >= SI5351_PLLS) {
            LOG("PLL %d out of range", si5351->pll[i]);
            return -1;
        }

        uint32_t vco_freq = si5351->vco_freq[si5351->pll[i]];

        uint32_t freq = si5351->freq[i];
        if (freq < SI5351_MIN_FREQ || freq > SI5351_MAX_FREQ) {
            LOG("Frequency %lu out of range", freq);
            return -1;
        }

        // Calculate the divider
        uint32_t omd_div = 0;
        while ((freq < 500000) && (omd_div < 128)) {
            omd_div += 1;
            freq *= 2;
        }

        uint32_t pll[4];
        si5351_calc_multisynth(si5351, vco_freq, freq, pll);

        // Write the multisynth parameters
        si5351->write(si5351->dev, SI5351_REG_CLK_SYNTH_BASE + (i * 8), (pll[2] >> 8) & 0xFF);
        si5351->write(si5351->dev, SI5351_REG_CLK_SYNTH_BASE + (i * 8) + 1, pll[2] & 0xFF);
        si5351->write(si5351->dev, SI5351_REG_CLK_SYNTH_BASE + (i * 8) + 2, (omd_div << 4) | ((pll[0] >> 16) & 0x03));
        si5351->write(si5351->dev, SI5351_REG_CLK_SYNTH_BASE + (i * 8) + 3, (pll[0] >> 8) & 0xFF);
        si5351->write(si5351->dev, SI5351_REG_CLK_SYNTH_BASE + (i * 8) + 4, pll[0] & 0xFF);
        si5351->write(si5351->dev, SI5351_REG_CLK_SYNTH_BASE + (i * 8) + 5, ((pll[2] >> 12) & 0xF0) | ((pll[1] >> 16) & 0x0F));
        si5351->write(si5351->dev, SI5351_REG_CLK_SYNTH_BASE + (i * 8) + 6, (pll[1] >> 8) & 0xFF);
        si5351->write(si5351->dev, SI5351_REG_CLK_SYNTH_BASE + (i * 8) + 7, pll[1] & 0xFF);

        // Set phase offset
        si5351->write(si5351->dev, SI5351_REG_PHASE_BASE + i, si5351->phase[i] & 0x7F);

        // Enable the clock
        si5351->write(si5351->dev, SI5351_REG_CLK0_CONTROL + i, 
            ((si5351->pll[i] << 5) & 0x20) | (si5351->clk_invert[i] ? 0x10: 0) | (0x0C) | (si5351->clk_drive[i] & 0x03)); 
    }

    // Output enables
    uint8_t oe = 0;
    for (int i=0; i<SI5351_CLOCKS; i++) {
        if (si5351->freq[i] == 0) oe |= (1 << i);
    }
    si5351->write(si5351->dev, SI5351_REG_OE, oe);

    return (0);
}

/* 
 * Initialise the Si5351 driver.  Pass in:
 * - a pointer to the si5351_t struct to initialise
 * - a void* which is passed to the write function (usually the I2C handle for the device)
 * - the crystal frequency (25 or 27MHz)
 * - the crystal load capacitance
 * - a function to write to the device - passed the register to write and the value to which to set it
 * - a function to log debug messages.  Can be null in which case no debug messages will be logged
 */

void si5351_init(si5351_t *si5351, void *dev, uint32_t cf, si5351_crystal_load_t cl, 
                    int (*write)(void *dev, uint8_t reg, uint8_t val), void (*log)(const char *fmt, ...))
{
    LOG("Si5351 init %d", sizeof(si5351_t));

    // Clear device data
    memset(si5351, 0, sizeof(si5351_t));

    // Populate
    si5351->crystal_freq = cf;
    si5351->crystal_load = cl;
    si5351->dev = dev;
    si5351->write = write;
    si5351->log = log;

    // Update config on each change
    si5351->config = 1;

    si5351_configure(si5351);
}

/*
 * Set up a clock output.  Pass in:
 * - a pointer to the si5351_t struct
 * - the output to be set
 * - which PLL it should be derived from
 * - the desired frequency
 * - the desired phase
 * - whether the clock should be inverted   
 * - whether this channel should be used to derive the PLL frequency
*/

int si5351_set(si5351_t *si5351, uint8_t output, si5351_PLL_t pll, uint32_t freq, uint32_t phase, bool invert, bool pll_master)
{
    // Validate inputs
    if (output >= SI5351_CLOCKS) {
        LOG("Clock output out of range");
        return -1;
    }

    si5351->freq[output] = freq;
    si5351->phase[output] = phase;
    si5351->clk_invert[output] = invert;
    si5351->pll[output] = pll;
    if (pll_master) {
        si5351->clk_pll[pll] = output;
        LOG("PLL %d is master for clock %d - %d", pll, output, si5351->clk_pll[pll]);
    }

    return (si5351_configure(si5351));
}

// Don't update after each change - useful if we're making a bunch of changes at once
void si5351_start_batch(si5351_t *si5351)
{
    si5351->config = 0;
}   

// Update the Si5351 with the new configuration
void si5351_write_batch(si5351_t *si5351)
{
    si5351->config = 1;
    si5351_configure(si5351);
}

// Set output disabled state for a specific output or for all channels
int si5351_set_disabled(si5351_t *si5351, int clock, si5351_disable_t ds)
{
    if (clock == SI5351_CLOCK_ALL) {
        for (int i=0; i<SI5351_CLOCKS; i++) {
            si5351->clk_dis[i] = ds;
        }
    } else {
        if ((clock >= 0) && (clock < SI5351_CLOCKS)) {
            si5351->clk_dis[clock] = ds;
        } else {
            LOG("Clock %d invalid", clock);
            return -1;
        }
    }
    si5351_configure(si5351);
    return 0;
}
