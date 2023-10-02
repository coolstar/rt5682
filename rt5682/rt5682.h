#if !defined(_RT5682_H_)
#define _RT5682_H_

#pragma warning(disable:4200)  // suppress nameless struct/union warning
#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <initguid.h>
#include <wdm.h>

#pragma warning(default:4200)
#pragma warning(default:4201)
#pragma warning(default:4214)
#include <wdf.h>

#pragma warning(disable:4201)  // suppress nameless struct/union warning
#pragma warning(disable:4214)  // suppress bit field types other than int warning
#include <hidport.h>

#include "hidcommon.h"
#include "spb.h"
#include <stdint.h>

#define true 1
#define false 0

typedef enum {
	CSAudioEndpointTypeDSP,
	CSAudioEndpointTypeSpeaker,
	CSAudioEndpointTypeHeadphone,
	CSAudioEndpointTypeMicArray,
	CSAudioEndpointTypeMicJack
} CSAudioEndpointType;

typedef enum {
	CSAudioEndpointRegister,
	CSAudioEndpointStart,
	CSAudioEndpointStop,
	CSAudioEndpointOverrideFormat,
	CSAudioEndpointI2SParameters
} CSAudioEndpointRequest;

typedef struct CSAUDIOFORMATOVERRIDE {
	UINT16 channels;
	UINT16 frequency;
	UINT16 bitsPerSample;
	UINT16 validBitsPerSample;
	BOOLEAN force32BitOutputContainer;
} CsAudioFormatOverride;

typedef struct CSAUDIOI2SPARAMS {
	UINT32 version;

	UINT32 mclk;
	UINT32 bclk_rate;
	UINT32 frequency;
	UINT32 tdm_slots;
	UINT32 tdm_slot_width;
	UINT32 rx_slots;
	UINT32 tx_slots;
	UINT32 valid_bits; //end of version 1
} CsAudioI2SParameters;

typedef struct CSAUDIOARG {
	UINT32 argSz;
	CSAudioEndpointType endpointType;
	CSAudioEndpointRequest endpointRequest;
	union {
		CsAudioFormatOverride formatOverride;
		CsAudioI2SParameters i2sParameters;
	};
} CsAudioArg, * PCsAudioArg;

typedef enum platform {
	PlatformNone,
	PlatformRyzen,
	PlatformGeminiLake,
	PlatformCometLake,
	PlatformTigerLake
} Platform;

enum snd_jack_types {
	SND_JACK_HEADPHONE = 0x0001,
	SND_JACK_MICROPHONE = 0x0002,
	SND_JACK_HEADSET = SND_JACK_HEADPHONE | SND_JACK_MICROPHONE,
};

struct reg {
	UINT16 reg;
	UINT16 val;
};

//
// String definitions
//

#define DRIVERNAME                 "rt5682.sys: "

#define RT5682_POOL_TAG            (ULONG) '856R'

	typedef UCHAR HID_REPORT_DESCRIPTOR, *PHID_REPORT_DESCRIPTOR;

#ifdef DESCRIPTOR_DEF
HID_REPORT_DESCRIPTOR DefaultReportDescriptor[] = {
	//
	// Consumer Control starts here
	//
	0x05, 0x0C, /*		Usage Page (Consumer Devices)		*/
	0x09, 0x01, /*		Usage (Consumer Control)			*/
	0xA1, 0x01, /*		Collection (Application)			*/
	0x85, REPORTID_MEDIA,	/*		Report ID=1							*/
	0x05, 0x0C, /*		Usage Page (Consumer Devices)		*/
	0x15, 0x00, /*		Logical Minimum (0)					*/
	0x25, 0x01, /*		Logical Maximum (1)					*/
	0x75, 0x01, /*		Report Size (1)						*/
	0x95, 0x04, /*		Report Count (4)					*/
	0x09, 0xCD, /*		Usage (Play / Pause)				*/
	0x09, 0xCF, /*		Usage (Voice Command)				*/
	0x09, 0xE9, /*		Usage (Volume Up)					*/
	0x09, 0xEA, /*		Usage (Volume Down)					*/
	0x81, 0x02, /*		Input (Data, Variable, Absolute)	*/
	0x95, 0x04, /*		Report Count (4)					*/
	0x81, 0x01, /*		Input (Constant)					*/
	0xC0,        /*        End Collection                        */

	0x06, 0x00, 0xff,                    // USAGE_PAGE (Vendor Defined Page 1)
	0x09, 0x04,                          // USAGE (Vendor Usage 4)
	0xa1, 0x01,                          // COLLECTION (Application)
	0x85, REPORTID_SPECKEYS,             //   REPORT_ID (Special Keys)
	0x15, 0x00,                          //   LOGICAL_MINIMUM (0)
	0x26, 0xff, 0x00,                    //   LOGICAL_MAXIMUM (256)
	0x75, 0x08,                          //   REPORT_SIZE  (8)   - bits
	0x95, 0x01,                          //   REPORT_COUNT (1)  - Bytes
	0x09, 0x02,                          //   USAGE (Vendor Usage 1)
	0x81, 0x02,                          //   INPUT (Data,Var,Abs)
	0x09, 0x03,                          //   USAGE (Vendor Usage 2)
	0x81, 0x02,                          //   INPUT (Data,Var,Abs)
	0x09, 0x02,                          //   USAGE (Vendor Usage 1)
	0x91, 0x02,                          //   OUTPUT (Data,Var,Abs)
	0xc0,                                // END_COLLECTION
};


//
// This is the default HID descriptor returned by the mini driver
// in response to IOCTL_HID_GET_DEVICE_DESCRIPTOR. The size
// of report descriptor is currently the size of DefaultReportDescriptor.
//

CONST HID_DESCRIPTOR DefaultHidDescriptor = {
	0x09,   // length of HID descriptor
	0x21,   // descriptor type == HID  0x21
	0x0100, // hid spec release
	0x00,   // country code == Not Specified
	0x01,   // number of HID class descriptors
	{ 0x22,   // descriptor type 
	sizeof(DefaultReportDescriptor) }  // total length of report descriptor
};
#endif

typedef struct _RTEK_CONTEXT
{

	WDFDEVICE FxDevice;

	WDFQUEUE ReportQueue;

	WDFQUEUE IdleQueue;

	SPB_CONTEXT I2CContext;

	WDFINTERRUPT Interrupt;

	BOOLEAN ConnectInterrupt;

	INT JackType;

	PCALLBACK_OBJECT CSAudioAPICallback;
	PVOID CSAudioAPICallbackObj;

	BOOLEAN CSAudioManaged;

	BOOLEAN ReclockRequested;
	UINT32 mclk;
	UINT32 freq;
	UINT32 slotWidth;
} RTEK_CONTEXT, *PRTEK_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(RTEK_CONTEXT, GetDeviceContext)

//
// Power Idle Workitem context
// 
typedef struct _IDLE_WORKITEM_CONTEXT
{
	// Handle to a WDF device object
	WDFDEVICE FxDevice;

	// Handle to a WDF request object
	WDFREQUEST FxRequest;

} IDLE_WORKITEM_CONTEXT, * PIDLE_WORKITEM_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(IDLE_WORKITEM_CONTEXT, GetIdleWorkItemContext)

//
// Function definitions
//

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_UNLOAD Rt5682DriverUnload;

EVT_WDF_DRIVER_DEVICE_ADD Rt5682EvtDeviceAdd;

EVT_WDFDEVICE_WDM_IRP_PREPROCESS Rt5682EvtWdmPreprocessMnQueryId;

EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL Rt5682EvtInternalDeviceControl;

NTSTATUS
Rt5682GetHidDescriptor(
	IN WDFDEVICE Device,
	IN WDFREQUEST Request
);

NTSTATUS
Rt5682GetReportDescriptor(
	IN WDFDEVICE Device,
	IN WDFREQUEST Request
);

NTSTATUS
Rt5682GetDeviceAttributes(
	IN WDFREQUEST Request
);

NTSTATUS
Rt5682GetString(
	IN WDFREQUEST Request
);

NTSTATUS
Rt5682WriteReport(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request
);

NTSTATUS
Rt5682ProcessVendorReport(
	IN PRTEK_CONTEXT DevContext,
	IN PVOID ReportBuffer,
	IN ULONG ReportBufferLen,
	OUT size_t* BytesWritten
);

NTSTATUS
Rt5682ReadReport(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
);

NTSTATUS
Rt5682SetFeature(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
);

NTSTATUS
Rt5682GetFeature(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
);

PCHAR
DbgHidInternalIoctlString(
	IN ULONG        IoControlCode
);

VOID
RtekCompleteIdleIrp(
	IN PRTEK_CONTEXT FxDeviceContext
);

//
// Helper macros
//

#define DEBUG_LEVEL_ERROR   1
#define DEBUG_LEVEL_INFO    2
#define DEBUG_LEVEL_VERBOSE 3

#define DBG_INIT  1
#define DBG_PNP   2
#define DBG_IOCTL 4

#if 0
#define RtekPrint(dbglevel, dbgcatagory, fmt, ...) {          \
    if (Rt5682DebugLevel >= dbglevel &&                         \
        (Rt5682DebugCatagories && dbgcatagory))                 \
	    {                                                           \
        DbgPrint(DRIVERNAME);                                   \
        DbgPrint(fmt, __VA_ARGS__);                             \
	    }                                                           \
}
#else
#define RtekPrint(dbglevel, fmt, ...) {                       \
}
#endif

#endif