// Copyright lowRISC contributors (OpenTitan project).
// Copyright zeroRISC Inc.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0

#include "sw/device/lib/base/memory.h"
#include "sw/device/silicon_creator/lib/acc_boot_services.h"
#include "sw/device/silicon_creator/lib/base/boot_measurements.h"
#include "sw/device/silicon_creator/lib/base/util.h"
#include "sw/device/silicon_creator/lib/cfi.h"
#include "sw/device/silicon_creator/lib/drivers/lifecycle.h"
#include "sw/device/silicon_creator/lib/drivers/otp.h"
#include "sw/device/silicon_creator/lib/drivers/rnd.h"
#include "sw/device/silicon_creator/lib/manifest.h"
#include "sw/device/silicon_creator/lib/sigverify/sigverify.h"
#include "sw/device/silicon_creator/rom/address_translation.h"
#include "sw/device/silicon_creator/rom/boot_policy.h"
#include "sw/device/silicon_creator/rom/sigverify_otp_keys.h"

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
                       void (*rom_verify_increment_cfi)(size_t value)) {
  const uint32_t *anti_rollback = NULL;
  size_t anti_rollback_len = 0;
#ifdef HAS_FLASH_CTRL
  // Check security version and manifest constraints.
  //
  // The poisoning work (`anti_rollback`) invalidates signatures if the
  // security version of the manifest is smaller than the minimum required
  // security version.
  const uint32_t extra_word = UINT32_MAX;
  if (launder32(manifest->security_version) <
      boot_data.min_security_version_rom_ext) {
    anti_rollback = &extra_word;
    anti_rollback_len = sizeof(extra_word);
  }
  *flash_exec = 0;
  HARDENED_RETURN_IF_ERROR(boot_policy_manifest_check(manifest, &boot_data));
#endif

  // Load ACC boot services app.
  //
  // This will be reused by later boot stages.
  HARDENED_RETURN_IF_ERROR(acc_boot_app_load());
  rom_verify_increment_cfi(1);

  // Load secure boot keys from OTP into RAM.
  HARDENED_RETURN_IF_ERROR(sigverify_otp_keys_init(sigverify_ctx));
  // Look up the ECDSA key in the manifest to ensure it is authorized for the
  // current lifecycle state in OTP.
  sigverify_spx_config_id_t unused;
  HARDENED_RETURN_IF_ERROR(sigverify_otp_key_lookup(
      sigverify_ctx, kSigVerifyOtpKeysKeySpecRotOwnerFirmwareCodesign,
      kSigVerifyOtpKeysAlgorithmEcdsa, &manifest->ecdsa_public_key,
      /*spx_key=*/NULL, lc_state,
      /*spx_config=*/&unused));

  sigverify_spx_config_id_t spx_config = 0;
  const sigverify_spx_signature_t *spx_signature = NULL;
  uint32_t sigverify_spx_en = sigverify_spx_verify_enabled(lc_state);
  const manifest_ext_spx_key_t *ext_spx_key;
  if (launder32(sigverify_spx_en) != kSigverifySpxDisabledOtp) {
    HARDENED_RETURN_IF_ERROR(manifest_ext_get_spx_key(manifest, &ext_spx_key));
    // Look up the SPX key in the manifest to ensure it is authorized for the
    // current lifecycle state in OTP.
    HARDENED_RETURN_IF_ERROR(sigverify_otp_key_lookup(
        sigverify_ctx, kSigVerifyOtpKeysKeySpecRotOwnerFirmwareCodesign,
        kSigVerifyOtpKeysAlgorithmSpx,
        /*ecdsa_key=*/NULL, &ext_spx_key->key, lc_state, &spx_config));
    const manifest_ext_spx_signature_t *ext_spx_signature;
    HARDENED_RETURN_IF_ERROR(
        manifest_ext_get_spx_signature(manifest, &ext_spx_signature));
    spx_signature = &ext_spx_signature->signature;
  } else {
    HARDENED_CHECK_EQ(sigverify_spx_en, kSigverifySpxDisabledOtp);
  }

  // Measure ROM_EXT and portions of manifest via SHA256 digest.
  // Initialize ROM_EXT measurement in .static_critical with garbage.
  memset(boot_measurements.rom_ext.data, (int)rnd_uint32(),
         sizeof(boot_measurements.rom_ext.data));
  // Add anti-rollback poisoning word to measurement.
  hmac_sha256_init();
  hmac_sha256_update(anti_rollback, anti_rollback_len);
#ifdef HAS_FLASH_CTRL
  HARDENED_CHECK_GE(manifest->security_version,
                    boot_data.min_security_version_rom_ext);
#endif
  // Add manifest usage constraints to the measurement.
  manifest_usage_constraints_t usage_constraints_from_hw;
  sigverify_usage_constraints_get(manifest->usage_constraints.selector_bits,
                                  &usage_constraints_from_hw);
  hmac_sha256_update(&usage_constraints_from_hw,
                     sizeof(usage_constraints_from_hw));
  // Add remaining part of manifest / ROM_EXT image to the measurement.
  manifest_digest_region_t digest_region = manifest_digest_region_get(manifest);
  // Add remaining part of manifest / ROM_EXT image to the measurement.
  hmac_sha256_update(digest_region.start, digest_region.length);
  hmac_sha256_process();
  // The ECDSA verify function expects the digest in reverse order, which
  // is what hmac_sha256_final produces.
  hmac_digest_t rev_digest;
  hmac_sha256_final(&rev_digest);
  // The SPHINCS+ verify function expects the digest in the natural order,
  // so we copy and reverse the bytes.
  hmac_digest_t fwd_digest = rev_digest;
  util_reverse_bytes(&fwd_digest, sizeof(fwd_digest));
  // Copy the ROM_EXT measurement to the .static_critical section.
  static_assert(sizeof(boot_measurements.rom_ext) == sizeof(rev_digest),
                "Unexpected ROM_EXT digest size.");
  memcpy(&boot_measurements.rom_ext, &rev_digest,
         sizeof(boot_measurements.rom_ext));

  rom_verify_increment_cfi(2);

  /**
   * Verify the ECDSA/SPX+ signatures of ROM_EXT.
   *
   * We swap the order of signature verifications randomly.
   */
  *flash_exec = 0;
  if (rnd_uint32() < 0x80000000) {
    HARDENED_RETURN_IF_ERROR(sigverify_ecdsa_p256_verify(
        &manifest->ecdsa_signature, &manifest->ecdsa_public_key, &rev_digest,
        flash_exec));

    return sigverify_spx_verify(
        spx_signature, &ext_spx_key->key, spx_config, lc_state,
        &usage_constraints_from_hw, sizeof(usage_constraints_from_hw),
        anti_rollback, anti_rollback_len, digest_region.start,
        digest_region.length, &fwd_digest, flash_exec);
  } else {
    HARDENED_RETURN_IF_ERROR(sigverify_spx_verify(
        spx_signature, &ext_spx_key->key, spx_config, lc_state,
        &usage_constraints_from_hw, sizeof(usage_constraints_from_hw),
        anti_rollback, anti_rollback_len, digest_region.start,
        digest_region.length, &fwd_digest, flash_exec));

    return sigverify_ecdsa_p256_verify(&manifest->ecdsa_signature,
                                       &manifest->ecdsa_public_key, &rev_digest,
                                       flash_exec);
  }
}
