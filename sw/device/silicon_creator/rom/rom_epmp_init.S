// Copyright lowRISC contributors (OpenTitan project).
// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/silicon_creator/lib/epmp_defs.h"

#if defined(PAVONA_IS_EGRET)
 #include "hw/top_egret/sw/autogen/top_egret_memory.h"
#define MMIO_BASE_ADDR TOP_EGRET_MMIO_BASE_ADDR
#define MMIO_SIZE_BYTES TOP_EGRET_MMIO_SIZE_BYTES
#elif defined(PAVONA_IS_DRAGONFLY)
#include "hw/top_dragonfly/sw/autogen/top_dragonfly_memory.h"
#define MMIO_BASE_ADDR TOP_DRAGONFLY_MMIO_BASE_ADDR
#define MMIO_SIZE_BYTES TOP_DRAGONFLY_MMIO_SIZE_BYTES
#endif

/**
 * Helper macro to move v into the correct position to set the pmp{N*4+i}cfg
 * field in the pmpcfg{N} register.
 *
 * Example: set value for pmp5cfg which is a field of pmpcfg1.
 *
 *   li   t0, CFG_INDEX(5 % 4, v)
 *   csrw pmpcfg1, t0
 */
#define CFG_INDEX(i, v) ((v) << ((i)*8))

/**
 * Encode address for Top-Of-Range addressing mode.
 */
#define TOR(addr) ((addr) >> 2)

// -----------------------------------------------------------------------------

  /**
   * The "ax" flag below is necessary to ensure that this section
   * is allocated space in ROM by the linker.
   */
  .section .crt, "ax", @progbits

  /**
   * Configure the CPU's Enhanced Physical Memory Protection (ePMP) feature.
   *
   * This function follows the standard ILP32 calling convention but does not
   * require a valid stack pointer, thread pointer or global pointer.
   *
   * Arguments:
   *
   * a0: ROM read-only region NAPOT encoding.
   * a1: eFLASH/CTN region NAPOT encoding.
   * a2: RAM region NAPOT encoding.
   *
   * May clobber temporary registers (t0-t6).
   */
  .balign 4
  .global rom_epmp_init
  .type rom_epmp_init, @function
rom_epmp_init:
  // Pre-encoded addresses defined in linker script.
  .extern _epmp_text_tor_lo
  .extern _epmp_text_tor_hi
  .extern _epmp_stack_guard_na4

  // Setup PMP address registers.

  // ROM TEXT
  la   t0, _epmp_text_tor_lo
  csrw pmpaddr0, t0
  la   t0, _epmp_text_tor_hi
  csrw pmpaddr1, t0

  // ROM (NAPOT encoding passed as argument in a0).
  csrw pmpaddr2, a0

  // MMIO
  li   t0, TOR(MMIO_BASE_ADDR)
  csrw pmpaddr10, t0
  li   t0, TOR(MMIO_BASE_ADDR + MMIO_SIZE_BYTES)
  csrw pmpaddr11, t0

  // eFLASH/CTN (NAPOT encoding passed as argument in a1).
#ifndef HAS_ROM_CTRL1
  csrw pmpaddr13, a1
#endif

  // Stack guard
  la   t0, _epmp_stack_guard_na4
  csrw pmpaddr14, t0

  // RAM (NAPOT encoding passed as argument in a2).
  csrw pmpaddr15, a2

  // Set PMP configuration registers.
  li   t0, CFG_INDEX(1 %  4, EPMP_CFG_A_TOR   | EPMP_CFG_LRX)  | /* ROM TEXT    */ \
           CFG_INDEX(2 %  4, EPMP_CFG_A_NAPOT | EPMP_CFG_LR)     /* ROM         */
  li   t2, CFG_INDEX(11 % 4, EPMP_CFG_A_TOR   | EPMP_CFG_LRW)    /* MMIO        */
#ifdef HAS_ROM_CTRL1
  li   t3, CFG_INDEX(14 % 4, EPMP_CFG_A_NA4   | EPMP_CFG_L)    | /* Stack Guard */ \
           CFG_INDEX(15 % 4, EPMP_CFG_A_NAPOT | EPMP_CFG_LRW)    /* RAM         */
#else
  li   t3, CFG_INDEX(13 % 4, EPMP_CFG_A_NAPOT | EPMP_CFG_LR)   | /* eFLASH/CTN  */ \
           CFG_INDEX(14 % 4, EPMP_CFG_A_NA4   | EPMP_CFG_L)    | /* Stack Guard */ \
           CFG_INDEX(15 % 4, EPMP_CFG_A_NAPOT | EPMP_CFG_LRW)    /* RAM         */
#endif

  csrw pmpcfg0, t0
  csrw pmpcfg1, zero
  csrw pmpcfg2, t2
  csrw pmpcfg3, t3

  ret
  .size rom_epmp_init, .-rom_epmp_init
