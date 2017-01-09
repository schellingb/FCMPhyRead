/*
 * Copyright (C) 2015  Siarhei Siamashka <siarhei.siamashka@gmail.com>
 * Copyright (C) 2016  Bernhard Nortmann <bernhard.nortmann@web.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _SUNXI_TOOLS_SOC_INFO_H
#define _SUNXI_TOOLS_SOC_INFO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* SoC version information, as retrieved by the FEL protocol */
#pragma pack(push, 1)
struct aw_fel_version {
	char signature[8];
	uint32_t soc_id;	/* 0x00162300 */
	uint32_t unknown_0a;	/* 1 */
	uint16_t protocol;	/* 1 */
	uint8_t  unknown_12;	/* 0x44 */
	uint8_t  unknown_13;	/* 0x08 */
	uint32_t scratchpad;	/* 0x7e00 */
	uint32_t pad[2];	/* unused */
} /* __attribute__((packed)) */ ;
#pragma pack(pop)

/*
 * Buffer for a SoC name string. We want at least 6 + 1 characters, to store
 * the hexadecimal ID "0xABCD" for unknown SoCs, plus the terminating NUL.
 */
typedef char soc_name_t[8];

/*
 * The 'sram_swap_buffers' structure is used to describe information about
 * pairwise memory regions in SRAM, the content of which needs to be exchanged
 * before calling the U-Boot SPL code and then exchanged again before returning
 * control back to the FEL code from the BROM.
 */
typedef struct {
	uint32_t buf1; /* BROM buffer */
	uint32_t buf2; /* backup storage location */
	uint32_t size; /* buffer size */
} sram_swap_buffers;

/*
 * Each SoC variant may have its own list of memory buffers to be exchanged
 * and the information about the placement of the thunk code, which handles
 * the transition of execution from the BROM FEL code to the U-Boot SPL and
 * back.
 *
 * Note: the entries in the 'swap_buffers' tables need to be sorted by 'buf1'
 * addresses. And the 'buf1' addresses are the BROM data buffers, while 'buf2'
 * addresses are the intended backup locations.
 *
 * Also for performance reasons, we optionally want to have MMU enabled with
 * optimal section attributes configured (the code from the BROM should use
 * I-cache, writing data to the DRAM area should use write combining). The
 * reason is that the BROM FEL protocol implementation moves data using the
 * CPU somewhere on the performance critical path when transferring data over
 * USB. The older SoC variants (A10/A13/A20/A31/A23) already have MMU enabled
 * and we only need to adjust section attributes. The BROM in newer SoC variants
 * (A33/A83T/H3) doesn't enable MMU any more, so we need to find some 16K of
 * spare space in SRAM to place the translation table there and specify it as
 * the 'mmu_tt_addr' field in the 'soc_sram_info' structure. The 'mmu_tt_addr'
 * address must be 16K aligned.
 */
typedef struct {
	uint32_t           soc_id;       /* ID of the SoC */
	const char         *name;        /* human-readable SoC name string */
	uint32_t           spl_addr;     /* SPL load address */
	uint32_t           scratch_addr; /* A safe place to upload & run code */
	uint32_t           thunk_addr;   /* Address of the thunk code */
	uint32_t           thunk_size;   /* Maximal size of the thunk code */
	bool               needs_l2en;   /* Set the L2EN bit */
	uint32_t           mmu_tt_addr;  /* MMU translation table address */
	uint32_t           sid_base;     /* base address for SID registers */
	uint32_t           sid_offset;   /* offset for SID_KEY[0-3], "root key" */
	uint32_t           rvbar_reg;    /* MMIO address of RVBARADDR0_L register */
	bool               sid_fix;      /* Use SID workaround (read via register) */
	sram_swap_buffers *swap_buffers;
} soc_info_t;


void get_soc_name_from_id(soc_name_t buffer, uint32_t soc_id);
soc_info_t *get_soc_info_from_id(uint32_t soc_id);
soc_info_t *get_soc_info_from_version(struct aw_fel_version *buf);

#endif /* _SUNXI_TOOLS_SOC_INFO_H */

/*
 * Copyright (C) 2016 Bernhard Nortmann <bernhard.nortmann@web.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _SUNXI_TOOLS_FEL_LIB_H
#define _SUNXI_TOOLS_FEL_LIB_H

#include <stdbool.h>
#include <stdint.h>

/* USB identifiers for Allwinner device in FEL mode */
#define AW_USB_VENDOR_ID	0x1F3A
#define AW_USB_PRODUCT_ID	0xEFE8

typedef struct _felusb_handle felusb_handle; /* opaque data type */

/* More general FEL "device" handle, including version data and SoC info */
typedef struct {
	felusb_handle *usb;
	struct aw_fel_version soc_version;
	soc_name_t soc_name;
	soc_info_t *soc_info;
} feldev_handle;

/* list_fel_devices() will return an array of this type */
typedef struct {
	int busnum, devnum;
	struct aw_fel_version soc_version;
	soc_name_t soc_name;
	soc_info_t *soc_info;
	uint32_t SID[4];
} feldev_list_entry;

/* FEL device management */

void feldev_init(void);
void feldev_done(feldev_handle *dev);

feldev_handle *feldev_open(int busnum, int devnum,
			   uint16_t vendor_id, uint16_t product_id);
void feldev_close(feldev_handle *dev);

feldev_list_entry *list_fel_devices(size_t *count);

/* FEL functions */

void aw_fel_read(feldev_handle *dev, uint32_t offset, void *buf, size_t len);
void aw_fel_write(feldev_handle *dev, void *buf, uint32_t offset, size_t len);
void aw_fel_write_buffer(feldev_handle *dev, void *buf, uint32_t offset,
			 size_t len, bool progress);
void aw_fel_execute(feldev_handle *dev, uint32_t offset);

void fel_readl_n(feldev_handle *dev, uint32_t addr, uint32_t *dst, size_t count);
void fel_writel_n(feldev_handle *dev, uint32_t addr, uint32_t *src, size_t count);

/* retrieve SID root key */
bool fel_get_sid_root_key(feldev_handle *dev, uint32_t *result,
			  bool force_workaround);

#ifdef __cplusplus
}
#endif

#endif /* _SUNXI_TOOLS_FEL_LIB_H */
