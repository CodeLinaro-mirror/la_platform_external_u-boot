// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (C) 2024 The Android Open Source Project
 */

#include <blk.h>
#include <efi.h>
#include <gbl_efi_boot_control_protocol.h>
#include <efi_loader.h>
#include <part.h>
#include <stdlib.h>

#include <android_bootloader_message.h>

#include <memalign.h>
#include <efi_selftest.h>
#include <log.h>
#include <string.h>
#include <u-boot/crc.h>

#define INITIAL_SLOT_PRIORITY 15
#define INITIAL_SLOT_TRIES_REMAINING 7

static const char *device_name = "virtio";
static const char *ab_partition_name = "misc";
static struct disk_partition ab_partition;
static struct blk_desc *block_device;

const efi_guid_t gbl_efi_boot_control_guid = GBL_EFI_BOOT_CONTROL_PROTOCOL_GUID;
static struct gbl_efi_boot_control_protocol efi_gbl_slot_proto;

static u8 *buffer;

static struct bootloader_control __aligned(ARCH_DMA_MINALIGN) android_metadata;
static bool data_loaded;

u32 calculate_metadata_checksum(const struct bootloader_control *data)
{
	return crc32(0, (const u8 *)data,
		     sizeof(*data) - sizeof(data->crc32_le));
}

static efi_status_t ensure_buffer_initialized(void)
{
	if (!block_device) {
		block_device = blk_get_dev(device_name, 0);
		if (!block_device) {
			log_err("Failed to get device: %s:0\n", device_name);
			return EFI_DEVICE_ERROR;
		}

		if (block_device->blksz < sizeof(android_metadata))
			return EFI_BUFFER_TOO_SMALL;

		if (part_get_info_by_name(block_device, ab_partition_name,
					  &ab_partition) < 1) {
			log_err("No partition '%s' on device '%s:0'\n",
				ab_partition_name, device_name);
			return EFI_DEVICE_ERROR;
		}
	}

	if (!buffer) {
		buffer = malloc_cache_aligned(block_device->blksz);
		if (!buffer)
			return EFI_OUT_OF_RESOURCES;
		memset(buffer, 0, block_device->blksz);
	}

	return EFI_SUCCESS;
}

struct disk_offset {
	u64 blocks;
	u64 remaining_bytes;
};

struct disk_offset byte_offset_to_blocks(size_t byte_offset, ulong blksize)
{
	struct disk_offset ret = {
		.blocks = byte_offset / blksize,
		.remaining_bytes = byte_offset % blksize,
	};
	return ret;
}

static efi_status_t load_boot_data(void)
{
	if (data_loaded) {
		return EFI_SUCCESS;
	}

	struct disk_offset offset =
		byte_offset_to_blocks(2048, ab_partition.blksz);
	long res;
	res = blk_dread(block_device, ab_partition.start + offset.blocks, 1,
			buffer);

	if (res != 1) {
		log_err("Failed to read AB metadata: %l\n", res);
		return EFI_DEVICE_ERROR;
	}

	data_loaded = true;
	memcpy(&android_metadata, buffer + offset.remaining_bytes,
	       sizeof(android_metadata));
	if (calculate_metadata_checksum(&android_metadata) !=
	    android_metadata.crc32_le) {
		log_warning("On-disk AB metadata corrupted\n");
		return EFI_CRC_ERROR;
	}

	return EFI_SUCCESS;
}

static efi_status_t EFIAPI
get_slot_count(struct gbl_efi_boot_control_protocol *self, uint8_t *slot_count)
{
	EFI_ENTRY("%p, %p", self, slot_count);
	if (self != &efi_gbl_slot_proto || !slot_count) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	efi_status_t res = ensure_buffer_initialized();
	if (res != EFI_SUCCESS)
		return EFI_EXIT(res);

	res = load_boot_data();
	if (res != EFI_SUCCESS)
		return EFI_EXIT(res);

	*slot_count = android_metadata.nb_slot;

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI
get_slot_info(struct gbl_efi_boot_control_protocol *self, u8 idx,
	      struct efi_gbl_slot_info *info)
{
	EFI_ENTRY("%p, %uc, %p", self, idx, info);
	if (self != &efi_gbl_slot_proto || !info) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	efi_status_t res = ensure_buffer_initialized();
	if (res != EFI_SUCCESS)
		return EFI_EXIT(res);

	res = load_boot_data();
	if (res != EFI_SUCCESS) {
		memset(info, 0, sizeof(*info));
		return EFI_EXIT(res);
	}

	if (idx >= android_metadata.nb_slot) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	struct slot_metadata const *slot = &android_metadata.slot_info[idx];

	info->suffix = android_metadata.slot_suffix[idx];
	info->unbootable_reason = 0;
	info->priority = slot->priority;
	info->remaining_tries = slot->tries_remaining;
	info->successful = slot->successful_boot;

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t
get_current_slot_idx(struct gbl_efi_boot_control_protocol *self, u8 *idx)
{
	if (self != &efi_gbl_slot_proto || !idx) {
		return EFI_INVALID_PARAMETER;
	}

	efi_status_t res = ensure_buffer_initialized();
	if (res != EFI_SUCCESS)
		return EFI_EXIT(res);

	res = load_boot_data();
	if (res != EFI_SUCCESS)
		return res;

	u8 max_idx = 0;

	for (int i = 1; i < android_metadata.nb_slot; i++) {
		struct slot_metadata *max =
			&android_metadata.slot_info[max_idx];
		struct slot_metadata *slot = &android_metadata.slot_info[i];

		if ((slot->tries_remaining || slot->successful_boot) &&
		    (slot->priority > max->priority ||
		     (slot->priority == max->priority &&
		      android_metadata.slot_suffix[i] <
			      android_metadata.slot_suffix[max_idx]))) {
			max_idx = i;
		}
	}

	*idx = max_idx;
	return EFI_SUCCESS;
}

static efi_status_t EFIAPI
get_current_slot(struct gbl_efi_boot_control_protocol *self,
		 struct efi_gbl_slot_info *info)
{
	EFI_ENTRY("%p, %p", self, info);
	if (!info) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	u8 idx;
	efi_status_t res = ensure_buffer_initialized();
	if (res != EFI_SUCCESS)
		return EFI_EXIT(res);

	res = get_current_slot_idx(self, &idx);
	if (res != EFI_SUCCESS)
		return EFI_EXIT(res);

	struct slot_metadata const *slot = &android_metadata.slot_info[idx];

	info->suffix = android_metadata.slot_suffix[idx];
	info->unbootable_reason = 0;
	info->priority = slot->priority;
	info->remaining_tries = slot->tries_remaining;
	info->successful = slot->successful_boot;

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI
flush_changes(struct gbl_efi_boot_control_protocol *self)
{
	EFI_ENTRY("%p", self);

	efi_status_t res = ensure_buffer_initialized();
	if (res != EFI_SUCCESS)
		return EFI_EXIT(res);

	android_metadata.crc32_le =
		calculate_metadata_checksum(&android_metadata);
	struct disk_offset offset =
		byte_offset_to_blocks(2048, ab_partition.blksz);
	memset(buffer, 0, block_device->blksz);
	memcpy(buffer + offset.remaining_bytes, &android_metadata,
	       sizeof(android_metadata));
	if (blk_dwrite(block_device, ab_partition.start + offset.blocks, 1,
		       buffer) != 1) {
		return EFI_EXIT(EFI_DEVICE_ERROR);
	}

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI
set_active_slot(struct gbl_efi_boot_control_protocol *self, u8 idx)
{
	EFI_ENTRY("%p, %uc", self, idx);
	if (self != &efi_gbl_slot_proto) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	efi_status_t res = ensure_buffer_initialized();
	if (res != EFI_SUCCESS)
		return EFI_EXIT(res);

	res = load_boot_data();
	if (res != EFI_SUCCESS)
		return EFI_EXIT(res);

	if (idx >= android_metadata.nb_slot) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	for (int i = 0; i < android_metadata.nb_slot; i++) {
		struct slot_metadata *slot = &android_metadata.slot_info[i];

		if (i == idx) {
			slot->tries_remaining = INITIAL_SLOT_TRIES_REMAINING;
			slot->priority = INITIAL_SLOT_PRIORITY;
			slot->successful_boot = 0;
		} else {
			slot->priority = INITIAL_SLOT_PRIORITY - 1;
		}
	}

	res = flush_changes(self);
	if (res != EFI_SUCCESS) {
		return EFI_EXIT(res);
	}

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI
get_one_shot_boot_mode(struct gbl_efi_boot_control_protocol *self,
		       enum gbl_efi_one_shot_boot_mode *mode)
{
	EFI_ENTRY("%p, %p", self, mode);
	if (self != &efi_gbl_slot_proto || !mode) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	/*
	 * TODO: GBL_EFI_BOOT_CONTROL_PROTOCOL.GetOneShotBootMode()
	 * must only be used for one-shot, non-persistent boot modes triggered
	 * by the user (e.g., the user holding the volume-down button during
	 * boot). We should not rely on persistent storage to determine a
	 * one-shot boot mode. Refer to the GBL documentation for this method.
	 *
	 * A serial console input-based implementation of this method (similar
	 * to the serial-based fastboot transport) could serve as a better
	 * reference implementation.
	 */
	return EFI_EXIT(EFI_UNSUPPORTED);
}

/* TODO: implement */
static efi_status_t EFIAPI
handle_loaded_os(struct gbl_efi_boot_control_protocol *self,
		 const struct gbl_efi_loaded_os *os)
{
	EFI_ENTRY("%p, %p", self, os);
	if (self != &efi_gbl_slot_proto || !os) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	return EFI_EXIT(EFI_UNSUPPORTED);
}

static struct gbl_efi_boot_control_protocol efi_gbl_slot_proto = {
	.revision = GBL_EFI_BOOT_CONTROL_REVISION,
	.get_slot_count = get_slot_count,
	.get_slot_info = get_slot_info,
	.get_current_slot = get_current_slot,
	.set_active_slot = set_active_slot,
	.get_one_shot_boot_mode = get_one_shot_boot_mode,
	.handle_loaded_os = handle_loaded_os,
};

efi_status_t gbl_efi_boot_control_register(void)
{
	efi_status_t ret = efi_add_protocol(
		efi_root, &gbl_efi_boot_control_guid, &efi_gbl_slot_proto);
	if (ret != EFI_SUCCESS) {
		log_err("Failed to install GBL_EFI_BOOT_CONTROL_PROTOCOL: 0x%lx\n",
			ret);
	}

	return ret;
}
