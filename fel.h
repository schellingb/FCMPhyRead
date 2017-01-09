//----------------------------------------------------------------//
// FCMPhyRead - based on https://github.com/madmonkey1907/hakchi  //
// License:  GNU GENERAL PUBLIC LICENSE - Version 3, 29 June 2007 //
//----------------------------------------------------------------//

#ifndef FEL_H
#define FEL_H

#define _HAS_EXCEPTIONS 0
#include <vector>
#include <string>
#include <stdint.h>

struct FeldevHandle;

struct uboot_t
{
	std::vector<unsigned char> data;
	std::string cmd;
	uint32_t cmdOffset;

	uboot_t();
	void init(const std::vector<unsigned char>&ba);
	void doCmd(const char*str);
};

#define fes1_base_m 0x2000u
#define dram_base 0x40000000u
#define uboot_base_m 0x47000000u
#define uboot_base_f 0x100000u
#define flash_mem_base 0x43800000u
#define flash_mem_size 0x20u
#define sector_size 0x20000u
#define kernel_base_f (sector_size*0x30)
#define kernel_base_m flash_mem_base
#define kernel_max_size (uboot_base_m-flash_mem_base)
#define kernel_max_flash_size (sector_size*0x20)

struct Fel
{
	Fel();
	~Fel();
	bool init();
	bool initDram(bool force=false);
	void release();
	void setFes1bin(const std::vector<unsigned char>&data);
	void setUboot(const std::vector<unsigned char>&data);
	bool haveUboot()const;
	bool runCode(uint32_t addr,uint32_t s);
	bool runUbootCmd(const char*str,bool noreturn=false,bool forceUbootUpload=false);
	size_t readMemory(uint32_t addr,size_t size,void*buf);
	size_t writeMemory(uint32_t addr,size_t size,void*buf);
	size_t readFlash(uint32_t addr,size_t size,void*buf);
	size_t writeFlash(uint32_t addr,size_t size,void*buf);
	FeldevHandle*dev;
	std::vector<unsigned char> fes1bin;
	uboot_t uboot;
	bool dramInitOk, ubootUploaded;
};

#endif // FEL_H
