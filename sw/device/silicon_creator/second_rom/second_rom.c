// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/silicon_creator/second_rom/second_rom.h"

#include <stdbool.h>
#include <stdint.h>

#include "sw/device/lib/arch/device.h"
#include "sw/device/lib/base/bitfield.h"
#include "sw/device/lib/base/csr.h"
#include "sw/device/lib/base/hardened.h"
#include "sw/device/lib/base/macros.h"
#include "sw/device/lib/base/memory.h"
#include "sw/device/lib/base/stdasm.h"
#include "sw/device/silicon_creator/lib/acc_boot_services.h"
#include "sw/device/silicon_creator/lib/base/boot_measurements.h"
#include "sw/device/silicon_creator/lib/base/sec_mmio.h"
#include "sw/device/silicon_creator/lib/base/static_critical_version.h"
#include "sw/device/silicon_creator/lib/cfi.h"
#include "sw/device/silicon_creator/lib/dbg_print.h"
#include "sw/device/silicon_creator/lib/drivers/alert.h"
#include "sw/device/silicon_creator/lib/drivers/ast.h"
#include "sw/device/silicon_creator/lib/drivers/ibex.h"
#include "sw/device/silicon_creator/lib/drivers/keymgr_dpe.h"
#include "sw/device/silicon_creator/lib/drivers/lifecycle.h"
#include "sw/device/silicon_creator/lib/drivers/otp.h"
#include "sw/device/silicon_creator/lib/drivers/pinmux.h"
#include "sw/device/silicon_creator/lib/drivers/pwrmgr.h"
#include "sw/device/silicon_creator/lib/drivers/retention_sram.h"
#include "sw/device/silicon_creator/lib/drivers/rnd.h"
#include "sw/device/silicon_creator/lib/drivers/rstmgr.h"
#include "sw/device/silicon_creator/lib/drivers/uart.h"
#include "sw/device/silicon_creator/lib/drivers/watchdog.h"
#include "sw/device/silicon_creator/lib/epmp_state.h"
#include "sw/device/silicon_creator/lib/error.h"
#include "sw/device/silicon_creator/lib/manifest.h"
#include "sw/device/silicon_creator/lib/shutdown.h"
#include "sw/device/silicon_creator/lib/sigverify/sigverify.h"
#include "sw/device/silicon_creator/rom/boot_policy.h"
#include "sw/device/silicon_creator/rom/boot_rom_ext.h"
#include "sw/device/silicon_creator/rom/pre_boot_check.h"
#include "sw/device/silicon_creator/rom/rom_verify.h"
#include "sw/device/silicon_creator/second_rom/second_rom_epmp.h"

#include "hw/top/otp_ctrl_regs.h"

/**
 * Type alias for the ROM_EXT entry point.
 *
 * The entry point address obtained from the ROM_EXT manifest must be cast to a
 * pointer to this type before being called.
 */
typedef void rom_ext_entry_point(void);

/**
 * Table of forward branch Control Flow Integrity (CFI) counters.
 *
 * Columns: Name, Initital Value.
 *
 * Each counter is indexed by Name. The Initial Value is used to initialize the
 * counters with unique values with a good hamming distance. The values are
 * restricted to 11-bit to be able use immediate load instructions.

 * Encoding generated with
 * $ ./util/design/sparse-fsm-encode.py -d 6 -m 7 -n 11 \
 *     -s 1630646358
 *
 * Minimum Hamming distance: 6
 * Maximum Hamming distance: 8
 * Minimum Hamming weight: 5
 * Maximum Hamming weight: 8
 */
// clang-format off
#define ROM_CFI_FUNC_COUNTERS_TABLE(X) \
  X(kCfiRomMain,            0x382) \
  X(kCfiRomInit,            0x4ab) \
  X(kCfiRomVerify,          0x0f0) \
  X(kCfiRomTryBoot,         0x1df) \
  X(kCfiRomConfigureRomExt, 0x565) \
  X(kCfiRomPreBootCheck,    0x22c) \
  X(kCfiRomBoot,            0x695) \
  X(kCfiRomBootRomExt,      0x518)
// clang-format on

// Define counters and constant values required by the CFI counter macros.
CFI_DEFINE_COUNTERS(rom_counters, ROM_CFI_FUNC_COUNTERS_TABLE);

// Life cycle state of the chip.
lifecycle_state_t lc_state = (lifecycle_state_t)0;
// Boot data from flash.
boot_data_t boot_data = {0};
// First stage (ROM-->ROM_EXT) secure boot keys loaded from OTP.
static sigverify_otp_key_ctx_t sigverify_ctx;

OT_ALWAYS_INLINE
OT_WARN_UNUSED_RESULT
static rom_error_t rom_irq_error(void) {
  uint32_t mcause;
  CSR_READ(CSR_REG_MCAUSE, &mcause);
  // Shuffle the mcause bits into the uppermost byte of the word and report
  // the cause as kErrorInterrupt.
  // Based on the ibex verilog, it appears that the most significant bit
  // indicates whether the cause is an exception (0) or external interrupt (1),
  // and the 5 least significant bits indicate which exception/interrupt.
  //
  // Preserve the MSB and shift the 7 LSBs into the upper byte.
  // (we preserve 7 instead of 5 because the verilog hardcodes the unused bits
  // as zero and those would be the next bits used should the number of
  // interrupt causes increase).
  mcause = (mcause & 0x80000000) | ((mcause & 0x7f) << 24);
  return kErrorInterrupt + mcause;
}

/**
 * Performs once-per-boot initialization of ROM modules and peripherals.
 */
OT_WARN_UNUSED_RESULT
static rom_error_t rom_init(void) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomInit, 1);

  dbg_printf("Starting 2nd stage ROM\r\n");

  // Reset MMIO counters
  sec_mmio_next_stage_init();

  // Set static_critical region format version.
  static_critical_version = kStaticCriticalVersion1;

  // Update ePMP register configuration.
  second_rom_epmp_state_init();
  HARDENED_RETURN_IF_ERROR(epmp_state_check());

  lc_state = lifecycle_state_get();

  // Re-initialize the watchdog timer.
  watchdog_init(lc_state);
  SEC_MMIO_WRITE_INCREMENT(kWatchdogSecMmioInit);

  // Check that AST is in the expected state.
  HARDENED_RETURN_IF_ERROR(ast_check(lc_state));

  // This function is a NOP unless ROM is built for an fpga.
  device_fpga_version_print();

  sec_mmio_check_values(rnd_uint32());
  sec_mmio_check_counters(/*expected_check_count=*/1);

  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomInit, 2);
  return kErrorOk;
}

/* Function that alerts to the UART we are waiting for a JTAG bootstrap, and
 * then busy waits to allow it to occur. This function never returns because we
 * expect the host performing the bootstrap to reset the chip afterwards.
 */
void wait_for_jtag_bootstrap(void) {
  dbg_printf("No valid ECDSA key found in CTN. Waiting for JTAG bootstrap.\n");
  while (true) {
  }
}

void rom_pre_boot_check_increment_cfi(size_t value) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomPreBootCheck, value);
}

void rom_verify_increment_cfi(size_t value) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomVerify, value);
}

void rom_configure_rom_ext_increment_cfi(size_t value) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomConfigureRomExt, value);
}

void rom_boot_rom_ext_increment_cfi(size_t value) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomBootRomExt, value);
}

enum {
  /**
   * Expected value of the `kCfiRomTryBoot` counter when jumping to the first
   * ROM_EXT image.
   */
  kCfiRomTryBootManifest0Val = 11 * kCfiIncrement + kCfiRomTryBootVal0,
};

/**
 * Attempts to boot ROM_EXT.
 *
 * @return Result of the last attempt.
 */
OT_WARN_UNUSED_RESULT
static rom_error_t rom_try_boot(void) {
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomTryBoot, 1);

  boot_policy_manifests_t manifests = boot_policy_manifests_get();
  const manifest_t *manifest = manifests.ordered[0];

  // Verify the ROM_EXT.
  uint32_t flash_exec = 0;
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomTryBoot, 2, kCfiRomVerify);
  HARDENED_RETURN_IF_ERROR(rom_verify(manifest, lc_state, &sigverify_ctx,
                                      &flash_exec, rom_verify_increment_cfi));
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomTryBoot, 4);
  CFI_FUNC_COUNTER_CHECK(rom_counters, kCfiRomVerify, 3);

  // Configure attestation values, virtual addressing, and ePMP for ROM_EXT.
  uintptr_t entry_point;
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomTryBoot, 5,
                            kCfiRomConfigureRomExt);
  HARDENED_RETURN_IF_ERROR(
      rom_configure_rom_ext(manifest, &sigverify_ctx, &entry_point,
                            rom_configure_rom_ext_increment_cfi));
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomTryBoot, 7);
  CFI_FUNC_COUNTER_CHECK(rom_counters, kCfiRomConfigureRomExt, 4);

  // Perform pre-boot checks.
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomTryBoot, 8,
                            kCfiRomPreBootCheck);
  rom_pre_boot_check(
#ifdef HAS_FLASH_CTRL
      boot_data,
#endif
      lc_state, rom_pre_boot_check_increment_cfi);
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomTryBoot, 10);
  CFI_FUNC_COUNTER_CHECK(rom_counters, kCfiRomPreBootCheck, 8);

  HARDENED_RETURN_IF_ERROR(epmp_state_check());

  // Check CFI against manifest.
  const manifest_t *manifest_check = NULL;
  switch (launder32(rom_counters[kCfiRomTryBoot])) {
    case kCfiRomTryBootManifest0Val:
      HARDENED_CHECK_EQ(rom_counters[kCfiRomTryBoot],
                        kCfiRomTryBootManifest0Val);
      manifest_check = boot_policy_manifests_get().ordered[0];
      break;
    default:
      HARDENED_TRAP();
  }
  HARDENED_CHECK_EQ(manifest, manifest_check);

  // Boot ROM_EXT.
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomTryBoot, 11,
                            kCfiRomBootRomExt);
  HARDENED_RETURN_IF_ERROR(rom_boot_rom_ext(manifest_check, entry_point,
#ifdef DISCRETE_OTP_MAP
                                            imm_section_entry_point,
#endif
                                            flash_exec,
                                            rom_boot_rom_ext_increment_cfi));
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomBoot, 10);
  CFI_FUNC_COUNTER_CHECK(rom_counters, kCfiRomBootRomExt, 3);

  return kErrorRomBootFailed;
}

void second_rom_main(void) {
  CFI_FUNC_COUNTER_INIT(rom_counters, kCfiRomMain);

  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomMain, 1, kCfiRomInit);
  SHUTDOWN_IF_ERROR(rom_init());
  CFI_FUNC_COUNTER_INCREMENT(rom_counters, kCfiRomMain, 3);
  CFI_FUNC_COUNTER_CHECK(rom_counters, kCfiRomInit, 3);

  // `rom_try_boot` will not return unless there is an error.
  CFI_FUNC_COUNTER_PREPCALL(rom_counters, kCfiRomMain, 4, kCfiRomTryBoot);
  shutdown_finalize(rom_try_boot());
}

void rom_interrupt_handler(void) {
  register rom_error_t error asm("a0") = rom_irq_error();
  asm volatile("tail shutdown_finalize;" ::"r"(error));
  OT_UNREACHABLE();
}

// We only need a single handler for all ROM interrupts, but we want to
// keep distinct symbols to make writing tests easier.  In the ROM,
// alias all interrupt handler symbols to the single handler.
OT_ALIAS("rom_interrupt_handler")
noreturn void rom_exception_handler(void);

OT_ALIAS("rom_interrupt_handler")
noreturn void rom_nmi_handler(void);
