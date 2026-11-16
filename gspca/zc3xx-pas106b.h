/* SPDX-License-Identifier: GPL-2.0-or-later */
/* extracted verbatim from linux drivers/media/usb/gspca/zc3xx.c (pas106b tables) */
static const struct usb_action pas106b_Initial_com[] = {
/* Sream and Sensor specific */
	{0xa1, 0x01, 0x0010},	/* CMOSSensorSelect */
/* System */
	{0xa0, 0x01, ZC3XX_R000_SYSTEMCONTROL},	/* SystemControl */
	{0xa0, 0x01, ZC3XX_R000_SYSTEMCONTROL},	/* SystemControl */
/* Picture size */
	{0xa0, 0x00, ZC3XX_R002_CLOCKSELECT},	/* ClockSelect */
	{0xa0, 0x03, 0x003a},
	{0xa0, 0x0c, 0x003b},
	{0xa0, 0x04, 0x0038},
	{}
};

static const struct usb_action pas106b_InitialScale[] = {	/* 176x144 */
/* JPEG control */
	{0xa0, 0x03, ZC3XX_R008_CLOCKSETTING},
/* Sream and Sensor specific */
	{0xa0, 0x0f, ZC3XX_R010_CMOSSENSORSELECT},
/* Picture size */
	{0xa0, 0x00, ZC3XX_R003_FRAMEWIDTHHIGH},
	{0xa0, 0xb0, ZC3XX_R004_FRAMEWIDTHLOW},
	{0xa0, 0x00, ZC3XX_R005_FRAMEHEIGHTHIGH},
	{0xa0, 0x90, ZC3XX_R006_FRAMEHEIGHTLOW},
/* System */
	{0xa0, 0x01, ZC3XX_R001_SYSTEMOPERATING},
/* Sream and Sensor specific */
	{0xa0, 0x03, ZC3XX_R012_VIDEOCONTROLFUNC},
	{0xa0, 0x01, ZC3XX_R012_VIDEOCONTROLFUNC},
/* Sensor Interface */
	{0xa0, 0x08, ZC3XX_R08D_COMPABILITYMODE},
/* Window inside sensor array */
	{0xa0, 0x03, ZC3XX_R09A_WINXSTARTLOW},
	{0xa0, 0x00, ZC3XX_R11A_FIRSTYLOW},
	{0xa0, 0x03, ZC3XX_R11C_FIRSTXLOW},
	{0xa0, 0x28, ZC3XX_R09C_WINHEIGHTLOW},
	{0xa0, 0x68, ZC3XX_R09E_WINWIDTHLOW},
/* Init the sensor */
	{0xaa, 0x02, 0x0004},
	{0xaa, 0x08, 0x0000},
	{0xaa, 0x09, 0x0005},
	{0xaa, 0x0a, 0x0002},
	{0xaa, 0x0b, 0x0002},
	{0xaa, 0x0c, 0x0005},
	{0xaa, 0x0d, 0x0000},
	{0xaa, 0x0e, 0x0002},
	{0xaa, 0x14, 0x0081},
/* Other registers */
	{0xa0, 0x37, ZC3XX_R101_SENSORCORRECTION},
/* Frame retrieving */
	{0xa0, 0x00, ZC3XX_R019_AUTOADJUSTFPS},
/* Gains */
	{0xa0, 0xa0, ZC3XX_R1A8_DIGITALGAIN},
/* Unknown */
	{0xa0, 0x00, 0x01ad},
/* Sharpness */
	{0xa0, 0x03, ZC3XX_R1C5_SHARPNESSMODE},
	{0xa0, 0x13, ZC3XX_R1CB_SHARPNESS05},
/* Other registers */
	{0xa0, 0x0d, ZC3XX_R100_OPERATIONMODE},
/* Auto exposure and white balance */
	{0xa0, 0x06, ZC3XX_R189_AWBSTATUS},
/*Dead pixels */
	{0xa0, 0x08, ZC3XX_R250_DEADPIXELSMODE},
/* EEPROM */
	{0xa0, 0x08, ZC3XX_R301_EEPROMACCESS},
/* JPEG control */
	{0xa0, 0x03, ZC3XX_R008_CLOCKSETTING},
	{0xa0, 0x08, ZC3XX_R1C6_SHARPNESS00},
	{0xa0, 0x0f, ZC3XX_R1CB_SHARPNESS05},
/* Other registers */
	{0xa0, 0x0d, ZC3XX_R100_OPERATIONMODE},
/* Auto exposure and white balance */
	{0xa0, 0x06, ZC3XX_R189_AWBSTATUS},
/*Dead pixels */
	{0xa0, 0x08, ZC3XX_R250_DEADPIXELSMODE},
/* EEPROM */
	{0xa0, 0x08, ZC3XX_R301_EEPROMACCESS},
/* JPEG control */
	{0xa0, 0x03, ZC3XX_R008_CLOCKSETTING},
	{0xa0, 0x08, ZC3XX_R1C6_SHARPNESS00},
	{0xa0, 0x0f, ZC3XX_R1CB_SHARPNESS05},

	{0xa0, 0x58, ZC3XX_R10A_RGB00},	/* matrix */
	{0xa0, 0xf4, ZC3XX_R10B_RGB01},
	{0xa0, 0xf4, ZC3XX_R10C_RGB02},
	{0xa0, 0xf4, ZC3XX_R10D_RGB10},
	{0xa0, 0x58, ZC3XX_R10E_RGB11},
	{0xa0, 0xf4, ZC3XX_R10F_RGB12},
	{0xa0, 0xf4, ZC3XX_R110_RGB20},
	{0xa0, 0xf4, ZC3XX_R111_RGB21},
	{0xa0, 0x58, ZC3XX_R112_RGB22},
/* Auto correction */
	{0xa0, 0x03, ZC3XX_R181_WINXSTART},
	{0xa0, 0x08, ZC3XX_R182_WINXWIDTH},
	{0xa0, 0x16, ZC3XX_R183_WINXCENTER},
	{0xa0, 0x03, ZC3XX_R184_WINYSTART},
	{0xa0, 0x05, ZC3XX_R185_WINYWIDTH},
	{0xa0, 0x14, ZC3XX_R186_WINYCENTER},
	{0xa0, 0x00, ZC3XX_R180_AUTOCORRECTENABLE},
/* Auto exposure and white balance */
	{0xa0, 0x00, ZC3XX_R190_EXPOSURELIMITHIGH},
	{0xa0, 0x03, ZC3XX_R191_EXPOSURELIMITMID},
	{0xa0, 0xb1, ZC3XX_R192_EXPOSURELIMITLOW},
	{0xa0, 0x00, ZC3XX_R195_ANTIFLICKERHIGH},
	{0xa0, 0x00, ZC3XX_R196_ANTIFLICKERMID},
	{0xa0, 0x87, ZC3XX_R197_ANTIFLICKERLOW},
	{0xa0, 0x0c, ZC3XX_R18C_AEFREEZE},
	{0xa0, 0x18, ZC3XX_R18F_AEUNFREEZE},
/* sensor on */
	{0xaa, 0x07, 0x00b1},
	{0xaa, 0x05, 0x0003},
	{0xaa, 0x04, 0x0001},
	{0xaa, 0x03, 0x003b},
/* Gains */
	{0xa0, 0x20, ZC3XX_R1A9_DIGITALLIMITDIFF},
	{0xa0, 0x26, ZC3XX_R1AA_DIGITALGAINSTEP},
	{0xa0, 0xa0, ZC3XX_R11D_GLOBALGAIN},
	{0xa0, 0x60, ZC3XX_R11D_GLOBALGAIN},
/* Auto correction */
	{0xa0, 0x40, ZC3XX_R180_AUTOCORRECTENABLE},
	{0xa1, 0x01, 0x0180},				/* AutoCorrectEnable */
	{0xa0, 0x42, ZC3XX_R180_AUTOCORRECTENABLE},
/* Gains */
	{0xa0, 0x40, ZC3XX_R116_RGAIN},
	{0xa0, 0x40, ZC3XX_R117_GGAIN},
	{0xa0, 0x40, ZC3XX_R118_BGAIN},
	{}
};

static const struct usb_action pas106b_Initial[] = {	/* 352x288 */
/* JPEG control */
	{0xa0, 0x03, ZC3XX_R008_CLOCKSETTING},
/* Sream and Sensor specific */
	{0xa0, 0x0f, ZC3XX_R010_CMOSSENSORSELECT},
/* Picture size */
	{0xa0, 0x01, ZC3XX_R003_FRAMEWIDTHHIGH},
	{0xa0, 0x60, ZC3XX_R004_FRAMEWIDTHLOW},
	{0xa0, 0x01, ZC3XX_R005_FRAMEHEIGHTHIGH},
	{0xa0, 0x20, ZC3XX_R006_FRAMEHEIGHTLOW},
/* System */
	{0xa0, 0x01, ZC3XX_R001_SYSTEMOPERATING},
/* Sream and Sensor specific */
	{0xa0, 0x03, ZC3XX_R012_VIDEOCONTROLFUNC},
	{0xa0, 0x01, ZC3XX_R012_VIDEOCONTROLFUNC},
/* Sensor Interface */
	{0xa0, 0x08, ZC3XX_R08D_COMPABILITYMODE},
/* Window inside sensor array */
	{0xa0, 0x03, ZC3XX_R09A_WINXSTARTLOW},
	{0xa0, 0x00, ZC3XX_R11A_FIRSTYLOW},
	{0xa0, 0x03, ZC3XX_R11C_FIRSTXLOW},
	{0xa0, 0x28, ZC3XX_R09C_WINHEIGHTLOW},
	{0xa0, 0x68, ZC3XX_R09E_WINWIDTHLOW},
/* Init the sensor */
	{0xaa, 0x02, 0x0004},
	{0xaa, 0x08, 0x0000},
	{0xaa, 0x09, 0x0005},
	{0xaa, 0x0a, 0x0002},
	{0xaa, 0x0b, 0x0002},
	{0xaa, 0x0c, 0x0005},
	{0xaa, 0x0d, 0x0000},
	{0xaa, 0x0e, 0x0002},
	{0xaa, 0x14, 0x0081},
/* Other registers */
	{0xa0, 0x37, ZC3XX_R101_SENSORCORRECTION},
/* Frame retrieving */
	{0xa0, 0x00, ZC3XX_R019_AUTOADJUSTFPS},
/* Gains */
	{0xa0, 0xa0, ZC3XX_R1A8_DIGITALGAIN},
/* Unknown */
	{0xa0, 0x00, 0x01ad},
/* Sharpness */
	{0xa0, 0x03, ZC3XX_R1C5_SHARPNESSMODE},
	{0xa0, 0x13, ZC3XX_R1CB_SHARPNESS05},
/* Other registers */
	{0xa0, 0x0d, ZC3XX_R100_OPERATIONMODE},
/* Auto exposure and white balance */
	{0xa0, 0x06, ZC3XX_R189_AWBSTATUS},
	{0xa0, 0x80, ZC3XX_R18D_YTARGET},
/*Dead pixels */
	{0xa0, 0x08, ZC3XX_R250_DEADPIXELSMODE},
/* EEPROM */
	{0xa0, 0x08, ZC3XX_R301_EEPROMACCESS},
/* JPEG control */
	{0xa0, 0x03, ZC3XX_R008_CLOCKSETTING},
	{0xa0, 0x08, ZC3XX_R1C6_SHARPNESS00},
	{0xa0, 0x0f, ZC3XX_R1CB_SHARPNESS05},
/* Other registers */
	{0xa0, 0x0d, ZC3XX_R100_OPERATIONMODE},
/* Auto exposure and white balance */
	{0xa0, 0x06, ZC3XX_R189_AWBSTATUS},
/*Dead pixels */
	{0xa0, 0x08, ZC3XX_R250_DEADPIXELSMODE},
/* EEPROM */
	{0xa0, 0x08, ZC3XX_R301_EEPROMACCESS},
/* JPEG control */
	{0xa0, 0x03, ZC3XX_R008_CLOCKSETTING},
	{0xa0, 0x08, ZC3XX_R1C6_SHARPNESS00},
	{0xa0, 0x0f, ZC3XX_R1CB_SHARPNESS05},

	{0xa0, 0x58, ZC3XX_R10A_RGB00},	/* matrix */
	{0xa0, 0xf4, ZC3XX_R10B_RGB01},
	{0xa0, 0xf4, ZC3XX_R10C_RGB02},
	{0xa0, 0xf4, ZC3XX_R10D_RGB10},
	{0xa0, 0x58, ZC3XX_R10E_RGB11},
	{0xa0, 0xf4, ZC3XX_R10F_RGB12},
	{0xa0, 0xf4, ZC3XX_R110_RGB20},
	{0xa0, 0xf4, ZC3XX_R111_RGB21},
	{0xa0, 0x58, ZC3XX_R112_RGB22},
/* Auto correction */
	{0xa0, 0x03, ZC3XX_R181_WINXSTART},
	{0xa0, 0x08, ZC3XX_R182_WINXWIDTH},
	{0xa0, 0x16, ZC3XX_R183_WINXCENTER},
	{0xa0, 0x03, ZC3XX_R184_WINYSTART},
	{0xa0, 0x05, ZC3XX_R185_WINYWIDTH},
	{0xa0, 0x14, ZC3XX_R186_WINYCENTER},
	{0xa0, 0x00, ZC3XX_R180_AUTOCORRECTENABLE},

/* Auto exposure and white balance */
	{0xa0, 0x00, ZC3XX_R190_EXPOSURELIMITHIGH},
	{0xa0, 0x03, ZC3XX_R191_EXPOSURELIMITMID},
	{0xa0, 0xb1, ZC3XX_R192_EXPOSURELIMITLOW},

	{0xa0, 0x00, ZC3XX_R195_ANTIFLICKERHIGH},
	{0xa0, 0x00, ZC3XX_R196_ANTIFLICKERMID},
	{0xa0, 0x87, ZC3XX_R197_ANTIFLICKERLOW},

	{0xa0, 0x10, ZC3XX_R18C_AEFREEZE},
	{0xa0, 0x20, ZC3XX_R18F_AEUNFREEZE},
/* sensor on */
	{0xaa, 0x07, 0x00b1},
	{0xaa, 0x05, 0x0003},
	{0xaa, 0x04, 0x0001},
	{0xaa, 0x03, 0x003b},
/* Gains */
	{0xa0, 0x20, ZC3XX_R1A9_DIGITALLIMITDIFF},
	{0xa0, 0x26, ZC3XX_R1AA_DIGITALGAINSTEP},
	{0xa0, 0x60, ZC3XX_R11D_GLOBALGAIN},
/* Auto correction */
	{0xa0, 0x40, ZC3XX_R180_AUTOCORRECTENABLE},
	{0xa1, 0x01, 0x0180},				/* AutoCorrectEnable */
	{0xa0, 0x42, ZC3XX_R180_AUTOCORRECTENABLE},
/* Gains */
	{0xa0, 0x40, ZC3XX_R116_RGAIN},
	{0xa0, 0x40, ZC3XX_R117_GGAIN},
	{0xa0, 0x40, ZC3XX_R118_BGAIN},

	{0xa0, 0x00, 0x0007},			/* AutoCorrectEnable */
	{0xa0, 0xff, ZC3XX_R018_FRAMELOST},	/* Frame adjust */
	{}
};
static const struct usb_action pas106b_50HZ[] = {
	{0xa0, 0x00, ZC3XX_R190_EXPOSURELIMITHIGH}, /* 01,90,00,cc */
	{0xa0, 0x06, ZC3XX_R191_EXPOSURELIMITMID}, /* 01,91,06,cc */
	{0xa0, 0x54, ZC3XX_R192_EXPOSURELIMITLOW}, /* 01,92,54,cc */
	{0xa0, 0x00, ZC3XX_R195_ANTIFLICKERHIGH}, /* 01,95,00,cc */
	{0xa0, 0x00, ZC3XX_R196_ANTIFLICKERMID}, /* 01,96,00,cc */
	{0xa0, 0x87, ZC3XX_R197_ANTIFLICKERLOW}, /* 01,97,87,cc */
	{0xa0, 0x10, ZC3XX_R18C_AEFREEZE},	/* 01,8c,10,cc */
	{0xa0, 0x30, ZC3XX_R18F_AEUNFREEZE},	/* 01,8f,30,cc */
	{0xaa, 0x03, 0x0021},			/* 00,03,21,aa */
	{0xaa, 0x04, 0x000c},			/* 00,04,0c,aa */
	{0xaa, 0x05, 0x0002},			/* 00,05,02,aa */
	{0xaa, 0x07, 0x001c},			/* 00,07,1c,aa */
	{0xa0, 0x04, ZC3XX_R1A9_DIGITALLIMITDIFF}, /* 01,a9,04,cc */
	{}
};
static const struct usb_action pas106b_60HZ[] = {
	{0xa0, 0x00, ZC3XX_R190_EXPOSURELIMITHIGH}, /* 01,90,00,cc */
	{0xa0, 0x06, ZC3XX_R191_EXPOSURELIMITMID}, /* 01,91,06,cc */
	{0xa0, 0x2e, ZC3XX_R192_EXPOSURELIMITLOW}, /* 01,92,2e,cc */
	{0xa0, 0x00, ZC3XX_R195_ANTIFLICKERHIGH}, /* 01,95,00,cc */
	{0xa0, 0x00, ZC3XX_R196_ANTIFLICKERMID}, /* 01,96,00,cc */
	{0xa0, 0x71, ZC3XX_R197_ANTIFLICKERLOW}, /* 01,97,71,cc */
	{0xa0, 0x10, ZC3XX_R18C_AEFREEZE},	/* 01,8c,10,cc */
	{0xa0, 0x30, ZC3XX_R18F_AEUNFREEZE},	/* 01,8f,30,cc */
	{0xaa, 0x03, 0x001c},			/* 00,03,1c,aa */
	{0xaa, 0x04, 0x0004},			/* 00,04,04,aa */
	{0xaa, 0x05, 0x0001},			/* 00,05,01,aa */
	{0xaa, 0x07, 0x00c4},			/* 00,07,c4,aa */
	{0xa0, 0x04, ZC3XX_R1A9_DIGITALLIMITDIFF}, /* 01,a9,04,cc */
	{}
};
static const struct usb_action pas106b_NoFlicker[] = {
	{0xa0, 0x00, ZC3XX_R190_EXPOSURELIMITHIGH}, /* 01,90,00,cc */
	{0xa0, 0x06, ZC3XX_R191_EXPOSURELIMITMID}, /* 01,91,06,cc */
	{0xa0, 0x50, ZC3XX_R192_EXPOSURELIMITLOW}, /* 01,92,50,cc */
	{0xa0, 0x00, ZC3XX_R195_ANTIFLICKERHIGH}, /* 01,95,00,cc */
	{0xa0, 0x00, ZC3XX_R196_ANTIFLICKERMID}, /* 01,96,00,cc */
	{0xa0, 0x10, ZC3XX_R197_ANTIFLICKERLOW}, /* 01,97,10,cc */
	{0xa0, 0x10, ZC3XX_R18C_AEFREEZE},	/* 01,8c,10,cc */
	{0xa0, 0x20, ZC3XX_R18F_AEUNFREEZE},	/* 01,8f,20,cc */
	{0xaa, 0x03, 0x0013},			/* 00,03,13,aa */
	{0xaa, 0x04, 0x0000},			/* 00,04,00,aa */
	{0xaa, 0x05, 0x0001},			/* 00,05,01,aa */
	{0xaa, 0x07, 0x0030},			/* 00,07,30,aa */
	{0xa0, 0x00, ZC3XX_R1A9_DIGITALLIMITDIFF}, /* 01,a9,00,cc */
	{}
};
