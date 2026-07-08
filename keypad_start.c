#include "keypad_start.h"

#include "mcc_generated_files/adc/adc0.h"

/*
 * RIGHT wurde bei euch sauber mit ca. ADC = 576 gemessen.
 * Bereich bewusst etwas breiter gewählt.
 */
#define KEY_RIGHT_MIN      450U
#define KEY_RIGHT_MAX      700U

static volatile uint16_t keyAdcValue = 0;
static volatile bool keyAdcReady = false;

static bool rightWasPressed = false;
static bool startRequest = false;

static void ADC_Key_Callback(void)
{
    keyAdcValue = ADC0_ConversionResultGet();
    keyAdcReady = true;
}

void keypad_start_init(void)
{
    keyAdcValue = 0;
    keyAdcReady = false;

    rightWasPressed = false;
    startRequest = false;

    ADC0_ConversionDoneCallbackRegister(ADC_Key_Callback);
    ADC0_ConversionStart();
}

void keypad_start_task(void)
{
    uint16_t adc;
    bool rightIsPressed;

    if (!keyAdcReady)
    {
        return;
    }

    keyAdcReady = false;
    adc = keyAdcValue;

    rightIsPressed = ((adc >= KEY_RIGHT_MIN) && (adc <= KEY_RIGHT_MAX));

    /*
     * Flankenerkennung:
     * Start wird nur einmal ausgelöst, wenn RIGHT neu gedrückt wird.
     */
    if (rightIsPressed && !rightWasPressed)
    {
        startRequest = true;
    }

    rightWasPressed = rightIsPressed;

    /*
     * Nächste ADC-Wandlung starten.
     * Der ADC läuft conversion-by-conversion weiter.
     */
    ADC0_ConversionStart();
}

bool keypad_start_requested(void)
{
    if (startRequest)
    {
        startRequest = false;
        return true;
    }

    return false;
}

uint16_t keypad_start_get_adc_value(void)
{
    return keyAdcValue;
}
