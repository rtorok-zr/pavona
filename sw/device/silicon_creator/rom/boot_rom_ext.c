// Copyright lowRISC contributors (OpenTitan project).
// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/lib/base/hardened.h"
#include "sw/device/lib/base/memory.h"
#include "sw/device/silicon_creator/lib/base/sec_mmio.h"
#include "sw/device/silicon_creator/lib/boot_log.h"
#include "sw/device/silicon_creator/lib/drivers/epmp.h"
#include "sw/device/silicon_creator/lib/drivers/hmac.h"
#include "sw/device/silicon_creator/lib/drivers/ibex.h"
#include "sw/device/silicon_creator/lib/drivers/otp.h"
#include "sw/device/silicon_creator/lib/drivers/retention_sram.h"
#include "sw/device/silicon_creator/lib/drivers/rnd.h"
#include "sw/device/silicon_creator/lib/error.h"
#include "sw/device/silicon_creator/lib/manifest.h"
#include "sw/device/silicon_creator/lib/stack_utilization.h"
#include "sw/device/silicon_creator/rom/address_translation.h"
#include "sw/device/silicon_creator/rom/boot_policy.h"
#include "sw/device/silicon_creator/rom/boot_policy_ptrs.h"
#include "sw/device/silicon_creator/rom/sigverify_otp_keys.h"

#ifdef HAS_FLASH_CTRL
#include "sw/device/silicon_creator/lib/drivers/flash_ctrl.h"
#endif

#ifdef HAS_KEYMGR_DPE
#include "sw/device/silicon_creator/lib/drivers/keymgr_dpe.h"
#else
#include "sw/device/silicon_creator/lib/drivers/keymgr.h"
#endif

/* These symbols are defined in
 * `opentitan/sw/device/silicon_creator/rom/rom.ld`, and describes the
 * location of the flash header.
 */
extern char _rom_ext_virtual_start_address[];
extern char _rom_ext_virtual_size[];

/**
 * Base address of the RAM.
 */
inline uint32_t otp_ctrl_regs_base(void) {
  return dt_otp_ctrl_reg_block(kDtOtpCtrl, kDtOtpCtrlRegBlockCore);
}

/**
 * Measures the combination of software configuration OTP digests and the digest
 * of the secure boot keys.
 *
 * @param measurement Pointer to the measurement of the partitions.
 * @return rom_error_t Result of the operation.
 */
static rom_error_t rom_measure_otp_partitions(
    sigverify_otp_key_ctx_t *sigverify_ctx,
    keymgr_binding_value_t *measurement) {
  memset(measurement, (int)rnd_uint32(), sizeof(keymgr_binding_value_t));
  // These is no need to harden these data copies as any poisoning of the OTP
  // measurements will result in the derivation of a different UDS identity
  // which will not be endorsed. Hence we sa-ve the cycles of using sec_mmio.
  hmac_sha256_init();
  static_assert(
      (OTP_CTRL_CREATOR_SW_CFG_DIGEST_CREATOR_SW_CFG_DIGEST_FIELD_WIDTH *
       OTP_CTRL_CREATOR_SW_CFG_DIGEST_MULTIREG_COUNT / 8) == sizeof(uint64_t),
      "CreatorSwCfg OTP partition digest no longer 64 bits.");
  static_assert(
      (OTP_CTRL_OWNER_SW_CFG_DIGEST_OWNER_SW_CFG_DIGEST_FIELD_WIDTH *
       OTP_CTRL_OWNER_SW_CFG_DIGEST_MULTIREG_COUNT / 8) == sizeof(uint64_t),
      "OwnerSwCfg OTP partition digest no longer 64 bits.");
  hmac_sha256_update(
      (unsigned char *)(otp_ctrl_regs_base(),
                        OTP_CTRL_SW_CFG_WINDOW_REG_OFFSET +
                            OTP_CTRL_PARAM_CREATOR_SW_CFG_DIGEST_OFFSET),
      sizeof(uint64_t));
  hmac_sha256_update(
      (unsigned char *)(otp_ctrl_regs_base(),
                        OTP_CTRL_SW_CFG_WINDOW_REG_OFFSET +
                            OTP_CTRL_PARAM_OWNER_SW_CFG_DIGEST_OFFSET),
      sizeof(uint64_t));
#ifdef DISCRETE_OTP_MAP
  hmac_sha256_update(sigverify_ctx->keys.integrity_measurement.digest,
                     kHmacDigestNumBytes);
#endif
  hmac_sha256_process();
  hmac_digest_t otp_measurement;
  hmac_sha256_final(&otp_measurement);
  memcpy(measurement->data, otp_measurement.digest, kHmacDigestNumBytes);
  return kErrorOk;
}

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
    uintptr_t *_entry_point,
    void (*rom_configure_rom_ext_increment_cfi)(size_t)) {
  HARDENED_RETURN_IF_ERROR(sc_keymgr_state_check(kScKeymgrStateReset));

  rom_configure_rom_ext_increment_cfi(1);
  boot_log_t *boot_log = &retention_sram_get()->creator.boot_log;
  boot_log->rom_ext_slot =
      manifest == boot_policy_manifest_get(kSlotA) ? kBootSlotA : kBootSlotB;
  boot_log_digest_update(boot_log);

  keymgr_binding_value_t otp_measurement;
  const keymgr_binding_value_t *attestation_measurement =
      &manifest->binding_value;
  uint32_t use_otp_measurement =
      otp_read32(OTP_CTRL_PARAM_OWNER_SW_CFG_ROM_KEYMGR_OTP_MEAS_EN_OFFSET);
  if (launder32(use_otp_measurement) == kHardenedBoolTrue) {
    HARDENED_CHECK_EQ(use_otp_measurement, kHardenedBoolTrue);
    rom_measure_otp_partitions(sigverify_ctx, &otp_measurement);
    attestation_measurement = &otp_measurement;
  } else {
    HARDENED_CHECK_NE(use_otp_measurement, kHardenedBoolTrue);
  }
  rom_configure_rom_ext_increment_cfi(2);
#ifdef HAS_KEYMGR_DPE
  sc_keymgr_sw_binding_set(attestation_measurement);
  sc_keymgr_max_ver_set(manifest->max_key_version);
  SEC_MMIO_WRITE_INCREMENT(kScKeymgrSecMmioSwBindingSet +
                           kScKeymgrSecMmioMaxVerSet);
#else
  sc_keymgr_sw_binding_set(&manifest->binding_value, attestation_measurement);
  sc_keymgr_creator_max_ver_set(manifest->max_key_version);
  SEC_MMIO_WRITE_INCREMENT(kScKeymgrSecMmioSwBindingSet +
                           kScKeymgrSecMmioCreatorMaxVerSet);
#endif

  sec_mmio_check_counters(/*expected_check_count=*/2);

  // Configure address translation, compute the epmp regions and the entry
  // point for the virtual address in case the address translation is enabled.
  // Otherwise, compute the epmp regions and the entry point for the load
  // address.
  epmp_region_t text_region = manifest_code_region_get(manifest);
  *_entry_point = manifest_entry_point_get(manifest);
  switch (launder32(manifest->address_translation)) {
    case kHardenedBoolTrue:
      HARDENED_CHECK_EQ(manifest->address_translation, kHardenedBoolTrue);
      ibex_addr_remap_set(0, (uintptr_t)_rom_ext_virtual_start_address,
                          (uintptr_t)manifest, (size_t)_rom_ext_virtual_size);
      SEC_MMIO_WRITE_INCREMENT(kAddressTranslationSecMmioConfigure);

      // Unlock read-execute for the text region and read-only for the whole
      // ROM_EXT virtual memory.
      HARDENED_RETURN_IF_ERROR(epmp_state_check());
      // Update the text region to point to the virtual addresses instead of the
      // physical ones.
      text_region.start = rom_ext_vma_get(manifest, text_region.start);
      text_region.end = rom_ext_vma_get(manifest, text_region.end);
      epmp_region_t rom_ext_virtual_region = {
          .start = (uintptr_t)_rom_ext_virtual_start_address,
          .end = (uintptr_t)_rom_ext_virtual_start_address +
                 (uintptr_t)_rom_ext_virtual_size,
      };
      epmp_prepare_boot_stage(text_region, rom_ext_virtual_region);
      // Move the ROM_EXT execution section from the load address to the virtual
      // address.
      text_region.start = rom_ext_vma_get(manifest, text_region.start);
      text_region.end = rom_ext_vma_get(manifest, text_region.end);
      *_entry_point = rom_ext_vma_get(manifest, *_entry_point);
      break;
    case kHardenedBoolFalse:
      HARDENED_CHECK_EQ(manifest->address_translation, kHardenedBoolFalse);
      // Unlock execution of ROM_EXT executable code (text) sections.
      HARDENED_RETURN_IF_ERROR(epmp_state_check());
      epmp_prepare_boot_stage_rx(text_region);
      break;
    default:
      HARDENED_TRAP();
  }
  rom_configure_rom_ext_increment_cfi(3);
  return kErrorOk;
}

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
                             uintptr_t _entry_point,
#ifdef DISCRETE_OTP_MAP
                             uintptr_t imm_section_entry_point,
#endif
                             uint32_t flash_exec,
                             void (*rom_boot_rom_ext_increment_cfi)(size_t)) {
  rom_boot_rom_ext_increment_cfi(1);
#ifdef HAS_FLASH_CTRL
  // Enable execution of code from flash if signature is verified.
  flash_ctrl_exec_set(flash_exec);
  SEC_MMIO_WRITE_INCREMENT(kFlashCtrlSecMmioExecSet);
  sec_mmio_check_values(rnd_uint32());
  sec_mmio_check_counters(/*expected_check_count=*/5);
#endif

#if OT_BUILD_FOR_STATIC_ANALYZER
  assert(manifest_check != NULL);
#endif

  if (launder32(manifest_check->address_translation) == kHardenedBoolTrue) {
    HARDENED_CHECK_EQ(manifest_check->address_translation, kHardenedBoolTrue);
    HARDENED_CHECK_EQ(rom_ext_vma_get(manifest_check,
                                      manifest_entry_point_get(manifest_check)),
                      _entry_point);
  } else {
    HARDENED_CHECK_EQ(manifest_check->address_translation, kHardenedBoolFalse);
    HARDENED_CHECK_EQ(manifest_entry_point_get(manifest_check), _entry_point);
  }
  rom_boot_rom_ext_increment_cfi(2);

  // In a normal build, this function inlines to nothing.
  stack_utilization_print();

#ifdef DISCRETE_OTP_MAP
  if (imm_section_entry_point != kHardenedBoolFalse) {
    ((entry_point *)imm_section_entry_point)();
  }
#endif
  // Jump to ROM_EXT.
  ((entry_point *)_entry_point)();
  return kErrorRomBootFailed;
}
