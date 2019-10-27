/*
*********************************************************************************************************
*
*	模块名称 : H7-TOOL扩展IO板驱动
*	文件名称 : bsp_ext_io.c
*	版    本 : V1.0
*	说    明 : 扩展IO板包括DAC8563两路16bitDAC, AD7606(正负5V)8路16bit ADC, 24个双刀双掷继电器，16路光耦隔离DI
*
*	修改记录 :
*		版本号  日期        作者     说明
*		V1.0    2019-10-16  armfly  正式发布
*
*	Copyright (C), 2015-2020, 安富莱电子 www.armfly.com
*
*********************************************************************************************************
*/

#include "bsp.h"
#include "param.h"

/*
	H7-TOOL扩展板 输出端口定义
	
	D9 --- 595_LCK 所存线
	D4 --- 595_SCK 时钟线
	D2 --- 595_SDI 数据线输入
	
	D5 --- 165_SCK 时钟线
	D7 --- 165_LCK  片选锁存
	D8 --- 165_SDO 数据线输出
	
	D6 --- AD7606_BUSY   转换结束标志
	D3 --- AD7606_MISO   数据线
	D5 --- AD7606_SCK    时钟
	D7 --- AD7606_CS     片选
	D0 --- AD7606_CVA	 启动转换
	
	D4 --- DAC8563_SCK   时钟线
	D2 --- DAC8563_MOSI  数据线
	D1 --- DAC8563_CS    片选
*/

/* 定义74HC595芯片的IO */
#define EX595_LCK_0() EIO_SetOutLevel(9, 0)
#define EX595_LCK_1() EIO_SetOutLevel(9, 1)
#define EX595_SCLK_0() EIO_SetOutLevel(4, 0)
#define EX595_SCLK_1() EIO_SetOutLevel(4, 1)
#define EX595_SDI_0() EIO_SetOutLevel(2, 0)
#define EX595_SDI_1() EIO_SetOutLevel(2, 1)

/* 定义74HC165芯片的IO */
#define EX165_LCK_0() EIO_SetOutLevel(7, 0)
#define EX165_LCK_1() EIO_SetOutLevel(7, 1)
#define EX165_SCK_0() EIO_SetOutLevel(5, 0)
#define EX165_SCK_1() EIO_SetOutLevel(5, 1)
#define EX165_SDI_IS_HIGH() (EIO_GetInputLevel(8)) /* (SDI == 1) */
#define EX165_CHIP_NUM 2

/*
	DAC8562基本特性:
	1、供电2.7 - 5V;  【本例使用3.3V】
	4、参考电压2.5V，使用内部参考

	对SPI的时钟速度要求: 高达50MHz， 速度很快.
	SCLK下降沿读取数据, 每次传送24bit数据， 高位先传
*/
/* SYNC, 也就是CS片选 */
#define DAC_CS_0() EIO_SetOutLevel(1, 0)
#define DAC_CS_1() EIO_SetOutLevel(1, 1)

#define DAC_SCK_0() EIO_SetOutLevel(4, 0)
#define DAC_SCK_1() EIO_SetOutLevel(4, 1)

#define DAC_MOSI_0() EIO_SetOutLevel(2, 0)
#define DAC_MOSI_1() EIO_SetOutLevel(2, 1)

/* AD7606 */
#define ADC_CS_0() EIO_SetOutLevel(7, 0)
#define ADC_CS_1() EIO_SetOutLevel(7, 1)

#define ADC_SCK_0() EIO_SetOutLevel(5, 0)
#define ADC_SCK_1() EIO_SetOutLevel(5, 1)

#define ADC_MISO_0() EIO_SetOutLevel(3, 0)
#define ADC_MOSI_1() EIO_SetOutLevel(3, 1)

#define ADC_CVA_0() EIO_SetOutLevel(0, 0)
#define ADC_CVA_1() EIO_SetOutLevel(0, 1)

#define ADC_BUSY_IS_HIGH() (EIO_GetInputLevel(6)) /* (BUSY == 1) */
#define ADC_MISO_IS_HIGH() (EIO_GetInputLevel(3)) /* (MISO == 1) */

static uint8_t s_ScanTask_Enable = 0;

static uint8_t s_hc165_buf[EX165_CHIP_NUM]; /* 缓存 595输出状态 */
static uint32_t s_uiEX595;

static int16_t s_AD7606Buf[8];

static void EX165_ScanData(void);

static void bsp_InitDAC8562(void);
static void DAC8562_WriteCmd(uint32_t _cmd);

static void AD7606_ReadNowAdc(void);
static void AD7606_StartConvst(void);

static void EX595_LockData(void);

/*
*********************************************************************************************************
*	函 数 名: EXIO_Start
*	功能说明: 配置扩展IO板相关的GPIO。
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
void EXIO_Start(void)
{
	EIO_D0_Config(ES_GPIO_OUT);
	EIO_D1_Config(ES_GPIO_OUT);
	EIO_D2_Config(ES_GPIO_OUT);
	EIO_D3_Config(ES_GPIO_IN);
	EIO_D4_Config(ES_GPIO_OUT);
	EIO_D5_Config(ES_GPIO_OUT);
	EIO_D6_Config(ES_GPIO_IN);
	EIO_D7_Config(ES_GPIO_OUT);
	EIO_D8_Config(ES_GPIO_IN);
	EIO_D9_Config(ES_GPIO_OUT);

	memset(s_hc165_buf, EX165_CHIP_NUM, 0);

	s_uiEX595 = 0;

	EX595_WritePort(0);

	bsp_InitDAC8562();
	DAC8562_SetDacData(0, 32768);
	DAC8562_SetDacData(1, 32768);

	s_ScanTask_Enable = 1;
}

/*
*********************************************************************************************************
*	函 数 名: EXIO_Stop
*	功能说明: 配置扩展IO板相关的GPIO。
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
void EXIO_Stop(void)
{
	EIO_D0_Config(ES_GPIO_IN);
	EIO_D1_Config(ES_GPIO_IN);
	EIO_D2_Config(ES_GPIO_IN);
	EIO_D3_Config(ES_GPIO_IN);
	EIO_D4_Config(ES_GPIO_IN);
	EIO_D5_Config(ES_GPIO_IN);
	EIO_D6_Config(ES_GPIO_IN);
	EIO_D7_Config(ES_GPIO_IN);
	EIO_D8_Config(ES_GPIO_IN);
	EIO_D9_Config(ES_GPIO_IN);

	memset(s_hc165_buf, EX165_CHIP_NUM, 0);

	EX595_WritePort(0);

	s_ScanTask_Enable = 0;
}

/*
*********************************************************************************************************
*	函 数 名: EXIO_ScanTask
*	功能说明: 扫描任务。读取AD7606和DI输入。插入bsp_Idle()执行。10ms执行一次
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
void EXIO_ScanTask(void)
{
	static int32_t s_last_time = 0;

	if (s_ScanTask_Enable == 1)
	{
		if (bsp_CheckRunTime(s_last_time) >= 30)
		{
			EX165_ScanData();

			AD7606_ReadNowAdc();
			AD7606_StartConvst(); /* 启动下次转换 */
			s_last_time = bsp_GetRunTime();
		}
	}
}

/*
*********************************************************************************************************
	74HC595
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*	函 数 名: EX595_Delay
*	功能说明: 延迟
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static void EX595_Delay(void)
{
	uint8_t i;

	for (i = 0; i < 5; i++)
		;
}

/*
*********************************************************************************************************
*	函 数 名: EX595_WritePin
*	功能说明: 设置单个输出状态
*	形    参: _pin : 0-23
*			  _value : 0,1
*	返 回 值: 无
*********************************************************************************************************
*/
void EX595_WritePin(uint8_t _pin, uint8_t _value)
{
	if (_value == 1)
	{
		s_uiEX595 |= (1U << _pin);
	}
	else
	{
		s_uiEX595 &= ~(1U << _pin);
	}
	EX595_LockData();
}

/*
*********************************************************************************************************
*	函 数 名: EX595_WritePort
*	功能说明: 设置GPIO输出状态
*	形    参: _pin : 0-7
*			  _value : 0,1
*	返 回 值: 无
*********************************************************************************************************
*/
void EX595_WritePort(uint32_t _value)
{
	s_uiEX595 = _value;
	EX595_LockData();
}

/*
*********************************************************************************************************
*	函 数 名: HC595_LockData
*	功能说明: 锁存数据
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static void EX595_LockData(void)
{
	uint8_t i;
	uint32_t dd;

	dd = s_uiEX595;
	for (i = 0; i < 24; i++)
	{
		if (dd & 0x800000)
		{
			EX595_SDI_1();
		}
		else
		{
			EX595_SDI_0();
		}

		EX595_Delay();
		EX595_SCLK_0();
		;
		EX595_Delay();
		EX595_SCLK_1();
		dd <<= 1;
	}

	EX595_Delay();
	EX595_LCK_1();
	;
	EX595_Delay();
	EX595_Delay();
	EX595_Delay();
	EX595_LCK_0();
}

/*
*********************************************************************************************************
	74HC165
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*	函 数 名: HC165_ScanData
*	功能说明: 扫描输入IO.  每ms执行一次
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
#define EX165_DELAY 5 /* V2.29以后由3改为5， 否则不稳定。 实测SCK时钟1MHz， 总时长40us */
static void EX165_ScanData(void)
{
	uint8_t i, j, m;

	EX165_LCK_0(); /* 下降沿时，IO状态装在到寄存器 */
	for (m = 0; m < EX165_DELAY; m++)
		;
	EX165_LCK_1(); /* 高电平，表示数据移位 */

	for (i = 0; i < EX165_CHIP_NUM; i++)
	{
		s_hc165_buf[i] = 0;
		for (j = 0; j < 8; j++)
		{
			for (m = 0; m < EX165_DELAY; m++)
				;
			EX165_SCK_0();
			for (m = 0; m < EX165_DELAY; m++)
				;
			if (EX165_SDI_IS_HIGH())
			{
				s_hc165_buf[i] |= (0x80 >> j);
			}
			EX165_SCK_1();
		}
	}
}

/*
*********************************************************************************************************
*	函 数 名: HC165_GetPin
*	功能说明: 读取管脚状态
*	形    参: _bit: pin序号 0-23
*	返 回 值: 输出电平 0或1
*********************************************************************************************************
*/
uint8_t EX165_GetPin(uint8_t _pin)
{
	if (s_hc165_buf[_pin / 8] & (1 << (_pin % 8)))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

/*
*********************************************************************************************************
	DAC8563
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*	函 数 名: bsp_InitDAC8562
*	功能说明: 配置GPIO并初始化DAC8562寄存器
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static void bsp_InitDAC8562(void)
{
	/* 初始化DAC8562寄存器 */
	{
		/* Power up DAC-A and DAC-B */
		DAC8562_WriteCmd((4 << 19) | (0 << 16) | (3 << 0));

		/* LDAC pin inactive for DAC-B and DAC-A  不使用LDAC引脚更新数据 */
		DAC8562_WriteCmd((6 << 19) | (0 << 16) | (3 << 0));

		/* 复位2个DAC到中间值, 输出0V */
		DAC8562_SetDacData(0, 32767);
		DAC8562_SetDacData(1, 32767);

		/* 选择内部参考并复位2个DAC的增益=2 （复位时，内部参考是禁止的) */
		DAC8562_WriteCmd((7 << 19) | (0 << 16) | (1 << 0));
	}
}

/*
*********************************************************************************************************
*	函 数 名: DAC8562_SetCS(0)
*	功能说明: 设置CS。 用于运行中SPI共享。
*	形    参: 无
	返 回 值: 无
*********************************************************************************************************
*/
static void DAC8562_SetCS(uint8_t _level)
{
	if (_level == 0)
	{
		DAC_SCK_0();
		DAC_CS_0();
	}
	else
	{
		DAC_CS_1();
	}
}

/*
*********************************************************************************************************
*	函 数 名: DAC8562_spiWrite0
*	功能说明: 向SPI总线发送8个bit数据。
*	形    参: _cmd : 数据
*	返 回 值: 无
*********************************************************************************************************
*/
static void DAC8562_spiWrite0(uint8_t _ucByte)
{
	uint8_t i;

	for (i = 0; i < 8; i++)
	{
		if (_ucByte & 0x80)
		{
			DAC_MOSI_1();
		}
		else
		{
			DAC_MOSI_0();
		}
		DAC_SCK_1();
		_ucByte <<= 1;
		DAC_SCK_0();
	}
}

/*
*********************************************************************************************************
*	函 数 名: DAC8562_WriteCmd
*	功能说明: 向SPI总线发送24个bit数据。
*	形    参: _cmd : 数据
*	返 回 值: 无
*********************************************************************************************************
*/
static void DAC8562_WriteCmd(uint32_t _cmd)
{
	DAC8562_SetCS(0);

	/*　DAC8562 SCLK时钟高达50M，因此可以不延迟 */

	DAC8562_spiWrite0(_cmd >> 16);
	DAC8562_spiWrite0(_cmd >> 8);
	DAC8562_spiWrite0(_cmd);

	DAC8562_SetCS(1);
}

/*
*********************************************************************************************************
*	函 数 名: DAC8562_SetDacData
*	功能说明: 设置DAC输出，并立即更新。
*	形    参: _ch, 通道, 0 , 1
*		     _data : 数据
*	返 回 值: 无
*********************************************************************************************************
*/
void DAC8562_SetDacData(uint8_t _ch, uint16_t _dac)
{
	if (_ch == 0)
	{
		/* Write to DAC-A input register and update DAC-A; */
		DAC8562_WriteCmd((3 << 19) | (0 << 16) | (_dac << 0));
	}
	else if (_ch == 1)
	{
		/* Write to DAC-B input register and update DAC-A; */
		DAC8562_WriteCmd((3 << 19) | (1 << 16) | (_dac << 0));
	}
}

/*
*********************************************************************************************************
	AD7606
*********************************************************************************************************
*/

/*
*********************************************************************************************************
*	函 数 名: TM7705_SetCS(0)
*	功能说明: 设置CS。 用于运行中SPI共享。
*	形    参: 无
	返 回 值: 无
*********************************************************************************************************
*/
static void AD7606_SetCS(uint8_t _level)
{
	if (_level == 0)
	{
		ADC_SCK_1();
		ADC_CS_0();
	}
	else
	{
		ADC_CS_1();
	}
}

/*
*********************************************************************************************************
*	函 数 名: AD7606_StartConvst
*	功能说明: 启动1次ADC转换
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static void AD7606_StartConvst(void)
{
	/* page 7：  CONVST 高电平脉冲宽度和低电平脉冲宽度最短 25ns */
	/* CONVST平时为高 */
	ADC_CVA_0();
	ADC_CVA_0();
	ADC_CVA_0();

	ADC_CVA_1();
}

/*
*********************************************************************************************************
*	函 数 名: AD7606_spiDelay
*	功能说明: 时序延迟
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static void AD7606_spiDelay(void)
{
	uint8_t i;

	for (i = 0; i < 5; i++)
		;
}

/*
*********************************************************************************************************
*	函 数 名: AD7606_spiRead1
*	功能说明: 从SPI总线接收8个bit数据。  SCK上升沿采集数据, SCK空闲时为高电平
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static uint8_t AD7606_spiRead1(void)
{
	uint8_t i;
	uint8_t read = 0;

	for (i = 0; i < 8; i++)
	{
		ADC_SCK_0();
		AD7606_spiDelay();
		read = read << 1;
		if (ADC_MISO_IS_HIGH())
		{
			read++;
		}
		ADC_SCK_1();
		AD7606_spiDelay();
	}
	return read;
}

/*
*********************************************************************************************************
*	函 数 名: AD7606_ReadNowAdc
*	功能说明: 读取8路采样结果。结果存储在全局变量 g_tAD7606。  8080总线访问或SPI总线访问。
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
static void AD7606_ReadNowAdc(void)
{
	uint8_t temp;

	AD7606_SetCS(0); /* SPI片选 = 0 */

	temp = AD7606_spiRead1();
	s_AD7606Buf[0] = temp * 256 + AD7606_spiRead1(); /* 读CH1数据 */
	temp = AD7606_spiRead1();
	s_AD7606Buf[1] = temp * 256 + AD7606_spiRead1(); /* 读CH1数据 */
	temp = AD7606_spiRead1();
	s_AD7606Buf[2] = temp * 256 + AD7606_spiRead1(); /* 读CH1数据 */
	temp = AD7606_spiRead1();
	s_AD7606Buf[3] = temp * 256 + AD7606_spiRead1(); /* 读CH1数据 */
	temp = AD7606_spiRead1();
	s_AD7606Buf[4] = temp * 256 + AD7606_spiRead1(); /* 读CH1数据 */
	temp = AD7606_spiRead1();
	s_AD7606Buf[5] = temp * 256 + AD7606_spiRead1(); /* 读CH1数据 */
	temp = AD7606_spiRead1();
	s_AD7606Buf[6] = temp * 256 + AD7606_spiRead1(); /* 读CH1数据 */
	temp = AD7606_spiRead1();
	s_AD7606Buf[7] = temp * 256 + AD7606_spiRead1(); /* 读CH1数据 */

	AD7606_SetCS(1); /* SPI片选 = 1 */
}

/*
*********************************************************************************************************
*	函 数 名: AD7606_ReadAdc
*	功能说明: 读取指定通道的ADC值
*	形    参: 无
*	返 回 值: 无
*********************************************************************************************************
*/
int16_t AD7606_ReadAdc(uint8_t _ch)
{
	if (_ch < 8)
	{
		return s_AD7606Buf[_ch];
	}
	else
	{
		return 0;
	}
}

/***************************** 安富莱电子 www.armfly.com (END OF FILE) *********************************/

/***************************** 安富莱电子 www.armfly.com (END OF FILE) *********************************/
