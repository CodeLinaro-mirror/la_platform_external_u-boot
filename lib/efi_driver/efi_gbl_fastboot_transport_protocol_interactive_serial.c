/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (C) 2025 The Android Open Source Project
 */

#include <efi.h>
#include <efi_api.h>
#include <efi_gbl_fastboot_transport.h>
#include <efi_gbl_fastboot_transport_interactive_serial.h>
#include <efi_loader.h>
#include <log.h>

static struct efi_gbl_fastboot_transport_protocol
	efi_gbl_fastboot_transport_interactive_serial_proto;

static void print_help(void)
{
	printf("Fastboot Interactive Serial options:\n"
	       "\t'a': send 'getvar:all'\n"
	       "\t'r': send 'reboot'\n"
	       "\t'b': send test bad command\n"
	       "\t'h': print this help message\n");
}

// static efi_status_t EFIAPI efi_gbl_fastboot_transport_protocol_start(
static efi_status_t EFIAPI
start(struct efi_gbl_fastboot_transport_protocol *this)
{
	EFI_ENTRY("%p", this);
	if (this != &efi_gbl_fastboot_transport_interactive_serial_proto) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	print_help();

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI stop(struct efi_gbl_fastboot_transport_protocol *this)
{
	EFI_ENTRY("%p", this);
	if (this != &efi_gbl_fastboot_transport_interactive_serial_proto) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	return EFI_EXIT(EFI_SUCCESS);
}

static void fill_reply(char *s, size_t *bufsize, void *buf)
{
	assert(s);
	*bufsize = min(strlen(s), *bufsize);
	strncpy((char *)buf, s, *bufsize);
}

static efi_status_t process_key(int key, size_t *bufsize, void *buf)
{
	efi_status_t res = EFI_SUCCESS;

	switch (key) {
	case 'a': {
		fill_reply("getvar:all", bufsize, buf);
		break;
	}
	case 'r': {
		fill_reply("reboot", bufsize, buf);
		break;
	}
	case 'b': {
		fill_reply("bad:command", bufsize, buf);
		break;
	}
	case 'h':
		print_help();
		// Fall through intentionally to return success
		// and no fastboot packets
	default:
		*bufsize = 0;
		res = EFI_SUCCESS;
		break;
	}

	return res;
}

static efi_status_t EFIAPI
receive(struct efi_gbl_fastboot_transport_protocol *this, size_t *bufsize,
	void *buf, efi_gbl_fastboot_rx_mode mode)
{
	EFI_ENTRY_NO_LOG("%p, %p, %p, %u", this, bufsize, buf, mode);
	if (this != &efi_gbl_fastboot_transport_interactive_serial_proto ||
	    buf == NULL || bufsize == NULL) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	if (tstc()) {
		efi_status_t res = process_key(getchar(), bufsize, buf);
		return EFI_EXIT_NO_LOG(res);
	}

	*bufsize = 0;
	return EFI_EXIT_NO_LOG(EFI_SUCCESS);
}

static efi_status_t EFIAPI send(struct efi_gbl_fastboot_transport_protocol *this,
				size_t *bufsize, void *buf)
{
	EFI_ENTRY_NO_LOG("%p, %p, %p", this, bufsize, buf);
	if (this != &efi_gbl_fastboot_transport_interactive_serial_proto ||
	    buf == NULL || bufsize == NULL) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	if (*bufsize < 4) {
		log_err("Bad fastboot command length: %i\n", *bufsize);
		return EFI_EXIT_NO_LOG(EFI_SUCCESS);
	}

	if (0 == strncmp(buf, "INFO", 4)) {
		printf("%.*s\n", *bufsize - 4, &buf[4]);
	} else if (0 == strncmp(buf, "FAIL", 4)) {
		printf("Fail: %.*s\n", *bufsize - 4, &buf[4]);
	} else if (0 == strncmp(buf, "OKAY", 4)) {
		// nop
	} else {
		log_err("Bad fastboot command: %.*s\n", 4, buf);
		return EFI_EXIT_NO_LOG(EFI_SUCCESS);
	}

	return EFI_EXIT_NO_LOG(EFI_SUCCESS);
}

static efi_status_t EFIAPI
efi_gbl_flush(struct efi_gbl_fastboot_transport_protocol *this)
{
	EFI_ENTRY_NO_LOG("%p", this);
	if (this != &efi_gbl_fastboot_transport_interactive_serial_proto) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	return EFI_EXIT_NO_LOG(EFI_SUCCESS);
}

efi_status_t efi_gbl_fastboot_transport_interactive_serial_register(void)
{
	efi_handle_t handle = NULL;
	efi_status_t ret = efi_install_multiple_protocol_interfaces(
		&handle, &efi_gbl_fastboot_transport_guid,
		&efi_gbl_fastboot_transport_interactive_serial_proto, NULL);

	if (ret != EFI_SUCCESS) {
		log_err("Failed to install Interactive Serial EFI_GBL_FASTBOOT_TRANSPORT_PROTOCOL: 0x%lx\n",
			ret);
	}

	return ret;
}

static struct efi_gbl_fastboot_transport_protocol
	efi_gbl_fastboot_transport_interactive_serial_proto = {
		.revision = 1,
		.description = "serial-interactive",
		.start = start,
		.stop = stop,
		.receive = receive,
		.send = send,
		.flush = efi_gbl_flush,
	};
