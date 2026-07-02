// Copyright lowRISC contributors (OpenTitan project).
// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/silicon_creator/rom/rom.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "hw/top/dt/rom_ctrl.h"  // Generated.
#include "sw/device/lib/arch/device.h"
#include "sw/device/lib/base/bitfield.h"
#include "sw/device/lib/base/csr.h"
#include "sw/device/lib/base/hardened.h"
#include "sw/device/lib/base/macros.h"
#include "sw/device/lib/base/memory.h"
#include "sw/device/lib/base/multibits.h"
#include "sw/device/lib/base/stdasm.h"
#include "sw/device/silicon_creator/lib/base/boot_measurements.h"
#include "sw/device/silicon_creator/lib/base/sec_mmio.h"
#include "sw/device/silicon_creator/lib/base/static_critical_version.h"
#include "sw/device/silicon_creator/lib/base/util.h"
#include "sw/device/silicon_creator/lib/boot_log.h"
#include "sw/device/silicon_creator/lib/cfi.h"
#include "sw/device/silicon_creator/lib/chip_info.h"
#include "sw/device/silicon_creator/lib/drivers/alert.h"
#include "sw/device/silicon_creator/lib/drivers/ast.h"
#include "sw/device/silicon_creator/lib/drivers/epmp.h"
#include "sw/device/silicon_creator/lib/drivers/hmac.h"
#include "sw/device/silicon_creator/lib/drivers/ibex.h"
#include "sw/device/silicon_creator/lib/drivers/lifecycle.h"
#include "sw/device/silicon_creator/lib/drivers/otp.h"
#include "sw/device/silicon_creator/lib/drivers/pinmux.h"
#include "sw/device/silicon_creator/lib/drivers/pwrmgr.h"
#include "sw/device/silicon_creator/lib/drivers/retention_sram.h"
#include "sw/device/silicon_creator/lib/drivers/rnd.h"
#include "sw/device/silicon_creator/lib/drivers/rstmgr.h"
#include "sw/device/silicon_creator/lib/drivers/uart.h"
#include "sw/device/silicon_creator/lib/drivers/watchdog.h"
#include "sw/device/silicon_creator/lib/error.h"
#include "sw/device/silicon_creator/lib/shutdown.h"
#include "sw/device/silicon_creator/rom/address_translation.h"
#include "sw/device/silicon_creator/rom/boot_policy.h"
#include "sw/device/silicon_creator/rom/boot_policy_ptrs.h"
#include "sw/device/silicon_creator/rom/boot_rom_ext.h"
#include "sw/device/silicon_creator/rom/pre_boot_check.h"
#include "sw/device/silicon_creator/rom/rom_epmp.h"
#include "sw/device/silicon_creator/rom/rom_state.h"
#include "sw/device/silicon_creator/rom/rom_verify.h"
#include "sw/device/silicon_creator/rom/sigverify_keys_ecdsa_p256.h"
#include "sw/device/silicon_creator/rom/sigverify_keys_spx.h"
#include "sw/device/silicon_creator/rom/sigverify_otp_keys.h"

#include "hw/top/hmac_regs.h"  // Generated.
#include "hw/top/otp_ctrl_regs.h"
#include "hw/top/rstmgr_regs.h"

#ifdef HAS_FLASH_CTRL
#include "sw/device/silicon_creator/lib/boot_data.h"
#include "sw/device/silicon_creator/lib/drivers/flash_ctrl.h"
#include "sw/device/silicon_creator/rom/bootstrap.h"
#endif

#ifdef HAS_SENSOR_CTRL
#include "sw/device/silicon_creator/lib/drivers/sensor_ctrl.h"
#endif

#ifdef HAS_ROM_CTRL1
#include "sw/device/silicon_creator/lib/rom_patch.h"
#endif

// Life cycle state of the chip.
lifecycle_state_t lc_state = (lifecycle_state_t)0;
// Boot data from flash.
boot_data_t boot_data = {0};
// Whether we are "simply" waking from low power mode.
static hardened_bool_t waking_from_low_power = 0;
#ifndef HAS_ROM_CTRL1
// First stage (ROM-->ROM_EXT) secure boot keys loaded from OTP.
static sigverify_otp_key_ctx_t sigverify_ctx;
#endif
// A ram copy of the OTP word controlling how to handle flash ECC errors.
uint32_t flash_ecc_exc_handler_en;
// A check value for the reset reason.
uint32_t reset_reason_check;

/**
 * Table of forward branch Control Flow Integrity (CFI) counters.
 *
 * Columns: Name, Initital Value.
 *
 * Each counter is indexed by Name. The Initial Value is used to initialize the
 * counters with unique values with a good hamming distance. The values are
 * restricted to 11-bit to be able use immediate load instructions.

 * Encoding generated with
 * $ ./util/design/sparse-fsm-encode.py -d 6 -m 9 -n 11 -s 1630646358
 *
 * Minimum Hamming distance: 6
 * Maximum Hamming distance: 8
 * Minimum Hamming weight: 4
 * Maximum Hamming weight: 8
 */
// clang-format off
#define ROM_CFI_FUNC_COUNTERS_TABLE(X) \
  X(kCfiRomMain,            0x382) \
  X(kCfiRomInit,            0x4ab) \
  X(kCfiRomVerify,          0x0f0) \
  X(kCfiRomTryBoot,         0x1df) \
  X(kCfiRomSecondRomPatch,  0x565) \
  X(kCfiRomConfigureRomExt, 0x22c) \
  X(kCfiRomBoot,            0x695) \
  X(kCfiRomBootRomExt,      0x518) \
  X(kCfiRomPreBootCheck,    0x289)
// clang-format on

// Define counters and constant values required by the CFI counter macros.
CFI_DEFINE_COUNTERS(rom_counters, ROM_CFI_FUNC_COUNTERS_TABLE);

static inline bool rom_console_enabled(void) {
#ifdef DISCRETE_OTP_MAP
  return otp_read32(OTP_CTRL_PARAM_OWNER_SW_CFG_ROM_BANNER_EN_OFFSET) !=
         kHardenedBoolFalse;
#else
  return true;
#endif
}

/**
 * Prints a banner during bootup.
 *
 * Pavona:ssss-pppp-rr
 *
 * Where:
 * - ssss: Silicon Creator ID.
 * - pppp: Product ID.
 * - rr: Revision ID.
 */
static void rom_banner(void) {
  if (!rom_console_enabled()) {
    return;
  }
  //                         : a n o v a P
  const uint64_t kTitle = 0x3a616e6f766150;
  const uint32_t kNewline = 0x0a0d;
  lifecycle_hw_rev_t hw;
  lifecycle_hw_rev_get(&hw);
  uart_write_imm(kTitle);
  uart_write_hex(hw.silicon_creator_id, sizeof(hw.silicon_creator_id), '-');
  uart_write_hex(hw.product_id, sizeof(hw.product_id), '-');
  uart_write_hex(hw.revision_id, sizeof(hw.revision_id), kNewline);
}

/**
 * Performs once-per-boot initialization of ROM modules and peripherals.
 */
OT_WARN_UNUSED_RESULT
static rom_error_t rom_init(void) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomInit, 1);
  sec_mmio_init();
  uint32_t reset_reasons = rstmgr_reason_get();
#ifdef DISCRETE_OTP_MAP
  reset_reason_check =
      reset_reasons ^
      (otp_read32(
           OTP_CTRL_PARAM_OWNER_SW_CFG_ROM_RESET_REASON_CHECK_VALUE_OFFSET) &
       0xFFFF);
#else
  reset_reason_check = reset_reasons ^ kHardenedBoolTrue;
#endif
  if (reset_reasons != (1U << RSTMGR_RESET_INFO_LOW_POWER_EXIT_BIT)) {
    // The above compares all bits, rather than just the one indication "low
    // power exit", because if there is any other reset reason, besides
    // LOW_POWER_EXIT, it means that the chip did full reset while coming out of
    // low power.  In that case, the state of AON IP blocks would have been
    // reset, and the ROM should not treat this as "waking from low power".
    waking_from_low_power = kHardenedBoolFalse;

    // Initialize pinmux configuration so we can use the UART, (except if waking
    // up from low power, as the pinmux will in such case have retained its
    // previous configuration.)
    pinmux_init();
  } else {
    waking_from_low_power = kHardenedBoolTrue;
  }

  // Configure UART0 as stdout.
  uart_init(kUartNCOValue);

  // Set static_critical region format version.
  static_critical_version = kStaticCriticalVersion2;

  // There are no conditional checks before writing to this CSR because it is
  // expected that if relevant Ibex countermeasures are disabled, this will
  // result in a nop.
  CSR_WRITE(CSR_REG_SECURESEED, rnd_uint32());

  // Write the OTP value to bits 0 to 5 of the cpuctrl CSR.
  uint32_t cpuctrl_csr;
  CSR_READ(CSR_REG_CPUCTRL, &cpuctrl_csr);
  cpuctrl_csr = bitfield_field32_write(
      cpuctrl_csr, (bitfield_field32_t){.mask = 0x3f, .index = 0},
      otp_read32(OTP_CTRL_PARAM_CREATOR_SW_CFG_CPUCTRL_OFFSET));
  CSR_WRITE(CSR_REG_CPUCTRL, cpuctrl_csr);

  lc_state = lifecycle_state_get();

  if (launder32(waking_from_low_power) != kHardenedBoolTrue) {
    HARDENED_CHECK_EQ(waking_from_low_power, kHardenedBoolFalse);
    // Re-initialize the watchdog timer, if the RESET was caused by anything
    // besides waking from low power (which would have left the watchdog in its
    // previous configuration).
    watchdog_init(lc_state);
    SEC_MMIO_WRITE_INCREMENT(kWatchdogSecMmioInit);

#if defined(HAS_SENSOR_CTRL)
    // Re-initialize sensor_ctrl.
    HARDENED_RETURN_IF_ERROR(sensor_ctrl_configure(lc_state));
    pwrmgr_cdc_sync(kSensorCtrlSyncCycles);
#endif
  } else {
    HARDENED_CHECK_EQ(waking_from_low_power, kHardenedBoolTrue);
  }

  // Initialize the shutdown policy.
  HARDENED_RETURN_IF_ERROR(shutdown_init(lc_state));

#if defined(HAS_FLASH_CTRL)
  flash_ctrl_init();
  SEC_MMIO_WRITE_INCREMENT(kFlashCtrlSecMmioInit);
  flash_ecc_exc_handler_en = otp_read32(
      OTP_CTRL_PARAM_OWNER_SW_CFG_ROM_FLASH_ECC_EXC_HANDLER_EN_OFFSET);
#endif

  // Initialize in-memory copy of the ePMP register configuration.
  rom_epmp_state_init(lc_state);

  // Check that AST is in the expected state.
  HARDENED_RETURN_IF_ERROR(ast_check(lc_state));

  // Initialize the retention RAM based on the reset reason and the OTP value.
  // Note: Retention RAM is always reset on PoR regardless of the OTP value.
  uint32_t reset_mask =
      (1 << kRstmgrReasonPowerOn) |
      otp_read32(OTP_CTRL_PARAM_CREATOR_SW_CFG_RET_RAM_RESET_MASK_OFFSET);
  if ((reset_reasons & reset_mask) != 0) {
    retention_sram_init();
    // In the discrete OTP map, the high nybble of `ROM_SRAM_READBACK_EN`
    // controls the retram readback enable. In the integrated OTP map, this
    // unconditionally runs.
    uint32_t sram_ret_readback_en;
#if DISCRETE_OTP_MAP
    sram_ret_readback_en =
        otp_read32(OTP_CTRL_PARAM_OWNER_SW_CFG_ROM_SRAM_READBACK_EN_OFFSET) >>
        4;
#else
    sram_ret_readback_en = kMultiBitBool4True;
#endif
    retention_sram_readback_enable(sram_ret_readback_en);
    retention_sram_get()->creator.last_shutdown_reason = kErrorOk;
  }

  // Initialize boot_log
  boot_log_t *boot_log = &retention_sram_get()->creator.boot_log;
  memset(boot_log, 0, sizeof(*boot_log));
  boot_log->identifier = kBootLogIdentifier;
  boot_log->chip_version = kChipInfo.scm_revision;
  boot_log->retention_ram_initialized =
      reset_reasons & reset_mask ? kHardenedBoolTrue : kHardenedBoolFalse;

  // Always store the retention RAM version so the ROM_EXT can depend on its
  // accuracy even after scrambling.
  retention_sram_get()->version = kRetentionSramVersion4;

  // Store the reset reason in retention RAM.
  retention_sram_get()->creator.reset_reasons = reset_reasons;

  // Print a nice message.
  if (waking_from_low_power != kHardenedBoolTrue) {
    rom_banner();
  }
  // This function is a NOP unless ROM is built for an fpga.
  device_fpga_version_print();

  // Double check the reset reason value against the OTP-defined value.
  reset_reason_check = launder32(reset_reason_check) ^ rstmgr_reason_get();
  uint32_t check_val;
#ifdef DISCRETE_OTP_MAP
  check_val =
      otp_read32(
          OTP_CTRL_PARAM_OWNER_SW_CFG_ROM_RESET_REASON_CHECK_VALUE_OFFSET) >>
      16;
#else
  check_val = kHardenedBoolTrue;
#endif
  if (launder32(check_val) != kHardenedBoolFalse) {
    // Double-check the reset reason.
    if (launder32(check_val) == reset_reason_check) {
      HARDENED_CHECK_EQ(check_val, reset_reason_check);
      // Reset reasons equal, do nothing.
    } else {
      return kErrorRomResetReasonFault;
    }
  } else {
    // Configured to not double-check the reset reason.
    HARDENED_CHECK_EQ(check_val, kHardenedBoolFalse);
  }

  // Clear the register if configured to do so in the discrete OTP map. In
  // integrated designs, the reset reason is unconditionally cleared.
  uint32_t preserve_reset_reason;
#ifdef DISCRETE_OTP_MAP
  preserve_reset_reason = otp_read32(
      OTP_CTRL_PARAM_OWNER_SW_CFG_ROM_PRESERVE_RESET_REASON_EN_OFFSET);
#else
  preserve_reset_reason = kHardenedBoolFalse;
#endif
  if (preserve_reset_reason != kHardenedBoolTrue) {
    rstmgr_reason_clear(reset_reasons);
  }

  sec_mmio_check_values(rnd_uint32());
  sec_mmio_check_counters(/*expected_check_count=*/1);

  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomInit, 2);
  return kErrorOk;
}

enum {
  /**
   * Expected value of the `kCfiRomTryBoot` counter when jumping to the first
   * ROM_EXT image.
   */
  kCfiRomTryBootManifest0Val = 3 * kCfiIncrement + kCfiRomTryBootVal0,
  /**
   * Expected value of the `kCfiRomTryBoot` counter when jumping to the second
   * ROM_EXT image.
   */
  kCfiRomTryBootManifest1Val = 10 * kCfiIncrement + kCfiRomTryBootVal0,
};

#ifdef HAS_ROM_CTRL1
// This symbol is defined in `rom_dual_stage.ld` and describes the location of
// the second ROM entry point.
extern char _second_rom_boot_address[];

/**
 * Patches second ROM code with an OTP ROM patch.
 *
 * If a patch is successfully applied, the patch digest
 * is stored into the boot measurement section.
 *
 * @return Result of the second ROM patching.
 */
static rom_error_t second_rom_patch(void) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomSecondRomPatch, 1);
  rom_patch_info_t latest_patch = rom_patch_latest(NULL);

  do {
    /* We could not find a latest patch, we're done */
    if (latest_patch.addr == kRomPatchInvalidAddr) {
      break;
    }

    hmac_digest_t patch_digest;
    rom_error_t error = rom_patch_apply(&latest_patch, &patch_digest);

    /* The latest patch could not be applied, let's try the next one */
    if (launder32(error) != kErrorOk) {
      latest_patch = rom_patch_latest(&latest_patch);
      continue;
    }
    HARDENED_CHECK_EQ(error, kErrorOk);

    /* Latest patch applied, let's store the patch measurement */
    static_assert(sizeof(boot_measurements.rom_patch) == sizeof(patch_digest),
                  "Unexpected ROM patch digest size.");
    memcpy(&boot_measurements.rom_patch, &patch_digest,
           sizeof(boot_measurements.rom_patch));

  } while (false);

  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomSecondRomPatch, 2);
  return kErrorOk;
}

/**
 * Attempts to boot the second-stage ROM in dual-ROM designs.
 *
 * Note: This function should not return under normal conditions. Any returns
 * from this function must result in shutdown.
 *
 * @return rom_error_t Result of the operation.
 */
static rom_error_t rom_boot_second_rom(void) {
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomBoot, 1,
                            kCfiRomSecondRomPatch);
  // In dual-ROM designs, apply the second ROM patch if we have one, and then
  // boot the second-stage ROM.
  HARDENED_RETURN_IF_ERROR(second_rom_patch());
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomBoot, 3);
  CFI_FUNC_COUNTER_CHECK(rom_counters, kCfiRomSecondRomPatch, 3);

  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomBoot, 4);
  uintptr_t _entry_point = ((uintptr_t)_second_rom_boot_address) + 0x180;

  // Configure ePMP for the second stage ROM
  //
  // ePMP for the second ROM patch was already configured in `second_rom_patch`.
  uint32_t rom_ctrl1_base =
      dt_rom_ctrl_memory_base(kDtRomCtrl1, kDtRomCtrlMemoryRom);
  uint32_t rom_ctrl1_size =
      dt_rom_ctrl_memory_size(kDtRomCtrl1, kDtRomCtrlMemoryRom);
  const epmp_region_t second_rom_text = {
      .start = _entry_point, .end = rom_ctrl1_base + rom_ctrl1_size};
  const epmp_region_t second_rom = {.start = rom_ctrl1_base,
                                    .end = rom_ctrl1_base + rom_ctrl1_size};
  epmp_prepare_boot_stage(second_rom_text, second_rom);

  // Check the ePMP state again
  HARDENED_RETURN_IF_ERROR(epmp_state_check());
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomBoot, 5);

  // Re-initialize mtvec
  CSR_WRITE(CSR_REG_MTVEC, ((uintptr_t)_second_rom_boot_address) | 1);

  // Jump to the second rom entry point
  ((entry_point *)_entry_point)();
  return kErrorRomBootFailed;
}

#else
static inline void rom_configure_rom_ext_increment_cfi(size_t value) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomConfigureRomExt, value);
}

static inline void rom_pre_boot_check_increment_cfi(size_t value) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomPreBootCheck, value);
}

static inline void rom_boot_rom_ext_increment_cfi(size_t value) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomBootRomExt, value);
}

/**
 * Attempts to boot the ROM_EXT in single-rom designs.
 *
 * Note: This function should not return under normal conditions. Any returns
 * from this function must result in shutdown.
 *
 * @param manifest Manifest of the ROM_EXT to boot.
 * @param flash_exec Value to write to the flash_ctrl EXEC register.
 * @return rom_error_t Result of the operation.
 */
OT_WARN_UNUSED_RESULT
static rom_error_t rom_boot(const manifest_t *manifest,
                            uintptr_t imm_section_entry_point,
                            uint32_t flash_exec) {
#ifdef HAS_ROM_CTRL1
#else
  // In single-stage ROM designs, configure and boot directly to ROM_EXT.
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomBoot, 1);

  // Configure ROM_EXT.
  uintptr_t entry_point;
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomBoot, 2,
                            kCfiRomConfigureRomExt);
  HARDENED_RETURN_IF_ERROR(
      rom_configure_rom_ext(manifest, &sigverify_ctx, &entry_point,
                            rom_configure_rom_ext_increment_cfi));
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomBoot, 4);
  CFI_FUNC_COUNTER_CHECK(rom_counters, kCfiRomConfigureRomExt, 4);

  // Perfrom pre-boot checks.
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomBoot, 5, kCfiRomPreBootCheck);
#ifdef HAS_FLASH_CTRL
  rom_pre_boot_check(boot_data, lc_state, rom_pre_boot_check_increment_cfi);
#else
  rom_pre_boot_check(lc_state, rom_pre_boot_check_increment_cfi);
#endif
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomBoot, 7);
  CFI_FUNC_COUNTER_CHECK(rom_counters, kCfiRomPreBootCheck, 8);

  // Check CFI against manifest.
  const manifest_t *manifest_check = NULL;
  switch (launder32(rom_counters[kCfiRomTryBoot])) {
    case kCfiRomTryBootManifest0Val:
      HARDENED_CHECK_EQ(rom_counters[kCfiRomTryBoot],
                        kCfiRomTryBootManifest0Val);
      manifest_check = boot_policy_manifests_get().ordered[0];
      break;
    case kCfiRomTryBootManifest1Val:
      HARDENED_CHECK_EQ(rom_counters[kCfiRomTryBoot],
                        kCfiRomTryBootManifest1Val);
      manifest_check = boot_policy_manifests_get().ordered[1];
      break;
    default:
      HARDENED_TRAP();
  }
  HARDENED_CHECK_EQ(manifest, manifest_check);

  // Boot ROM_EXT.
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomBoot, 8, kCfiRomBootRomExt);
  HARDENED_RETURN_IF_ERROR(rom_boot_rom_ext(manifest_check, entry_point,
#ifdef DISCRETE_OTP_MAP
                                            imm_section_entry_point,
#endif
                                            flash_exec,
                                            rom_boot_rom_ext_increment_cfi));
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomBoot, 10);
  CFI_FUNC_COUNTER_CHECK(rom_counters, kCfiRomBootRomExt, 3);
#endif
  return kErrorRomBootFailed;
}

void rom_verify_increment_cfi(size_t value) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomVerify, value);
}

/**
 * Attempts to boot ROM_EXTs in the order given by the boot policy module.
 *
 * @return Result of the last attempt.
 */
OT_WARN_UNUSED_RESULT
static rom_error_t rom_try_boot(void) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomTryBoot, 1);

  boot_policy_manifests_t manifests = boot_policy_manifests_get();
  uint32_t flash_exec = 0;
  uintptr_t imm_section_entry_point = kHardenedBoolFalse;

#ifdef HAS_FLASH_CTRL
  // Read boot data from flash
  HARDENED_RETURN_IF_ERROR(boot_data_read(lc_state, &boot_data));

  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomTryBoot, 2, kCfiRomVerify);
  rom_error_t error =
      rom_verify(manifests.ordered[0], boot_data, lc_state, &sigverify_ctx,
                 &flash_exec, &rom_verify_increment_cfi);
#else
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomTryBoot, 2, kCfiRomVerify);
  rom_error_t error = rom_verify(manifests.ordered[0], lc_state, &sigverify_ctx,
                                 &flash_exec, &rom_verify_increment_cfi);
#endif
#ifdef DISCRETE_OTP_MAP
  error = rom_verify_immutable_section(error, manifests.ordered[0],
                                       &imm_section_entry_point);
#endif
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomTryBoot, 4);

  if (launder32(error) == kErrorOk) {
    HARDENED_CHECK_EQ(error, kErrorOk);
    CFI_FUNC_COUNTER_CHECK(rom_counters, kCfiRomVerify, 3);
    CFI_FUNC_COUNTER_INIT(rom_counters, kCfiRomTryBoot);
    CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomTryBoot, 1, kCfiRomBoot);
    HARDENED_RETURN_IF_ERROR(
        rom_boot(manifests.ordered[0], imm_section_entry_point, flash_exec));
    return kErrorRomBootFailed;
  }
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomTryBoot, 5, kCfiRomVerify);
#ifdef HAS_FLASH_CTRL
  error = rom_verify(manifests.ordered[1], boot_data, lc_state, &sigverify_ctx,
                     &flash_exec, &rom_verify_increment_cfi);
#else
  error = rom_verify(manifests.ordered[1], lc_state, &sigverify_ctx,
                     &flash_exec, &rom_verify_increment_cfi);
#endif
#ifdef DISCRETE_OTP_MAP
  HARDENED_RETURN_IF_ERROR(rom_verify_immutable_section(
      error, manifests.ordered[1], &imm_section_entry_point));
#endif
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomTryBoot, 7);
  CFI_FUNC_COUNTER_CHECK(rom_counters, kCfiRomVerify, 3);

  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomTryBoot, 8, kCfiRomBoot);
  HARDENED_RETURN_IF_ERROR(
      rom_boot(manifests.ordered[1], imm_section_entry_point, flash_exec));
  return kErrorRomBootFailed;
}
#endif

/**
 * Table of ROM states.
 *
 * Encoding generated with:
 * $ ./util/design/sparse-fsm-encode.py -d 6 -m 4 -n 16 \
 *     -s 519644925 --language=c
 */
// clang-format off
#ifdef HAS_FLASH_CTRL
enum {
  kRomStateCnt = 4,
};

/*
 * The bootstrap request is the `kRomStateBootstrapCheck` and
 * `kRomStateBootstrap` ROM states argument. It must be undefined before
 * entering the `kRomStateBootstrapCheck` state as only the
 * `kRomStateBootstrapCheck` run callback or hooks should set it to either
 * `kHardenedBoolFalse` or `kHardenedBoolTrue`.
 */
static hardened_bool_t bootstrap_request = 0;

#define ROM_STATES(X)                                                               \
  X(kRomStateInit,           0x5616, rom_state_init, NULL)                          \
  X(kRomStateBootstrapCheck, 0x0a92, rom_state_bootstrap_check, &bootstrap_request) \
  X(kRomStateBootstrap,      0xd0a0, rom_state_bootstrap, &bootstrap_request)       \
  X(kRomStateBoot,           0xed14, rom_state_boot_rom_ext, NULL)
#else
enum {
  kRomStateCnt = 2,
};

#define ROM_STATES(X)                                                               \
  X(kRomStateInit,           0x5616, rom_state_init, NULL)                          \
  X(kRomStateBoot,           0xed14, rom_state_boot_rom_ext, NULL)
#endif
// clang-format on

ROM_STATE_INIT_TABLE(rom_states, kRomStateCnt, ROM_STATES);

static OT_WARN_UNUSED_RESULT rom_error_t rom_state_init(void *arg,
                                                        uint32_t *next_state) {
  CFI_FUNC_COUNTER_INIT(rom_counters, kCfiRomMain);

  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomMain, 1, kCfiRomInit);
  HARDENED_RETURN_IF_ERROR(rom_init());
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomMain, 3);

#ifdef HAS_FLASH_CTRL
  *next_state = kRomStateBootstrapCheck;
#else
  // In designs without onboard flash, bootstrapping is not supported in ROM, so
  // we skip straight to the Boot state.
  *next_state = kRomStateBoot;
#endif

  return kErrorOk;
}

#ifdef HAS_FLASH_CTRL
/**
 * Prints a status message indicating that the ROM is entering bootstrap mode.
 */
static void rom_bootstrap_message(void) {
  //                              a r t s t o o b
  const uint64_t kBootstrap1 = 0x61727473746f6f62;
  //                             \n\r 1 : p
  const uint64_t kBootstrap2 = 0x0a0d313a70;
  uart_write_imm(kBootstrap1);
  uart_write_imm(kBootstrap2);
}

static OT_WARN_UNUSED_RESULT rom_error_t
rom_state_bootstrap_check(void *arg, uint32_t *next_state) {
  if (launder32(waking_from_low_power) != kHardenedBoolTrue) {
    HARDENED_CHECK_EQ(waking_from_low_power, kHardenedBoolFalse);

    hardened_bool_t *bootstrap_req = (hardened_bool_t *)arg;

    if (launder32(*bootstrap_req) == 0) {
      // The pre_ hook has not set the bootstrap request flag, it has to be
      // checked and set to True or False
      HARDENED_CHECK_EQ(*bootstrap_req, 0);
      *bootstrap_req = bootstrap_requested();
    }

    // The bootstrap request flag must now be True or False.
    if (launder32(*bootstrap_req) == kHardenedBoolTrue) {
      HARDENED_CHECK_EQ(*bootstrap_req, kHardenedBoolTrue);
      *next_state = kRomStateBootstrap;
      return kErrorOk;
    }
  }

  // We are not bootstrapping, aiming for ROM_EXT in single-ROM designs, or the
  // second ROM in dual-ROM designs.
  *next_state = kRomStateBoot;
  return kErrorOk;
}

static OT_WARN_UNUSED_RESULT rom_error_t
rom_state_bootstrap(void *arg, uint32_t *next_state) {
  hardened_bool_t *bootstrap_req = (hardened_bool_t *)arg;

  if (launder32(*bootstrap_req) == kHardenedBoolTrue) {
    HARDENED_CHECK_EQ(*bootstrap_req, kHardenedBoolTrue);
    rom_bootstrap_message();
    watchdog_disable();
    // `bootstrap` will not return unless there is an error.
    HARDENED_RETURN_IF_ERROR(bootstrap());
  }

  return kErrorRomBootFailed;
}
#endif

static OT_WARN_UNUSED_RESULT rom_error_t
rom_state_boot_rom_ext(void *arg, uint32_t *next_state) {
#ifdef HAS_ROM_CTRL1
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomMain, 4, kCfiRomBoot);
  // `rom_boot_second_rom` will not return unless there is an error.
  return rom_boot_second_rom();
#else
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomMain, 4, kCfiRomTryBoot);
  // `rom_try_boot` will not return unless there is an error.
  return rom_try_boot();
#endif
}

void rom_main(void) {
  CFI_FUNC_COUNTER_INIT(rom_counters, kCfiRomMain);
  // `rom_state_fsm_walk` will not return unless there is an error.
  shutdown_finalize(rom_state_fsm_walk(rom_states, kRomStateCnt, kRomStateInit,
                                       rom_states_cfi));
}
