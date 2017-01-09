﻿/*
 * Copyright (C) 2012 Henrik Nordstrom <henrik@henriknordstrom.net>
 * Copyright (C) 2015 Siarhei Siamashka <siarhei.siamashka@gmail.com>
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

#if (defined(_WIN16) || defined(_WIN32) || defined(_WIN64)) && !defined(__WINDOWS__) || defined(WIN32)
#define _CRT_SECURE_NO_WARNINGS
#pragma comment (lib, "libusb-1.0.lib")
#endif

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <malloc.h>

#include "fel_lib.h"
#include "libusb.h"

/**********************************************************************
 * SoC information and retrieval of soc_sram_info
 **********************************************************************/

/*
 * The FEL code from BROM in A10/A13/A20 sets up two stacks for itself. One
 * at 0x2000 (and growing down) for the IRQ handler. And another one at 0x7000
 * (and also growing down) for the regular code. In order to use the whole
 * 32 KiB in the A1/A2 sections of SRAM, we need to temporarily move these
 * stacks elsewhere. And the addresses 0x7D00-0x7FFF contain something
 * important too (overwriting them kills FEL). On A10/A13/A20 we can use
 * the SRAM sections A3/A4 (0x8000-0xBFFF) for this purpose.
 */
sram_swap_buffers a10_a13_a20_sram_swap_buffers[] = {
	/* 0x1C00-0x1FFF (IRQ stack) */
	{ .buf1 = 0x1C00, .buf2 = 0xA400, .size = 0x0400 },
	/* 0x5C00-0x6FFF (Stack) */
	{ .buf1 = 0x5C00, .buf2 = 0xA800, .size = 0x1400 },
	/* 0x7C00-0x7FFF (Something important) */
	{ .buf1 = 0x7C00, .buf2 = 0xBC00, .size = 0x0400 },
	{ .size = 0 }  /* End of the table */
};

/*
 * A31 is very similar to A10/A13/A20, except that it has no SRAM at 0x8000.
 * So we use the SRAM section B at 0x20000-0x2FFFF instead. In the FEL mode,
 * the MMU translation table is allocated by the BROM at 0x20000. But we can
 * also safely use it as the backup storage because the MMU is temporarily
 * disabled during the time of the SPL execution.
 */
sram_swap_buffers a31_sram_swap_buffers[] = {
	{ .buf1 = 0x1800, .buf2 = 0x20000, .size = 0x800 },
	{ .buf1 = 0x5C00, .buf2 = 0x20800, .size = 0x8000 - 0x5C00 },
	{ .size = 0 }  /* End of the table */
};

/*
 * A64 has 32KiB of SRAM A at 0x10000 and a large SRAM C at 0x18000. SRAM A
 * and SRAM C reside in the address space back-to-back without any gaps, thus
 * representing a singe large contiguous area. Everything is the same as on
 * A10/A13/A20, but just shifted by 0x10000.
 */
sram_swap_buffers a64_sram_swap_buffers[] = {
	/* 0x11C00-0x11FFF (IRQ stack) */
	{ .buf1 = 0x11C00, .buf2 = 0x1A400, .size = 0x0400 },
	/* 0x15C00-0x16FFF (Stack) */
	{ .buf1 = 0x15C00, .buf2 = 0x1A800, .size = 0x1400 },
	/* 0x17C00-0x17FFF (Something important) */
	{ .buf1 = 0x17C00, .buf2 = 0x1BC00, .size = 0x0400 },
	{ .size = 0 }  /* End of the table */
};

/*
 * Use the SRAM section at 0x44000 as the backup storage. This is the memory,
 * which is normally shared with the OpenRISC core (should we do an extra check
 * to ensure that this core is powered off and can't interfere?).
 */
sram_swap_buffers ar100_abusing_sram_swap_buffers[] = {
	{ .buf1 = 0x1800, .buf2 = 0x44000, .size = 0x800 },
	{ .buf1 = 0x5C00, .buf2 = 0x44800, .size = 0x8000 - 0x5C00 },
	{ .size = 0 }  /* End of the table */
};

/*
 * A80 has 40KiB SRAM A1 at 0x10000 where the SPL has to be loaded to. The
 * secure SRAM B at 0x20000 is used as backup area for FEL stacks and data.
 */
sram_swap_buffers a80_sram_swap_buffers[] = {
	{ .buf1 = 0x11800, .buf2 = 0x20000, .size = 0x800 },
	{ .buf1 = 0x15400, .buf2 = 0x20800, .size = 0x18000 - 0x15400 },
	{ .size = 0 }  /* End of the table */
};

soc_info_t soc_info_table[] = {
	{
		.soc_id       = 0x1623, /* Allwinner A10 */
		.name         = "A10",
		.scratch_addr = 0x1000,
		.thunk_addr   = 0xA200, .thunk_size = 0x200,
		.swap_buffers = a10_a13_a20_sram_swap_buffers,
		.needs_l2en   = true,
		.sid_base     = 0x01C23800,
	},{
		.soc_id       = 0x1625, /* Allwinner A10s, A13, R8 */
		.name         = "A13",
		.scratch_addr = 0x1000,
		.thunk_addr   = 0xA200, .thunk_size = 0x200,
		.swap_buffers = a10_a13_a20_sram_swap_buffers,
		.needs_l2en   = true,
		.sid_base     = 0x01C23800,
	},{
		.soc_id       = 0x1651, /* Allwinner A20 */
		.name         = "A20",
		.scratch_addr = 0x1000,
		.thunk_addr   = 0xA200, .thunk_size = 0x200,
		.swap_buffers = a10_a13_a20_sram_swap_buffers,
		.sid_base     = 0x01C23800,
	},{
		.soc_id       = 0x1650, /* Allwinner A23 */
		.name         = "A23",
		.scratch_addr = 0x1000,
		.thunk_addr   = 0x46E00, .thunk_size = 0x200,
		.swap_buffers = ar100_abusing_sram_swap_buffers,
		.sid_base     = 0x01C23800,
	},{
		.soc_id       = 0x1633, /* Allwinner A31 */
		.name         = "A31",
		.scratch_addr = 0x1000,
		.thunk_addr   = 0x22E00, .thunk_size = 0x200,
		.swap_buffers = a31_sram_swap_buffers,
	},{
		.soc_id       = 0x1667, /* Allwinner A33, R16 */
		.name         = "A33",
		.scratch_addr = 0x1000,
		.thunk_addr   = 0x46E00, .thunk_size = 0x200,
		.swap_buffers = ar100_abusing_sram_swap_buffers,
		.sid_base     = 0x01C23800,
	},{
		.soc_id       = 0x1689, /* Allwinner A64 */
		.name         = "A64",
		.spl_addr     = 0x10000,
		.scratch_addr = 0x11000,
		.thunk_addr   = 0x1A200, .thunk_size = 0x200,
		.swap_buffers = a64_sram_swap_buffers,
		.sid_base     = 0x01C14000,
		.sid_offset   = 0x200,
		.rvbar_reg    = 0x017000A0,
	},{
		.soc_id       = 0x1639, /* Allwinner A80 */
		.name         = "A80",
		.spl_addr     = 0x10000,
		.scratch_addr = 0x11000,
		.thunk_addr   = 0x23400, .thunk_size = 0x200,
		.swap_buffers = a80_sram_swap_buffers,
		.sid_base     = 0X01C0E000,
		.sid_offset   = 0x200,
	},{
		.soc_id       = 0x1673, /* Allwinner A83T */
		.name         = "A83T",
		.scratch_addr = 0x1000,
		.thunk_addr   = 0x46E00, .thunk_size = 0x200,
		.swap_buffers = ar100_abusing_sram_swap_buffers,
		.sid_base     = 0x01C14000,
		.sid_offset   = 0x200,
	},{
		.soc_id       = 0x1680, /* Allwinner H3, H2+ */
		.name         = "H3",
		.scratch_addr = 0x1000,
		.mmu_tt_addr  = 0x8000,
		.thunk_addr   = 0xA200, .thunk_size = 0x200,
		.swap_buffers = a10_a13_a20_sram_swap_buffers,
		.sid_base     = 0x01C14000,
		.sid_offset   = 0x200,
		.sid_fix      = true,
	},{
		.soc_id       = 0x1681, /* Allwinner V3s */
		.name         = "V3s",
		.scratch_addr = 0x1000,
		.mmu_tt_addr  = 0x8000,
		.thunk_addr   = 0xA200, .thunk_size = 0x200,
		.swap_buffers = a10_a13_a20_sram_swap_buffers,
		.sid_base     = 0x01C23800,
	},{
		.soc_id       = 0x1718, /* Allwinner H5 */
		.name         = "H5",
		.spl_addr     = 0x10000,
		.scratch_addr = 0x11000,
		.thunk_addr   = 0x1A200, .thunk_size = 0x200,
		.swap_buffers = a64_sram_swap_buffers,
		.sid_base     = 0x01C14000,
		.sid_offset   = 0x200,
		.rvbar_reg    = 0x017000A0,
	},{
		.soc_id       = 0x1701, /* Allwinner R40 */
		.name         = "R40",
		.scratch_addr = 0x1000,
		.thunk_addr   = 0xA200, .thunk_size = 0x200,
		.swap_buffers = a10_a13_a20_sram_swap_buffers,
		.sid_base     = 0x01C1B000,
		.sid_offset   = 0x200,
	},{
		.swap_buffers = NULL /* End of the table */
	}
};

/*
 * This generic record assumes BROM with similar properties to A10/A13/A20/A31,
 * but no extra SRAM sections beyond 0x8000. It also assumes that the IRQ
 * handler stack usage never exceeds 0x400 bytes.
 *
 * The users may or may not hope that the 0x7000-0x8000 area is also unused
 * by the BROM and re-purpose it for the SPL stack.
 *
 * The size limit for the ".text + .data" sections is ~21 KiB.
 */
sram_swap_buffers generic_sram_swap_buffers[] = {
	{ .buf1 = 0x1C00, .buf2 = 0x5800, .size = 0x400 },
	{ .size = 0 }  /* End of the table */
};

soc_info_t generic_soc_info = {
	.scratch_addr = 0x1000,
	.thunk_addr   = 0x5680, .thunk_size = 0x180,
	.swap_buffers = generic_sram_swap_buffers,
};

/* functions to retrieve SoC information */

soc_info_t *get_soc_info_from_id(uint32_t soc_id)
{
	soc_info_t *soc, *result = NULL;

	for (soc = soc_info_table; soc->swap_buffers; soc++)
		if (soc->soc_id == soc_id) {
			result = soc;
			break;
		}

	if (!result) {
		printf("Warning: no 'soc_sram_info' data for your SoC (id=%04X)\n",
		       soc_id);
		result = &generic_soc_info;
	}
	return result;
}

soc_info_t *get_soc_info_from_version(struct aw_fel_version *buf)
{
	return get_soc_info_from_id(buf->soc_id);
}

void get_soc_name_from_id(soc_name_t buffer, uint32_t soc_id)
{
	soc_info_t *soc;
	for (soc = soc_info_table; soc->swap_buffers; soc++)
		if (soc->soc_id == soc_id && soc->name != NULL) {
			strncpy(buffer, soc->name, sizeof(soc_name_t) - 1);
			return;
		}

	/* unknown SoC (or name string missing), use the hexadecimal ID */
	sprintf(buffer, /*sizeof(soc_name_t) - 1,*/ "0x%04X", soc_id);
}

/**********************************************************************
 * portable_endian.h
 **********************************************************************/
// "License": Public Domain
// I, Mathias Panzenboeck, place this file hereby into the public domain. Use it at your own risk for whatever you like.
// In case there are jurisdictions that don't support putting things in the public domain you can also consider it to
// be "dual licensed" under the BSD, MIT and Apache licenses, if you want to. This code is trivial anyway. Consider it
// an example on how to get the endian conversion functions on different platforms.
#ifndef PORTABLE_ENDIAN_H__
#define PORTABLE_ENDIAN_H__
#if (defined(_WIN16) || defined(_WIN32) || defined(_WIN64)) && !defined(__WINDOWS__) || defined(WIN32)
#	define __WINDOWS__
#endif
#if defined(__linux__) || defined(__CYGWIN__)
#	include <endian.h>
#elif defined(__APPLE__)
#	include <libkern/OSByteOrder.h>
#	define htole16(x) OSSwapHostToLittleInt16(x)
#	define htole32(x) OSSwapHostToLittleInt32(x)
#	define le16toh(x) OSSwapLittleToHostInt16(x)
#	define le32toh(x) OSSwapLittleToHostInt32(x)
#elif defined(__OpenBSD__) ||  defined(__NetBSD__) || defined(__FreeBSD__) || defined(__DragonFly__)
#	include <sys/endian.h>
/* For functions still missing, try to substitute 'historic' OpenBSD names */
#ifndef le16toh
#	define le16toh(x) letoh16(x)
#endif
#ifndef le32toh
#	define le32toh(x) letoh32(x)
#endif
#elif defined(__WINDOWS__)
#	if BYTE_ORDER == LITTLE_ENDIAN
#		define htole16(x) (x)
#		define htole32(x) (x)
#		define le16toh(x) (x)
#		define le32toh(x) (x)
#	elif BYTE_ORDER == BIG_ENDIAN
#		include <winsock2.h>
#		include <sys/param.h>
#		define htole16(x) __builtin_bswap16(x)
#		define htole32(x) __builtin_bswap32(x)
#		define le16toh(x) __builtin_bswap16(x)
#		define le32toh(x) __builtin_bswap32(x)
#	else
#		error byte order not supported
#	endif
#else
#	error platform not supported
#endif
#endif

/**********************************************************************
 * USB library and helper functions for the FEL utility
 **********************************************************************/

#define USB_TIMEOUT	1000 /* 2 seconds */

static bool fel_lib_initialized = false;
static bool fel_lib_errored = false;

/* This is out 'private' data type that will be part of a "FEL device" handle */
struct _felusb_handle {
	libusb_device_handle *handle;
	int endpoint_out, endpoint_in;
	bool iface_detached;
};

/* a helper function to report libusb errors */
void usb_error(int rc, const char *caption, int exitcode)
{
	fel_lib_errored = true;
	if (caption)
		fprintf(stderr, " [%s ", caption);

#if defined(LIBUSBX_API_VERSION) && (LIBUSBX_API_VERSION >= 0x01000102)
	fprintf(stderr, "ERROR %d: %s]", rc, libusb_strerror(rc));
#else
	/* assume that libusb_strerror() is missing in the libusb API */
	fprintf(stderr, "ERROR %d]", rc);
#endif

	//if (exitcode != 0)
	//	exit(exitcode);
}

/*
 * AW_USB_MAX_BULK_SEND and the timeout constant USB_TIMEOUT are related.
 * Both need to be selected in a way that transferring the maximum chunk size
 * with (SoC-specific) slow transfer speed won't time out.
 *
 * The 512 KiB here are chosen based on the assumption that we want a 10 seconds
 * timeout, and "slow" transfers take place at approx. 64 KiB/sec - so we can
 * expect the maximum chunk being transmitted within 8 seconds or less.
 */
static const int AW_USB_MAX_BULK_SEND = 512 * 1024; /* 512 KiB per bulk request */

void usb_bulk_send(libusb_device_handle *usb, int ep, const void *data,
		   size_t length, bool progress)
{
	if (fel_lib_errored) return;

	/*
	 * With no progress notifications, we'll use the maximum chunk size.
	 * Otherwise, it's useful to lower the size (have more chunks) to get
	 * more frequent status updates. 128 KiB per request seem suitable.
	 * (Worst case of "slow" transfers -> one update every two seconds.)
	 */
	size_t max_chunk = progress ? 128 * 1024 : AW_USB_MAX_BULK_SEND;

	size_t chunk;
	int rc, sent;
	while (length > 0) {
		chunk = length < max_chunk ? length : max_chunk;
		rc = libusb_bulk_transfer(usb, ep, (void *)data, (int)chunk,
					  &sent, USB_TIMEOUT);
		if (rc != 0)
		{
			usb_error(rc, "usb_bulk_send()", 2);
			return;
		}
		length -= sent;
		data = (char*)data + sent;

		if (progress)
			{} //progress_update(sent); /* notification after each chunk */
	}
}

void usb_bulk_recv(libusb_device_handle *usb, int ep, void *data, int length)
{
	if (fel_lib_errored) { memset(data, 0, length); return; }

	int rc, recv;
	while (length > 0) {
		rc = libusb_bulk_transfer(usb, ep, data, length,
					  &recv, USB_TIMEOUT);
		if (rc != 0)
		{
			usb_error(rc, "usb_bulk_recv()", 2);
			return;
		}
		length -= recv;
		data = (char*)data + recv;
	}
}

#pragma pack(push, 1)
struct aw_usb_request {
	char signature[8];
	uint32_t length;
	uint32_t unknown1;	/* 0x0c000000 */
	uint16_t request;
	uint32_t length2;	/* Same as length */
	char     pad[10];
} /* __attribute__((packed)) */ ;
#pragma pack(pop)

#define AW_USB_READ	0x11
#define AW_USB_WRITE	0x12

struct aw_fel_request {
	uint32_t request;
	uint32_t address;
	uint32_t length;
	uint32_t pad;
};

/* FEL request types */
#define AW_FEL_VERSION	0x001
#define AW_FEL_1_WRITE	0x101
#define AW_FEL_1_EXEC	0x102
#define AW_FEL_1_READ	0x103

static void aw_send_usb_request(feldev_handle *dev, int type, int length)
{
	struct aw_usb_request req = {
		.signature = "AWUC",
		.request = htole16(type),
		.length = htole32(length),
		.unknown1 = htole32(0x0c000000)
	};
	req.length2 = req.length;
	usb_bulk_send(dev->usb->handle, dev->usb->endpoint_out,
		      &req, sizeof(req), false);
}

static void aw_read_usb_response(feldev_handle *dev)
{
	char buf[13];
	usb_bulk_recv(dev->usb->handle, dev->usb->endpoint_in,
		      buf, sizeof(buf));
	if (fel_lib_errored) return;
	assert(strcmp(buf, "AWUS") == 0);
}

static void aw_usb_write(feldev_handle *dev, const void *data, size_t len,
			 bool progress)
{
	aw_send_usb_request(dev, AW_USB_WRITE, (int)len);
	usb_bulk_send(dev->usb->handle, dev->usb->endpoint_out,
		      data, len, progress);
	aw_read_usb_response(dev);
}

static void aw_usb_read(feldev_handle *dev, const void *data, size_t len)
{
	aw_send_usb_request(dev, AW_USB_READ, (int)len);
	usb_bulk_send(dev->usb->handle, dev->usb->endpoint_in,
		      data, len, false);
	aw_read_usb_response(dev);
}

void aw_send_fel_request(feldev_handle *dev, int type,
			 uint32_t addr, uint32_t length)
{
	struct aw_fel_request req = {
		.request = htole32(type),
		.address = htole32(addr),
		.length = htole32(length)
	};
	aw_usb_write(dev, &req, sizeof(req), false);
}

void aw_read_fel_status(feldev_handle *dev)
{
	char buf[8];
	aw_usb_read(dev, buf, sizeof(buf));
}

/* AW_FEL_VERSION request */
static void aw_fel_get_version(feldev_handle *dev, struct aw_fel_version *buf)
{
	aw_send_fel_request(dev, AW_FEL_VERSION, 0, 0);
	aw_usb_read(dev, buf, sizeof(*buf));
	aw_read_fel_status(dev);

	buf->soc_id = (le32toh(buf->soc_id) >> 8) & 0xFFFF;
	buf->unknown_0a = le32toh(buf->unknown_0a);
	buf->protocol = le32toh(buf->protocol);
	buf->scratchpad = le16toh(buf->scratchpad);
	buf->pad[0] = le32toh(buf->pad[0]);
	buf->pad[1] = le32toh(buf->pad[1]);
}

/* AW_FEL_1_READ request */
void aw_fel_read(feldev_handle *dev, uint32_t offset, void *buf, size_t len)
{
	aw_send_fel_request(dev, AW_FEL_1_READ, offset, (uint32_t)len);
	aw_usb_read(dev, buf, len);
	aw_read_fel_status(dev);
}

/* AW_FEL_1_WRITE request */
void aw_fel_write(feldev_handle *dev, void *buf, uint32_t offset, size_t len)
{
	aw_send_fel_request(dev, AW_FEL_1_WRITE, offset, (uint32_t)len);
	aw_usb_write(dev, buf, len, false);
	aw_read_fel_status(dev);
}

/* AW_FEL_1_EXEC request */
void aw_fel_execute(feldev_handle *dev, uint32_t offset)
{
	aw_send_fel_request(dev, AW_FEL_1_EXEC, offset, 0);
	aw_read_fel_status(dev);
}

/*
 * This function is a higher-level wrapper for the FEL write functionality.
 * Unlike aw_fel_write() above - which is reserved for internal use - this
 * routine optionally allows progress callbacks.
 */
void aw_fel_write_buffer(feldev_handle *dev, void *buf, uint32_t offset,
			 size_t len, bool progress)
{
	aw_send_fel_request(dev, AW_FEL_1_WRITE, offset, (uint32_t)len);
	aw_usb_write(dev, buf, len, progress);
	aw_read_fel_status(dev);
}

/*
 * We don't want the scratch code/buffer to exceed a maximum size of 0x400 bytes
 * (256 32-bit words) on readl_n/writel_n transfers. To guarantee this, we have
 * to account for the amount of space the ARM code uses.
 */
#define LCODE_ARM_WORDS  12 /* word count of the [read/write]l_n scratch code */
#define LCODE_ARM_SIZE   (LCODE_ARM_WORDS << 2) /* code size in bytes */
#define LCODE_MAX_TOTAL  0x100 /* max. words in buffer */
#define LCODE_MAX_WORDS  (LCODE_MAX_TOTAL - LCODE_ARM_WORDS) /* data words */

/* multiple "readl" from sequential addresses to a destination buffer */
static void aw_fel_readl_n(feldev_handle *dev, uint32_t addr,
			   uint32_t *dst, size_t count)
{
	if (count == 0) return;
	if (count > LCODE_MAX_WORDS) {
		fprintf(stderr,
			"ERROR: Max. word count exceeded, truncating aw_fel_readl_n() transfer\n");
		count = LCODE_MAX_WORDS;
	}

	assert(LCODE_MAX_WORDS < 256); /* protect against corruption of ARM code */
	uint32_t arm_code[] = {
		htole32(0xe59f0020), /* ldr  r0, [pc, #32] ; ldr r0,[read_addr]  */
		htole32(0xe28f1024), /* add  r1, pc, #36   ; adr r1, read_data   */
		htole32(0xe59f201c), /* ldr  r2, [pc, #28] ; ldr r2,[read_count] */
		htole32(0xe3520000 + LCODE_MAX_WORDS), /* cmp	r2, #LCODE_MAX_WORDS */
		htole32(0xc3a02000 + LCODE_MAX_WORDS), /* movgt	r2, #LCODE_MAX_WORDS */
		/* read_loop: */
		htole32(0xe2522001), /* subs r2, r2, #1    ; r2 -= 1             */
		htole32(0x412fff1e), /* bxmi lr            ; return if (r2 < 0)  */
		htole32(0xe4903004), /* ldr  r3, [r0], #4  ; load and post-inc   */
		htole32(0xe4813004), /* str  r3, [r1], #4  ; store and post-inc  */
		htole32(0xeafffffa), /* b    read_loop                           */
		htole32(addr),       /* read_addr */
		htole32((uint32_t)count)       /* read_count */
		/* read_data (buffer) follows, i.e. values go here */
	};
	assert(sizeof(arm_code) == LCODE_ARM_SIZE);

	/* scratch buffer setup: transfers ARM code, including addr and count */
	aw_fel_write(dev, arm_code, dev->soc_info->scratch_addr, sizeof(arm_code));
	/* execute code, read back the result */
	aw_fel_execute(dev, dev->soc_info->scratch_addr);
	uint32_t* buffer = (uint32_t*)(size_t)alloca(count * sizeof(uint32_t)); // [count];
	aw_fel_read(dev, dev->soc_info->scratch_addr + LCODE_ARM_SIZE,
		    buffer, sizeof(buffer));
	/* extract values to destination buffer */
	uint32_t *val = buffer;
	while (count-- > 0)
		*dst++ = le32toh(*val++);
}

/*
 * aw_fel_readl_n() wrapper that can handle large transfers. If necessary,
 * those will be done in separate 'chunks' of no more than LCODE_MAX_WORDS.
 */
void fel_readl_n(feldev_handle *dev, uint32_t addr, uint32_t *dst, size_t count)
{
	while (count > 0) {
		size_t n = count > LCODE_MAX_WORDS ? LCODE_MAX_WORDS : count;
		aw_fel_readl_n(dev, addr, dst, n);
		addr += (uint32_t)n * sizeof(uint32_t);
		dst += n;
		count -= n;
	}
}

/* multiple "writel" from a source buffer to sequential addresses */
static void aw_fel_writel_n(feldev_handle *dev, uint32_t addr,
			    uint32_t *src, size_t count)
{
	if (count == 0) return;
	if (count > LCODE_MAX_WORDS) {
		fprintf(stderr,
			"ERROR: Max. word count exceeded, truncating aw_fel_writel_n() transfer\n");
		count = LCODE_MAX_WORDS;
	}

	assert(LCODE_MAX_WORDS < 256); /* protect against corruption of ARM code */
	/*
	 * We need a fixed array size to allow for (partial) initialization,
	 * so we'll claim the maximum total number of words (0x100) here.
	 */
	uint32_t arm_code[LCODE_MAX_TOTAL] = {
		htole32(0xe59f0020), /* ldr  r0, [pc, #32] ; ldr r0,[write_addr] */
		htole32(0xe28f1024), /* add  r1, pc, #36   ; adr r1, write_data  */
		htole32(0xe59f201c), /* ldr  r2, [pc, #28] ; ldr r2,[write_count]*/
		htole32(0xe3520000 + LCODE_MAX_WORDS), /* cmp	r2, #LCODE_MAX_WORDS */
		htole32(0xc3a02000 + LCODE_MAX_WORDS), /* movgt	r2, #LCODE_MAX_WORDS */
		/* write_loop: */
		htole32(0xe2522001), /* subs r2, r2, #1    ; r2 -= 1             */
		htole32(0x412fff1e), /* bxmi lr            ; return if (r2 < 0)  */
		htole32(0xe4913004), /* ldr  r3, [r1], #4  ; load and post-inc   */
		htole32(0xe4803004), /* str  r3, [r0], #4  ; store and post-inc  */
		htole32(0xeafffffa), /* b    write_loop                          */
		htole32(addr),       /* write_addr */
		htole32((uint32_t)count)       /* write_count */
		/* write_data (buffer) follows, i.e. values taken from here */
	};

	/* copy values from source buffer */
	size_t i;
	for (i = 0; i < count; i++)
		arm_code[LCODE_ARM_WORDS + i] = htole32(*src++);
	/* scratch buffer setup: transfers ARM code and data */
	aw_fel_write(dev, arm_code, dev->soc_info->scratch_addr,
	             (LCODE_ARM_WORDS + count) * sizeof(uint32_t));
	/* execute, and we're done */
	aw_fel_execute(dev, dev->soc_info->scratch_addr);
}

/*
 * aw_fel_writel_n() wrapper that can handle large transfers. If necessary,
 * those will be done in separate 'chunks' of no more than LCODE_MAX_WORDS.
 */
void fel_writel_n(feldev_handle *dev, uint32_t addr, uint32_t *src, size_t count)
{
	while (count > 0) {
		size_t n = count > LCODE_MAX_WORDS ? LCODE_MAX_WORDS : count;
		aw_fel_writel_n(dev, addr, src, n);
		addr += (uint32_t)n * sizeof(uint32_t);
		src += n;
		count -= n;
	}
}

/*
 * Memory access to the SID (root) keys proved to be unreliable for certain
 * SoCs. This function uses an alternative, register-based approach to retrieve
 * the values.
 */
static void fel_get_sid_registers(feldev_handle *dev, uint32_t *result)
{
	uint32_t arm_code[] = {
		htole32(0xe59f0040), /*    0:  ldr   r0, [pc, #64]           */
		htole32(0xe3a01000), /*    4:  mov   r1, #0                  */
		htole32(0xe28f303c), /*    8:  add   r3, pc, #60             */
		/* <sid_read_loop>: */
		htole32(0xe1a02801), /*    c:  lsl   r2, r1, #16             */
		htole32(0xe3822b2b), /*   10:  orr   r2, r2, #44032          */
		htole32(0xe3822002), /*   14:  orr   r2, r2, #2              */
		htole32(0xe5802040), /*   18:  str   r2, [r0, #64]           */
		/* <sid_read_wait>: */
		htole32(0xe5902040), /*   1c:  ldr   r2, [r0, #64]           */
		htole32(0xe3120002), /*   20:  tst   r2, #2                  */
		htole32(0x1afffffc), /*   24:  bne   1c <sid_read_wait>      */
		htole32(0xe5902060), /*   28:  ldr   r2, [r0, #96]           */
		htole32(0xe7832001), /*   2c:  str   r2, [r3, r1]            */
		htole32(0xe2811004), /*   30:  add   r1, r1, #4              */
		htole32(0xe3510010), /*   34:  cmp   r1, #16                 */
		htole32(0x3afffff3), /*   38:  bcc   c <sid_read_loop>       */
		htole32(0xe3a02000), /*   3c:  mov   r2, #0                  */
		htole32(0xe5802040), /*   40:  str   r2, [r0, #64]           */
		htole32(0xe12fff1e), /*   44:  bx    lr                      */
		htole32(dev->soc_info->sid_base), /* SID base addr */
		/* retrieved SID values go here */
	};
	/* write and execute code */
	aw_fel_write(dev, arm_code, dev->soc_info->scratch_addr, sizeof(arm_code));
	aw_fel_execute(dev, dev->soc_info->scratch_addr);
	/* read back the result */
	aw_fel_read(dev, dev->soc_info->scratch_addr + sizeof(arm_code),
		    result, 4 * sizeof(uint32_t));
	unsigned i;
	for (i = 0; i < 4; i++)
		result[i] = le32toh(result[i]);
}

/* Read the SID "root" key (128 bits). You need to pass the device handle,
 * a pointer to a result array capable of receiving at least four 32-bit words,
 * and a flag specifying if the register-access workaround should be enforced.
 * Return value indicates whether the result is expected to be usable:
 * The function will return `false` (and zero the result) if it cannot access
 * the SID registers.
 */
bool fel_get_sid_root_key(feldev_handle *dev, uint32_t *result,
			  bool force_workaround)
{
	if (!dev->soc_info->sid_base) {
		/* SID unavailable */
		unsigned i;
		for (i = 0; i < 4; i++) result[i] = 0;
		return false;
	}

	if (dev->soc_info->sid_fix || force_workaround)
		/* Work around SID issues by using ARM thunk code */
		fel_get_sid_registers(dev, result);
	else
		/* Read SID directly from memory */
		fel_readl_n(dev, dev->soc_info->sid_base
			       + dev->soc_info->sid_offset, result, 4);
	return true;
}

/* general functions, "FEL device" management */

static int feldev_get_endpoint(feldev_handle *dev)
{
	struct libusb_device *usb = libusb_get_device(dev->usb->handle);
	struct libusb_config_descriptor *config;
	int if_idx, set_idx, ep_idx, ret;
	const struct libusb_interface *iface;
	const struct libusb_interface_descriptor *setting;
	const struct libusb_endpoint_descriptor *ep;

	ret = libusb_get_active_config_descriptor(usb, &config);
	if (ret)
		return ret;

	for (if_idx = 0; if_idx < config->bNumInterfaces; if_idx++) {
		iface = config->interface + if_idx;

		for (set_idx = 0; set_idx < iface->num_altsetting; set_idx++) {
			setting = iface->altsetting + set_idx;

			for (ep_idx = 0; ep_idx < setting->bNumEndpoints; ep_idx++) {
				ep = setting->endpoint + ep_idx;

				/* Test for bulk transfer endpoint */
				if ((ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK)
				    != LIBUSB_TRANSFER_TYPE_BULK)
					continue;

				if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK)
				    == LIBUSB_ENDPOINT_IN)
					dev->usb->endpoint_in = ep->bEndpointAddress;
				else
					dev->usb->endpoint_out = ep->bEndpointAddress;
			}
		}
	}

	libusb_free_config_descriptor(config);
	return LIBUSB_SUCCESS;
}

/* claim USB interface associated with the libusb handle for a FEL device */
void feldev_claim(feldev_handle *dev)
{
	int rc = libusb_claim_interface(dev->usb->handle, 0);
#if defined(__linux__)
	if (rc != LIBUSB_SUCCESS) {
		libusb_detach_kernel_driver(dev->usb->handle, 0);
		dev->usb->iface_detached = true;
		rc = libusb_claim_interface(dev->usb->handle, 0);
	}
#endif
	if (rc)
		usb_error(rc, "libusb_claim_interface()", 1);

	rc = feldev_get_endpoint(dev);
	if (rc)
		usb_error(rc, "FAILED to get FEL mode endpoint addresses!", 1);
}

/* release USB interface associated with the libusb handle for a FEL device */
void feldev_release(feldev_handle *dev)
{
	libusb_release_interface(dev->usb->handle, 0);
#if defined(__linux__)
	if (dev->usb->iface_detached)
		libusb_attach_kernel_driver(dev->usb->handle, 0);
#endif
}

/* open handle to desired FEL device */
feldev_handle *feldev_open(int busnum, int devnum,
			   uint16_t vendor_id, uint16_t product_id)
{
	fel_lib_errored = false;
	if (!fel_lib_initialized) /* if not already done: auto-initialize */
		feldev_init();
	feldev_handle *result = calloc(1, sizeof(feldev_handle));
	if (!result) {
		fprintf(stderr, "FAILED to allocate feldev_handle memory.\n");
		exit(1);
	}
	result->usb = calloc(1, sizeof(felusb_handle));
	if (!result->usb) {
		fprintf(stderr, "FAILED to allocate felusb_handle memory.\n");
		free(result);
		exit(1);
	}

	if (busnum < 0 || devnum < 0) {
		/* With the default values (busnum -1, devnum -1) we don't care
		 * for a specific USB device; so let libusb open the first
		 * device that matches VID/PID.
		 */
		result->usb->handle = libusb_open_device_with_vid_pid(NULL, vendor_id, product_id);
		if (!result->usb->handle) {
			switch (errno) {
			case EACCES:
				fprintf(stderr, "ERROR: You don't have permission to access Allwinner USB FEL device\n");
				break;
			default:
				fprintf(stderr, "ERROR: Allwinner USB FEL device not found!\n");
				break;
			}
			exit(1);
		}
	} else {
		/* look for specific bus and device number */
		bool found = false;
		ssize_t rc, i;
		libusb_device **list;

		rc = libusb_get_device_list(NULL, &list);
		if (rc < 0)
			usb_error((int)rc, "libusb_get_device_list()", 1);
		for (i = 0; i < rc; i++) {
			if (libusb_get_bus_number(list[i]) == busnum
			    && libusb_get_device_address(list[i]) == devnum) {
				found = true; /* bus:devnum matched */
				struct libusb_device_descriptor desc;
				libusb_get_device_descriptor(list[i], &desc);
				if (desc.idVendor != vendor_id
				    || desc.idProduct != product_id) {
					fprintf(stderr, "ERROR: Bus %03d Device %03d not a FEL device "
						"(expected %04x:%04x, got %04x:%04x)\n", busnum, devnum,
						vendor_id, product_id, desc.idVendor, desc.idProduct);
					exit(1);
				}
				/* open handle to this specific device (incrementing its refcount) */
				rc = libusb_open(list[i], &result->usb->handle);
				if (rc != 0)
					usb_error((int)rc, "libusb_open()", 1);
				break;
			}
		}
		libusb_free_device_list(list, true);

		if (!found) {
			fprintf(stderr, "ERROR: Bus %03d Device %03d not found in libusb device list\n",
				busnum, devnum);
			exit(1);
		}
	}

	feldev_claim(result); /* claim interface, detect USB endpoints */

	/* retrieve BROM version and SoC information */
	aw_fel_get_version(result, &result->soc_version);
	get_soc_name_from_id(result->soc_name, result->soc_version.soc_id);
	if (fel_lib_errored) result->soc_info = NULL;
	else result->soc_info = get_soc_info_from_version(&result->soc_version);

	return result;
}

/* close FEL device (optional, dev may be NULL) */
void feldev_close(feldev_handle *dev)
{
	if (dev) {
		if (dev->usb->handle) {
			feldev_release(dev);
			libusb_close(dev->usb->handle);
		}
		free(dev->usb); /* release memory allocated for felusb_handle */
	}
}

void feldev_init(void)
{
	int rc = libusb_init(NULL);
	if (rc != 0)
		usb_error(rc, "libusb_init()", 1);
	fel_lib_initialized = true;
}

void feldev_done(feldev_handle *dev)
{
	feldev_close(dev);
	free(dev);
	if (fel_lib_initialized) libusb_exit(NULL);
}

/*
 * Enumerate (all) FEL devices. Allocates a list (array of feldev_list_entry)
 * and optionally returns the number of elements via "count". You may
 * alternatively detect the end of the list by checking the entry's soc_version
 * for a zero ID.
 * It's your responsibility to call free() on the result later.
 */
feldev_list_entry *list_fel_devices(size_t *count)
{
	feldev_list_entry *list, *entry;
	ssize_t rc, i;
	libusb_context *ctx;
	libusb_device **usb;
	struct libusb_device_descriptor desc;
	feldev_handle *dev;
	size_t devices = 0;

	libusb_init(&ctx);
	rc = libusb_get_device_list(ctx, &usb);
	if (rc < 0)
		usb_error((int)rc, "libusb_get_device_list()", 1);

	/*
	 * Size our array to hold entries for every USB device,
	 * plus an empty one at the end (for list termination).
	 */
	list = calloc(rc + 1, sizeof(feldev_list_entry));
	if (!list) {
		fprintf(stderr, "list_fel_devices() FAILED to allocate list memory.\n");
		exit(1);
	}

	for (i = 0; i < rc; i++) {
		libusb_get_device_descriptor(usb[i], &desc);
		if (desc.idVendor != AW_USB_VENDOR_ID
		    || desc.idProduct != AW_USB_PRODUCT_ID)
		continue; /* not an Allwinner FEL device */

		entry = list + devices; /* pointer to current feldev_list_entry */
		devices += 1;

		entry->busnum = libusb_get_bus_number(usb[i]);
		entry->devnum = libusb_get_device_address(usb[i]);
		dev = feldev_open(entry->busnum, entry->devnum,
				  AW_USB_VENDOR_ID, AW_USB_PRODUCT_ID);

		/* copy relevant fields */
		entry->soc_version = dev->soc_version;
		entry->soc_info = dev->soc_info;
		strncpy(entry->soc_name, dev->soc_name, sizeof(soc_name_t));

		/* retrieve SID bits */
		fel_get_sid_root_key(dev, entry->SID, false);

		feldev_close(dev);
		free(dev);
	}
	libusb_free_device_list(usb, true);
	libusb_exit(ctx);

	if (count) *count = devices;
	return list;
}
