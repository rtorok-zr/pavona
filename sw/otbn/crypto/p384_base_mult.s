/* Copyright zeroRISC Inc. */
/* Licensed under the Apache License, Version 2.0, see LICENSE for details. */
/* SPDX-License-Identifier: Apache-2.0 */

/* Copyright lowRISC contributors (OpenTitan project). */
/* Licensed under the Apache License, Version 2.0, see LICENSE for details. */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 *   P-384 specific routines for constant-time base point multiplication.
 */

 .section .text

/**
 * Externally callable routine for P-384 base point multiplication
 *
 * returns Q = d (*) G
 *         where Q is a resulting valid P-384 curve point in affine
 *                   coordinates,
 *               G is the base point of curve P-384, and
 *               d is a 384-bit scalar.
 *
 * This routine calls the base point multiplication routine.
 * Furthermore, this routine does an is on curve check on the result.
 *
 * @param[in]  dmem[d0]: 1st private key share d0 in dmem
 * @param[in]  dmem[d1]: 2nd private key share d1 in dmem
 * @param[out]  dmem[x]: x-coordinate in dmem
 * @param[out]  dmem[y]: y-coordinate in dmem
 *
 * 384-bit quantities have to be provided in dmem in little-endian format,
 * 512 bit aligned, with the highest 128 bit set to zero.
 *
 * Flags: When leaving this subroutine, the M, L and Z flags of FG0 correspond
 *        to the computed affine y-coordinate.
 *
 * clobbered registers: x2, x3, x9 to x13, x17 to x23, x26 to x30
 *                      w0 to w30
 * clobbered flag groups: FG0
 */
.globl p384_base_mult_checked
p384_base_mult_checked:
  jal       x1, p384_base_mult

  /* check if the result is on the p384 curve */
  jal       x1, p384_isoncurve_check

  ret


/**
 * Trigger a fault if the FG0.Z flag is 0.
 *
 * If the flag is 0, then this routine will trigger an `ILLEGAL_INSN` error and
 * abort the OTBN program. If the flag is 1, the routine will essentially do
 * nothing.
 *
 * NOTE: Be careful when calling this routine that the FG0.Z flag is not
 * sensitive; since aborting the program will be quicker than completing it,
 * the flag's value is likely clearly visible to an attacker through timing.
 *
 * @param[in]    w31: all-zero
 * @param[in]  FG0.Z: boolean indicating (complement of) fault condition
 *
 * clobbered registers: x2
 * clobbered flag groups: none
 */
trigger_fault_if_fg0_not_z:
  /* Read the FG0.Z flag (position 3).
       x2 <= FG0.Z */
  csrrw     x2, FG0, x0
  andi      x2, x2, 8
  srli      x2, x2, 3

  /* Subtract 1 from FG0.Z.
       x2 <= x2 - 1 = FG0.Z ? 0 : 2^32 - 1 */
  addi      x2, x2, -1

  /* The `bn.lid` instruction causes an `BAD_DATA_ADDR` error if the
     memory address is out of bounds. Therefore, if FG0.Z is 0, this
     instruction causes an error, but if FG0.Z is 1 it simply loads the word at
     address 0 into w31. */
  li         x3, 31
  bn.lid     x3, 0(x2)

  /* If we get here, the flag must have been 1. Restore w31 to zero and return.
       w31 <= 0 */
  bn.xor     w31, w31, w31

  ret


/**
 * Externally callable routine for P-384 base point multiplication
 *
 * returns Q = d (*) G
 *         where Q is a resulting valid P-384 curve point in affine
 *                   coordinates,
 *               G is the base point of curve P-384, and
 *               d is a 384-bit scalar.
 *
 * Sets up context and calls the internal scalar multiplication routine.
 * This routine runs in constant time.
 *
 * @param[in]  dmem[d0]: 1st private key share d0 in dmem
 * @param[in]  dmem[d1]: 2nd private key share d1 in dmem
 * @param[out]  dmem[x]: x-coordinate in dmem
 * @param[out]  dmem[y]: y-coordinate in dmem
 *
 * 384-bit quantities have to be provided in dmem in little-endian format,
 * 512 bit aligned, with the highest 128 bit set to zero.
 *
 * Flags: When leaving this subroutine, the M, L and Z flags of FG0 correspond
 *        to the computed affine y-coordinate.
 *
 * clobbered registers: x2, x3, x9 to x13, x17 to x21, x26 to x30
 *                      w0 to w30
 * clobbered flag groups: FG0
 */
.globl p384_base_mult
p384_base_mult:

  /* set dmem pointer to x-coordinate of base point*/
  la        x20, p384_gx

  /* set dmem pointer to y-coordinate of base point */
  la        x21, p384_gy

  /* set dmem pointer to domain parameter b */
  la        x28, p384_b

  /* set dmem pointer to scratchpad */
  la        x30, scratchpad

  /* set dmem pointer to 1st private key share d0 */
  la        x17, d0

  /* set dmem pointer to 1st private key share d0 */
  la        x19, d1

  /* load domain parameter n (order of base point)
     [w11, w10] = n = dmem[p384_n] */
  li        x2, 10
  la        x3, p384_n
  bn.lid    x2++, 0(x3)
  bn.lid    x2++, 32(x3)

  /* load domain parameter p (modulus)
     [w13, w12] = p = dmem[p384_p] */
  la        x3, p384_p
  bn.lid    x2++, 0(x3)
  bn.lid    x2++, 32(x3)

  /* init all-zero reg */
  bn.xor    w31, w31, w31

  /* scalar multiplication in projective space
     [w30:w25] <= (x, y, z) = d * G */
  jal       x1, scalar_mult_int_p384

  /* conversion into affine space
     [w28:w25] <= (x, y) */
  jal       x1, proj_to_affine_p384

  /* set dmem pointer to point x-coordinate */
  la        x20, x

  /* set dmem pointer to point y-coordinate */
  la        x21, y

  /* store result in dmem */
  li        x2, 25
  bn.sid    x2++, 0(x20)
  bn.sid    x2++, 32(x20)
  bn.sid    x2++, 0(x21)
  bn.sid    x2++, 32(x21)

  ret

/* variables and scratchpad memory */
.section .bss

.balign 32

/* 1st private key share d0 */
.globl d0
.weak d0
d0:
  .zero 64

/* 2nd private key share d1 */
.globl d1
.weak d1
d1:
  .zero 64

/* buffer for x-coordinate */
.globl x
.weak x
x:
  .zero 64

/* buffer for y-coordinate */
.globl y
.weak y
y:
  .zero 64

/* 704 bytes of scratchpad memory */
.balign 32
.globl scratchpad
.weak scratchpad
scratchpad:
  .zero 704
