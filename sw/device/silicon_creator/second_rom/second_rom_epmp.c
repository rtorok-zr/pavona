// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/silicon_creator/second_rom/second_rom_epmp.h"

#include "hw/top/dt/rv_dm.h"
#include "hw/top/dt/soc_proxy.h"
#include "hw/top/dt/sram_ctrl.h"
#include "sw/device/lib/base/bitfield.h"
#include "sw/device/lib/base/csr.h"
#include "sw/device/lib/base/memory.h"
#include "sw/device/silicon_creator/lib/drivers/epmp.h"

#ifdef PAVONA_IS_DRAGONFLY
#include "hw/top_dragonfly/sw/autogen/top_dragonfly.h"
#endif

// Symbols defined in linker script.
extern char _rom_ext_virtual_start_address[];  // Start of ROM_EXT (VMA)
extern char _rom_ext_virtual_size[];           // Size of ROM_EXT (VMA)

void second_rom_epmp_state_init(void) {
  // Open Mailbox RAM and section of eFLASH/CTN containing ROM_EXT manifest.
  epmp_region_t ram_mbox = {
      .start = sram_ctrl_mbox_ram_base(),
      .end = sram_ctrl_mbox_ram_base() + sram_ctrl_mbox_ram_size()};
#ifdef HAS_FLASH_CTRL
  epmp_region_t rom_ext = {
      .start = flash_ctrl_mem_base(),
      .end = flash_ctrl_mem_base() + _rom_ext_virtual_size};
#else
  // TODO: Make this offset available via `dt`. This won't compile for tops
  // other than Dragonfly.
#ifdef PAVONA_IS_DRAGONFLY
  epmp_region_t rom_ext = {.start = TOP_DRAGONFLY_SOC_PROXY_RAM_CTN_BASE_ADDR,
                           .end = TOP_DRAGONFLY_SOC_PROXY_RAM_CTN_BASE_ADDR +
                                  (uintptr_t)_rom_ext_virtual_size};
#endif
#endif
  // Update ePMP hardware registers and in-memory state.
  epmp_set_napot(12, ram_mbox, kEpmpPermLockedReadWrite);
  epmp_set_napot(13, rom_ext, kEpmpPermLockedReadOnly);
}

/**
 * Base address of the Mbox RAM.
 */
inline uint32_t sram_ctrl_mbox_ram_base(void) {
  return dt_sram_ctrl_memory_base(kDtSramCtrlMbox, kDtSramCtrlMemoryRam);
}
/**
 * Size of the Mbox RAM.
 */
inline uint32_t sram_ctrl_mbox_ram_size(void) {
  return dt_sram_ctrl_memory_size(kDtSramCtrlMbox, kDtSramCtrlMemoryRam);
}
#ifdef HAS_FLASH_CTRL
/**
 * Base address of the flash memory.
 */
inline uint32_t flash_ctrl_mem_base(void) {
  return dt_flash_ctrl_memory_base(kDtFlashCtrl, kDtFlashCtrlMemoryMem);
}
/**
 * Size of the flash memory.
 */
inline uint32_t flash_ctrl_mem_size(void) {
  return dt_flash_ctrl_memory_size(kDtFlashCtrl, kDtFlashCtrlMemoryMem);
}
#else
/**
 * Base address of the CTN SRAM.
 */
inline uint32_t soc_proxy_ctn_base(void) {
  return dt_soc_proxy_memory_base(kDtSocProxy, kDtSocProxyMemoryCtn);
}
/**
 * Size of the CTN SRAM.
 */
inline uint32_t soc_proxy_ctn_size(void) {
  return dt_soc_proxy_memory_size(kDtSocProxy, kDtSocProxyMemoryCtn) / 2;
}
#endif
