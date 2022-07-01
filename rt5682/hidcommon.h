#if !defined(_RT5682_COMMON_H_)
#define _RT5682_COMMON_H_

//
//These are the device attributes returned by vmulti in response
// to IOCTL_HID_GET_DEVICE_ATTRIBUTES.
//

#define RT5682_PID              0x5682
#define RT5682_VID              0x10EC
#define RT5682_VERSION          0x0001

//
// These are the report ids
//

#define REPORTID_MEDIA	0x01
#define REPORTID_SPECKEYS		0x02

#pragma pack(1)
typedef struct _RT5682_MEDIA_REPORT
{

	BYTE      ReportID;

	BYTE	  ControlCode;

} Rt5682MediaReport;
#pragma pack()

#define CONTROL_CODE_JACK_TYPE 0x1

#pragma pack(1)
typedef struct _CSAUDIO_SPECKEY_REPORT
{

	BYTE      ReportID;

	BYTE	  ControlCode;

	BYTE	  ControlValue;

} CsAudioSpecialKeyReport;

#pragma pack()

#endif
