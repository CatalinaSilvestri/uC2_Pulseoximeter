#ifndef KEYPAD_START_H
#define KEYPAD_START_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Initialisiert die ADC-Keypad-Logik.
 * Muss nach SYSTEM_Initialize() aufgerufen werden.
 */
void keypad_start_init(void);

/*
 * Muss regelmäßig in der while(1) aufgerufen werden.
 * Wertet neue ADC-Werte aus und erkennt RIGHT als Starttaste.
 */
void keypad_start_task(void);

/*
 * Gibt true zurück, wenn RIGHT neu gedrückt wurde.
 * Das Start-Flag wird dabei automatisch zurückgesetzt.
 */
bool keypad_start_requested(void);

/*
 * Optional für Debug.
 * Gibt den zuletzt gemessenen ADC-Wert zurück.
 */
uint16_t keypad_start_get_adc_value(void);

#endif /* KEYPAD_START_H */
