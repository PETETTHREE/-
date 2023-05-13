/*
 * main.c
 *
 *  Created on: 2023
 *      Author: LZ
 */

#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/ioctl.h>
#include <unistd.h>
#include <string.h>

#define soc_cv_av

#include "hwlib.h"
#include "socal/socal.h"
#include "socal/hps.h"
#include "hps_0.h"//芯片公司给的，定义了寄存器地址等信息

#define HW_REGS_BASE (ALT_STM_OFST)//硬件 寄存器 基地址  ALT_STM_OFST是地址，地址可能为 0xfc000000
#define HW_REGS_SPAN (0x04000000)//硬件 寄存器 可以访问的内存区域的大小   64MB
#define HW_REGS_MASK (HW_REGS_SPAN - 1)//在这种情况下，HW_REGS_MASK的值是0x03FFFFFF，即64 MB - 1字节  以确保内存地址只访问到硬件资源内存区域的范围内

//这个宏定义了一个字符常量 'x'，作为一个特殊的标记，用于标识这些宏的起始位置。
#define AMM_WR_MAGIC 'x'//用来标识一个设备或驱动程序的标识符
//_IOR代表ioctl命令，ioctl（input/output control）是一种用于设备驱动程序和用户空间应用程序之间进行通信的系统调用。
//_IOR 宏定义的第一个参数是命令类型（通常是设备的主设备号），第二个参数是命令编号，第三个参数是 ioctl 命令数据类型的大小。
#define AMM_WR_CMD_DMA_BASE _IOR(AMM_WR_MAGIC, 0x1a, int)//它指定了一个读取命令，命令类型为 AMM_WR_MAGIC，命令编号为 0x1a，int 是命令参数类型。
//意思是把命令交给AMM_WR_MAGIC设备驱动程序处理，0x1a是命令的含义，int是发送的数据或者接受的数据
#define AMM_WR_CMD_PHY_BASE _IOR(AMM_WR_MAGIC,0x1b,int)//它指定了一个读取命令，命令类型为 AMM_WR_MAGIC，命令编号为 0x1b，int 是命令参数类型。

#define IMG_WIDTH  400
#define IMG_HEIGHT 320
#define BURST_LEN 48

#define IMG_BUF_SIZE IMG_WIDTH * IMG_HEIGHT * 3

static unsigned char *transfer_data_base = NULL;
static unsigned char *p_transfer_data_base = NULL;
static volatile unsigned int *dvp_ddr3_cfg_base = NULL;//寄存器
static volatile unsigned int *ddr3_vga_cfg_base = NULL;//寄存器

//#define ERRINDEXLOG

int fpga_init(void)
{
	void *per_virtual_base;
	int transfer_fd;
	int mem_fd;

	system("insmod amm_wr_drv.ko");

	transfer_fd = open("/dev/amm_wr", (O_RDWR|O_SYNC));
	if(transfer_fd == -1)
	{
		printf("open amm_wr is failed\n");
		return(0);
	}
	mem_fd = open("/dev/mem", (O_RDWR|O_SYNC));
	if(mem_fd == -1)
	{
		printf("open mmu is failed\n");
		return(0);
	}

	//ioctl是Linux应用层与驱动层指令交互的函数，如果有需要去修改驱动的可以查查这个了解一下
	//没有修改驱动想法的直接按照这里边的用法直接用即可
	ioctl(transfer_fd, AMM_WR_CMD_DMA_BASE, &p_transfer_data_base);//作用是使用amm_wr_drv.ko这个驱动申请了，一块连续的内存，并把连续内存的指针地址给到transfer_data_base这个变量。CMA连续内存区域
	printf("p_transfer_data_base = %x\n", p_transfer_data_base);

	//地址映射和偏移地址的概念  ，mmap这个函数可百度搜索资料很多，为什么做映射，映射完后为什么要做地址偏移
	//mmap内存映射
											   NULL：表示让内核自动分配一个虚拟地址，也就是不指定虚拟地址；
                                                     IMG_BUF_SIZE * 3：要映射的字节数，即图像缓存的大小（宽度 x 高度 x 3）；
                                                                      (PROT_READ | PROT_WRITE)：内存保护模式，表示可以读写；
												                                                 MAP_SHARED：映射的内存区域可以被多个进程共享；
                                                                                                             mem_fd：对应的内存设备文件的文件描述符；
                                                                                                                     p_transfer_data_base：要映射的物理地址。映射后，该地址对应的内存就会被映射到进程的虚拟地址空间中
    transfer_data_base：映射成功后返回的指向映射区域的指针。
	transfer_data_base = (unsigned char *)mmap(NULL, IMG_BUF_SIZE * 3, (PROT_READ | PROT_WRITE), MAP_SHARED, mem_fd, p_transfer_data_base);

	per_virtual_base = (unsigned int*)mmap(NULL, HW_REGS_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED, mem_fd, HW_REGS_BASE);
	//地址偏移
	//这两个是寄存器
	dvp_ddr3_cfg_base = (unsigned int *)(per_virtual_base + ((unsigned long)(ALT_LWFPGASLVS_OFST + DVP_DDR3_0_BASE) & (unsigned long)(HW_REGS_MASK)));
	ddr3_vga_cfg_base = (unsigned int *)(per_virtual_base + ((unsigned long)(ALT_LWFPGASLVS_OFST + DDR3_VGA_0_BASE) & (unsigned long)(HW_REGS_MASK)));
	
	//初始化
	//使用偏移地址进行简单配置
	//config dvp_ddr3
	//寄存器的用法看云盘 ip使用说明，对照文档可以看到0是首地址，1是图像，2是工作状态
	*dvp_ddr3_cfg_base = p_transfer_data_base;   //地址等于一个指针    ，意思是把dvp的信号存在ddr3的地址中，即从dvp_ddr3获取到的一帧图像放在p_transfer_data_base中
	*(dvp_ddr3_cfg_base + 1) = IMG_BUF_SIZE;	//加一等于BUF_SIZE即图片大小，数据大小
	*(dvp_ddr3_cfg_base + 2) = 0x00000000;		//加二等于0，看文档发现0是失能，停掉初始化不需要工作
	//初始化
	//config ddr3_vga
	*ddr3_vga_cfg_base = p_transfer_data_base + IMG_BUF_SIZE;
	*(ddr3_vga_cfg_base + 1) = IMG_BUF_SIZE;
	*(ddr3_vga_cfg_base + 2) = 0x00000000;

	usleep(1000000);

	return 1;
}

int main ()
{
	//初始化
	unsigned char buf[IMG_BUF_SIZE];

	if(fpga_init() != 1)
	{
		printf("fpga init failed!\n");
	}

	//dvp_ddr3开始工作
	*(dvp_ddr3_cfg_base + 2) = 0x00000001;//看说明文档写1时，是使能
	//ddr3_vga开始工作
	*(ddr3_vga_cfg_base + 2) = 0x00000001;

	while(1)
	{
		//获取一帧图像
		*(dvp_ddr3_cfg_base + 3) = 0x00000001;
		while(1)
		{
			if(*(dvp_ddr3_cfg_base + 3) == 0x00000000)
				break;
		}
		memcpy(buf, transfer_data_base, IMG_BUF_SIZE);//从写到寄存器中的基地址（transfer_data_base）中拷贝一张图像大小（IMG_BUF_SIZE）到临时区域（buf）
													  //buf其实没有用，是用来官方用来调试的



		//因为ddr3_vga这个ip使用的是双缓冲，所以要进行判断哪一个buffer是可写的

		//输出一帧图像
		if(((*(ddr3_vga_cfg_base + 3)) & 0x00000003) == 0x00000002)
		{
			printf("write buffer0\n");			//如果buffer0可写，就把图像写入buffer0

			//实际我们用的是这个，把（transfer_data_base）地址，拷贝到（transfer_data_base + IMG_BUF_SIZE偏移）中
			//transfer_data_base + IMG_BUF_SIZE看前面初始化，ddr3_vga基地址是这个
			//即把要显示的一帧图像拷贝到ddr3_vga这个IP中了
			memcpy(transfer_data_base + IMG_BUF_SIZE, transfer_data_base, IMG_BUF_SIZE);
			//这个是官方用来调试的
			memcpy(buf, transfer_data_base + IMG_BUF_SIZE, IMG_BUF_SIZE);
			*(ddr3_vga_cfg_base + 3) = *(ddr3_vga_cfg_base + 3) | 0x00000001;
		}
		else if(((*(ddr3_vga_cfg_base + 3)) & 0x00000003) == 0x00000001)
		{
			printf("write buffer1\n");			//如果buffer1可写，就把图像写入buffer1

			//	IMG_BUF_SIZE * 2 偏移乘以2，偏移了两张图片的位置因为用了双缓冲
			memcpy(transfer_data_base + IMG_BUF_SIZE * 2, transfer_data_base, IMG_BUF_SIZE);
			memcpy(buf, transfer_data_base + IMG_BUF_SIZE * 2, IMG_BUF_SIZE);
			*(ddr3_vga_cfg_base + 3) = *(ddr3_vga_cfg_base + 3) | 0x00000002;
		}

		usleep(1000);
	}
	return 0;
}




