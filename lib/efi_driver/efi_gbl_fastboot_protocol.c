/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (C) 2025 The Android Open Source Project
 */

#include <efi.h>
#include <efi_api.h>
#include <efi_gbl_fastboot.h>
#include <efi_loader.h>
#include <log.h>

const efi_guid_t efi_gbl_fastboot_guid = EFI_GBL_FASTBOOT_PROTOCOL_GUID;
static struct gbl_efi_fastboot_protocol gbl_efi_fastboot_proto;

// Deliberately simplified fastboot variable representation.
struct fastboot_var {
	// NULL terminated array of strings
	// representing a variable-argument tuple.
	char const *const *const args;
	// String representation of the variable's value.
	char const *const val;
};

// Array of fastboot variables with a NULL sentinel.
static struct fastboot_var vars[] = {
	{ .args = NULL, .val = NULL }, // Sentinel
};

size_t args_len(struct fastboot_var *var)
{
	size_t i = 0;
	while (var->args[i]) {
		i++;
	}
	return i;
}

static bool args_match_var(const char *const *args, size_t num_args,
			   const struct fastboot_var *var)
{
	int i;
	for (i = 0; i < num_args && var->args[i]; i++) {
		if (strcmp(args[i], var->args[i])) {
			return false;
		}
	}

	return (i == num_args && !var->args[i]);
}

static efi_status_t EFIAPI get_var(struct gbl_efi_fastboot_protocol *this,
				   const char *const *fb_args, size_t num_args,
				   char *buf, size_t *bufsize)
{
	EFI_ENTRY("%p, %p, %lu, %p, %p", this, fb_args, num_args, buf, bufsize);
	if (this != &gbl_efi_fastboot_proto || fb_args == NULL || buf == NULL ||
	    bufsize == NULL) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	for (struct fastboot_var *var = &vars[0]; var->val; var++) {
		if (args_match_var(fb_args, num_args, var)) {
			size_t val_len = strlen(var->val);
			efi_status_t ret;
			if (val_len <= *bufsize) {
				memcpy(buf, var->val, val_len);
				ret = EFI_SUCCESS;
			} else {
				ret = EFI_BUFFER_TOO_SMALL;
			}
			return EFI_EXIT(ret);
		}
	}

	return EFI_EXIT(EFI_NOT_FOUND);
}

static efi_status_t EFIAPI get_var_all(struct gbl_efi_fastboot_protocol *this,
				       void *ctx, get_var_all_callback cb)
{
	EFI_ENTRY("%p, %p, %p", this, ctx, cb);
	if (this != &gbl_efi_fastboot_proto || cb == NULL) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	for (struct fastboot_var *var = &vars[0]; var->args; var++) {
		cb(ctx, var->args, args_len(var), var->val);
	}

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI get_staged(struct gbl_efi_fastboot_protocol *this,
				      uint8_t *out, size_t *out_size,
				      size_t *out_remain)
{
	EFI_ENTRY("%p, %p, %p, %p", this, out, out_size, out_remain);

	return EFI_EXIT(EFI_UNSUPPORTED);
}

static efi_status_t EFIAPI set_lock(struct gbl_efi_fastboot_protocol *this,
				    bool critical, bool lock)
{
	EFI_ENTRY("%p, %i, %i", this, critical, lock);

	return EFI_EXIT(EFI_UNSUPPORTED);
}

static efi_status_t EFIAPI get_lock(struct gbl_efi_fastboot_protocol *this,
				    bool critical, bool *out_lock)
{
	EFI_ENTRY("%p, %i, %p", this, critical, out_lock);

	return EFI_EXIT(EFI_UNSUPPORTED);
}

static efi_status_t EFIAPI vendor_erase(struct gbl_efi_fastboot_protocol *this,
					const uint8_t *part_name,
					size_t part_name_len,
					gbl_efi_fastboot_erase_action *action)
{
	EFI_ENTRY("%p, %p, %zu, %p", this, part_name, part_name_len, action);

	return EFI_EXIT(EFI_UNSUPPORTED);
}

static efi_status_t EFIAPI
command_exec(struct gbl_efi_fastboot_protocol *this, size_t num_args,
	     const char *const *args, size_t download_data_used_len,
	     uint8_t *download_data, size_t download_data_full_size,
	     gbl_efi_fastboot_command_exec_result *implementation,
	     fastboot_message_sender sender, void *ctx)
{
	EFI_ENTRY("%p, %zu, %p, %zu, %p, %zu, %p, %p, %p", this, num_args, args,
		  download_data_used_len, download_data,
		  download_data_full_size, implementation, sender, ctx);

	return EFI_EXIT(EFI_UNSUPPORTED);
}

static struct gbl_efi_fastboot_protocol gbl_efi_fastboot_proto = {
	.revision = 4,
	.serial_number = "cuttlefish-0xCAFED00D",
	.get_var = get_var,
	.get_var_all = get_var_all,
	.get_staged = get_staged,
	.set_lock = set_lock,
	.get_lock = get_lock,
	.vendor_erase = vendor_erase,
	.command_exec = command_exec,
};

efi_status_t efi_gbl_fastboot_register(void)
{
	efi_status_t ret = efi_add_protocol(efi_root, &efi_gbl_fastboot_guid,
					    &gbl_efi_fastboot_proto);
	if (ret != EFI_SUCCESS) {
		log_err("Failed to install GBL_EFI_FASTBOOT_PROTOCOL: 0x%lx\n",
			ret);
	}

	return ret;
}
