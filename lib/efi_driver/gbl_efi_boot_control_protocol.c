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

static const char *device_name = "virtio";
static const char *ab_partition_name = "misc";
static struct disk_partition ab_partition;
static struct blk_desc *block_device;

const efi_guid_t gbl_efi_boot_control_guid = GBL_EFI_BOOT_CONTROL_PROTOCOL_GUID;
static struct gbl_efi_boot_control_protocol efi_gbl_slot_proto;

static u8 *buffer;

#define COMMAND_LEN 32
static char command[COMMAND_LEN];
static struct bootloader_control __aligned(ARCH_DMA_MINALIGN) android_metadata;
static bool dirty;
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

	long res = blk_dread(block_device, ab_partition.start, 1, buffer);

	if (res != 1) {
		log_err("Failed to read bootloader command: %l\n", res);
		return EFI_DEVICE_ERROR;
	}
	memcpy(command, buffer, COMMAND_LEN);

	struct disk_offset offset =
		byte_offset_to_blocks(2048, ab_partition.blksz);
	res = blk_dread(block_device, ab_partition.start + offset.blocks, 1,
			buffer);

	if (res != 1) {
		log_err("Failed to read AB metadata: %l\n", res);
		return EFI_DEVICE_ERROR;
	}

	dirty = false;
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
get_slot_info(struct gbl_efi_boot_control_protocol *this, u8 idx,
	      struct efi_gbl_slot_info *info)
{
	EFI_ENTRY("%p, %uc, %p", this, idx, info);
	if (this != &efi_gbl_slot_proto || !info) {
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
get_current_slot_idx(struct gbl_efi_boot_control_protocol *this, u8 *idx)
{
	if (this != &efi_gbl_slot_proto || !idx) {
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
get_current_slot(struct gbl_efi_boot_control_protocol *this,
		 struct efi_gbl_slot_info *info)
{
	EFI_ENTRY("%p, %p", this, info);
	if (!info) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	u8 idx;
	efi_status_t res = ensure_buffer_initialized();
	if (res != EFI_SUCCESS)
		return EFI_EXIT(res);

	res = get_current_slot_idx(this, &idx);
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
set_active_slot(struct gbl_efi_boot_control_protocol *this, u8 idx)
{
	EFI_ENTRY("%p, %uc", this, idx);
	if (this != &efi_gbl_slot_proto) {
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
			slot->tries_remaining = 7;
			slot->priority = INITIAL_SLOT_PRIORITY;
			slot->successful_boot = 0;
		} else {
			slot->priority = INITIAL_SLOT_PRIORITY - 1;
		}
	}

	dirty = true;
	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI
flush_changes(struct gbl_efi_boot_control_protocol *this)
{
	EFI_ENTRY("%p", this);

	efi_status_t res = ensure_buffer_initialized();
	if (res != EFI_SUCCESS)
		return EFI_EXIT(res);

	if (!dirty) {
		return EFI_EXIT(EFI_SUCCESS);
	}

	memset(buffer, 0, block_device->blksz);
	memcpy(buffer, command, COMMAND_LEN);
	if (blk_dwrite(block_device, ab_partition.start, 1, buffer) != 1) {
		return EFI_EXIT(EFI_DEVICE_ERROR);
	}

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

	dirty = false;
	return EFI_EXIT(EFI_SUCCESS);
}

static struct gbl_efi_boot_control_protocol efi_gbl_slot_proto = {
	.revision = GBL_EFI_BOOT_CONTROL_REVISION,
	.get_slot_info = get_slot_info,
	.get_current_slot = get_current_slot,
	.set_active_slot = set_active_slot,
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
