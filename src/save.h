
#include <Arduino.h>
#include <EEPROM.h>

template <class T>
int EEPROM_readAnything(int ee, T &value)
{
    byte *p = (byte *)(void *)&value;
    unsigned int i;
    for (i = 0; i < sizeof(value); i++)
        *p++ = EEPROM.read(ee++);
    return i;
}

template <class T>
int EEPROM_writeAnything(int ee, const T &value)
{
    T readValue;
    EEPROM_readAnything(ee, readValue);

    if (readValue != value)
    {
        const byte *p = (const byte *)(const void *)&value;
        unsigned int i;
        for (i = 0; i < sizeof(value); i++)
            EEPROM.write(ee++, *p++);

        return i;
    }

    return 0;
}

//void EEPROMWritelong(int address, unsigned long value);
//unsigned long EEPROMReadlong(long address);