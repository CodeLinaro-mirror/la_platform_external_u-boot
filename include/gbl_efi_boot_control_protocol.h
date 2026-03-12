/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (C) 2024 The Android Open Source Project
 */

#ifndef __EFI_GBL_AB_H__
#define __EFI_GBL_AB_H__

#include <efi_api.h>

#define EFI_GBL_AB_PROTOCOL_REVISION 0x00010000

enum gbl_efi_unbootable_reason {
	UNKNOWN_REASON = 0,
	NO_MORE_TRIES,
	SYSTEM_UPDATE,
	USER_REQUESTED,
	VERIFICATION_FAILURE,
};

enum gbl_efi_boot_reason {
	EMPTY_EFI_BOOT_REASON = 0,
	UNKNOWN_EFI_BOOT_REASON = 1,
	WATCHDOG = 14,
	KERNEL_PANIC = 15,
	RECOVERY = 3,
	BOOTLOADER = 55,
	COLD = 56,
	HARD = 57,
	WARM = 58,
	SHUTDOWN,
	REBOOT = 18,
};

struct efi_gbl_slot_info {
	/* One UTF-8 encoded single character */
	u32 suffix;
	/* Any value other than those explicitly enumerated in EFI_UNBOOTABLE_REASON
	 will be interpreted as UNKNOWN_REASON. */
	u32 unbootable_reason;
	u8 priority;
	u8 tries;
	/* Value of 1 if slot has successfully booted. */
	u8 successful;
	u8 merge_status;
};

struct efi_gbl_slot_metadata_block {
	/* Value of 1 if persistent metadata tracks slot unbootable reasons. */
	u8 unbootable_metadata;
	u8 max_retries;
	u8 slot_count;
};

extern const efi_guid_t efi_gbl_ab_boot_guid;

struct efi_gbl_slot_protocol {
	u32 version;
	/* Slot metadata query methods */
	efi_status_t(EFIAPI *load_boot_data)(
		/* in */ struct efi_gbl_slot_protocol *,
		/* out */ struct efi_gbl_slot_metadata_block *);
	efi_status_t(EFIAPI *get_slot_info)(
		/* in */ struct efi_gbl_slot_protocol *, /* in */ u8,
		/* out */ struct efi_gbl_slot_info *);
	efi_status_t(EFIAPI *get_current_slot)(
		/* in */ struct efi_gbl_slot_protocol *,
		/* out */ struct efi_gbl_slot_info *);
	/* Slot metadata manipulation methods */
	efi_status_t(EFIAPI *set_active_slot)(
		/* in */ struct efi_gbl_slot_protocol *, /* in */ u8);
	efi_status_t(EFIAPI *set_slot_unbootable)(
		/* in */ struct efi_gbl_slot_protocol *, /* in */ u8,
		/* in */ u32);
	efi_status_t(EFIAPI *mark_boot_attempt)(
		/* in */ struct efi_gbl_slot_protocol *);
	efi_status_t(EFIAPI *reinitialize)(
		/* in */ struct efi_gbl_slot_protocol *);
	/* Miscellaneous methods  */
	efi_status_t(EFIAPI *get_boot_reason)(
		/* in */ struct efi_gbl_slot_protocol *, /* out */ u32 *,
		/* in-out */ size_t *, /* out */ u8 *);
	efi_status_t(EFIAPI *set_boot_reason)(
		/* in */ struct efi_gbl_slot_protocol *, /* in */ u32,
		/* in */ size_t, /* in */ const u8 *);
	efi_status_t(EFIAPI *flush)(/* in */ struct efi_gbl_slot_protocol *);
};

efi_status_t efi_gbl_ab_register(void);

#endif /* __EFI_GBL_AB_H__ */
