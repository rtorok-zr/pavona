// Copyright lowRISC contributors (OpenTitan project).
// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#ifndef OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_BOOT_ROM_EXT_H_
#define OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_BOOT_ROM_EXT_H_

#include "sw/device/lib/base/hardened.h"
#include "sw/device/silicon_creator/lib/error.h"
#include "sw/device/silicon_creator/lib/manifest.h"
#include "sw/device/silicon_creator/rom/sigverify_otp_keys.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/**
 * Configures keymgr software bindings and ePMP for ROM_EXT. In dual-ROM
 * designs, this is invoked in the second-stage ROM.
 *
 * @param manifest Manifest of the ROM_EXT to boot.
 * @param flash_exec Value to write to the flash_ctrl EXEC register.
 * @return rom_error_t Result of the operation.
 */
OT_WARN_UNUSED_RESULT
rom_error_t rom_configure_rom_ext(
    const manifest_t *manifest, sigverify_otp_key_ctx_t *sigverify_ctx,
    uintptr_t *entry_point,
    void (*rom_configure_rom_ext_increment_cfi)(size_t));

/**
 * Attempts to boot the ROM_EXT. In dual-ROM designs, this is invoked in the
 * second-stage ROM.
 *
 * Note: This function should not return under normal conditions. Any returns
 * from this function must result in shutdown.
 *
 * @param manifest Manifest of the ROM_EXT to boot.
 * @param flash_exec Value to write to the flash_ctrl EXEC register.
 * @return rom_error_t Result of the operation.
 */
OT_WARN_UNUSED_RESULT
rom_error_t rom_boot_rom_ext(const manifest_t *manifest_check,
                             uintptr_t entry_point,
#ifdef DISCRETE_OTP_MAP
                             uintptr_t imm_section_entry_point,
#endif
                             uint32_t flash_exec,
                             void (*rom_boot_rom_ext_increment_cfi)(size_t));

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_BOOT_ROM_EXT_H_
