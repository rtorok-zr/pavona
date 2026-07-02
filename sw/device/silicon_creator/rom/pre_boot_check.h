// Copyright lowRISC contributors (OpenTitan project).
// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#ifndef OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_PRE_BOOT_CHECK_H_
#define OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_PRE_BOOT_CHECK_H_

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/**
 * Performs consistency checks before booting a ROM_EXT.
 *
 * All of the checks in this function are expected to pass and any failures
 * result in shutdown.
 */
void rom_pre_boot_check(
#ifdef HAS_FLASH_CTRL
    const boot_data_t boot_data,
#endif
    const lifecycle_state_t lc_state,
    void (*rom_pre_boot_check_increment_cfi)(size_t));

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_PRE_BOOT_CHECK_H_
