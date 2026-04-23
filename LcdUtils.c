
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <xc.h>

#include "lcd.h"


char * strPulse[4];
char * strSaturation[4];

void ReverseString(char* str){
    int start = 0;
    int end = strlen(str) - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

int itoa(int n,char * str, int size){ //vllt benutzen
    int i = 0;
    int rem;

    size--;

    if (n == 0){
        str[0] = '0';
        str[1] = '\0';
        return 1;
    }

    char num[] = "0123456789";

    while (n != 0 && i < size){
        rem = n % 10;
        str[i++] = num[rem];
        n /= 10; // n will take floor of division
    }
    if (i+1 > size && n != 0)
        return 0;
    str[i] = '\0';
    // reverse(str, i);
    return 1;
}


void Uint8ToStr(uint8_t value, char *str)
{
    int i = 0;

    // Handle 0 explicitly
    if (value == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    // Convert digits in reverse order (units first)
    while (value > 0) {

        str[i++] = (value % 10) + '0'; // conversion to char
        value /= 10;
    }

    str[i] = '\0';

    // Reverse string
    ReverseString(str); // could use sizeof()

}

void LCDDisplayLines(char *pulseLine, char *sauerstoffLine, char *puls, char *sauerstoff){
    LCDGoto(0,0);
    LCDPutStr(pulseLine);
    LCDPutStr(puls)
    _delay_ms(100);
    LCDGoto(0,1);
    LCDPutStr(sauerstoffLine);
    LCDPutStr(sauerstoff);
    LCDGoto(5,0);
    LCDGoto(5,1);
}

int main(void)
{
    const char *string1 = "Current pulse is ";
    const char *string2 = " bpm";
    uint8_t value = 52;

    char buffer[32];
    char num[4];

    Uint8ToStr(value, num);

    int i = 0;

    // copy string1
    for (int j = 0; string1[j]; j++)
        buffer[i++] = string1[j];

    // copy number
    for (int j = 0; num[j]; j++)
        buffer[i++] = num[j];

    // copy string2
    for (int j = 0; string2[j]; j++)
        buffer[i++] = string2[j];

    buffer[i] = '\0';

    printf("%s\n", buffer);

    return 0;
}