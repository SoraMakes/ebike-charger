# Komponentenliste

| Komponente                          | Stromversorgung          | ESP-Pins                          |
|-------------------------------------|--------------------------|-----------------------------------|
| **ESP32 (TTGO T-Display)**          | 5V                       | –                                 |
| **5V-Wandler**                      | PD-Eingang (VBUS)        | –                                 |
| **HUSB238**                         | -                        | I2C: SDA=21, SCL=22               |
| **VR (Boost-Konverter)**            | PD-Eingang (VBUS)        | -                                 |
| **VR: Digipot (MCP4018)**           | 3V3                      | I2C: SDA=21, SCL=22               |
| **42V Out: MOSFET**                 | PD-Eingang (VBUS)        | Enable=13                         |
| **Spannungsteiler**                 | 42V-Ausgang              | ADC=33                            |
| **AHT10**                           | 3,3V                     | I2C: SDA=21, SCL=22               |
| **Lüfter (MOSFET)**                 | 5V                       | PWM=12 (LEDC)                     |
| **INA219** *(deaktiviert)*          | PD-Eingang / 3V3         | I2C: SDA=21, SCL=22               |
| **42V-Ausgang**                     | 42V MOSFET               | –                                 |
| **5V-Ausgang**                      | 5V                       | –                                 |

## Ausgänge

- **42V-Ausgang** (Boost-Konverter)
- **5V-Ausgang** (5V-Wandler)

## I2C-Adressen

| Komponente | Adresse |
|------------|---------|
| HUSB238    | 0x38    |
| AHT10      | 0x38    |
| MCP4018    | 0x2F    |
| INA219     | 0x40    |

## Hinweise

- Der **INA219** (`PowerMonitor`) ist aktuell im Code auskommentiert.
- Die **Anzeigewerte** auf dem Display sind noch Dummy-Werte.
- Die **ESP8266 D1 mini**-Konfiguration ist als Kommentar in `platformio.ini` erhalten (frühere Hardware-Variante).
