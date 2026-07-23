/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (C) 2026 The Android Open Source Project
 */

#include <efi.h>
#include <efi_api.h>
#include <gbl_efi_avf_protocol.h>
#include <efi_loader.h>
#include <log.h>
#include <string.h>
#if CONFIG_IS_ENABLED(ARM64)
#include <asm/system.h>
#endif

const efi_guid_t gbl_efi_avf_guid = GBL_EFI_AVF_PROTOCOL_GUID;
static struct gbl_efi_avf_protocol gbl_efi_avf_proto;

/*
 * Minimal valid AndroidDiceHandover CBOR structure:
 * - 0xa3: Map of 3 key-value pairs (AndroidDiceHandover)
 * - Key 1: CDI_Attest (0x01, 0x58, 0x20 + 32 zero bytes)
 * - Key 2: CDI_Seal (0x02, 0x58, 0x20 + 32 zero bytes)
 * - Key 3: DiceChain (0x03, 0x82 + 2-element array: Root Authority + Leaf COSE_Sign1)
 *   - Element 0: Root Authority descriptor (CBOR Map)
 *   - Element 1: Leaf COSE_Sign1 cert (protected header {1: -8}, empty unprotected,
 *     payload SubjectPublicKey, empty signature)
 */
static const u8 dummy_vendor_handover[] = {
	0xa3, 0x01, 0x58, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x02, 0x58, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
	0x82, 0xa6, 0x01, 0x02, 0x03, 0x27, 0x04, 0x02, 0x20, 0x01, 0x21, 0x40,
	0x22, 0x40, 0x84, 0x43, 0xa1, 0x01, 0x27, 0xa0, 0x4a, 0xa1, 0x3a, 0x00,
	0x47, 0x44, 0x57, 0x43, 0xa1, 0x03, 0x27, 0x40,
};

static const char dummy_secretkeeper_public_key[] = "secret_keeper_public_key";

static efi_status_t EFIAPI read_vendor_dice_handover(
	struct gbl_efi_avf_protocol *this, size_t *handover_size, u8 *handover)
{
	EFI_ENTRY("%p, %p, %p", this, handover_size, handover);
	if (this != &gbl_efi_avf_proto || handover_size == NULL) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}
	size_t required_size = sizeof(dummy_vendor_handover);
	if (*handover_size < required_size) {
		*handover_size = required_size;
		return EFI_EXIT(EFI_BUFFER_TOO_SMALL);
	}

	*handover_size = required_size;
	if (handover != NULL) {
		memcpy(handover, dummy_vendor_handover, required_size);
	}

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI
read_secretkeeper_public_key(struct gbl_efi_avf_protocol *this,
			     size_t *public_key_size, u8 *public_key)
{
	EFI_ENTRY("%p, %p, %p", this, public_key_size, public_key);
	if (this != &gbl_efi_avf_proto || public_key_size == NULL) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	size_t required_size = strlen(dummy_secretkeeper_public_key);
	if (*public_key_size < required_size) {
		*public_key_size = required_size;
		return EFI_EXIT(EFI_BUFFER_TOO_SMALL);
	}

	*public_key_size = required_size;
	if (public_key != NULL) {
		memcpy(public_key, dummy_secretkeeper_public_key,
		       required_size);
	}

	return EFI_EXIT(EFI_SUCCESS);
}

static struct gbl_efi_avf_protocol gbl_efi_avf_proto = {
	.revision = GBL_EFI_AVF_PROTOCOL_REVISION,
	.read_vendor_dice_handover = read_vendor_dice_handover,
	.read_secretkeeper_public_key = read_secretkeeper_public_key,
};

efi_status_t gbl_efi_avf_register(void)
{
#if CONFIG_IS_ENABLED(ARM64)
	if (current_el() != 2) {
		log_info(
			"Skipping GBL_EFI_AVF_PROTOCOL: U-Boot is not running at EL2\n");
		return EFI_SUCCESS;
	}
#endif

	efi_status_t ret = efi_add_protocol(efi_root, &gbl_efi_avf_guid,
					    &gbl_efi_avf_proto);
	if (ret != EFI_SUCCESS) {
		log_err("Failed to install GBL_EFI_AVF_PROTOCOL: 0x%lx\n", ret);
	}

	return ret;
}
