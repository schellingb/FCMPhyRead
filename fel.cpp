//----------------------------------------------------------------//
// FCMPhyRead - based on https://github.com/madmonkey1907/hakchi  //
// License:  GNU GENERAL PUBLIC LICENSE - Version 3, 29 June 2007 //
//----------------------------------------------------------------//

#define _CRT_SECURE_NO_WARNINGS
#include "fel.h"
#include "fel_lib.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if (defined(_WIN16) || defined(_WIN32) || defined(_WIN64)) && !defined(__WINDOWS__) || defined(WIN32)
#define sleep(s) Sleep((s)*1000)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

struct FeldevHandle:public feldev_handle{};
static const char*fastboot="fastboot_test";

uboot_t::uboot_t()
{
	cmdOffset=0;
}

void uboot_t::init(const std::vector<unsigned char>&ba)
{
	data=ba;
	cmdOffset=0;
	const char pattern[] = "bootcmd=";
	for(unsigned int i=0;i<(data.size()-sizeof(pattern)-1);i++)
	{
		if(memcmp(data.data()+i,pattern,sizeof(pattern)-1)==0)
		{
			cmdOffset=i+sizeof(pattern)-1;
			cmd = (char*)&data[0]+cmdOffset;
			break;
		}
	}
	if(cmdOffset==0)
	{
		*this=uboot_t();
	}
}

void uboot_t::doCmd(const char*str)
{
	size_t size=strlen(str);
	assert(size<=(size_t)cmd.size());
	memset(data.data()+cmdOffset,0,cmd.size());
	memcpy(data.data()+cmdOffset,str,size);
}

Fel::Fel()
{
	dev=0;
	dramInitOk=ubootUploaded=false;
	feldev_init();
}

Fel::~Fel()
{
	feldev_done(dev);
}

bool Fel::init()
{
	if(dev==0)
	{
#if 0
		size_t size=0;
		feldev_list_entry* devs = list_fel_devices(&size);
		free(devs);
		if(size==0)
		{
			return false;
		}
		if(size>1)
		{
			return false;
		}
#endif
		printf("opening fel device...");
		dev=static_cast<FeldevHandle*>(feldev_open(-1,-1,AW_USB_VENDOR_ID,AW_USB_PRODUCT_ID));
		while((dev!=0)&&(dev->soc_info==0||dev->soc_info->soc_id!=0x1667))
		{
			release();
			printf(" retry...");
			dev=static_cast<FeldevHandle*>(feldev_open(-1,-1,AW_USB_VENDOR_ID,AW_USB_PRODUCT_ID));
		}
		printf(" done\n");
	}
	return dev!=0;
}

bool Fel::initDram(bool force)
{
	if(dramInitOk&&!force)
		return true;

	uint8_t buf[0x80];
	if((size_t)fes1bin.size()<sizeof(buf))
		return false;
	if((force)||(readMemory(fes1_base_m+(uint32_t)fes1bin.size()-sizeof(buf),sizeof(buf),buf)==sizeof(buf)))
	{
		if((!force)&&(memcmp(buf,fes1bin.data()+fes1bin.size()-sizeof(buf),sizeof(buf))==0))
		{
			dramInitOk=true;
			return true;
		}
		printf("uploading fes1.bin...");
		if(writeMemory(fes1_base_m,fes1bin.size(),fes1bin.data())==(size_t)fes1bin.size())
		{
			printf(" done\n");
			dramInitOk=true;
			return runCode(fes1_base_m,2);
		}
		printf(" failed\n");
	}
	return false;
}

void Fel::release()
{
	feldev_close(dev);
	dev=0;
}

void Fel::setFes1bin(const std::vector<unsigned char>&data)
{
	fes1bin=data;
}

void Fel::setUboot(const std::vector<unsigned char>&data)
{
	uboot.init(data);
}

bool Fel::haveUboot()const
{
	return uboot.cmdOffset>0;
}

bool Fel::runCode(uint32_t addr,uint32_t s)
{
	if(init())
	{
		aw_fel_execute(dev,addr);
		release();
		if(s==0xffffffff)
		{
			dramInitOk=false;
			return true;
		}
		sleep(s);
		for(int i=0;i<8;i++)
			if(init())
				break;
		return init();
	}
	return false;
}

bool Fel::runUbootCmd(const char*str,bool noreturn,bool forceUbootUpload)
{
	if(init()&&haveUboot())
	{
		uboot.doCmd(str);
		uint8_t buf[0x20];
		if(!forceUbootUpload && (ubootUploaded || ((readMemory(uboot_base_m,sizeof(buf),buf) == sizeof(buf)) && !memcmp(buf,uboot.data.data(),sizeof(buf)))))
		{
			size_t size=(uboot.cmd.size()+1+3)/4;
			size*=4;
			printf("writing uboot command...");
			if(writeMemory(uboot_base_m+uboot.cmdOffset,size,uboot.data.data()+uboot.cmdOffset)!=size)
				{ printf(" failed\n"); return false; }
		}
		else
		{
			if (forceUbootUpload) initDram(true);
			printf("uploading uboot.bin...");
			if(writeMemory(uboot_base_m,uboot.data.size(),uboot.data.data())!=(size_t)uboot.data.size())
				{ printf(" failed\n"); return false; }
		}
		ubootUploaded = true;
		printf(" done\n");
		printf("execute: %s\n",str);
		return runCode(uboot_base_m,noreturn?0xffffffff:3);
	}
	return false;
}


size_t Fel::readMemory(uint32_t addr,size_t size,void*buf)
{
	static const size_t maxTransfer=0x30000;
	if(init())
	{
		if((addr>=dram_base)&&(!initDram()))
			return 0;
		size&=(~3);
		size_t transfer=size;
		while(transfer)
		{
			size_t b= (transfer < maxTransfer ? transfer : maxTransfer);
			aw_fel_read(dev,addr,buf,b);
			addr+=(uint32_t)b;
			buf=((uint8_t*)buf)+b;
			transfer-=b;
		}
		return size;
	}
	return 0;
}

size_t Fel::writeMemory(uint32_t addr,size_t size,void*buf)
{
	static const size_t maxTransfer=0x20000;
	if(init())
	{
		if((addr>=dram_base)&&(!initDram()))
			return 0;
		size&=(~3);
		size_t transfer=size;
		while(transfer)
		{
			size_t b= (transfer < maxTransfer ? transfer : maxTransfer);
			aw_fel_write(dev,buf,addr,b);
			addr+=(uint32_t)b;
			buf=((uint8_t*)buf)+b;
			transfer-=b;
		}
		return size;
	}
	return 0;
}

size_t Fel::readFlash(uint32_t addr,size_t size,void*buf)
{
	if((!init())||(!haveUboot()))
		return 0;
	if(((size+addr%sector_size+sector_size-1)/sector_size)>flash_mem_size)
	{
		size_t sectors=(size+addr%sector_size+sector_size-1)/sector_size-flash_mem_size;
		size_t read=readFlash(addr,sectors*sector_size-addr%sector_size,buf);
		addr+=(uint32_t)read;
		size-=read;
		buf=static_cast<uint8_t*>(buf)+read;
	}
	if(((size+addr%sector_size+sector_size-1)/sector_size)>flash_mem_size)
		return 0;

	char cmd[1024];
	sprintf(cmd,"sunxi_flash phy_read %x %x %x;%s",flash_mem_base,addr/sector_size,((uint32_t)size+addr%sector_size+sector_size-1)/sector_size,fastboot);
	if(runUbootCmd(cmd))
	{
		printf("reading %d MB ...", (int)(size / 1024 / 1024));
		size_t res = readMemory(flash_mem_base+addr%sector_size,size,buf);
		printf(res ? " done\n" : " failed\n");
		return res;
	}
	return 0;
}

size_t Fel::writeFlash(uint32_t addr,size_t size,void*buf)
{
	if((!init())||(!haveUboot()))
		return 0;
	if((addr%sector_size)!=0)
		return 0;
	if((size%sector_size)!=0)
		return 0;
	if((size/sector_size)>flash_mem_size)
	{
		size_t sectors=(size/sector_size)-flash_mem_size;
		size_t write=writeFlash(addr,sectors*sector_size,buf);
		if((write%sector_size)!=0)
			return 0;
		addr+=(uint32_t)write;
		size-=write;
		buf=static_cast<uint8_t*>(buf)+write;
	}
	if((size/sector_size)>flash_mem_size)
	{
		return 0;
	}
	if(writeMemory(flash_mem_base,size,buf)==size)
	{
		char cmd[1024];
		sprintf(cmd,"sunxi_flash phy_write %x %x %x;%s",flash_mem_base,addr/sector_size,(uint32_t)size/sector_size,fastboot);
		if(runUbootCmd(cmd))
			return size;
	}
	return 0;
}
