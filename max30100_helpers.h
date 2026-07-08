#ifndef MAX30100_HELPERS_H
#define MAX30100_HELPERS_H


#include <stdint.h>

/*
 * MAX30100 registers
 */
#define MAX30100_ADDR              0x57

#define REG_FIFO_WR_PTR            0x02
#define REG_OVF_COUNTER            0x03
#define REG_FIFO_RD_PTR            0x04
#define REG_FIFO_DATA              0x05
#define REG_MODE_CONFIG            0x06
#define REG_SPO2_CONFIG            0x07
#define REG_LED_CONFIG             0x09

#define MODE_SPO2                  0x03




#endif