// Copyright lowRISC contributors (OpenTitan project).
// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#ifndef OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_ROM_VERIFY_H_
#define OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_ROM_VERIFY_H_

#include "sw/device/silicon_creator/lib/boot_data.h"
#include "sw/device/silicon_creator/lib/drivers/lifecycle.h"
#include "sw/device/silicon_creator/lib/manifest.h"
#include "sw/device/silicon_creator/rom/sigverify_otp_keys.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/**
 * Verifies a ROM_EXT.
 *
 * This function performs bounds checks on the fields of the manifest, checks
 * its `identifier` and `security_version` fields, and verifies its signature.
 *
 * @param Manifest of the ROM_EXT to be verified.
 * @param[out] flash_exec Value to write to the flash_ctrl EXEC register.
 * @return Result of the operation.
 */
OT_WARN_UNUSED_RESULT
rom_error_t rom_verify(const manifest_t *manifest,
#ifdef HAS_FLASH_CTRL
                       const boot_data_t boot_data,
#endif
                       const lifecycle_state_t lc_state,
                       sigverify_otp_key_ctx_t *sigverify_ctx,
                       uint32_t *flash_exec,
                       void (*rom_verify_increment_cfi)(size_t));

/**
 * Verifies the immutable section of a ROM_EXT.
 *
 * This function performs bounds checks on the fields of the manifest, checks
 * its `identifier` and `security_version` fields, and verifies its signature.
 *
 * @param Manifest of the ROM_EXT to be verified.
 * @param[out] flash_exec Value to write to the flash_ctrl EXEC register.
 * @return Result of the operation.
 */
rom_error_t rom_verify_immutable_section(rom_error_t verify_result,
                                         const manifest_t *manifest,
                                         uintptr_t *imm_section_entry_point);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // OPENTITAN_SW_DEVICE_SILICON_CREATOR_ROM_ROM_VERIFY_H_
