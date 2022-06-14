#if !defined(_RT5682_COMMON_H_)
#define _RT5682_COMMON_H_

//
//These are the device attributes returned by vmulti in response
// to IOCTL_HID_GET_DEVICE_ATTRIBUTES.
//

#define RT5682_PID              0xBACC
#define RT5682_VID              0x00FF
#define RT5682_VERSION          0x0001

//
// These are the report ids
//

#define REPORTID_MEDIA	0x01

#pragma pack(1)
typedef struct _RT5682_MEDIA_REPORT
{

	BYTE      ReportID;

	BYTE	  ControlCode;

} Rt5682MediaReport;
#pragma pack()

//
// Feature report infomation
//

#define DEVICE_MODE_MOUSE        0x00
#define DEVICE_MODE_SINGLE_INPUT 0x01
#define DEVICE_MODE_MULTI_INPUT  0x02

#endif
