/*
 * max_7219.h
 *
 *  Created on: 02-Jul-2026
 *      Author: user
 */


#ifndef MAX7219_H
#define MAX7219_H

#include "main.h"

void MAX7219_Init(void);
void MAX7219_Send(uint8_t reg, uint8_t data);
void MAX7219_DisplayNumber(uint32_t num);
void MAX7219_DisplayChar(uint8_t digit, uint8_t pattern);

void MAX7219_DisplayCPU(uint16_t cpu);
void MAX7219_DisplayGPU(uint16_t gpu);
void MAX7219_DisplayTemp(uint16_t temp);


#endif
