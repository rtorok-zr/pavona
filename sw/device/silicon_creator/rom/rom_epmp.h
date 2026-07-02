// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#ifndef OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_ROM_EPMP_H_
#define OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_ROM_EPMP_H_

#include <stdint.h>

#include "sw/device/silicon_creator/lib/drivers/lifecycle.h"
#include "sw/device/silicon_creator/lib/epmp_state.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/**
 * ROM enhanced Physical Memory Protection (ePMP) library.
 *
 * The ePMP configuration is managed in two parts:
 *
 *   1. The actual hardware configuration held in CSRs
 *   2. The in-memory copy of register values in `epmp_state_t` that is used
 *      to verify the CSRs
 *
 * Every time the hardware configuration is updated the in-memory copy
 * must also be updated. The hardware configuration is usually interacted
 * with directly using the CSR library or assembly whereas the in-memory
 * copy of the state should normally be modified using configuration functions
 * from the silicon creator ePMP library.
 *
 * This separation of concerns allows the hardware configuration to be
 * updated efficiently as needed (including before the C runtime is
 * initialized) with the in-memory copy of the state used to double check the
 * configuration as required.
 */

/**
 * Initialise the ePMP in-memory copy of the register state to reflect the
 * hardware configuration expected at entry to the ROM C code.
 *
 * The actual hardware configuration is performed separately, either by reset
 * logic or in assembly. This code must be kept in sync with any changes
 * to the hardware configuration.
 *
 * @param lc_state The current lifecycle state to check for debug enable.
 */
void rom_epmp_state_init(lifecycle_state_t lc_state);

/* Helpers to get register addresses in rom_epmp_start.S . */

/**
 * Base address of the ROM.
 */
uint32_t rom_ctrl_rom_base(void);
/**
 * Size of the ROM.
 */
uint32_t rom_ctrl_rom_size(void);
/**
 * NAPOT encoding of the ROM region.
 */
uint32_t rom_ctrl_rom_napot(void);
#ifdef HAS_FLASH_CTRL
/**
 * Base address of the flash memory.
 */
uint32_t flash_ctrl_mem_base(void);
/**
 * Size of the flash memory.
 */
uint32_t flash_ctrl_mem_size(void);
/**
 * NAPOT encoding of the Flash memory region.
 */
uint32_t flash_ctrl_mem_napot(void);

#else
/**
 * Base address of the flash memory.
 */
uint32_t soc_proxy_ctn_base(void);
/**
 * Size of the flash memory.
 */
uint32_t soc_proxy_ctn_size(void);
/**
 * NAPOT encoding of the CTN SRAM region.
 */
uint32_t soc_proxy_ctn_napot(void);
#endif
/**
 * Base address of the RAM.
 */
uint32_t sram_ctrl_ram_base(void);
/**
 * Size of the RAM.
 */
uint32_t sram_ctrl_ram_size(void);
/**
 * NAPOT encoding of the RAM region.
 */
uint32_t sram_ctrl_ram_napot(void);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_ROM_EPMP_H_
