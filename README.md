# rt5682
Realtek ALC 5682 I2C Codec driver

Supports:
* Jack Detection
* Headphone output
* Sleep/Wake

Currently Not implemented:
* Microphone input

Note:
* Intel SST and AMD ACP proprietary drivers do NOT have documented interfaces, so this driver will not work with them.
* Using this driver on chromebooks with this audio chip will require using CoolStar ACP Audio or CoolStar SOF Audio
* Certain chromebooks will also need the Chrome EC + Chrome EC I2C drivers to be able to use this driver

Tested on HP Chromebook 14b (Ryzen 3 3250C)