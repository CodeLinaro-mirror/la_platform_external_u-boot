// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (C) 2026 The Android Open Source Project
 */

#include <efi_dt_fixup_cf.h>
#include <efi_dt_fixup.h>
#include <efi_loader.h>
#include <efi_rng.h>
#include <linux/libfdt.h>
#include <linux/string.h>
#include <log.h>

#define CHOSEN "/chosen"
#define RESERVED_MEMORY "/reserved-memory"
#define BOOTARGS "bootargs"
#define ADDRESS_CELLS "#address-cells"
#define SIZE_CELLS "#size-cells"

/* merge_bootargs() - Appends source bootargs to destination with a space
 * separator.
 *
 * Uses fdt_setprop_placeholder to expand the existing bootargs property and
 * appends the new arguments, ensuring they are separated by a space.
 */
static int merge_bootargs(void *dst_fdt, const void *src_fdt)
{
	int dst_off = fdt_path_offset(dst_fdt, CHOSEN);
	int src_off = fdt_path_offset(src_fdt, CHOSEN);
	if (dst_off < 0 || src_off < 0)
		return 0;

	int src_len;
	const char *src_val = fdt_getprop(src_fdt, src_off, BOOTARGS, &src_len);
	if (!src_val)
		return 0;
	int src_str_len = strnlen(src_val, src_len);
	/* If source is empty or no terminator is found, skip it. */
	if (src_str_len == src_len || src_str_len == 0)
		return 0;

	int dst_len;
	const char *dst_val = fdt_getprop(dst_fdt, dst_off, BOOTARGS, &dst_len);
	if (!dst_val)
		goto override_prop;
	int dst_str_len = strnlen(dst_val, dst_len);
	/* If no terminator is found, existing bootargs are invalid, override them. */
	if (dst_str_len == dst_len)
		goto override_prop;

	/* If they are identical, there's no need to append. */
	if (dst_str_len == src_str_len &&
	    memcmp(dst_val, src_val, dst_str_len) == 0)
		return 0;

	int final_len = dst_str_len + 1 + src_str_len + 1;
	void *new_dst_val;
	int ret = fdt_setprop_placeholder(dst_fdt, dst_off, BOOTARGS, final_len,
					  &new_dst_val);
	if (ret < 0)
		return ret;

	char *dst_str_val = (char *)new_dst_val;
	dst_str_val[dst_str_len] = ' ';
	memcpy(dst_str_val + dst_str_len + 1, src_val, src_str_len + 1);

	return 0;

override_prop:
	return fdt_setprop(dst_fdt, dst_off, BOOTARGS, src_val, src_len);
}

/* merge_node() - Recursively merges missing nodes and properties from src to
 * dst.
 *
 * Existing properties in dst are skipped. Missing subnodes are created in dst
 * and then merged recursively.
 */
static int merge_node(void *dst_fdt, int dst_node_off, const void *src_fdt,
		      int src_node_off)
{
	int prop, child, ret;

	fdt_for_each_property_offset(prop, src_fdt, src_node_off)
	{
		const char *name;
		int src_len;
		const void *src_val =
			fdt_getprop_by_offset(src_fdt, prop, &name, &src_len);

		if (fdt_getprop(dst_fdt, dst_node_off, name, NULL)) {
			log_debug("%s(%d): skip existing property '%s'\n",
				  __func__, dst_node_off, name);
			continue;
		}

		ret = fdt_setprop(dst_fdt, dst_node_off, name, src_val,
				  src_len);
		if (ret < 0) {
			log_err("%s(%d): fdt_setprop('%s') failed: %d (%s)\n",
				__func__, dst_node_off, name, ret,
				fdt_strerror(ret));
			return ret;
		}
	}

	fdt_for_each_subnode(child, src_fdt, src_node_off)
	{
		const char *name = fdt_get_name(src_fdt, child, NULL);

		int dst_child = fdt_subnode_offset(dst_fdt, dst_node_off, name);

		if (dst_child == -FDT_ERR_NOTFOUND) {
			dst_child =
				fdt_add_subnode(dst_fdt, dst_node_off, name);
			if (dst_child < 0) {
				log_err("%s(%d): fdt_add_subnode('%s') failed: %d (%s)\n",
					__func__, dst_node_off, name, dst_child,
					fdt_strerror(dst_child));
				return dst_child;
			}
		} else if (dst_child < 0) {
			log_err("%s(%d): fdt_subnode_offset('%s') failed: %d (%s)\n",
				__func__, dst_node_off, name, dst_child,
				fdt_strerror(dst_child));
			return dst_child;
		}

		ret = merge_node(dst_fdt, dst_child, src_fdt, child);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* read_cells() - Read an explicit #address-cells/#size-cells value.
 *
 * Returns the cells value when 'name' is present on 'node' and valid, 0 when
 * it is absent, or a negative FDT error when it is present but malformed.
 */
static int read_cells(const void *fdt, int node, const char *name)
{
	int len;
	const fdt32_t *p = fdt_getprop(fdt, node, name, &len);

	if (!p)
		return len == -FDT_ERR_NOTFOUND ? 0 : len;
	if (len != sizeof(*p))
		return -FDT_ERR_BADNCELLS;

	u32 val = fdt32_ld(p);
	if (val < 1 || val > 2)
		return -FDT_ERR_BADNCELLS;

	return val;
}

/* decode_cells() - Read 'cells' big-endian FDT cells into a 64-bit value.
 *
 * Advances *p past the cells read. 'cells' is 1 or 2.
 */
static u64 decode_cells(const fdt32_t **p, int cells)
{
	u64 val = 0;

	while (cells--)
		val = (val << 32) | fdt32_ld((*p)++);

	return val;
}

/* encode_cells() - Write 'val' as 'cells' big-endian FDT cells.
 *
 * Returns the number of cells written. 'cells' is 1 or 2 and 'val' is
 * guaranteed to fit, both ensured by the caller.
 */
static u32 encode_cells(fdt32_t *p, u64 val, int cells)
{
	if (cells == 2)
		fdt32_st(p++, val >> 32);
	fdt32_st(p, val);

	return cells;
}

/* reserved-memory regions realistically carry a single reg entry */
#define MAX_REG_ENTRIES 64

/* reencode_reg() - Rewrite one node's "reg" from old to new cell widths.
 *
 * Decodes every (address, size) entry using the old cells, then rewrites the
 * property in place at the new width. Nodes without a "reg" (dynamic
 * carve-outs) are left untouched.
 */
static int reencode_reg(void *fdt, int node, int old_na, int old_ns, int new_na,
			int new_ns)
{
	int len;
	const fdt32_t *reg = fdt_getprop(fdt, node, "reg", &len);
	if (!reg || len == 0)
		return 0;

	int old_stride = old_na + old_ns;
	if (len % (int)(old_stride * sizeof(fdt32_t)))
		return -FDT_ERR_BADVALUE;

	int n = len / (old_stride * sizeof(fdt32_t));
	if (n > MAX_REG_ENTRIES)
		return -FDT_ERR_BADVALUE;

	u64 addr[MAX_REG_ENTRIES], size[MAX_REG_ENTRIES];
	for (int i = 0; i < n; i++) {
		addr[i] = decode_cells(&reg, old_na);
		size[i] = decode_cells(&reg, old_ns);
		if ((new_na == 1 && (addr[i] >> 32)) ||
		    (new_ns == 1 && (size[i] >> 32)))
			return -FDT_ERR_BADVALUE;
	}

	void *prop;
	int new_len = n * (new_na + new_ns) * sizeof(fdt32_t);
	int ret = fdt_setprop_placeholder(fdt, node, "reg", new_len, &prop);
	if (ret < 0)
		return ret;

	fdt32_t *p = prop;
	for (int i = 0; i < n; i++) {
		p += encode_cells(p, addr[i], new_na);
		p += encode_cells(p, size[i], new_ns);
	}

	return 0;
}

/* override_reserved_memory() - Adopt the firmware DT's address width.
 *
 * The firmware DT defines the platform #address-cells/#size-cells, and the
 * kernel drops /reserved-memory whose cells do not match the root. Set the app
 * root cells to the firmware's and re-encode the app's own reserved-memory
 * regions to that width so the kernel keeps the reservations.
 */
static int override_reserved_memory(void *dst, const void *fw)
{
	int root_na = read_cells(fw, 0, ADDRESS_CELLS);
	int root_ns = read_cells(fw, 0, SIZE_CELLS);
	int resv_mem, node, ret;

	/* Malformed cells fail the merge, absent cells leave the app as-is. */
	if (root_na < 0 || root_ns < 0)
		return -FDT_ERR_BADNCELLS;
	if (!root_na || !root_ns)
		return 0;

	/* Override the app root cells with the firmware's. */
	ret = fdt_setprop_u32(dst, 0, ADDRESS_CELLS, root_na);
	if (ret < 0)
		return ret;
	ret = fdt_setprop_u32(dst, 0, SIZE_CELLS, root_ns);
	if (ret < 0)
		return ret;

	resv_mem = fdt_path_offset(dst, RESERVED_MEMORY);
	if (resv_mem < 0)
		return 0;

	int resv_na = read_cells(dst, resv_mem, ADDRESS_CELLS);
	int resv_ns = read_cells(dst, resv_mem, SIZE_CELLS);

	if (resv_na < 0 || resv_ns < 0)
		return -FDT_ERR_BADNCELLS;
	if (!resv_na || !resv_ns)
		return 0;

	/* Already at the firmware root width, nothing to re-encode. */
	if (resv_na == root_na && resv_ns == root_ns)
		return 0;

	log_info("%s: re-encoding /reserved-memory cells %d/%d -> %d/%d\n",
		 __func__, resv_na, resv_ns, root_na, root_ns);

	fdt_for_each_subnode(node, dst, resv_mem)
	{
		ret = reencode_reg(dst, node, resv_na, resv_ns, root_na,
				   root_ns);
		if (ret < 0)
			return ret;
	}

	ret = fdt_setprop_u32(dst, resv_mem, ADDRESS_CELLS, root_na);
	if (ret < 0)
		return ret;

	return fdt_setprop_u32(dst, resv_mem, SIZE_CELLS, root_ns);
}

/* efi_dt_fixup_merge() - EFI_DT_FIXUP_PROTOCOL implementation to merge FW DT
 * into UEFI app DT.
 *
 * Obtains the FW DT from the EFI configuration table and merges its missing
 * nodes and properties into the provided DTB. Special handling is applied to
 * 'bootargs', which are appended rather than overwritten. The app root
 * #address-cells/#size-cells are overridden with the firmware's and the app's
 * /reserved-memory regions re-encoded to match, before the firmware nodes are
 * merged in. The firmware DT owns the platform address width, and the kernel
 * rejects /reserved-memory unless its cells match the root.
 *
 * On hardware, the UEFI application is usually the source of truth for the FDT.
 * However, in VM environments (e.g., QEMU or Crosvm), the VMM-injected DT must
 * be respected and merged into the application's DT.
 *
 * Known limitations:
 *
 * Since the firmware DT is merged into the target `dtb` as-is, all
 * phandles are also propagated. This may conflict with existing phandles
 * in the target `dtb` and result in an invalid final device tree. Therefore,
 * the provided `dtb` must not have any phandles prior to merging.
 *
 * Note that in the GBL/CF flow, the base `dtb` is known to be free of phandles,
 * so this limitation is safely avoided in practice.
 */
static efi_status_t EFIAPI efi_dt_fixup_merge(
	struct efi_dt_fixup_protocol *this, void *dtb, size_t *buffer_size)
{
	EFI_ENTRY("%p, %p, %p", this, dtb, buffer_size);

	if (!dtb || !buffer_size || fdt_check_header(dtb)) {
		log_err("%s: invalid parameters\n", __func__);
		return EFI_EXIT(EFI_INVALID_PARAMETER);
	}

	void *fdt_fw = efi_get_configuration_table(&efi_guid_fdt);

	if (!fdt_fw) {
		log_warning("%s: FW DT not found in EFI config table\n",
			    __func__);
		return EFI_EXIT(EFI_SUCCESS);
	}
	if (fdt_check_header(fdt_fw) < 0) {
		log_err("%s: FW DT header check failed\n", __func__);
		return EFI_EXIT(EFI_DEVICE_ERROR);
	}

	size_t required = fdt_totalsize(dtb) + fdt_totalsize(fdt_fw);

	if (required > *buffer_size) {
		log_err("%s: buffer too small (required %zu, available %zu)\n",
			__func__, required, *buffer_size);
		*buffer_size = required;
		return EFI_EXIT(EFI_BUFFER_TOO_SMALL);
	}

	if (fdt_open_into(dtb, dtb, *buffer_size) < 0) {
		log_err("%s: fdt_open_into failed\n", __func__);
		return EFI_EXIT(EFI_DEVICE_ERROR);
	}

	if (override_reserved_memory(dtb, fdt_fw) < 0) {
		log_err("%s: override_reserved_memory failed\n", __func__);
		return EFI_EXIT(EFI_DEVICE_ERROR);
	}

	if (merge_node(dtb, 0, fdt_fw, 0) < 0) {
		log_err("%s: merge_node failed\n", __func__);
		return EFI_EXIT(EFI_DEVICE_ERROR);
	}

	if (merge_bootargs(dtb, fdt_fw) < 0) {
		log_err("%s: merge_bootargs failed\n", __func__);
		return EFI_EXIT(EFI_DEVICE_ERROR);
	}

	fdt_pack(dtb);
	*buffer_size = fdt_totalsize(dtb);
	log_info("%s: merge success, final size %zu\n", __func__, *buffer_size);

	return EFI_EXIT(EFI_SUCCESS);
}

static struct efi_dt_fixup_protocol efi_dt_fixup_merge_proto = {
	.revision = EFI_DT_FIXUP_PROTOCOL_REVISION,
	.fixup = efi_dt_fixup_merge
};

efi_status_t efi_dt_fixup_cf_register(void)
{
	return efi_add_protocol(efi_root, &efi_guid_dt_fixup_protocol,
				&efi_dt_fixup_merge_proto);
}
