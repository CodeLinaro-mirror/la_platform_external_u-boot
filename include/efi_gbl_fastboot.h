/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (C) 2025 The Android Open Source Project
 */

#ifndef __EFI_GBL_FASTBOOT_H__
#define __EFI_GBL_FASTBOOT_H__

#include <efi.h>
#include <efi_api.h>

#define GBL_EFI_FASTBOOT_SERIAL_NUMBER_MAX_LEN_UTF8 32

// Callback function pointer passed to GblEfiFastbootProtocol.get_var_all.
//
// context: Caller specific context.
// args: An array of NULL-terminated strings that contains the variable name
//       followed by additional arguments if any.
// val: A NULL-terminated string representing the value.
typedef void (*get_var_all_callback)(void *context, const char *const *args,
				     size_t num_args, const char *val);

typedef enum {
	GBL_EFI_FASTBOOT_MESSAGE_TYPE_OKAY = 0,
	GBL_EFI_FASTBOOT_MESSAGE_TYPE_FAIL = 1,
	GBL_EFI_FASTBOOT_MESSAGE_TYPE_INFO = 2,
} gbl_efi_fastboot_message_type;

typedef efi_status_t (*fastboot_message_sender)(
	void *context, gbl_efi_fastboot_message_type msg_type, const char *msg,
	size_t msg_len);

typedef enum {
	// Treats the partition as a physical on disk partition and erases it.
	GBL_EFI_FASTBOOT_ERASE_ACTION_ERASE_AS_PHYSICAL_PARTITION = 0,
	// Ignores the partition.
	GBL_EFI_FASTBOOT_ERASE_ACTION_NOOP = 1,
} gbl_efi_fastboot_erase_action;

typedef enum {
	GBL_EFI_FASTBOOT_COMMAND_EXEC_RESULT_PROHIBITED = 0,
	GBL_EFI_FASTBOOT_COMMAND_EXEC_RESULT_DEFAULT_IMPL = 1,
	GBL_EFI_FASTBOOT_COMMAND_EXEC_RESULT_CUSTOM_IMPL = 2,
} gbl_efi_fastboot_command_exec_result;

extern const efi_guid_t efi_gbl_fastboot_guid;

struct gbl_efi_fastboot_protocol {
	// Revision of the protocol supported.
	u64 revision;
	// Null-terminated UTF-8 encoded string
	char serial_number[GBL_EFI_FASTBOOT_SERIAL_NUMBER_MAX_LEN_UTF8];

	// Fastboot variable methods
	efi_status_t(EFIAPI *get_var)(struct gbl_efi_fastboot_protocol *this,
				      const char *const *args, size_t num_args,
				      char *buf, size_t *bufsize);
	efi_status_t(EFIAPI *get_var_all)(struct gbl_efi_fastboot_protocol *self,
					  void *ctx, get_var_all_callback cb);

	// Fastboot get_staged backend
	efi_status_t(EFIAPI *get_staged)(struct gbl_efi_fastboot_protocol *self,
					 uint8_t *out, size_t *out_size,
					 size_t *out_remain);

	// Device lock methods
	efi_status_t(EFIAPI *set_lock)(struct gbl_efi_fastboot_protocol *this,
				       bool critical, bool lock);
	efi_status_t(EFIAPI *get_lock)(struct gbl_efi_fastboot_protocol *this,
				       bool critical, bool *out_lock);

	// Misc methods
	efi_status_t(EFIAPI *vendor_erase)(
		struct gbl_efi_fastboot_protocol *self,
		const uint8_t *part_name, size_t part_name_len,
		gbl_efi_fastboot_erase_action *action);

	efi_status_t(EFIAPI *command_exec)(
		struct gbl_efi_fastboot_protocol *self, size_t num_args,
		const char *const *args, size_t download_data_used_len,
		uint8_t *download_data, size_t download_data_full_size,
		gbl_efi_fastboot_command_exec_result *implementation,
		fastboot_message_sender sender, void *ctx);
};

efi_status_t efi_gbl_fastboot_register(void);

#endif /* __EFI_GBL_FASTBOOT_H__ */
