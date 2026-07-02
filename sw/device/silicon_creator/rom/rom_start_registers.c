// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "hw/top/dt/aon_timer.h"
#include "hw/top/dt/ast.h"
#include "hw/top/dt/clkmgr.h"
#include "hw/top/dt/csrng.h"
#include "hw/top/dt/edn.h"
#include "hw/top/dt/entropy_src.h"
#include "hw/top/dt/gpio.h"
#include "hw/top/dt/lc_ctrl.h"
#include "hw/top/dt/otp_ctrl.h"
#include "hw/top/dt/pinmux.h"
#include "hw/top/dt/pwrmgr.h"
#include "hw/top/dt/rstmgr.h"
#include "hw/top/dt/rv_core_ibex.h"
#include "hw/top/dt/sram_ctrl.h"

#include "hw/top/otp_ctrl_regs.h"

#define DT_SECTION __attribute__((section(".dt")))

/* Helpers to get register base addresses in rom_start.S . */

/**
 * Base address of the OTP SwCfg partition.
 */
DT_SECTION uint32_t otp_sw_cfg_base(void) {
  return dt_otp_ctrl_reg_block(kDtOtpCtrl, kDtOtpCtrlRegBlockCore) +
         OTP_CTRL_SW_CFG_WINDOW_REG_OFFSET;
}
/**
 * Base address of the rv_core_ibex registers.
 */
DT_SECTION uint32_t rv_core_ibex_reg_base(void) {
  return dt_rv_core_ibex_reg_block(kDtRvCoreIbex, kDtRvCoreIbexRegBlockCfg);
}
/**
 * Base address of the pwrmgr registers.
 */
DT_SECTION uint32_t pwrmgr_reg_base(void) {
  return dt_pwrmgr_reg_block(kDtPwrmgrAon, kDtPwrmgrRegBlockCore);
}
/**
 * Base address of the rstmgr registers.
 */
DT_SECTION uint32_t rstmgr_reg_base(void) {
  return dt_rstmgr_reg_block(kDtRstmgrAon, kDtRstmgrRegBlockCore);
}
/**
 * Base address of the aon_timer registers.
 */
DT_SECTION uint32_t aon_timer_reg_base(void) {
  return dt_aon_timer_reg_block(kDtAonTimerAon, kDtAonTimerRegBlockCore);
}
/**
 * Base address of the ast registers.
 */
DT_SECTION uint32_t ast_reg_base(void) {
  return dt_ast_reg_block(kDtAst, kDtAstRegBlockCore);
}
/**
 * Base address of the clkmgr registers.
 */
DT_SECTION uint32_t clkmgr_reg_base(void) {
  return dt_clkmgr_reg_block(kDtClkmgrAon, kDtClkmgrRegBlockCore);
}
/**
 * Base address of the pinmux registers.
 */
DT_SECTION uint32_t pinmux_reg_base(void) {
  return dt_pinmux_reg_block(kDtPinmuxAon, kDtPinmuxRegBlockCore);
}
/**
 * Base address of the gpio registers.
 */
DT_SECTION uint32_t gpio_reg_base(void) {
  return dt_gpio_reg_block(kDtGpio, kDtGpioRegBlockCore);
}
/**
 * Base address of the lc_ctrl registers.
 */
DT_SECTION uint32_t lc_ctrl_reg_base(void) {
  return dt_lc_ctrl_reg_block(kDtLcCtrl, kDtLcCtrlRegBlockRegs);
}
/**
 * Base address of the entropy_src registers.
 */
DT_SECTION uint32_t entropy_src_reg_base(void) {
  return dt_entropy_src_reg_block(kDtEntropySrc, kDtEntropySrcRegBlockCore);
}
/**
 * Base address of the csrng registers.
 */
DT_SECTION uint32_t csrng_reg_base(void) {
  return dt_csrng_reg_block(kDtCsrng, kDtCsrngRegBlockCore);
}
/**
 * Base address of the edn0 registers.
 */
DT_SECTION uint32_t edn0_reg_base(void) {
  return dt_edn_reg_block(kDtEdn0, kDtEdnRegBlockCore);
}
/**
 * Base address of the sram_ctrl_main registers.
 */
DT_SECTION uint32_t sram_ctrl_main_reg_base(void) {
  return dt_sram_ctrl_reg_block(kDtSramCtrlMain, kDtSramCtrlRegBlockRegs);
}
