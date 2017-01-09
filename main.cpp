//----------------------------------------------------------------//
// FCMPhyRead - based on https://github.com/madmonkey1907/hakchi  //
// License:  GNU GENERAL PUBLIC LICENSE - Version 3, 29 June 2007 //
//----------------------------------------------------------------//

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <string.h>
#include "fel.h"

static Fel* g_Fel;

#define BOOT_MAGIC "ANDROID!"
enum { BOOT_MAGIC_SIZE = 8, BOOT_NAME_SIZE = 16, BOOT_ARGS_SIZE = 512, BOOT_EXTRA_ARGS_SIZE = 1024 };
#pragma pack(push, 1)
struct boot_img_hdr
{
    uint8_t magic[BOOT_MAGIC_SIZE];
    uint32_t kernel_size;  /* size in bytes */
    uint32_t kernel_addr;  /* physical load addr */
    uint32_t ramdisk_size; /* size in bytes */
    uint32_t ramdisk_addr; /* physical load addr */
    uint32_t second_size;  /* size in bytes */
    uint32_t second_addr;  /* physical load addr */
    uint32_t tags_addr;    /* physical addr for kernel tags */
    uint32_t page_size;    /* flash page size we assume */
    uint32_t dt_size;      /* device tree in bytes */
    uint32_t os_version;   /* operating system version and security patch level */
    uint8_t name[BOOT_NAME_SIZE]; /* asciiz product name */
    uint8_t cmdline[BOOT_ARGS_SIZE];
    uint32_t id[8]; /* timestamp / checksum / sha1 / etc */
    uint8_t extra_cmdline[BOOT_EXTRA_ARGS_SIZE];
} /* __attribute__((packed)) */ ;
#pragma pack(pop)

static size_t kernelSize(const std::vector<unsigned char>& data)
{
    const boot_img_hdr*h=(const boot_img_hdr*)&data[0];
    if(!memcmp(h->magic,BOOT_MAGIC,BOOT_MAGIC_SIZE)==0) return 0;
    size_t pages=1;
    pages+=(h->kernel_size+h->page_size-1)/h->page_size;
    pages+=(h->ramdisk_size+h->page_size-1)/h->page_size;
    pages+=(h->second_size+h->page_size-1)/h->page_size;
    pages+=(h->dt_size+h->page_size-1)/h->page_size;
    size_t ks=pages*h->page_size;
    return (ks<=kernel_max_size?ks:0);
}

static std::vector<unsigned char> LoadFileContents(const char* fn)
{
	std::vector<unsigned char> r;
	FILE* f = fopen(fn, "rb");
	if (!f) return r;
	fseek(f, 0, SEEK_END);
	size_t sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	r.resize(sz);
	fread(&r[0], 1, sz, f);
	fclose(f);
	return r;
}

static bool init()
{
	if (g_Fel) return (g_Fel->init() && g_Fel->haveUboot());
	g_Fel = new Fel();

	std::vector<unsigned char> data=LoadFileContents("fes1.bin");
	if(data.size()) g_Fel->setFes1bin(data);
	else { printf("fes1.bin not found\n"); return false; }

	data=LoadFileContents("uboot.bin");
	if(data.size()) g_Fel->setUboot(data);
	else { printf("uboot.bin not found\n"); return false; }

	return (g_Fel->init() && g_Fel->haveUboot());
}

static void do_dumpKernel()
{
	if (!init()) return;

	std::vector<unsigned char> buf;
	buf.resize(sector_size*0x20);
	if (!g_Fel->readFlash(kernel_base_f,buf.size(),buf.data())==(size_t)buf.size()) { printf("kernel: read error\n"); return; }

	size_t size = kernelSize(buf);
	if((size==0)||(size>kernel_max_size)) { printf("kernel: invalid size in header\n"); return; }

	if(size > (size_t)buf.size())
	{
		size_t oldSize = buf.size();
		buf.resize(size);
		if (g_Fel->readFlash(kernel_base_f+(uint32_t)oldSize,size-oldSize,buf.data()+oldSize)!=(size-oldSize)) { printf("kernel: read error\n"); return; }
	}

	printf("kernel: save to KERNEL.DUMP (%d bytes == %d MB)\n", (int)size, (int)(size/1024/1024));
	FILE* f = fopen("KERNEL.DUMP", "wb");
	fwrite(&buf[0], 1, size, f);
	fclose(f);
}

static void do_dumpNand()
{
	if (!init()) return;

	std::vector<unsigned char> buf;
	buf.resize(sector_size*0x20);
	FILE* f = fopen("SECTOR0TO1000.DUMP", "wb");
	for (int i = 0x0; i < 0x1000; i+= 0x20)
	{
		size_t read = g_Fel->readFlash(sector_size*i ,buf.size(),buf.data());
		printf("NAND: writing sector 0x%x ~ 0x%x to SECTOR0TO1000.DUMP\n", i, i+0x1F);
		fwrite(buf.data(), 1, read, f);
		if (read != buf.size()) break;
	}
	fclose(f);
}

int main()
{
	do_dumpKernel();
	//do_dumpNand();
	return 0;
}
