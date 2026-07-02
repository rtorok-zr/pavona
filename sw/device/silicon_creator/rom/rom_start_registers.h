// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#ifndef OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_ROM_START_REGISTERS_H_
#define OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_ROM_START_REGISTERS_H_

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/* Helpers to get register base addresses in rom_start.S . */

/**
 * Base address of the OTP SwCfg partition.
 */
uint32_t otp_sw_cfg_base(void);

/**
 * Base address of the rv_core_ibex registers.
 */
uint32_t rv_core_ibex_reg_base(void);

/**
 * Base address of the pwrmgr registers.
 */
uint32_t pwrmgr_reg_base(void);

/**
 * Base address of the rstmgr registers.
 */
uint32_t rstmgr_reg_base(void);

/**
 * Base address of the aon_timer registers.
 */
uint32_t aon_timer_reg_base(void);

/**
 * Base address of the ast registers.
 */
uint32_t ast_reg_base(void);

/**
 * Base address of the clkmgr registers.
 */
uint32_t clkmgr_reg_base(void);

/**
 * Base address of the pinmux registers.
 */
uint32_t pinmux_reg_base(void);

/**
 * Base address of the gpio registers.
 */
uint32_t gpio_reg_base(void);

/**
 * Base address of the lc_ctrl registers.
 */
uint32_t lc_ctrl_reg_base(void);

/**
 * Base address of the entropy_src registers.
 */
uint32_t entropy_src_reg_base(void);

/**
 * Base address of the csrng registers.
 */
uint32_t csrng_reg_base(void);

/**
 * Base address of the edn0 registers.
 */
uint32_t edn0_reg_base(void);

/**
 * Base address of the sram_ctrl_main registers.
 */
uint32_t sram_ctrl_main_reg_base(void);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_ROM_START_REGISTERS_H_
