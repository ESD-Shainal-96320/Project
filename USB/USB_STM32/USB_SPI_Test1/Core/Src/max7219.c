#include "max7219.h"
//#include "spi.h"
extern SPI_HandleTypeDef hspi1;

#define MAX7219_CS_LOW() \
HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_RESET)

#define MAX7219_CS_HIGH() \
HAL_GPIO_WritePin(GPIOE, GPIO_PIN_3, GPIO_PIN_SET)

#define SEG_0 0x7E
#define SEG_1 0x30
#define SEG_2 0x6D
#define SEG_3 0x79
#define SEG_4 0x33
#define SEG_5 0x5B
#define SEG_6 0x5F
#define SEG_7 0x70
#define SEG_8 0x7F
#define SEG_9 0x7B

#define SEG_C 0x4E
#define SEG_G 0x5E
#define SEG_T 0x0F
#define SEG_P 0x67
#define SEG_U 0x3E
#define SEG_BLANK 0x00

const uint8_t digitTable[10] =
{
    SEG_0,
    SEG_1,
    SEG_2,
    SEG_3,
    SEG_4,
    SEG_5,
    SEG_6,
    SEG_7,
    SEG_8,
    SEG_9
};

void MAX7219_Send(uint8_t reg, uint8_t data)
{
    uint8_t tx[2];

    tx[0] = reg;
    tx[1] = data;

    MAX7219_CS_LOW();
    HAL_SPI_Transmit(&hspi1, tx, 2, 100);
    MAX7219_CS_HIGH();
}

void MAX7219_Init()
{
    MAX7219_Send(0x0C,0x01);
    MAX7219_Send(0x09,0x00);
    MAX7219_Send(0x0A,0x08);
    MAX7219_Send(0x0B,0x07);
    MAX7219_Send(0x0F,0x00);
}

void MAX7219_DisplayCPU(uint16_t cpu)
{
    MAX7219_Send(1, SEG_C);
    MAX7219_Send(2, digitTable[(cpu/100)%10]);
    MAX7219_Send(3, digitTable[(cpu/10)%10]);
    MAX7219_Send(4, digitTable[cpu%10]);

}
void MAX7219_DisplayGPU(uint16_t gpu)
{
    MAX7219_Send(5, SEG_G);
    MAX7219_Send(6, digitTable[(gpu/100)%10]);
    MAX7219_Send(7, digitTable[(gpu/10)%10]);
    MAX7219_Send(8, digitTable[gpu%10]);

}

void MAX7219_DisplayTemp(uint16_t temp)
{
    MAX7219_Send(5, SEG_T);
    MAX7219_Send(6, digitTable[(temp/100)%10]);
    MAX7219_Send(7, digitTable[(temp/10)%10]);
    MAX7219_Send(8, digitTable[temp%10]);

}

void MAX7219_DisplayChar(uint8_t digit, uint8_t pattern)
{
    MAX7219_Send(digit, pattern);
}

void MAX7219_DisplayNumber(uint32_t num)
{
    MAX7219_Send(8,(num/1)%10);
    MAX7219_Send(7,(num/10)%10);
    MAX7219_Send(6,(num/100)%10);
    MAX7219_Send(5,(num/1000)%10);
    MAX7219_Send(4,(num/10000)%10);
    MAX7219_Send(3,(num/100000)%10);
    MAX7219_Send(2,(num/1000000)%10);
    MAX7219_Send(1,(num/10000000)%10);
}
