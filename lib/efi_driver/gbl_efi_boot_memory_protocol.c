// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (C) 2026 The Android Open Source Project
 */

#include <efi.h>
#include <efi_api.h>
#include <efi_loader.h>
#include <gbl_efi_boot_memory_protocol.h>
#include <linux/kernel.h>
#include <linux/sizes.h>
#include <log.h>

// Designated GBL boot buffers, lazily allocated on first request.
struct gbl_boot_buf {
	u64 size;
	u64 align;
	enum efi_memory_type type;
	void *addr;
};

static struct gbl_boot_buf gbl_boot_bufs[] = {
	[GBL_EFI_BOOT_BUFFER_TYPE_GENERAL_LOAD] = {
		.size = SZ_16M,
		.align = EFI_PAGE_SIZE,
		.type = EFI_LOADER_DATA,
	},
	[GBL_EFI_BOOT_BUFFER_TYPE_KERNEL] = {
		.size = 86 * SZ_1M,
		.align = SZ_2M,
		.type = EFI_LOADER_DATA,
	},
	[GBL_EFI_BOOT_BUFFER_TYPE_RAMDISK] = {
		.size = SZ_64M,
		.align = EFI_PAGE_SIZE,
		.type = EFI_LOADER_DATA,
	},
	[GBL_EFI_BOOT_BUFFER_TYPE_FDT] = {
		.size = SZ_2M,
		.align = EFI_PAGE_SIZE,
		.type = EFI_LOADER_DATA,
	},
	[GBL_EFI_BOOT_BUFFER_TYPE_PVMFW_DATA] = {
		.size = SZ_2M,
		.align = SZ_64K,
		.type = EFI_RESERVED_MEMORY_TYPE,
	},
	[GBL_EFI_BOOT_BUFFER_TYPE_FASTBOOT_DOWNLOAD] = {
		.size = SZ_256M,
		.align = EFI_PAGE_SIZE,
		.type = EFI_LOADER_DATA,
	},
};

const efi_guid_t gbl_efi_boot_memory_guid = GBL_EFI_BOOT_MEMORY_PROTOCOL_GUID;
static struct gbl_efi_boot_memory_protocol gbl_efi_boot_memory_proto;

static bool gbl_loads_pvmfw(void)
{
	const efi_guid_t avf_guid = GBL_EFI_AVF_PROTOCOL_GUID;

	return efi_search_protocol(efi_root, &avf_guid, NULL) == EFI_SUCCESS;
}

static efi_status_t EFIAPI get_partition_buffer(
	struct gbl_efi_boot_memory_protocol *self, const char *base_name,
	size_t *size, void **addr, enum gbl_efi_partition_buffer_flag *flag)
{
	EFI_ENTRY("%p, %p, %p, %p, %p", self, base_name, size, addr, flag);

	if (self != &gbl_efi_boot_memory_proto || !base_name || !size ||
	    !addr || !flag)
		return EFI_EXIT(EFI_INVALID_PARAMETER);

	*size = 0;
	*addr = NULL;
	*flag = 0;
	return EFI_EXIT(EFI_NOT_FOUND);
}

static efi_status_t EFIAPI sync_partition_buffer(
	struct gbl_efi_boot_memory_protocol *self, bool sync_preloaded)
{
	EFI_ENTRY("%p, %d", self, sync_preloaded);

	if (self != &gbl_efi_boot_memory_proto)
		return EFI_EXIT(EFI_INVALID_PARAMETER);

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI get_boot_buffer(
	struct gbl_efi_boot_memory_protocol *self,
	enum gbl_efi_boot_buffer_type buf_type, size_t *size, void **addr)
{
	struct gbl_boot_buf *buf;

	EFI_ENTRY("%p, %u, %p, %p", self, buf_type, size, addr);

	if (self != &gbl_efi_boot_memory_proto || !size || !addr)
		return EFI_EXIT(EFI_INVALID_PARAMETER);

	*size = 0;
	*addr = NULL;

	if ((u32)buf_type >= ARRAY_SIZE(gbl_boot_bufs))
		return EFI_EXIT(EFI_NOT_FOUND);

	if (buf_type == GBL_EFI_BOOT_BUFFER_TYPE_PVMFW_DATA &&
	    !gbl_loads_pvmfw())
		return EFI_EXIT(EFI_NOT_FOUND);

	buf = &gbl_boot_bufs[buf_type];
	if (!buf->addr) {
		buf->addr = efi_alloc_aligned_pages(buf->size, buf->type,
						    buf->align);
		if (!buf->addr) {
			log_err("Failed to allocate GBL boot buffer %u (0x%llx bytes)\n",
				buf_type, buf->size);
			return EFI_EXIT(EFI_OUT_OF_RESOURCES);
		}
	}

	*size = buf->size;
	*addr = buf->addr;
	return EFI_EXIT(EFI_SUCCESS);
}

static struct gbl_efi_boot_memory_protocol gbl_efi_boot_memory_proto = {
	.revision = GBL_EFI_BOOT_MEMORY_PROTOCOL_REVISION,
	.get_partition_buffer = get_partition_buffer,
	.sync_partition_buffer = sync_partition_buffer,
	.get_boot_buffer = get_boot_buffer,
};

efi_status_t gbl_efi_boot_memory_register(void)
{
	efi_status_t ret = efi_add_protocol(efi_root, &gbl_efi_boot_memory_guid,
					    &gbl_efi_boot_memory_proto);
	if (ret != EFI_SUCCESS)
		log_err("Failed to install GBL_EFI_BOOT_MEMORY_PROTOCOL: 0x%lx\n",
			ret);

	return ret;
}
