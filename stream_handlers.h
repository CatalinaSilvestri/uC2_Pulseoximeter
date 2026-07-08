/* 
 * File:   stream_handlers.h
 * Author: cat
 *
 * Created on June 18, 2026, 2:06 PM
 */

#ifndef STREAM_HANDLERS_H
#define	STREAM_HANDLERS_H

#include <stdint.h>

#include "mcc_generated_files/system/system.h"
#include "mcc_generated_files/i2c_host/twi0.h"

void logInt16ToDataStream(int16_t value);
void logToDataStream(uint8_t* raw);

void logFinalSignalToDataStream(int16_t value);

#endif	/* STREAM_HANDLERS_H */

