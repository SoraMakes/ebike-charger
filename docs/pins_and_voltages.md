gnd
- PD source
- lüfter (mosfet)
- output 42V
- digipot
- aht10/bmp280
- esp

5v
- PD source
- esp
- lüfter (mosfet)
- output

3v3
- esp
- digipot
- aht10/bmp280
- pd trigger

42v (über spannungsteiler-board)
- big VR
- output
- spannungsteiler

PD (direkt)
- PD source
- big VR



---

i2c
- esp
- aht10/bmp280
- pd trigger

---

- 21 (SDA)   -> i2c bus (pd trigger, digipot, aht10/bmp280)
- 22 (SCL)   -> i2c bus (pd trigger, digipot, aht10/bmp280)
- 12 (PWM)   -> lüfter (mosfet), LEDC PWM
- 13 (INT)   -> pd trigger (RotoPD Pro) Interrupt-Ausgang
- 33 (ADC)   -> spannungsteiler (42V ausgang)
