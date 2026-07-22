/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (C) 2026 The Android Open Source Project
 */

#ifndef __GBL_EFI_AVF_H__
#define __GBL_EFI_AVF_H__

#include <efi.h>
#include <efi_api.h>

#define GBL_EFI_AVF_PROTOCOL_REVISION 0x00010000

extern const efi_guid_t gbl_efi_avf_guid;

struct gbl_efi_avf_protocol {
	u64 revision;

	efi_status_t(EFIAPI *read_vendor_dice_handover)(
		/* in */ struct gbl_efi_avf_protocol *self,
		/* in out */ size_t *handover_size,
		/* out */ u8 *handover);

	efi_status_t(EFIAPI *read_secretkeeper_public_key)(
		/* in */ struct gbl_efi_avf_protocol *self,
		/* in out */ size_t *public_key_size,
		/* out */ u8 *public_key);
};

efi_status_t gbl_efi_avf_register(void);

#endif /* __GBL_EFI_AVF_H__ */
