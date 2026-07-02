// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/silicon_creator/rom/rom_epmp.h"

#include "hw/top/dt/rom_ctrl.h"
#include "hw/top/dt/sram_ctrl.h"

#if HAS_FLASH_CTRL
#include "hw/top/dt/flash_ctrl.h"
#else
#include "hw/top/dt/soc_proxy.h"
#endif

#include "sw/device/lib/base/bitfield.h"
#include "sw/device/lib/base/csr.h"
#include "sw/device/lib/base/memory.h"
#include "sw/device/silicon_creator/lib/drivers/epmp.h"

// Symbols defined in linker script.
extern char _stack_start[];  // Lowest stack address.
extern char _text_start[];   // Start of executable code.
extern char _text_end[];     // End of executable code.

void rom_epmp_state_init(lifecycle_state_t lc_state) {
  // Address space definitions.
  //
  // Note that the stack guard is placed at _stack_start because the stack
  // grows downward from _stack_end.
  const epmp_region_t rom_text = {.start = (uintptr_t)_text_start,
                                  .end = (uintptr_t)_text_end};
  epmp_region_t rom = {.start = rom_ctrl_rom_base(),
                       .end = rom_ctrl_rom_base() + rom_ctrl_rom_size()};
#ifndef HAS_ROM_CTRL1
#ifdef HAS_FLASH_CTRL
  epmp_region_t rom_ext = {
      .start = flash_ctrl_mem_base(),
      .end = flash_ctrl_mem_base() + flash_ctrl_mem_size()};
#else
  epmp_region_t rom_ext = {.start = soc_proxy_ctn_base(),
                           .end = soc_proxy_ctn_base() + soc_proxy_ctn_size()};
#endif
#endif
  // TODO: Have topgen generate a generic `top.h` header with these constants.
#if defined(PAVONA_IS_EGRET)
  const epmp_region_t mmio = {
      .start = TOP_EGRET_MMIO_BASE_ADDR,
      .end = TOP_EGRET_MMIO_BASE_ADDR + TOP_EGRET_MMIO_SIZE_BYTES};
#elif defined(PAVONA_IS_DRAGONFLY)
  const epmp_region_t mmio = {
      .start = TOP_DRAGONFLY_MMIO_BASE_ADDR,
      .end = TOP_DRAGONFLY_MMIO_BASE_ADDR + TOP_DRAGONFLY_MMIO_SIZE_BYTES};
#endif
  const epmp_region_t stack_guard = {.start = (uintptr_t)_stack_start,
                                     .end = (uintptr_t)_stack_start + 4};
  epmp_region_t ram = {.start = sram_ctrl_ram_base(),
                       .end = sram_ctrl_ram_base() + sram_ctrl_ram_size()};

  // Initialize in-memory copy of ePMP register state.
  //
  // The actual hardware configuration is performed separately, either by reset
  // logic or in assembly. This code must be kept in sync with any changes
  // to the hardware configuration.
  memset(&epmp_state, 0, sizeof(epmp_state));
  epmp_state_configure_tor(1, rom_text, kEpmpPermLockedReadExecute);
  epmp_state_configure_napot(2, rom, kEpmpPermLockedReadOnly);
  epmp_state_configure_tor(11, mmio, kEpmpPermLockedReadWrite);
#ifndef HAS_ROM_CTRL1
  epmp_state_configure_napot(13, rom_ext, kEpmpPermLockedReadOnly);
#endif
  epmp_state_configure_na4(14, stack_guard, kEpmpPermLockedNoAccess);
  epmp_state_configure_napot(15, ram, kEpmpPermLockedReadWrite);
  epmp_state.mseccfg = EPMP_MSECCFG_MMWP | EPMP_MSECCFG_RLB;
}

/* Helpers to get ePMP region addresses in rom_epmp_start.S . */

#define DT_SECTION __attribute__((section(".dt")))

/**
 * Encodes a memory region in Naturally Aligned Power-Of-Two (NAPOT) encoding
 * for ePMP.
 */
DT_SECTION uint32_t encode_napot(uint32_t base, uint32_t length) {
  return (base >> 2) | ((length - 1) >> 3);
}
/**
 * Base address of the ROM.
 */
DT_SECTION uint32_t rom_ctrl_rom_base(void) {
  return dt_rom_ctrl_memory_base(kDtRomCtrlFirst, kDtRomCtrlMemoryRom);
}
/**
 * Size of the ROM.
 */
DT_SECTION uint32_t rom_ctrl_rom_size(void) {
  return dt_rom_ctrl_memory_size(kDtRomCtrlFirst, kDtRomCtrlMemoryRom);
}
/**
 * NAPOT encoding of the ROM region.
 */
DT_SECTION uint32_t rom_ctrl_rom_napot(void) {
  return encode_napot(rom_ctrl_rom_base(), rom_ctrl_rom_size());
}
#ifdef HAS_FLASH_CTRL
/**
 * Base address of the flash memory.
 */
DT_SECTION uint32_t flash_ctrl_mem_base(void) {
  return dt_flash_ctrl_memory_base(kDtFlashCtrl, kDtFlashCtrlMemoryMem);
}
/**
 * Size of the flash memory.
 */
DT_SECTION uint32_t flash_ctrl_mem_size(void) {
  return dt_flash_ctrl_memory_size(kDtFlashCtrl, kDtFlashCtrlMemoryMem);
}
/**
 * NAPOT encoding of the Flash memory region.
 */
DT_SECTION uint32_t flash_ctrl_mem_napot(void) {
  return encode_napot(flash_ctrl_mem_base(), flash_ctrl_mem_size());
}
#else
/**
 * Base address of the CTN SRAM.
 */
DT_SECTION uint32_t soc_proxy_ctn_base(void) {
  return dt_soc_proxy_memory_base(kDtSocProxy, kDtSocProxyMemoryCtn);
}
/**
 * Size of the CTN SRAM.
 */
DT_SECTION uint32_t soc_proxy_ctn_size(void) {
  return dt_soc_proxy_memory_size(kDtSocProxy, kDtSocProxyMemoryCtn);
}
/**
 * NAPOT encoding of the CTN SRAM region.
 */
DT_SECTION uint32_t soc_proxy_ctn_napot(void) {
  return encode_napot(soc_proxy_ctn_base(), soc_proxy_ctn_size());
}
#endif
/**
 * Base address of the RAM.
 */
DT_SECTION uint32_t sram_ctrl_ram_base(void) {
  return dt_sram_ctrl_memory_base(kDtSramCtrlMain, kDtSramCtrlMemoryRam);
}
/**
 * Size of the RAM.
 */
DT_SECTION uint32_t sram_ctrl_ram_size(void) {
  return dt_sram_ctrl_memory_size(kDtSramCtrlMain, kDtSramCtrlMemoryRam);
}
/**
 * NAPOT encoding of the RAM region.
 */
DT_SECTION uint32_t sram_ctrl_ram_napot(void) {
  return encode_napot(sram_ctrl_ram_base(), sram_ctrl_ram_size());
}
