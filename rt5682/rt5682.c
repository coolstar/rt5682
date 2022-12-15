#define DESCRIPTOR_DEF
#include "rt5682.h"
#include "registers.h"

#define bool int
#define MHz 1000000

static ULONG Rt5682DebugLevel = 100;
static ULONG Rt5682DebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

NTSTATUS
DriverEntry(
	__in PDRIVER_OBJECT  DriverObject,
	__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	RtekPrint(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, Rt5682EvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
	);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}


static NTSTATUS rt5682_reg_write(PRTEK_CONTEXT pDevice, uint16_t reg, uint16_t data)
{
	uint16_t rawdata[2];
	rawdata[0] = RtlUshortByteSwap(reg);
	rawdata[1] = RtlUshortByteSwap(data);
	return SpbWriteDataSynchronously(&pDevice->I2CContext, rawdata, sizeof(rawdata));
}

static NTSTATUS rt5682_reg_read(PRTEK_CONTEXT pDevice, uint16_t reg, uint16_t* data)
{
	uint16_t reg_swap = RtlUshortByteSwap(reg);
	uint16_t data_swap = 0;
	NTSTATUS ret = SpbXferDataSynchronously(&pDevice->I2CContext, &reg_swap, sizeof(uint16_t), &data_swap, sizeof(uint16_t));
	*data = RtlUshortByteSwap(data_swap);
	return ret;
}

static NTSTATUS rt5682_reg_update(
	_In_ PRTEK_CONTEXT pDevice,
	uint16_t reg,
	uint16_t mask,
	uint16_t val
) {
	uint16_t tmp = 0, orig = 0;

	NTSTATUS status = rt5682_reg_read(pDevice, reg, &orig);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	tmp = orig & ~mask;
	tmp |= val & mask;

	if (tmp != orig) {
		status = rt5682_reg_write(pDevice, reg, tmp);
	}
	return status;
}

/*
static NTSTATUS rt5682_reg_burstWrite(PRTEK_CONTEXT pDevice, struct reg* regs, int regCount) {
	if (regCount > 4) {
		DbgPrint("Count > 4; Splitting!\n");
		NTSTATUS status = rt5682_reg_burstWrite(pDevice, regs, 4);
		if (!NT_SUCCESS(status)) {
			return status;
		}
		DbgPrint("Done first batch. Doing 2nd\n");
		status = rt5682_reg_burstWrite(pDevice, &regs[4], regCount - 4);
		return status;
	}

	NTSTATUS status = STATUS_NO_MEMORY;

	SPB_BURST_INFO* burstInfo = ExAllocatePoolWithTag(NonPagedPool, sizeof(SPB_BURST_INFO) * regCount, RT5682_POOL_TAG);
	if (!burstInfo) {
		status = STATUS_NO_MEMORY;
		goto exit;
	}
	RtlZeroMemory(burstInfo, sizeof(SPB_BURST_INFO) * regCount);

	for (int i = 0; i < regCount; i++) {
		struct reg* regToSet = &regs[i];

		UINT16* rawdata = ExAllocatePoolWithTag(NonPagedPool, sizeof(UINT16) * 2, RT5682_POOL_TAG);
		if (!rawdata) {
			status = STATUS_NO_MEMORY;
			goto exit;
		}

		rawdata[0] = RtlUshortByteSwap(regToSet->reg);
		rawdata[1] = RtlUshortByteSwap(regToSet->val);

		SPB_BURST_INFO* info = &burstInfo[i];
		info->Data = rawdata;
		info->Length = sizeof(UINT16) * 2;
	}
	status = SpbBurstWriteDataSynchronously(&pDevice->I2CContext, burstInfo, regCount);

exit:
	if (burstInfo) {
		for (int i = 0; i < regCount; i++) {
			if (burstInfo[i].Data)
				ExFreePoolWithTag(burstInfo[i].Data, RT5682_POOL_TAG);
		}
		ExFreePoolWithTag(burstInfo, RT5682_POOL_TAG);
	}
	return status;
}
*/

static NTSTATUS rt5682_reg_burstWrite(PRTEK_CONTEXT pDevice, struct reg* regs, int regCount) {
	NTSTATUS status = STATUS_NO_MEMORY;
	for (int i = 0; i < regCount; i++) {
		struct reg* regToSet = &regs[i];
		status = rt5682_reg_write(pDevice, regToSet->reg, regToSet->val);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}
	return status;
}

static Platform GetPlatform() {
	int cpuinfo[4];
	__cpuidex(cpuinfo, 0, 0);

	int temp = cpuinfo[2];
	cpuinfo[2] = cpuinfo[3];
	cpuinfo[3] = temp;

	char vendorName[13];
	RtlZeroMemory(vendorName, 13);
	memcpy(vendorName, &cpuinfo[1], 12);

	__cpuidex(cpuinfo, 1, 0);

	UINT16 family = (cpuinfo[0] >> 8) & 0xF;
	UINT8 model = (cpuinfo[0] >> 4) & 0xF;
	UINT8 stepping = cpuinfo[0] & 0xF;
	if (family == 0xF || family == 0x6) {
		model += (((cpuinfo[0] >> 16) & 0xF) << 4);
	}
	if (family == 0xF) {
		family += (cpuinfo[0] >> 20) & 0xFF;
	}

	if (strcmp(vendorName, "AuthenticAMD") == 0) {
		return PlatformRyzen; //family 23 for Picasso / Dali
	} else if (strcmp(vendorName, "GenuineIntel") == 0) {
		if (model == 122 || model == 92) //92 = Apollo Lake but keep for compatibility
			return PlatformGeminiLake;
		else
			return PlatformTigerLake; //should be 140
	}
	return PlatformNone;
}

NTSTATUS BOOTCODEC(
	_In_  PRTEK_CONTEXT  devContext
)
{
	NTSTATUS status = rt5682_reg_write(devContext, RT5682_RESET, 0);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	
	status = rt5682_reg_write(devContext, RT5682_I2C_MODE, 1);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	LARGE_INTEGER WaitInterval;
	WaitInterval.QuadPart = -10 * 1000 * 15;
	KeDelayExecutionThread(KernelMode, false, &WaitInterval);

	UINT16 val;
	status = rt5682_reg_read(devContext, RT5682_DEVICE_ID, &val);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	if (val != DEVICE_ID) {
		DbgPrint("Device with ID 0x%x is not ALC5682\n", val);
		return STATUS_NO_SUCH_DEVICE;
	}

	{ //Calibrate
		struct reg initAnlg[] = {
			{RT5682_I2C_CTRL, 0x000f},
			{RT5682_PWR_ANLG_1, 0xa2af}
		};
		status = rt5682_reg_burstWrite(devContext, initAnlg, sizeof(initAnlg) / sizeof(struct reg));
		if (!NT_SUCCESS(status)) {
			return status;
		}

		WaitInterval.QuadPart = -10 * 1000 * 15;
		KeDelayExecutionThread(KernelMode, false, &WaitInterval);

		struct reg preCalib[] = {
			{RT5682_PWR_ANLG_1, 0xf2af},
			{RT5682_MICBIAS_2, 0x0300 },
			{RT5682_GLB_CLK, 0x8000},
			{RT5682_PWR_DIG_1, 0x0100},
			{RT5682_HP_IMP_SENS_CTRL_19, 0x3800},
			{RT5682_CHOP_DAC, 0x3000},
			{RT5682_CALIB_ADC_CTRL, 0x7005},
			{RT5682_STO1_ADC_MIXER, 0x686c},
			{RT5682_CAL_REC, 0x0d0d},
			{RT5682_HP_CALIB_CTRL_2, 0x0321},
			{RT5682_HP_LOGIC_CTRL_2, 0x0004},
			{RT5682_HP_CALIB_CTRL_1, 0x7c00},
			{RT5682_HP_CALIB_CTRL_3, 0x06a1},
			{RT5682_A_DAC1_MUX, 0x0311},
			{RT5682_HP_CALIB_CTRL_1, 0x7c00},
			{RT5682_HP_CALIB_CTRL_1, 0xfc00}
		};
		status = rt5682_reg_burstWrite(devContext, preCalib, sizeof(preCalib) / sizeof(struct reg));
		if (!NT_SUCCESS(status)) {
			return status;
		}

		int count;
		for (count = 0; count < 60; count++) {
			UINT16 value;
			rt5682_reg_read(devContext, RT5682_HP_CALIB_STA_1, &value);
			if (!(value & 0x8000))
				break;

			WaitInterval.QuadPart = -10 * 1000 * 10;
			KeDelayExecutionThread(KernelMode, false, &WaitInterval);
		}

		if (count >= 60) {
			DbgPrint("HP Calibration Failure\n");
		}
	}

	struct reg restoreSettingsAndPatch[] = {
		{RT5682_PWR_ANLG_1, 0x002f},
		{RT5682_MICBIAS_2, 0x0080 },
		{RT5682_GLB_CLK, 0x0000},
		{RT5682_PWR_DIG_1, 0x0000},
		{RT5682_CHOP_DAC, 0x2000},
		{RT5682_CALIB_ADC_CTRL, 0x2005},
		{RT5682_STO1_ADC_MIXER, 0xc064},
		{RT5682_CAL_REC, 0x0c0c},

		//Patch
		{RT5682_HP_IMP_SENS_CTRL_19, 0x1000},
		{RT5682_DAC_ADC_DIG_VOL1, 0xa0a0}, //0xa020 for internal mic
		{RT5682_I2C_CTRL, 0x000f},
		{RT5682_PLL2_INTERNAL, 0x8266},
		{RT5682_SAR_IL_CMD_1, 0x22b7},
		{RT5682_SAR_IL_CMD_3, 0x0365},
		{RT5682_SAR_IL_CMD_6, 0x0110},
		{RT5682_CHARGE_PUMP_1, 0x0210},
		{RT5682_HP_LOGIC_CTRL_2, 0x0007},
		{RT5682_SAR_IL_CMD_2, 0xac01}, //0xac00 for internal mic
		{RT5682_CBJ_CTRL_7, 0x0104}
	};
	status = rt5682_reg_burstWrite(devContext, restoreSettingsAndPatch, sizeof(restoreSettingsAndPatch) / sizeof(struct reg));
	if (!NT_SUCCESS(status)) {
		return status;
	}

	struct reg finishInit1[] = {
		//{RT5682_DEPOP_1, 0x0000},
		//{RT5682_DMIC_CTRL_1, 0xa00},
		//{RT5682_GPIO_CTRL_1, 0x2160},
		{RT5682_TEST_MODE_CTRL_1, 0x0000},
		{RT5682_BIAS_CUR_CTRL_8, 0x4a6},
		//{RT5682_CHARGE_PUMP_1, 0x220},
		{RT5682_HP_CHARGE_PUMP_1, 0xe26},
		{RT5682_DMIC_CTRL_1, 0x1a00}
	};

	struct reg setClocksRyzen[] = {
		//set DAI format
		//{RT5682_TDM_TCON_CTRL, 0x1a00},

		//Set component PLL Ryzen
		{RT5682_PLL2_CTRL_1, 0x1730},
		{RT5682_PLL2_CTRL_2, 0x1e},
		{RT5682_PLL2_CTRL_3, 0x200},
		{RT5682_PLL2_CTRL_4, 0x180f},

		//set component sysclk Ryzen
		{RT5682_GLB_CLK, 0x4000},

		//set bclk1 ratio Ryzen
		{RT5682_TDM_TCON_CTRL, 0x201},

		//set wclk prepare
		{RT5682_PWR_ANLG_3, 0x34}, //0x30 for internal mic [Ryzen],

		//set clk
		{RT5682_ADDA_CLK_1, 0x1121} //[Ryzen]
	};

	struct reg setClocksGeminiLake[] = {
		//Set component PLL GLK
		{RT5682_PLL_CTRL_1, 0x0f03},
		{RT5682_PLL_CTRL_2, 0x3003},

		//set component sysclk GLK
		{RT5682_GLB_CLK, 0x2000},

		//set bclk1 ratio GLK
		{RT5682_TDM_TCON_CTRL, 0x0020},

		//set wclk prepare
		{RT5682_PWR_ANLG_3, 0x44}, //0x40 for internal mic [Gemini Lake]

		{RT5682_ADDA_CLK_1, 0x1001}, //[Gemini Lake]
	};

	struct reg setClocksTigerLake[] = {
		//set bclk1 ratio TGL
		{RT5682_TDM_TCON_CTRL, 0x0030},

		{RT5682_ADDA_CLK_1, 0x1001}, //[Gemini Lake]
	};
		
	struct reg finishInit2[] = {
		//set wclk prepare
		//{RT5682_PWR_ANLG_1, 0x722f},
		//{RT5682_PWR_DIG_1, 0x8000},	

		//set clk
		{RT5682_PLL_TRACK_2, 0x0100},

		//set bias level
		//{RT5682_PWR_DIG_1, 0x8001},

		//Update more defaults
		//{RT5682_HP_CTRL_2, 0x0000},
		{RT5682_DAC1_DIG_VOL, 0x9f9f},
		{RT5682_STO1_ADC_DIG_VOL, 0x2f2f}, //0xaf2f for internal mic
		{RT5682_REC_MIXER, 0x0000},
		//{RT5682_PWR_ANLG_1, 0xc000},
		{RT5682_I2S2_SDP, 0xc000},
		{RT5682_TDM_ADDA_CTRL_1, 0x8000},
		{RT5682_PLL_TRACK_3, 0x0100},
		{RT5682_GPIO_CTRL_1, 0x1160},

		//Headphone defaults
		{RT5682_HP_CTRL_2, 0x6000},
		{RT5682_STO1_ADC_MIXER, 0x6064},
		{RT5682_PWR_DIG_1, 0x8d11},
		{RT5682_PWR_DIG_2, 0x8400},
		{RT5682_PWR_ANLG_1, 0xf2af},
		{RT5682_CLK_DET, 0x8001},
		{RT5682_DEPOP_1, 0x69},
		{RT5682_CHARGE_PUMP_1, 0x420},
		{RT5682_CHOP_DAC, 0x3000},
		{RT5682_CHOP_ADC, 0x3000} //not present for internal mic
	};

	struct reg setDefaultsRyzen[] = {
		//For Ryzen
		{RT5682_CBJ_BST_CTRL, 0x0200},
		{RT5682_I2S1_SDP, 0x0000},
		{RT5682_HP_LOGIC_CTRL_2, 0x17}
	};

	struct reg setDefaultsGeminiLake[] = {
		//For Gemini Lake
		{RT5682_HP_CTRL_1, 0x0000},
		{RT5682_CBJ_BST_CTRL, 0x0300},
		{RT5682_DAC1_DIG_VOL, 0xa3a3},
		{RT5682_STO1_DAC_MIXER, 0x2080}, //was 0xa0a0
		{RT5682_PWR_ANLG_2, 0xe}, //was 0a0a
		{RT5682_I2S1_SDP, 0x2200},
		{RT5682_TDM_ADDA_CTRL_2, 0x0080},
		{RT5682_HP_LOGIC_CTRL_2, 0x12}
	};

	struct reg setDefaultsTigerLake[] = {
		//For Tiger Lake
		{RT5682_HP_CTRL_1, 0x8080},
		{RT5682_CBJ_BST_CTRL, 0x0300},
		{RT5682_DAC1_DIG_VOL, 0xafaf},
		{RT5682_STO1_DAC_MIXER, 0x2080}, //was 0xa0a0
		{RT5682_I2S1_SDP, 0x3330},
		{RT5682_TDM_ADDA_CTRL_2, 0x0080},
		{RT5682_HP_LOGIC_CTRL_2, 0x17}
	};

	Platform currentPlatform = GetPlatform();

	status = rt5682_reg_burstWrite(devContext, finishInit1, sizeof(finishInit1) / sizeof(struct reg));
	if (!NT_SUCCESS(status)) {
		return status;
	}

	switch (currentPlatform) {
	case PlatformRyzen:
		status = rt5682_reg_burstWrite(devContext, setClocksRyzen, sizeof(setClocksRyzen) / sizeof(struct reg));
		break;
	case PlatformGeminiLake:
		status = rt5682_reg_burstWrite(devContext, setClocksGeminiLake, sizeof(setClocksGeminiLake) / sizeof(struct reg));
		break;
	case PlatformTigerLake:
		status = rt5682_reg_burstWrite(devContext, setClocksTigerLake, sizeof(setClocksTigerLake) / sizeof(struct reg));
		break;
	}
	
	if (!NT_SUCCESS(status)) {
		return status;
	}

	status = rt5682_reg_burstWrite(devContext, finishInit2, sizeof(finishInit2) / sizeof(struct reg));
	if (!NT_SUCCESS(status)) {
		return status;
	}

	switch (currentPlatform) {
	case PlatformRyzen:
		status = rt5682_reg_burstWrite(devContext, setDefaultsRyzen, sizeof(setDefaultsRyzen) / sizeof(struct reg));
		break;
	case PlatformGeminiLake:
		status = rt5682_reg_burstWrite(devContext, setDefaultsGeminiLake, sizeof(setDefaultsGeminiLake) / sizeof(struct reg));
		break;
	case PlatformTigerLake:
		status = rt5682_reg_burstWrite(devContext, setDefaultsTigerLake, sizeof(setDefaultsTigerLake) / sizeof(struct reg));
		break;
	}
	if (!NT_SUCCESS(status)) {
		return status;
	}

	struct reg prepJackDetect[] = {
		{RT5682_CBJ_CTRL_5, 0xa60a},
		{RT5682_CBJ_CTRL_2, 0x40},
		{RT5682_CBJ_CTRL_1, 0xd142},
		{RT5682_CBJ_CTRL_3, 0x1484},
		{RT5682_SAR_IL_CMD_1, 0x32b7},
		{RT5682_GPIO_CTRL_1, 0x6960},
		{RT5682_RC_CLK_CTRL, 0xd000},
		{RT5682_PWR_ANLG_2, 0xa},
		{RT5682_IRQ_CTRL_2, 0x8000},
		{RT5682_4BTN_IL_CMD_4, 0x1010},
		{RT5682_4BTN_IL_CMD_5, 0x1010},
		{RT5682_4BTN_IL_CMD_6, 0x1010},
		{RT5682_4BTN_IL_CMD_7, 0x1010}
	};
	status = rt5682_reg_burstWrite(devContext, prepJackDetect, sizeof(prepJackDetect) / sizeof(struct reg));
	if (!NT_SUCCESS(status)) {
		return status;
	}
	return STATUS_SUCCESS;
}

NTSTATUS
OnPrepareHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesRaw,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PRTEK_CONTEXT pDevice = GetDeviceContext(FxDevice);
	BOOLEAN fSpbResourceFound = FALSE;
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;
		UCHAR Class;
		UCHAR Type;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		case CmResourceTypeConnection:
			//
			// Look for I2C or SPI resource and save connection ID.
			//
			Class = pDescriptor->u.Connection.Class;
			Type = pDescriptor->u.Connection.Type;
			if (Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
				Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
			{
				if (fSpbResourceFound == FALSE)
				{
					status = STATUS_SUCCESS;
					pDevice->I2CContext.I2cResHubId.LowPart = pDescriptor->u.Connection.IdLowPart;
					pDevice->I2CContext.I2cResHubId.HighPart = pDescriptor->u.Connection.IdHighPart;
					fSpbResourceFound = TRUE;
				}
				else
				{
				}
			}
			break;
		default:
			//
			// Ignoring all other resource types.
			//
			break;
		}
	}

	//
	// An SPB resource is required.
	//

	if (fSpbResourceFound == FALSE)
	{
		status = STATUS_NOT_FOUND;
	}

	status = SpbTargetInitialize(FxDevice, &pDevice->I2CContext);

	if (!NT_SUCCESS(status))
	{
		return status;
	}

	return status;
}

NTSTATUS
OnReleaseHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PRTEK_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	SpbTargetDeinitialize(FxDevice, &pDevice->I2CContext);

	return status;
}

NTSTATUS
OnD0Entry(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PRTEK_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	pDevice->JackType = 0;

	status = BOOTCODEC(pDevice);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	pDevice->ConnectInterrupt = true;

	return status;
}

NTSTATUS
OnD0Exit(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PRTEK_CONTEXT pDevice = GetDeviceContext(FxDevice);

	pDevice->ConnectInterrupt = false;

	return STATUS_SUCCESS;
}

static void rt5682_enable_push_button_irq(PRTEK_CONTEXT pDevice,
	bool enable)
{

	if (enable) {
		rt5682_reg_update(pDevice, RT5682_SAR_IL_CMD_1,
			RT5682_SAR_BUTT_DET_MASK, RT5682_SAR_BUTT_DET_EN);
		rt5682_reg_update(pDevice, RT5682_SAR_IL_CMD_13,
			RT5682_SAR_SOUR_MASK, RT5682_SAR_SOUR_BTN);
		rt5682_reg_write(pDevice, RT5682_IL_CMD_1, 0x0040);
		rt5682_reg_update(pDevice, RT5682_4BTN_IL_CMD_2,
			RT5682_4BTN_IL_MASK | RT5682_4BTN_IL_RST_MASK,
			RT5682_4BTN_IL_EN | RT5682_4BTN_IL_NOR);

		rt5682_reg_update(pDevice,
			RT5682_IRQ_CTRL_3, RT5682_IL_IRQ_MASK,
			RT5682_IL_IRQ_EN);
	}
	else {
		rt5682_reg_update(pDevice, RT5682_IRQ_CTRL_3,
			RT5682_IL_IRQ_MASK, RT5682_IL_IRQ_DIS);
		rt5682_reg_update(pDevice, RT5682_SAR_IL_CMD_1,
			RT5682_SAR_BUTT_DET_MASK, RT5682_SAR_BUTT_DET_DIS);
		rt5682_reg_update(pDevice, RT5682_4BTN_IL_CMD_2,
			RT5682_4BTN_IL_MASK, RT5682_4BTN_IL_DIS);
		rt5682_reg_update(pDevice, RT5682_4BTN_IL_CMD_2,
			RT5682_4BTN_IL_RST_MASK, RT5682_4BTN_IL_RST);
		rt5682_reg_update(pDevice, RT5682_SAR_IL_CMD_13,
			RT5682_SAR_SOUR_MASK, RT5682_SAR_SOUR_TYPE);
	}
}

int rt5682_headset_detect(PRTEK_CONTEXT pDevice, int jack_insert) {
	UINT16 val, count;
	if (jack_insert) {
		rt5682_reg_update(pDevice,
			RT5682_PWR_ANLG_1, RT5682_PWR_FV2, 0);

		LARGE_INTEGER WaitInterval;
		WaitInterval.QuadPart = -10 * 1000 * 15;
		KeDelayExecutionThread(KernelMode, false, &WaitInterval);

		rt5682_reg_update(pDevice,
			RT5682_PWR_ANLG_1, RT5682_PWR_FV2, RT5682_PWR_FV2);
		rt5682_reg_update(pDevice, RT5682_PWR_ANLG_3,
			RT5682_PWR_CBJ, RT5682_PWR_CBJ);
		rt5682_reg_update(pDevice,
			RT5682_HP_CHARGE_PUMP_1,
			RT5682_OSW_L_MASK | RT5682_OSW_R_MASK, 0);

		rt5682_enable_push_button_irq(pDevice, false);

		rt5682_reg_update(pDevice, RT5682_CBJ_CTRL_1,
			RT5682_TRIG_JD_MASK, RT5682_TRIG_JD_LOW);

		WaitInterval.QuadPart = -10 * 1000 * 55;
		KeDelayExecutionThread(KernelMode, false, &WaitInterval);

		rt5682_reg_update(pDevice, RT5682_CBJ_CTRL_1,
			RT5682_TRIG_JD_MASK, RT5682_TRIG_JD_HIGH);

		count = 0;
		rt5682_reg_read(pDevice, RT5682_CBJ_CTRL_2, &val);
		val &= RT5682_JACK_TYPE_MASK;
		while (val == 0 && count < 50) {
			WaitInterval.QuadPart = -10 * 1000 * 15;
			KeDelayExecutionThread(KernelMode, false, &WaitInterval);

			rt5682_reg_read(pDevice, RT5682_CBJ_CTRL_2, &val);
			val &= RT5682_JACK_TYPE_MASK;
			count++;
		}

		switch (val) {
		case 0x1:
		case 0x2:
			pDevice->JackType = SND_JACK_HEADSET;
			rt5682_reg_update(pDevice, RT5682_CBJ_CTRL_1,
				RT5682_FAST_OFF_MASK, RT5682_FAST_OFF_EN);
			rt5682_enable_push_button_irq(pDevice, true);
			break;
		default:
			pDevice->JackType = SND_JACK_HEADPHONE;
		}

		rt5682_reg_update(pDevice,
			RT5682_HP_CHARGE_PUMP_1,
			RT5682_OSW_L_MASK | RT5682_OSW_R_MASK,
			RT5682_OSW_L_EN | RT5682_OSW_R_EN);
		rt5682_reg_update(pDevice, RT5682_MICBIAS_2,
			RT5682_PWR_CLK25M_MASK | RT5682_PWR_CLK1M_MASK,
			RT5682_PWR_CLK25M_PU | RT5682_PWR_CLK1M_PU);
	}
	else {
		rt5682_enable_push_button_irq(pDevice, false);
		rt5682_reg_update(pDevice, RT5682_CBJ_CTRL_1,
			RT5682_TRIG_JD_MASK, RT5682_TRIG_JD_LOW);

		rt5682_reg_update(pDevice, RT5682_PWR_ANLG_3,
			RT5682_PWR_CBJ, 0);
		rt5682_reg_update(pDevice, RT5682_MICBIAS_2,
			RT5682_PWR_CLK25M_MASK | RT5682_PWR_CLK1M_MASK,
			RT5682_PWR_CLK25M_PD | RT5682_PWR_CLK1M_PD);
		rt5682_reg_update(pDevice, RT5682_CBJ_CTRL_1,
			RT5682_FAST_OFF_MASK, RT5682_FAST_OFF_DIS);

		pDevice->JackType = 0;
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Jack Type: %d\n", pDevice->JackType);
	return pDevice->JackType;
}

static int rt5682_button_detect(PRTEK_CONTEXT pDevice)
{
	UINT16 btn_type, val;

	rt5682_reg_read(pDevice, RT5682_4BTN_IL_CMD_1, &val);
	btn_type = val & 0xfff0;
	rt5682_reg_write(pDevice, RT5682_4BTN_IL_CMD_1, val);
	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"btn_type=%x\n", btn_type);
	rt5682_reg_update(pDevice,
		RT5682_SAR_IL_CMD_2, 0x10, 0x10);

	return btn_type;
}

void rt5682_jackdetect(PRTEK_CONTEXT pDevice) {
	NTSTATUS status = STATUS_SUCCESS;

	UINT16 val;
	status = rt5682_reg_read(pDevice, RT5682_AJD1_CTRL, &val);
	if (!NT_SUCCESS(status)) {
		DbgPrint("Failed to read jack detect\n");
		return;
	}

	val = val & RT5682_JDH_RS_MASK;
	if (!val) {
		/* jack in */
		if (pDevice->JackType == 0) {
			/* jack was out, report jack type */
			pDevice->JackType = rt5682_headset_detect(pDevice, 1);
		}
		else if ((pDevice->JackType & SND_JACK_HEADSET) == SND_JACK_HEADSET) {
			/* jack is already in, report button event */
			int btn_type = rt5682_button_detect(pDevice);
			/**
			 * rt5682 can report three kinds of button behavior,
			 * one click, double click and hold. However,
			 * currently we will report button pressed/released
			 * event. So all the three button behaviors are
			 * treated as button pressed.
			 */
			int rawButton = 0;

			switch (btn_type) {
			case 0x8000:
			case 0x4000:
			case 0x2000:
				rawButton = 1;
				break;
			case 0x1000:
			case 0x0800:
			case 0x0400:
				rawButton = 2;
				break;
			case 0x0200:
			case 0x0100:
			case 0x0080:
				rawButton = 3;
				break;
			case 0x0040:
			case 0x0020:
			case 0x0010:
				rawButton = 4;
				break;
			case 0x0000: /* unpressed */
				break;
			default:
				RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"Unexpected button code 0x%04x\n",
					btn_type);
				break;
			}

			Rt5682MediaReport report;
			report.ReportID = REPORTID_MEDIA;
			report.ControlCode = rawButton;

			size_t bytesWritten;
			Rt5682ProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
		}
	}
	else {
		/* jack out */
		pDevice->JackType = rt5682_headset_detect(pDevice, 0);
	}

	CsAudioSpecialKeyReport report;
	report.ReportID = REPORTID_SPECKEYS;
	report.ControlCode = CONTROL_CODE_JACK_TYPE;
	report.ControlValue = pDevice->JackType;

	size_t bytesWritten;
	Rt5682ProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
}

VOID
RtekJdetWorkItem(
	IN WDFWORKITEM  WorkItem
)
{
	WDFDEVICE Device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
	PRTEK_CONTEXT pDevice = GetDeviceContext(Device);

	rt5682_jackdetect(pDevice);
}

BOOLEAN OnInterruptIsr(
	WDFINTERRUPT Interrupt,
	ULONG MessageID) {
	UNREFERENCED_PARAMETER(MessageID);

	WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
	PRTEK_CONTEXT pDevice = GetDeviceContext(Device);

	if (!pDevice->ConnectInterrupt)
		return true;

	NTSTATUS status = STATUS_SUCCESS;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_WORKITEM_CONFIG workitemConfig;
	WDFWORKITEM hWorkItem;

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, RTEK_CONTEXT);
	attributes.ParentObject = Device;
	WDF_WORKITEM_CONFIG_INIT(&workitemConfig, RtekJdetWorkItem);

	WdfWorkItemCreate(&workitemConfig,
		&attributes,
		&hWorkItem);

	WdfWorkItemEnqueue(hWorkItem);

	return true;
}

NTSTATUS
Rt5682EvtDeviceAdd(
	IN WDFDRIVER       Driver,
	IN PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS                      status = STATUS_SUCCESS;
	WDF_IO_QUEUE_CONFIG           queueConfig;
	WDF_OBJECT_ATTRIBUTES         attributes;
	WDFDEVICE                     device;
	WDF_INTERRUPT_CONFIG interruptConfig;
	WDFQUEUE                      queue;
	UCHAR                         minorFunction;
	PRTEK_CONTEXT               devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	RtekPrint(DEBUG_LEVEL_INFO, DBG_PNP,
		"Rt5682EvtDeviceAdd called\n");

	//
	// Tell framework this is a filter driver. Filter drivers by default are  
	// not power policy owners. This works well for this driver because
	// HIDclass driver is the power policy owner for HID minidrivers.
	//

	WdfFdoInitSetFilter(DeviceInit);

	{
		WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

		pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
		pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
		pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
		pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

		WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
	}

	//
	// Setup the device context
	//

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, RTEK_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	{
		WDF_DEVICE_STATE deviceState;
		WDF_DEVICE_STATE_INIT(&deviceState);

		deviceState.NotDisableable = WdfFalse;
		WdfDeviceSetDeviceState(device, &deviceState);
	}

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

	queueConfig.EvtIoInternalDeviceControl = Rt5682EvtInternalDeviceControl;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue
	);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create manual I/O queue to take care of hid report read requests
	//

	devContext = GetDeviceContext(device);

	devContext->FxDevice = device;

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->ReportQueue
	);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create an interrupt object for hardware notifications
	//
	WDF_INTERRUPT_CONFIG_INIT(
		&interruptConfig,
		OnInterruptIsr,
		NULL);
	interruptConfig.PassiveHandling = TRUE;

	status = WdfInterruptCreate(
		device,
		&interruptConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->Interrupt);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"Error creating WDF interrupt object - %!STATUS!",
			status);

		return status;
	}

	return status;
}

VOID
Rt5682EvtInternalDeviceControl(
	IN WDFQUEUE     Queue,
	IN WDFREQUEST   Request,
	IN size_t       OutputBufferLength,
	IN size_t       InputBufferLength,
	IN ULONG        IoControlCode
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	WDFDEVICE           device;
	PRTEK_CONTEXT     devContext;
	BOOLEAN             completeRequest = TRUE;

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	device = WdfIoQueueGetDevice(Queue);
	devContext = GetDeviceContext(device);

	RtekPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
		"%s, Queue:0x%p, Request:0x%p\n",
		DbgHidInternalIoctlString(IoControlCode),
		Queue,
		Request
	);

	//
	// Please note that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl. So depending on the ioctl code, we will either
	// use retreive function or escape to WDM to get the UserBuffer.
	//

	switch (IoControlCode)
	{

	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		//
		// Retrieves the device's HID descriptor.
		//
		status = Rt5682GetHidDescriptor(device, Request);
		break;

	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		//
		//Retrieves a device's attributes in a HID_DEVICE_ATTRIBUTES structure.
		//
		status = Rt5682GetDeviceAttributes(Request);
		break;

	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		//
		//Obtains the report descriptor for the HID device.
		//
		status = Rt5682GetReportDescriptor(device, Request);
		break;

	case IOCTL_HID_GET_STRING:
		//
		// Requests that the HID minidriver retrieve a human-readable string
		// for either the manufacturer ID, the product ID, or the serial number
		// from the string descriptor of the device. The minidriver must send
		// a Get String Descriptor request to the device, in order to retrieve
		// the string descriptor, then it must extract the string at the
		// appropriate index from the string descriptor and return it in the
		// output buffer indicated by the IRP. Before sending the Get String
		// Descriptor request, the minidriver must retrieve the appropriate
		// index for the manufacturer ID, the product ID or the serial number
		// from the device extension of a top level collection associated with
		// the device.
		//
		status = Rt5682GetString(Request);
		break;

	case IOCTL_HID_WRITE_REPORT:
	case IOCTL_HID_SET_OUTPUT_REPORT:
		//
		//Transmits a class driver-supplied report to the device.
		//
		status = Rt5682WriteReport(devContext, Request);
		break;

	case IOCTL_HID_READ_REPORT:
	case IOCTL_HID_GET_INPUT_REPORT:
		//
		// Returns a report from the device into a class driver-supplied buffer.
		// 
		status = Rt5682ReadReport(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_SET_FEATURE:
		//
		// This sends a HID class feature report to a top-level collection of
		// a HID class device.
		//
		status = Rt5682SetFeature(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_GET_FEATURE:
		//
		// returns a feature report associated with a top-level collection
		status = Rt5682GetFeature(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_ACTIVATE_DEVICE:
		//
		// Makes the device ready for I/O operations.
		//
	case IOCTL_HID_DEACTIVATE_DEVICE:
		//
		// Causes the device to cease operations and terminate all outstanding
		// I/O requests.
		//
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	if (completeRequest)
	{
		WdfRequestComplete(Request, status);

		RtekPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s completed, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			Queue,
			Request
		);
	}
	else
	{
		RtekPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s deferred, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			Queue,
			Request
		);
	}

	return;
}

NTSTATUS
Rt5682GetHidDescriptor(
	IN WDFDEVICE Device,
	IN WDFREQUEST Request
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	size_t              bytesToCopy = 0;
	WDFMEMORY           memory;

	UNREFERENCED_PARAMETER(Device);

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682GetHidDescriptor Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded "HID Descriptor" 
	//
	bytesToCopy = DefaultHidDescriptor.bLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0, // Offset
		(PVOID)&DefaultHidDescriptor,
		bytesToCopy);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682GetHidDescriptor Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5682GetReportDescriptor(
	IN WDFDEVICE Device,
	IN WDFREQUEST Request
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	ULONG_PTR           bytesToCopy;
	WDFMEMORY           memory;

	PRTEK_CONTEXT devContext = GetDeviceContext(Device);

	UNREFERENCED_PARAMETER(Device);

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682GetReportDescriptor Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);
	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded Report descriptor
	//
	bytesToCopy = DefaultHidDescriptor.DescriptorList[0].wReportLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor's reportLength is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0,
		(PVOID)DefaultReportDescriptor,
		bytesToCopy);
	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682GetReportDescriptor Exit = 0x%x\n", status);

	return status;
}


NTSTATUS
Rt5682GetDeviceAttributes(
	IN WDFREQUEST Request
)
{
	NTSTATUS                 status = STATUS_SUCCESS;
	PHID_DEVICE_ATTRIBUTES   deviceAttributes = NULL;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682GetDeviceAttributes Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputBuffer(Request,
		sizeof(HID_DEVICE_ATTRIBUTES),
		(PVOID *)&deviceAttributes,
		NULL);
	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Set USB device descriptor
	//

	deviceAttributes->Size = sizeof(HID_DEVICE_ATTRIBUTES);
	deviceAttributes->VendorID = RT5682_VID;
	deviceAttributes->ProductID = RT5682_PID;
	deviceAttributes->VersionNumber = RT5682_VERSION;

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, sizeof(HID_DEVICE_ATTRIBUTES));

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682GetDeviceAttributes Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5682GetString(
	IN WDFREQUEST Request
)
{

	NTSTATUS status = STATUS_SUCCESS;
	PWSTR pwstrID;
	size_t lenID;
	WDF_REQUEST_PARAMETERS params;
	void *pStringBuffer = NULL;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682GetString Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	switch ((ULONG_PTR)params.Parameters.DeviceIoControl.Type3InputBuffer & 0xFFFF)
	{
	case HID_STRING_ID_IMANUFACTURER:
		pwstrID = L"Rt5682.\0";
		break;

	case HID_STRING_ID_IPRODUCT:
		pwstrID = L"MaxTouch Touch Screen\0";
		break;

	case HID_STRING_ID_ISERIALNUMBER:
		pwstrID = L"123123123\0";
		break;

	default:
		pwstrID = NULL;
		break;
	}

	lenID = pwstrID ? wcslen(pwstrID) * sizeof(WCHAR) + sizeof(UNICODE_NULL) : 0;

	if (pwstrID == NULL)
	{

		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Rt5682GetString Invalid request type\n");

		status = STATUS_INVALID_PARAMETER;

		return status;
	}

	status = WdfRequestRetrieveOutputBuffer(Request,
		lenID,
		&pStringBuffer,
		&lenID);

	if (!NT_SUCCESS(status))
	{

		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Rt5682GetString WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);

		return status;
	}

	RtlCopyMemory(pStringBuffer, pwstrID, lenID);

	WdfRequestSetInformation(Request, lenID);

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682GetString Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5682WriteReport(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;
	size_t bytesWritten = 0;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682WriteReport Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Rt5682WriteReport Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"Rt5682WriteReport No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			default:

				RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"Rt5682WriteReport Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682WriteReport Exit = 0x%x\n", status);

	return status;

}

NTSTATUS
Rt5682ProcessVendorReport(
	IN PRTEK_CONTEXT DevContext,
	IN PVOID ReportBuffer,
	IN ULONG ReportBufferLen,
	OUT size_t* BytesWritten
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFREQUEST reqRead;
	PVOID pReadReport = NULL;
	size_t bytesReturned = 0;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682ProcessVendorReport Entry\n");

	status = WdfIoQueueRetrieveNextRequest(DevContext->ReportQueue,
		&reqRead);

	if (NT_SUCCESS(status))
	{
		status = WdfRequestRetrieveOutputBuffer(reqRead,
			ReportBufferLen,
			&pReadReport,
			&bytesReturned);

		if (NT_SUCCESS(status))
		{
			//
			// Copy ReportBuffer into read request
			//

			if (bytesReturned > ReportBufferLen)
			{
				bytesReturned = ReportBufferLen;
			}

			RtlCopyMemory(pReadReport,
				ReportBuffer,
				bytesReturned);

			//
			// Complete read with the number of bytes returned as info
			//

			WdfRequestCompleteWithInformation(reqRead,
				status,
				bytesReturned);

			RtekPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"Rt5682ProcessVendorReport %d bytes returned\n", bytesReturned);

			//
			// Return the number of bytes written for the write request completion
			//

			*BytesWritten = bytesReturned;

			RtekPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"%s completed, Queue:0x%p, Request:0x%p\n",
				DbgHidInternalIoctlString(IOCTL_HID_READ_REPORT),
				DevContext->ReportQueue,
				reqRead);
		}
		else
		{
			RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);
		}
	}
	else
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfIoQueueRetrieveNextRequest failed Status 0x%x\n", status);
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682ProcessVendorReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5682ReadReport(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682ReadReport Entry\n");

	//
	// Forward this read request to our manual queue
	// (in other words, we are going to defer this request
	// until we have a corresponding write request to
	// match it with)
	//

	status = WdfRequestForwardToIoQueue(Request, DevContext->ReportQueue);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestForwardToIoQueue failed Status 0x%x\n", status);
	}
	else
	{
		*CompleteRequest = FALSE;
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682ReadReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5682SetFeature(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682SetFeature Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Rt5682SetFeature Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"Rt5682WriteReport No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			default:

				RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"Rt5682SetFeature Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682SetFeature Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5682GetFeature(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682GetFeature Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.OutputBufferLength < sizeof(HID_XFER_PACKET))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Rt5682GetFeature Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"Rt5682GetFeature No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			default:

				RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"Rt5682GetFeature Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5682GetFeature Exit = 0x%x\n", status);

	return status;
}

PCHAR
DbgHidInternalIoctlString(
	IN ULONG IoControlCode
)
{
	switch (IoControlCode)
	{
	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		return "IOCTL_HID_GET_DEVICE_DESCRIPTOR";
	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		return "IOCTL_HID_GET_REPORT_DESCRIPTOR";
	case IOCTL_HID_READ_REPORT:
		return "IOCTL_HID_READ_REPORT";
	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		return "IOCTL_HID_GET_DEVICE_ATTRIBUTES";
	case IOCTL_HID_WRITE_REPORT:
		return "IOCTL_HID_WRITE_REPORT";
	case IOCTL_HID_SET_FEATURE:
		return "IOCTL_HID_SET_FEATURE";
	case IOCTL_HID_GET_FEATURE:
		return "IOCTL_HID_GET_FEATURE";
	case IOCTL_HID_GET_STRING:
		return "IOCTL_HID_GET_STRING";
	case IOCTL_HID_ACTIVATE_DEVICE:
		return "IOCTL_HID_ACTIVATE_DEVICE";
	case IOCTL_HID_DEACTIVATE_DEVICE:
		return "IOCTL_HID_DEACTIVATE_DEVICE";
	case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:
		return "IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST";
	case IOCTL_HID_SET_OUTPUT_REPORT:
		return "IOCTL_HID_SET_OUTPUT_REPORT";
	case IOCTL_HID_GET_INPUT_REPORT:
		return "IOCTL_HID_GET_INPUT_REPORT";
	default:
		return "Unknown IOCTL";
	}
}