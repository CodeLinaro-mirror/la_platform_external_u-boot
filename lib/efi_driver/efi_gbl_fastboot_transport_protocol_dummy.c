/* SPDX-License-Identifier: BSD-2-Clause
 * Copyright (C) 2025 The Android Open Source Project
 */

// This is dummy implementation to verify multiple Transport Protocols can be
// registered.

#include <cyclic.h>
#include <efi.h>
#include <efi_api.h>
#include <efi_gbl_fastboot_transport.h>
#include <efi_gbl_fastboot_transport_dummy.h>
#include <efi_loader.h>
#include <log.h>
#include <membuff.h>

#define DESCRIPTION "dummy"

static struct efi_gbl_fastboot_transport_protocol
	efi_gbl_fastboot_transport_dummy_proto;

static void print_help(void)
{
	printf("Dummy Fastboot Transport Protocol\n");
}

#define BUFFER_SIZE (100)
static char context_inner_buffer[BUFFER_SIZE];
typedef struct _Context {
	struct membuff mb;
} Context;
static Context ctx;

static struct cyclic_info *cyclic_info = NULL;
static void poll_loop(void *ctx)
{
	struct membuff *mb = &((Context *)ctx)->mb;
	if (tstc()) {
		membuff_putbyte(mb, getchar());
	}
}

static efi_status_t EFIAPI
start(struct efi_gbl_fastboot_transport_protocol *this)
{
	EFI_ENTRY("%p", this);
	if (this != &efi_gbl_fastboot_transport_dummy_proto) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	membuff_init(&ctx.mb, context_inner_buffer, BUFFER_SIZE);
	cyclic_info = cyclic_register(poll_loop, 100 * 1000 /*100ms*/,
				      DESCRIPTION, &ctx);
	print_help();

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t EFIAPI stop(struct efi_gbl_fastboot_transport_protocol *this)
{
	EFI_ENTRY("%p", this);
	if (this != &efi_gbl_fastboot_transport_dummy_proto) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	cyclic_unregister(cyclic_info);
	membuff_uninit(&ctx.mb);

	return EFI_EXIT(EFI_SUCCESS);
}

static efi_status_t process_key(int key, int *bufsize, void *)
{
	switch (key) {
	case 'd':
		print_help();
		// Fall through
	default:
		*bufsize = 0;
		break;
	}

	return EFI_SUCCESS;
}

static efi_status_t EFIAPI
receive(struct efi_gbl_fastboot_transport_protocol *this, size_t *bufsize,
	void *buf, efi_gbl_fastboot_rx_mode mode)
{
	struct membuff *mb = &ctx.mb;
	EFI_ENTRY_NO_LOG("%p, %p, %p, %u", this, bufsize, buf, mode);
	if (this != &efi_gbl_fastboot_transport_dummy_proto || buf == NULL ||
	    bufsize == NULL) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	if (membuff_avail(mb)) {
		char a;
		do {
			a = membuff_getbyte(mb);
		} while (a == membuff_peekbyte(mb));

		efi_status_t res = process_key(a, bufsize, buf);
		return EFI_EXIT_NO_LOG(res);
	}

	*bufsize = 0;
	return EFI_EXIT_NO_LOG(EFI_SUCCESS);
}

static efi_status_t EFIAPI send(struct efi_gbl_fastboot_transport_protocol *this,
				size_t *bufsize, void *buf)
{
	EFI_ENTRY_NO_LOG("%p, %p, %p", this, bufsize, buf);
	if (this != &efi_gbl_fastboot_transport_dummy_proto || buf == NULL ||
	    bufsize == NULL) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	printf("dummy send: '%.*s'\n", (int)*bufsize, (char *)buf);

	return EFI_EXIT_NO_LOG(EFI_SUCCESS);
}

static efi_status_t EFIAPI
efi_gbl_flush(struct efi_gbl_fastboot_transport_protocol *this)
{
	EFI_ENTRY_NO_LOG("%p", this);
	if (this != &efi_gbl_fastboot_transport_dummy_proto) {
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	return EFI_EXIT_NO_LOG(EFI_SUCCESS);
}

efi_status_t efi_gbl_fastboot_transport_dummy_register(void)
{
	efi_handle_t handle = NULL;
	efi_status_t ret = EFI_SUCCESS;

	ret = efi_install_multiple_protocol_interfaces(
		&handle, &efi_gbl_fastboot_transport_guid,
		&efi_gbl_fastboot_transport_dummy_proto, NULL);
	if (ret != EFI_SUCCESS) {
		log_err("Failed to install Dummy EFI_GBL_FASTBOOT_TRANSPORT_PROTOCOL: 0x%lx\n",
			ret);
	}

	return ret;
}

static struct efi_gbl_fastboot_transport_protocol
	efi_gbl_fastboot_transport_dummy_proto = {
		.revision = 1,
		.description = DESCRIPTION,
		.start = start,
		.stop = stop,
		.receive = receive,
		.send = send,
		.flush = efi_gbl_flush,
	};
