/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (C) 2025 The Android Open Source Project
 */

#ifndef __EFI_GBL_FASTBOOT_TRANSPORT_H__
#define __EFI_GBL_FASTBOOT_TRANSPORT_H__

#include <efi.h>
#include <efi_api.h>

extern const efi_guid_t efi_gbl_fastboot_transport_guid;

typedef enum {
	// Single packet receive mode
	EFI_GBL_FASTBOOT_RX_MODE_SINGLE_PACKET = 0,
	// Fixed length receive mode
	EFI_GBL_FASTBOOT_RX_MODE_FIXED_LENGTH = 1,
} efi_gbl_fastboot_rx_mode;

struct efi_gbl_fastboot_transport_protocol {
	// Revision of the protocol supported.
	u64 revision;
	const char *description;

	efi_status_t(EFIAPI *start)(
		struct efi_gbl_fastboot_transport_protocol *this);
	efi_status_t(EFIAPI *stop)(
		struct efi_gbl_fastboot_transport_protocol *this);
	efi_status_t(EFIAPI *receive)(
		struct efi_gbl_fastboot_transport_protocol *this,
		size_t *bufsize, void *buf, efi_gbl_fastboot_rx_mode mode);
	efi_status_t(EFIAPI *send)(
		struct efi_gbl_fastboot_transport_protocol *this,
		size_t *bufsize, void *buf);
	efi_status_t(EFIAPI *flush)(
		struct efi_gbl_fastboot_transport_protocol *this);
};

efi_status_t efi_gbl_fastboot_transport_register(void);

#endif /* __EFI_GBL_FASTBOOT_TRANSPORT_H__ */
