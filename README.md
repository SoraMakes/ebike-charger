# USB-PD Charger

Firmware for an ESP8266 D1 mini based USB-PD charger. It negotiates the highest
available USB-PD voltage, controls the regulator's output current through an
MCP4018 digipot, and limits power according to temperature.

## Build and upload

Install [PlatformIO](https://platformio.org/), connect the D1 mini, then run:

```text
pio run
pio run -t upload
pio device monitor
```

The serial monitor uses **115200 baud**. The required libraries are declared in
`platformio.ini` and are installed automatically by PlatformIO.

## TODOs:

- Spannungswandler Steuerung auf positive side controlled umstellen. Mit negeative side kann nicht der ganze Wandler
abgeschaltet werden, sondern nur nach dem Ausgang des Wandlers, da über den digipot GND des VR mit dem allgemeinen GND
verbunden ist. Dadurch wird ein low side MOSFET vor dem VR umgangen
