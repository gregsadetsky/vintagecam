/* SPDX-License-Identifier: GPL-2.0-or-later */
/* extracted verbatim from linux drivers/media/usb/gspca/zc3xx.c (gc0303 tables) */
static const struct usb_action gc0303_Initial[] = {
	{0xa0, 0x01, ZC3XX_R000_SYSTEMCONTROL},		/* 00,00,01,cc, */
	{0xa0, 0x02, ZC3XX_R008_CLOCKSETTING},		/* 00,08,02,cc, */
	{0xa0, 0x01, ZC3XX_R010_CMOSSENSORSELECT},	/* 00,10,01,cc, */
	{0xa0, 0x00, ZC3XX_R002_CLOCKSELECT},
	{0xa0, 0x02, ZC3XX_R003_FRAMEWIDTHHIGH},	/* 00,03,02,cc, */
	{0xa0, 0x80, ZC3XX_R004_FRAMEWIDTHLOW},		/* 00,04,80,cc, */
	{0xa0, 0x01, ZC3XX_R005_FRAMEHEIGHTHIGH},	/* 00,05,01,cc, */
	{0xa0, 0xe0, ZC3XX_R006_FRAMEHEIGHTLOW},	/* 00,06,e0,cc, */
	{0xa0, 0x98, ZC3XX_R08B_I2CDEVICEADDR},		/* 00,8b,98,cc, */
	{0xa0, 0x01, ZC3XX_R001_SYSTEMOPERATING},	/* 00,01,01,cc, */
	{0xa0, 0x03, ZC3XX_R012_VIDEOCONTROLFUNC},	/* 00,12,03,cc, */
	{0xa0, 0x01, ZC3XX_R012_VIDEOCONTROLFUNC},	/* 00,12,01,cc, */
	{0xa0, 0x00, ZC3XX_R098_WINYSTARTLOW},		/* 00,98,00,cc, */
	{0xa0, 0x00, ZC3XX_R09A_WINXSTARTLOW},		/* 00,9a,00,cc, */
	{0xa0, 0x00, ZC3XX_R11A_FIRSTYLOW},		/* 01,1a,00,cc, */
	{0xa0, 0x00, ZC3XX_R11C_FIRSTXLOW},		/* 01,1c,00,cc, */
	{0xa0, 0xe8, ZC3XX_R09C_WINHEIGHTLOW},		/* 00,9c,e6,cc,
							 * 6<->8 */
	{0xa0, 0x88, ZC3XX_R09E_WINWIDTHLOW},		/* 00,9e,86,cc,
							 * 6<->8 */
	{0xa0, 0x10, ZC3XX_R087_EXPTIMEMID},		/* 00,87,10,cc, */
	{0xa0, 0x98, ZC3XX_R08B_I2CDEVICEADDR},		/* 00,8b,98,cc, */
	{0xaa, 0x01, 0x0000},
	{0xaa, 0x1a, 0x0000},		/* 00,1a,00,aa, */
	{0xaa, 0x1c, 0x0017},		/* 00,1c,17,aa, */
	{0xaa, 0x1b, 0x0000},
	{0xa0, 0x82, ZC3XX_R086_EXPTIMEHIGH},		/* 00,86,82,cc, */
	{0xa0, 0x83, ZC3XX_R087_EXPTIMEMID},		/* 00,87,83,cc, */
	{0xa0, 0x84, ZC3XX_R088_EXPTIMELOW},		/* 00,88,84,cc, */
	{0xaa, 0x05, 0x0010},		/* 00,05,10,aa, */
	{0xaa, 0x0a, 0x0002},
	{0xaa, 0x0b, 0x0000},
	{0xaa, 0x0c, 0x0002},
	{0xaa, 0x0d, 0x0000},
	{0xaa, 0x0e, 0x0002},
	{0xaa, 0x0f, 0x0000},
	{0xaa, 0x10, 0x0002},
	{0xaa, 0x11, 0x0000},
	{0xaa, 0x16, 0x0001},		/* 00,16,01,aa, */
	{0xaa, 0x17, 0x00e8},		/* 00,17,e6,aa, (e6 -> e8) */
	{0xaa, 0x18, 0x0002},		/* 00,18,02,aa, */
	{0xaa, 0x19, 0x0088},		/* 00,19,86,aa, */
	{0xaa, 0x20, 0x0020},		/* 00,20,20,aa, */
	{0xa0, 0xb7, ZC3XX_R101_SENSORCORRECTION},	/* 01,01,b7,cc, */
	{0xa0, 0x05, ZC3XX_R012_VIDEOCONTROLFUNC},	/* 00,12,05,cc, */
	{0xa0, 0x0d, ZC3XX_R100_OPERATIONMODE},		/* 01,00,0d,cc, */
	{0xa0, 0x76, ZC3XX_R189_AWBSTATUS},		/* 01,89,76,cc, */
	{0xa0, 0x09, 0x01ad},				/* 01,ad,09,cc, */
	{0xa0, 0x03, ZC3XX_R1C5_SHARPNESSMODE},		/* 01,c5,03,cc, */
	{0xa0, 0x13, ZC3XX_R1CB_SHARPNESS05},		/* 01,cb,13,cc, */
	{0xa0, 0x08, ZC3XX_R250_DEADPIXELSMODE},	/* 02,50,08,cc, */
	{0xa0, 0x08, ZC3XX_R301_EEPROMACCESS},		/* 03,01,08,cc, */
	{0xa0, 0x58, ZC3XX_R1A8_DIGITALGAIN},
	{0xa0, 0x61, ZC3XX_R116_RGAIN},			/* 01,16,61,cc, */
	{0xa0, 0x65, ZC3XX_R118_BGAIN},			/* 01,18,65,cc */
	{0xaa, 0x1b, 0x0000},
	{}
};

static const struct usb_action gc0303_InitialScale[] = {
	{0xa0, 0x01, ZC3XX_R000_SYSTEMCONTROL},		/* 00,00,01,cc, */
	{0xa0, 0x02, ZC3XX_R008_CLOCKSETTING},		/* 00,08,02,cc, */
	{0xa0, 0x01, ZC3XX_R010_CMOSSENSORSELECT},	/* 00,10,01,cc, */
	{0xa0, 0x10, ZC3XX_R002_CLOCKSELECT},
	{0xa0, 0x02, ZC3XX_R003_FRAMEWIDTHHIGH},	/* 00,03,02,cc, */
	{0xa0, 0x80, ZC3XX_R004_FRAMEWIDTHLOW},		/* 00,04,80,cc, */
	{0xa0, 0x01, ZC3XX_R005_FRAMEHEIGHTHIGH},	/* 00,05,01,cc, */
	{0xa0, 0xe0, ZC3XX_R006_FRAMEHEIGHTLOW},	/* 00,06,e0,cc, */
	{0xa0, 0x98, ZC3XX_R08B_I2CDEVICEADDR},		/* 00,8b,98,cc, */
	{0xa0, 0x01, ZC3XX_R001_SYSTEMOPERATING},	/* 00,01,01,cc, */
	{0xa0, 0x03, ZC3XX_R012_VIDEOCONTROLFUNC},	/* 00,12,03,cc, */
	{0xa0, 0x01, ZC3XX_R012_VIDEOCONTROLFUNC},	/* 00,12,01,cc, */
	{0xa0, 0x00, ZC3XX_R098_WINYSTARTLOW},		/* 00,98,00,cc, */
	{0xa0, 0x00, ZC3XX_R09A_WINXSTARTLOW},		/* 00,9a,00,cc, */
	{0xa0, 0x00, ZC3XX_R11A_FIRSTYLOW},		/* 01,1a,00,cc, */
	{0xa0, 0x00, ZC3XX_R11C_FIRSTXLOW},		/* 01,1c,00,cc, */
	{0xa0, 0xe8, ZC3XX_R09C_WINHEIGHTLOW},		/* 00,9c,e8,cc,
							 * 8<->6 */
	{0xa0, 0x88, ZC3XX_R09E_WINWIDTHLOW},		/* 00,9e,88,cc,
							 * 8<->6 */
	{0xa0, 0x10, ZC3XX_R087_EXPTIMEMID},		/* 00,87,10,cc, */
	{0xa0, 0x98, ZC3XX_R08B_I2CDEVICEADDR},		/* 00,8b,98,cc, */
	{0xaa, 0x01, 0x0000},
	{0xaa, 0x1a, 0x0000},		/* 00,1a,00,aa, */
	{0xaa, 0x1c, 0x0017},		/* 00,1c,17,aa, */
	{0xaa, 0x1b, 0x0000},
	{0xa0, 0x82, ZC3XX_R086_EXPTIMEHIGH},	/* 00,86,82,cc, */
	{0xa0, 0x83, ZC3XX_R087_EXPTIMEMID},	/* 00,87,83,cc, */
	{0xa0, 0x84, ZC3XX_R088_EXPTIMELOW},	/* 00,88,84,cc, */
	{0xaa, 0x05, 0x0010},		/* 00,05,10,aa, */
	{0xaa, 0x0a, 0x0001},
	{0xaa, 0x0b, 0x0000},
	{0xaa, 0x0c, 0x0001},
	{0xaa, 0x0d, 0x0000},
	{0xaa, 0x0e, 0x0001},
	{0xaa, 0x0f, 0x0000},
	{0xaa, 0x10, 0x0001},
	{0xaa, 0x11, 0x0000},
	{0xaa, 0x16, 0x0001},		/* 00,16,01,aa, */
	{0xaa, 0x17, 0x00e8},		/* 00,17,e6,aa (e6 -> e8) */
	{0xaa, 0x18, 0x0002},		/* 00,18,02,aa, */
	{0xaa, 0x19, 0x0088},		/* 00,19,88,aa, */
	{0xa0, 0xb7, ZC3XX_R101_SENSORCORRECTION},	/* 01,01,b7,cc, */
	{0xa0, 0x05, ZC3XX_R012_VIDEOCONTROLFUNC},	/* 00,12,05,cc, */
	{0xa0, 0x0d, ZC3XX_R100_OPERATIONMODE},		/* 01,00,0d,cc, */
	{0xa0, 0x76, ZC3XX_R189_AWBSTATUS},		/* 01,89,76,cc, */
	{0xa0, 0x09, 0x01ad},				/* 01,ad,09,cc, */
	{0xa0, 0x03, ZC3XX_R1C5_SHARPNESSMODE},		/* 01,c5,03,cc, */
	{0xa0, 0x13, ZC3XX_R1CB_SHARPNESS05},		/* 01,cb,13,cc, */
	{0xa0, 0x08, ZC3XX_R250_DEADPIXELSMODE},	/* 02,50,08,cc, */
	{0xa0, 0x08, ZC3XX_R301_EEPROMACCESS},		/* 03,01,08,cc, */
	{0xa0, 0x58, ZC3XX_R1A8_DIGITALGAIN},
	{0xa0, 0x61, ZC3XX_R116_RGAIN},		/* 01,16,61,cc, */
	{0xa0, 0x65, ZC3XX_R118_BGAIN},		/* 01,18,65,cc */
	{0xaa, 0x1b, 0x0000},
	{}
};
static const struct usb_action gc0303_50HZ[] = {
	{0xaa, 0x82, 0x0000},		/* 00,82,00,aa */
	{0xaa, 0x83, 0x0001},		/* 00,83,01,aa */
	{0xaa, 0x84, 0x0063},
	{0xa0, 0x00, ZC3XX_R190_EXPOSURELIMITHIGH},	/* 01,90,00,cc, */
	{0xa0, 0x06, ZC3XX_R191_EXPOSURELIMITMID},	/* 01,91,0d,cc, */
	{0xa0, 0xa8, ZC3XX_R192_EXPOSURELIMITLOW},	/* 01,92,50,cc, */
	{0xa0, 0x00, ZC3XX_R195_ANTIFLICKERHIGH},	/* 01,95,00,cc, */
	{0xa0, 0x00, ZC3XX_R196_ANTIFLICKERMID},	/* 01,96,00,cc, */
	{0xa0, 0x47, ZC3XX_R197_ANTIFLICKERLOW},	/* 01,97,47,cc, */
	{0xa0, 0x0e, ZC3XX_R18C_AEFREEZE},		/* 01,8c,0e,cc, */
	{0xa0, 0x15, ZC3XX_R18F_AEUNFREEZE},		/* 01,8f,15,cc, */
	{0xa0, 0x10, ZC3XX_R1A9_DIGITALLIMITDIFF},	/* 01,a9,10,cc, */
	{0xa0, 0x48, ZC3XX_R1AA_DIGITALGAINSTEP},
	{0xa0, 0x62, ZC3XX_R01D_HSYNC_0},		/* 00,1d,62,cc, */
	{0xa0, 0x90, ZC3XX_R01E_HSYNC_1},		/* 00,1e,90,cc, */
	{0xa0, 0xc8, ZC3XX_R01F_HSYNC_2},		/* 00,1f,c8,cc, */
	{0xa0, 0xff, ZC3XX_R020_HSYNC_3},		/* 00,20,ff,cc, */
	{0xa0, 0x58, ZC3XX_R11D_GLOBALGAIN},		/* 01,1d,58,cc, */
	{0xa0, 0x42, ZC3XX_R180_AUTOCORRECTENABLE},	/* 01,80,42,cc, */
	{0xa0, 0x7f, ZC3XX_R18D_YTARGET},
	{}
};

static const struct usb_action gc0303_50HZScale[] = {
	{0xaa, 0x82, 0x0000},		/* 00,82,00,aa */
	{0xaa, 0x83, 0x0003},		/* 00,83,03,aa */
	{0xaa, 0x84, 0x0054},		/* 00,84,54,aa */
	{0xa0, 0x00, ZC3XX_R190_EXPOSURELIMITHIGH},	/* 01,90,00,cc, */
	{0xa0, 0x0d, ZC3XX_R191_EXPOSURELIMITMID},	/* 01,91,0d,cc, */
	{0xa0, 0x50, ZC3XX_R192_EXPOSURELIMITLOW},	/* 01,92,50,cc, */
	{0xa0, 0x00, ZC3XX_R195_ANTIFLICKERHIGH},	/* 01,95,00,cc, */
	{0xa0, 0x00, ZC3XX_R196_ANTIFLICKERMID},	/* 01,96,00,cc, */
	{0xa0, 0x8e, ZC3XX_R197_ANTIFLICKERLOW},	/* 01,97,8e,cc, */
	{0xa0, 0x0e, ZC3XX_R18C_AEFREEZE},		/* 01,8c,0e,cc, */
	{0xa0, 0x15, ZC3XX_R18F_AEUNFREEZE},		/* 01,8f,15,cc, */
	{0xa0, 0x10, ZC3XX_R1A9_DIGITALLIMITDIFF},	/* 01,a9,10,cc, */
	{0xa0, 0x48, ZC3XX_R1AA_DIGITALGAINSTEP},	/* 01,aa,24,cc, */
	{0xa0, 0x62, ZC3XX_R01D_HSYNC_0},		/* 00,1d,62,cc, */
	{0xa0, 0x90, ZC3XX_R01E_HSYNC_1},		/* 00,1e,90,cc, */
	{0xa0, 0xc8, ZC3XX_R01F_HSYNC_2},		/* 00,1f,c8,cc, */
	{0xa0, 0xff, ZC3XX_R020_HSYNC_3},		/* 00,20,ff,cc, */
	{0xa0, 0x58, ZC3XX_R11D_GLOBALGAIN},		/* 01,1d,58,cc, */
	{0xa0, 0x42, ZC3XX_R180_AUTOCORRECTENABLE},	/* 01,80,42,cc, */
	{0xa0, 0x7f, ZC3XX_R18D_YTARGET},
	{}
};

static const struct usb_action gc0303_60HZ[] = {
	{0xaa, 0x82, 0x0000},		/* 00,82,00,aa */
	{0xaa, 0x83, 0x0000},
	{0xaa, 0x84, 0x003b},
	{0xa0, 0x00, ZC3XX_R190_EXPOSURELIMITHIGH},	/* 01,90,00,cc, */
	{0xa0, 0x05, ZC3XX_R191_EXPOSURELIMITMID},	/* 01,91,05,cc, */
	{0xa0, 0x88, ZC3XX_R192_EXPOSURELIMITLOW},	/* 01,92,88,cc, */
	{0xa0, 0x00, ZC3XX_R195_ANTIFLICKERHIGH},	/* 01,95,00,cc, */
	{0xa0, 0x00, ZC3XX_R196_ANTIFLICKERMID},	/* 01,96,00,cc, */
	{0xa0, 0x3b, ZC3XX_R197_ANTIFLICKERLOW},	/* 01,97,3b,cc, */
	{0xa0, 0x0e, ZC3XX_R18C_AEFREEZE},		/* 01,8c,0e,cc, */
	{0xa0, 0x15, ZC3XX_R18F_AEUNFREEZE},		/* 01,8f,15,cc, */
	{0xa0, 0x10, ZC3XX_R1A9_DIGITALLIMITDIFF},	/* 01,a9,10,cc, */
	{0xa0, 0x24, ZC3XX_R1AA_DIGITALGAINSTEP},	/* 01,aa,24,cc, */
	{0xa0, 0x62, ZC3XX_R01D_HSYNC_0},		/* 00,1d,62,cc, */
	{0xa0, 0x90, ZC3XX_R01E_HSYNC_1},		/* 00,1e,90,cc, */
	{0xa0, 0xc8, ZC3XX_R01F_HSYNC_2},		/* 00,1f,c8,cc, */
	{0xa0, 0xff, ZC3XX_R020_HSYNC_3},		/* 00,20,ff,cc, */
	{0xa0, 0x58, ZC3XX_R11D_GLOBALGAIN},		/* 01,1d,58,cc, */
	{0xa0, 0x42, ZC3XX_R180_AUTOCORRECTENABLE},	/* 01,80,42,cc, */
	{0xa0, 0x80, ZC3XX_R18D_YTARGET},
	{}
};

static const struct usb_action gc0303_60HZScale[] = {
	{0xaa, 0x82, 0x0000},		/* 00,82,00,aa */
	{0xaa, 0x83, 0x0000},
	{0xaa, 0x84, 0x0076},
	{0xa0, 0x00, ZC3XX_R190_EXPOSURELIMITHIGH},	/* 01,90,00,cc, */
	{0xa0, 0x0b, ZC3XX_R191_EXPOSURELIMITMID},	/* 01,1,0b,cc, */
	{0xa0, 0x10, ZC3XX_R192_EXPOSURELIMITLOW},	/* 01,2,10,cc, */
	{0xa0, 0x00, ZC3XX_R195_ANTIFLICKERHIGH},	/* 01,5,00,cc, */
	{0xa0, 0x00, ZC3XX_R196_ANTIFLICKERMID},	/* 01,6,00,cc, */
	{0xa0, 0x76, ZC3XX_R197_ANTIFLICKERLOW},	/* 01,7,76,cc, */
	{0xa0, 0x0e, ZC3XX_R18C_AEFREEZE},		/* 01,c,0e,cc, */
	{0xa0, 0x15, ZC3XX_R18F_AEUNFREEZE},		/* 01,f,15,cc, */
	{0xa0, 0x10, ZC3XX_R1A9_DIGITALLIMITDIFF},	/* 01,9,10,cc, */
	{0xa0, 0x24, ZC3XX_R1AA_DIGITALGAINSTEP},	/* 01,a,24,cc, */
	{0xa0, 0x62, ZC3XX_R01D_HSYNC_0},		/* 00,d,62,cc, */
	{0xa0, 0x90, ZC3XX_R01E_HSYNC_1},		/* 00,e,90,cc, */
	{0xa0, 0xc8, ZC3XX_R01F_HSYNC_2},		/* 00,f,c8,cc, */
	{0xa0, 0xff, ZC3XX_R020_HSYNC_3},		/* 00,0,ff,cc, */
	{0xa0, 0x58, ZC3XX_R11D_GLOBALGAIN},		/* 01,d,58,cc, */
	{0xa0, 0x42, ZC3XX_R180_AUTOCORRECTENABLE},	/* 01,80,42,cc, */
	{0xa0, 0x80, ZC3XX_R18D_YTARGET},
	{}
};

static const struct usb_action gc0303_NoFlicker[] = {
	{0xa0, 0x0c, ZC3XX_R100_OPERATIONMODE},		/* 01,00,0c,cc, */
	{0xaa, 0x82, 0x0000},		/* 00,82,00,aa */
	{0xaa, 0x83, 0x0000},		/* 00,83,00,aa */
	{0xaa, 0x84, 0x0020},		/* 00,84,20,aa */
	{0xa0, 0x00, ZC3XX_R190_EXPOSURELIMITHIGH},	/* 01,0,00,cc, */
	{0xa0, 0x00, ZC3XX_R191_EXPOSURELIMITMID},
	{0xa0, 0x48, ZC3XX_R192_EXPOSURELIMITLOW},
	{0xa0, 0x00, ZC3XX_R195_ANTIFLICKERHIGH},	/* 01,95,00,cc, */
	{0xa0, 0x00, ZC3XX_R196_ANTIFLICKERMID},	/* 01,96,00,cc, */
	{0xa0, 0x10, ZC3XX_R197_ANTIFLICKERLOW},	/* 01,97,10,cc, */
	{0xa0, 0x0e, ZC3XX_R18C_AEFREEZE},		/* 01,8c,0e,cc, */
	{0xa0, 0x15, ZC3XX_R18F_AEUNFREEZE},		/* 01,8f,15,cc, */
	{0xa0, 0x62, ZC3XX_R01D_HSYNC_0},		/* 00,1d,62,cc, */
	{0xa0, 0x90, ZC3XX_R01E_HSYNC_1},		/* 00,1e,90,cc, */
	{0xa0, 0xc8, ZC3XX_R01F_HSYNC_2},		/* 00,1f,c8,cc, */
	{0xa0, 0xff, ZC3XX_R020_HSYNC_3},		/* 00,20,ff,cc, */
	{0xa0, 0x58, ZC3XX_R11D_GLOBALGAIN},		/* 01,1d,58,cc, */
	{0xa0, 0x03, ZC3XX_R180_AUTOCORRECTENABLE},	/* 01,80,03,cc */
	{}
};

static const struct usb_action gc0303_NoFlickerScale[] = {
	{0xa0, 0x0c, ZC3XX_R100_OPERATIONMODE},		/* 01,00,0c,cc, */
	{0xaa, 0x82, 0x0000},		/* 00,82,00,aa */
	{0xaa, 0x83, 0x0000},		/* 00,83,00,aa */
	{0xaa, 0x84, 0x0020},		/* 00,84,20,aa */
	{0xa0, 0x00, ZC3XX_R190_EXPOSURELIMITHIGH},	/* 01,90,00,cc, */
	{0xa0, 0x00, ZC3XX_R191_EXPOSURELIMITMID},
	{0xa0, 0x48, ZC3XX_R192_EXPOSURELIMITLOW},
	{0xa0, 0x00, ZC3XX_R195_ANTIFLICKERHIGH},	/* 01,95,00,cc, */
	{0xa0, 0x00, ZC3XX_R196_ANTIFLICKERMID},	/* 01,96,00,cc, */
	{0xa0, 0x10, ZC3XX_R197_ANTIFLICKERLOW},	/* 01,97,10,cc, */
	{0xa0, 0x0e, ZC3XX_R18C_AEFREEZE},		/* 01,8c,0e,cc, */
	{0xa0, 0x15, ZC3XX_R18F_AEUNFREEZE},		/* 01,8f,15,cc, */
	{0xa0, 0x62, ZC3XX_R01D_HSYNC_0},		/* 00,1d,62,cc, */
	{0xa0, 0x90, ZC3XX_R01E_HSYNC_1},		/* 00,1e,90,cc, */
	{0xa0, 0xc8, ZC3XX_R01F_HSYNC_2},		/* 00,1f,c8,cc, */
	{0xa0, 0xff, ZC3XX_R020_HSYNC_3},		/* 00,20,ff,cc, */
	{0xa0, 0x58, ZC3XX_R11D_GLOBALGAIN},		/* 01,1d,58,cc, */
	{0xa0, 0x03, ZC3XX_R180_AUTOCORRECTENABLE},	/* 01,80,03,cc */
	{}
};
