# USB-PD Charger

Firmware for a TTGO T-Display (ESP32) based USB-PD charger. It selects the
highest eligible fixed PDO within its configured operating range, controls the
regulator's output current through an MCP4018 digipot, limits power according to
temperature, and shows metrics on the onboard display.

## Build and upload

Install [PlatformIO](https://platformio.org/), connect the board, then run:

```text
pio run
pio run -t upload
pio device monitor
```

The serial monitor uses **115200 baud**. The required libraries are declared in
`platformio.ini` and are installed automatically by PlatformIO.

## Pinout (TTGO T-Display)

| Function | GPIO | Notes |
|---|---|---|
| I2C SDA | 21 | Shared bus: AP33772S, BMP280, MCP4018, INA238 |
| I2C SCL | 22 | Shared bus |
| Voltage divider (ADC) | 33 | ADC1 input; measures charger output voltage |
| PD INT | 13 | PD trigger board (RotoPD Pro) interrupt output |
| Fan PWM | 12 | LEDC PWM output |
| TFT MOSI | 19 | Display SPI data |
| TFT SCLK | 18 | Display SPI clock |
| TFT CS | 5 | Display chip select |
| TFT DC | 16 | Display data/command |
| TFT RST | 23 | Display reset |
| TFT BL | 4 | Display backlight |

## TODOs:

- Spannungswandler Steuerung auf positive side controlled umstellen. Mit negeative side kann nicht der ganze Wandler
abgeschaltet werden, sondern nur nach dem Ausgang des Wandlers, da über den digipot GND des VR mit dem allgemeinen GND
verbunden ist. Dadurch wird ein low side MOSFET vor dem VR umgangen


# Calibration readings

Voltage pairs are raw ADC counts from GPIO33's 219 kΩ / 4.86 kΩ divider and
the corresponding measured output voltage. Current pairs are MCP4018 digipot
steps and the corresponding measured output current.

## Output voltage

| ADC count | Measured output voltage (V) |
|---:|---:|
| 3538 | 40.8 |
| 4000 | 45.7 |
| 3128 | 36.5 |
| 2510 | 29.9 |

## Output current

| Digipot step | Measured output current (A) |
|---:|---:|
| 0 | 0.100 |
| 1 | 0.156 |
| 2 | 0.216 |
| 3 | 0.276 |
| 4 | 0.335 |
| 5 | 0.394 |
| 10 | 0.696 |
| 15 | 0.996 |
| 20 | 1.300 |
| 25 | 1.599 |
