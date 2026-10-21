/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (C) 2026 The Android Open Source Project
 */

#ifndef __GBL_EFI_BOOT_MEMORY_H__
#define __GBL_EFI_BOOT_MEMORY_H__

#include <efi.h>
#include <efi_api.h>

#define GBL_EFI_BOOT_MEMORY_PROTOCOL_REVISION 0x00010000

enum gbl_efi_partition_buffer_flag {
	GBL_EFI_PARTITION_BUFFER_FLAG_PRELOADED = 1 << 0,
};

enum gbl_efi_boot_buffer_type {
	GBL_EFI_BOOT_BUFFER_TYPE_GENERAL_LOAD,
	GBL_EFI_BOOT_BUFFER_TYPE_KERNEL,
	GBL_EFI_BOOT_BUFFER_TYPE_RAMDISK,
	GBL_EFI_BOOT_BUFFER_TYPE_FDT,
	GBL_EFI_BOOT_BUFFER_TYPE_PVMFW_DATA,
	GBL_EFI_BOOT_BUFFER_TYPE_FASTBOOT_DOWNLOAD,
};

extern const efi_guid_t gbl_efi_boot_memory_guid;

struct gbl_efi_boot_memory_protocol {
	u64 revision;

	efi_status_t(EFIAPI *get_partition_buffer)(
		/* in */ struct gbl_efi_boot_memory_protocol *self,
		/* in */ const char *base_name,
		/* out */ size_t *size,
		/* out */ void **addr,
		/* out */ enum gbl_efi_partition_buffer_flag *flag);

	efi_status_t(EFIAPI *sync_partition_buffer)(
		/* in */ struct gbl_efi_boot_memory_protocol *self,
		/* in */ bool sync_preloaded);

	efi_status_t(EFIAPI *get_boot_buffer)(
		/* in */ struct gbl_efi_boot_memory_protocol *self,
		/* in */ enum gbl_efi_boot_buffer_type buf_type,
		/* out */ size_t *size,
		/* out */ void **addr);
};

efi_status_t gbl_efi_boot_memory_register(void);

#endif /* __GBL_EFI_BOOT_MEMORY_H__ */
