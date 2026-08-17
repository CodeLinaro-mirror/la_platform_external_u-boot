/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (C) 2024 The Android Open Source Project
 */

#include <avb_verify.h>
#include <efi_api.h>
#include <gbl_efi_os_configuration.h>
#include <efi_loader.h>
#include <efi.h>
#include <linux/string.h>

#define ANDROID_PARTITION_BOOTCONFIG "bootconfig"

const efi_guid_t gbl_efi_os_config_guid =
	GBL_EFI_OS_CONFIGURATION_PROTOCOL_GUID;

#ifdef CONFIG_ANDROID_PERSISTENT_RAW_DISK
static efi_status_t
bootconfig_load_from_persistent_disk_device(char *fixup,
					    size_t *fixup_buffer_size)
{
	AvbSlotVerifyData *avb_verify_data = NULL;
	AvbPartitionData *avb_bootconfig_data = NULL;
	struct AvbOps *ops = NULL;
	const char *data = NULL;
	size_t len;
	int ret = 0;
	char devnum_str[12];
	const char *slot_suffix = "";
	static const char *const requested_partitions[] = {
		ANDROID_PARTITION_BOOTCONFIG, NULL
	};
	efi_status_t status = EFI_SUCCESS;

	snprintf(devnum_str, sizeof(devnum_str), "%d",
		 CONFIG_ANDROID_PERSISTENT_RAW_DISK_DEVICE);
	ops = avb_ops_alloc("virtio", devnum_str);
	if (!ops) {
		printf("Failed to allocate AVB ops for persistent disk\n");
		return EFI_OUT_OF_RESOURCES;
	}

	ret = avb_verify_partitions(ops, slot_suffix, requested_partitions,
				    &avb_verify_data, NULL);
	if (ret != CMD_RET_SUCCESS) {
		printf("Failed to verify bootconfig partition from persistent disk\n");
		status = EFI_LOAD_ERROR;
		goto out;
	}

	for (int i = 0; i < avb_verify_data->num_loaded_partitions; i++) {
		AvbPartitionData *p = &avb_verify_data->loaded_partitions[i];
		if (p->partition_name &&
		    !strcmp(ANDROID_PARTITION_BOOTCONFIG, p->partition_name)) {
			avb_bootconfig_data = p;
			break;
		}
	}
	if (!avb_bootconfig_data) {
		printf("Failed to verify bootconfig partition from persistent disk\n");
		status = EFI_LOAD_ERROR;
		goto out;
	}

	data = (const char *)avb_bootconfig_data->data;
	len = strnlen(data, avb_bootconfig_data->data_size);

	if (len > *fixup_buffer_size) {
		printf("Buffer too small for bootconfig\n");
		*fixup_buffer_size = len;
		status = EFI_BUFFER_TOO_SMALL;
		goto out;
	}

	memcpy(fixup, data, len);
	*fixup_buffer_size = len;

out:
	if (avb_verify_data)
		avb_slot_verify_data_free(avb_verify_data);
	if (ops)
		avb_ops_free(ops);

	return status;
}
#endif

static efi_status_t EFIAPI fixup_bootconfig(
	struct gbl_efi_os_configuration_protocol *self, size_t bootconfig_size,
	const char *bootconfig, size_t *fixup_buffer_size, char *fixup)
{
	EFI_ENTRY("%p, %zu, %p, %p, %p", self, bootconfig_size, bootconfig,
		  fixup_buffer_size, fixup);

	if (!self || !bootconfig || !fixup_buffer_size || !fixup)
		return EFI_EXIT(EFI_INVALID_PARAMETER);

#ifdef CONFIG_ANDROID_PERSISTENT_RAW_DISK
	return EFI_EXIT(bootconfig_load_from_persistent_disk_device(fixup,
								    fixup_buffer_size));
#else
	/* No fixup needed, set fixup_buffer_size to 0 */
	*fixup_buffer_size = 0;

	return EFI_EXIT(EFI_SUCCESS);
#endif
}

static efi_status_t EFIAPI select_device_trees(
	struct gbl_efi_os_configuration_protocol *self, size_t num_device_trees,
	struct gbl_efi_verified_device_tree *device_trees)
{
	EFI_ENTRY("%p, %zu, %p", self, num_device_trees, device_trees);

	if (!self || !num_device_trees || !device_trees)
		return EFI_EXIT(EFI_INVALID_PARAMETER);

	// Select first base device tree and ignore all overlays / device assignment overlays.
	for (size_t i = 0; i < num_device_trees; i++) {
		if (device_trees[i].metadata.type ==
		    GBL_EFI_DEVICE_TREE_TYPE_DEVICE_TREE) {
			device_trees[i].selected = true;
			return EFI_EXIT(EFI_SUCCESS);
		}
	}

	log_err("No base device tree provided, nothing to select.\n");
	return EFI_EXIT(EFI_INVALID_PARAMETER);
}

static efi_status_t EFIAPI select_fit_configuration(
	struct gbl_efi_os_configuration_protocol *self, size_t fit_size,
	const u8 *fit, size_t metadata_size, const u8 *metadata,
	size_t *selected_configuration_offset)
{
	EFI_ENTRY("%p, %zu, %p, %zu, %p, %p", self, fit_size, fit,
		  metadata_size, metadata, selected_configuration_offset);

	if (!self || !fit || !selected_configuration_offset ||
	    (metadata_size > 0 && !metadata))
		return EFI_EXIT(EFI_INVALID_PARAMETER);

	return EFI_EXIT(EFI_UNSUPPORTED);
}

static struct gbl_efi_os_configuration_protocol gbl_efi_os_config_proto = {
	.revision = GBL_EFI_OS_CONFIGURATION_PROTOCOL_REVISION,
	.fixup_bootconfig = fixup_bootconfig,
	.select_device_trees = select_device_trees,
	.select_fit_configuration = select_fit_configuration,
};

efi_status_t gbl_efi_os_config_register(void)
{
	efi_status_t ret = efi_add_protocol(efi_root, &gbl_efi_os_config_guid,
					    &gbl_efi_os_config_proto);
	if (ret != EFI_SUCCESS)
		log_err("Failed to install GBL_EFI_OS_CONFIGURATION_PROTOCOL: 0x%lx\n",
			ret);

	return ret;
}
