#include "fpga.h"
#include "bus.h"
#include "cpu.h"

#define DCLK(x) GPIO_WriteBit(GPIO1, GPIO_Pin_6, x)
#define DATA(x) GPIO_WriteBit(GPIO1, GPIO_Pin_7, x)
#define nCONFIG(x) GPIO_WriteBit(GPIO1, GPIO_Pin_9, x)

#define CONFIG_DONE() GPIO_ReadBit(GPIO1, GPIO_Pin_5)
#define nSTATUS() GPIO_ReadBit(GPIO1, GPIO_Pin_8)

//---------------------------------------------------------------------------------------
byte timer_flag_1Hz = 0;
byte timer_flag_100Hz = 0;

dword fpgaConfigVersionPrev = 0;
dword romConfigPrev = -1;

static const char *fpgaConfigName = "speccy2010.rbf";
//---------------------------------------------------------------------------------------
void FPGA_Config()
{
	if ((disk_status(0) & STA_NOINIT) != 0) {
		__TRACE("Cannot init SD card...\n");
		return;
	}

	FILINFO fpgaConfigInfo;
	char lfn[1];
	fpgaConfigInfo.lfname = lfn;
	fpgaConfigInfo.lfsize = 0;

	if (f_stat(fpgaConfigName, &fpgaConfigInfo) != FR_OK) {
		__TRACE("FPGA_Config: Cannot open '%s'!\n", fpgaConfigName);
		return;
	}

	dword fpgaConfigVersion = (fpgaConfigInfo.fdate << 16) | fpgaConfigInfo.ftime;

	__TRACE("FPGA_Config: chkver [current: %08x > new: %08x]\n", fpgaConfigVersionPrev, fpgaConfigVersion);

	if (fpgaConfigVersionPrev == fpgaConfigVersion) {
		__TRACE("FPGA_Config: The version of '%s' is match...\n", fpgaConfigName);
		return;
	}

	FIL fpgaConfig;
	if (f_open(&fpgaConfig, fpgaConfigName, FA_READ) != FR_OK) {
		__TRACE("FPGA_Config: Cannot open '%s'!\n", fpgaConfigName);
		return;
	}
	else {
		__TRACE("FPGA_Config: '%s' is opened...\n", fpgaConfigName);
	}
	//--------------------------------------------------------------------------

	__TRACE("FPGA_Config: Flashing started...\n");

	GPIO_InitTypeDef GPIO_InitStructure;

	//BOOT0 pin
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
	GPIO_Init(GPIO0, &GPIO_InitStructure);

	//Reset FPGA pin
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13;
	GPIO_Init(GPIO1, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_8;
	GPIO_Init(GPIO1, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_9;
	GPIO_Init(GPIO1, &GPIO_InitStructure);

	nCONFIG(Bit_SET);
	DCLK(Bit_SET);
	DATA(Bit_SET);

	nCONFIG(Bit_RESET);
	DelayMs(2);
	nCONFIG(Bit_SET);

	int i = 10;
	while (i-- > 0 && nSTATUS() != Bit_RESET)
		DelayMs(10);

	if (nSTATUS() == Bit_RESET) {
		__TRACE("FPGA_Config: Status OK...\n");

		for (dword pos = 0; pos < fpgaConfig.fsize; pos++) {
			byte data8;

			UINT res;
			if (f_read(&fpgaConfig, &data8, 1, &res) != FR_OK)
				break;
			if (res == 0)
				break;

			for (byte j = 0; j < 8; j++) {
				DCLK(Bit_RESET);

				DATA((data8 & 0x01) == 0 ? Bit_RESET : Bit_SET);
				data8 >>= 1;

				DCLK(Bit_SET);
			}

			if ((pos & 0xfff) == 0) {
				WDT_Kick();
				__TRACE(".");
			}
		}
	}

	f_close(&fpgaConfig);
	__TRACE("\nFPGA_Config: Flashing done...\n");

	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_9;
	GPIO_Init(GPIO1, &GPIO_InitStructure);

	i = 10;
	while (i-- > 0 && !CONFIG_DONE())
		DelayMs(10);

	if (CONFIG_DONE()) {
		__TRACE("FPGA_Config: Finished...\n");
		fpgaConfigVersionPrev = fpgaConfigVersion;
	}
	else {
		__TRACE("FPGA_Config: Failed!\n");
		fpgaConfigVersionPrev = 0;
	}

	GPIO_WriteBit(GPIO1, GPIO_Pin_13, Bit_RESET); // FPGA RESET LOW
	DelayMs(100);
	GPIO_WriteBit(GPIO1, GPIO_Pin_13, Bit_SET); // FPGA RESET HIGH
	DelayMs(10);

	romConfigPrev = -1;
	SystemBus_TestConfiguration();
	__TRACE("Speccy2010 FPGA configuration found...\n");

	WDT_Kick();

	timer_flag_1Hz = 0;
	timer_flag_100Hz = 0;
}
//---------------------------------------------------------------------------------------
