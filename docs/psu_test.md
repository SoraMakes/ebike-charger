ugreen nexode 160w 
- alle ports funktionieren

anker 511 nano 3 30W
- funktioniert

anker nano 45W
- funktioniert

UGREEN USB C Ladegerät 65W, 3-Port 
- funktioniert


powerbank ugreen nexode pro 25000mAh 200W  PB722
[PDDIAG] st=REQ contract=20.0V/3.25A pdo=5 | rail=5.04V r11=4.80V VREQ=5.00V IREQ=3.00A Iout=0.17A Tchip=26C | status=0x02 sticky=0x07 msgrlt=0 opmode=0x83 cfg=0xF0 mask=0x7F sys=0x10 voutctl=0 | ina=5.04V/0.01A out=3.79V pot=0 | boot#1 power-on
[DIAG] b1       0.050s  boot #1 (power-on)
[DIAG] b1       0.052s  digipot at boot: step 63 (~3.74A)
[DIAG] b1       1.796s  rail measurement available (5.04V)
[DIAG] b1       1.798s  opmode 0xFF -> 0x00
[DIAG] b1       1.800s  attach: rail=5.04V opmode=0x00 (early)
[DIAG] b1       1.850s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       2.006s  rail ? -> LOW(~5V) (5.04V) contract=0.0
[DIAG] b1       2.151s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       2.451s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       2.751s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       3.051s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       3.351s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       3.651s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       3.971s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       4.300s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       4.649s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       4.980s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       5.300s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       5.599s  caps re-request (rail=5.04V opmode=0x00
[DIAG] b1       5.745s  status=0x01 STARTED
[DIAG] b1       5.746s  opmode 0x00 -> 0x80 CCFLIP
[DIAG] b1       5.747s  CHIP RESTARTED (STARTED while in state 
[DIAG] b1       5.913s  caps re-request (rail=5.04V opmode=0x80
[DIAG] b1       6.234s  caps re-request (rail=4.90V opmode=0x80
[DIAG] b1       6.550s  caps re-request (rail=4.90V opmode=0x80
[DIAG] b1       6.934s  caps re-request (rail=4.92V opmode=0x80
[DIAG] b1       7.029s  opmode 0x80 -> 0x00
[DIAG] b1       7.129s  opmode 0x00 -> 0x80 CCFLIP
[DIAG] b1       7.129s  CHIP RESTARTED (STARTED while in state 
[DIAG] b1       7.300s  caps re-request (rail=4.90V opmode=0x80
[DIAG] b1       7.445s  opmode 0x80 -> 0x82 CCFLIP PDMOD
[DIAG] b1       7.500s  request pdo5 20.0V 3.25A
[DIAG] b1       7.645s  status=0x06 READY NEWPDO
[DIAG] b1       8.646s  opmode 0x82 -> 0x83 CCFLIP PDMOD LGCYMO
[DIAG] b1       9.497s  request TIMEOUT msgrlt=1 rail=5.04V VRE
[DIAG] b1       9.549s  request pdo5 20.0V 3.25A
[DIAG] b1      11.547s  request TIMEOUT msgrlt=0 rail=5.04V VRE
[DIAG] b1      11.600s  request pdo5 20.0V 3.25A

powerbank anker solix c300dc
- all ports work
80V VREQ=28.00V IREQ=5.00A Iout=0.17A Tchip=27C | status=0x02 sticky=0x07 msgrlt=1 opmode=0x82 cfg=0xF0 mask=0x7F sys=0x10 voutctl=0 | ina=28.12V/0.02A out=39.02V pot=42 | boot#1 power-on
[DIAG] b1       0.050s  boot #1 (power-on)
[DIAG] b1       0.052s  digipot at boot: step 63 (~3.74A)
[DIAG] b1       1.796s  rail measurement available (5.10V)
[DIAG] b1       1.798s  status=0x07 STARTED READY NEWPDO
[DIAG] b1       1.799s  opmode 0xFF -> 0x82 CCFLIP PDMOD
[DIAG] b1       1.800s  attach: rail=5.10V opmode=0x82 (STARTED
[DIAG] b1       1.851s  request pdo8 28.0V 5.00A
[DIAG] b1       2.000s  READY 28.0V/5.00A rail=28.12V
[DIAG] b1       2.018s  rail ? -> HIGH (28.12V) contract=28.0V


powerbank einova laptop powerbank 20000mAh 63W
- doesnt work
[PDDIAG] st=SETTLE contract=0.0V/0.00A pdo=0 | rail=5.13V r11=4.88V VREQ=5.00V IREQ=0.00A Iout=0.17A Tchip=27C | status=0x01 sticky=0x01 msgrlt=0 opmode=0x80 cfg=0xF0 mask=0x7F sys=0x10 voutctl=0 | ina=5.13V/0.00A out=0.00V pot=0 | boot#1 power-on
[EVT 6.253s] caps re-request (rail=5.08V opmode=0x80
[EVT 6.553s] caps re-request (rail=4.90V opmode=0x80
[DIAG] b1       0.050s  boot #1 (power-on)
[DIAG] b1       0.052s  digipot at boot: step 63 (~3.74A)
[DIAG] b1       1.796s  rail measurement available (5.14V)
[DIAG] b1       1.798s  opmode 0xFF -> 0x00
[DIAG] b1       1.800s  attach: rail=5.14V opmode=0x00 (early)
[DIAG] b1       1.850s  caps re-request (rail=5.14V opmode=0x00
[DIAG] b1       2.006s  rail ? -> LOW(~5V) (5.13V) contract=0.0
[DIAG] b1       2.151s  caps re-request (rail=5.13V opmode=0x00
[DIAG] b1       2.451s  caps re-request (rail=5.14V opmode=0x00
[DIAG] b1       2.751s  caps re-request (rail=5.13V opmode=0x00
[DIAG] b1       3.051s  caps re-request (rail=5.14V opmode=0x00
[DIAG] b1       3.351s  caps re-request (rail=5.14V opmode=0x00
[DIAG] b1       3.651s  caps re-request (rail=5.13V opmode=0x00
[DIAG] b1       3.971s  caps re-request (rail=5.14V opmode=0x00
[DIAG] b1       4.300s  caps re-request (rail=5.14V opmode=0x00
[DIAG] b1       4.649s  caps re-request (rail=5.14V opmode=0x00
[DIAG] b1       4.981s  caps re-request (rail=5.14V opmode=0x00
[DIAG] b1       5.300s  caps re-request (rail=5.14V opmode=0x00
[DIAG] b1       5.599s  caps re-request (rail=5.14V opmode=0x00
[DIAG] b1       5.899s  status=0x01 STARTED
[DIAG] b1       5.899s  opmode 0x00 -> 0x80 CCFLIP
[DIAG] b1       5.900s  CHIP RESTARTED (STARTED while in state 
[DIAG] b1       5.915s  caps re-request (rail=5.14V opmode=0x80
[DIAG] b1       6.253s  caps re-request (rail=5.08V opmode=0x80
[DIAG] b1       6.553s  caps re-request (rail=4.90V opmode=0x80


ugreen 69W kfz charger
- usb c2: works
- usb c1 (12V/1.5A): sehr schnelle bootloop, sieht etwas aus wie die loops vor dem Hinzufügen den Kondensatoren. Spannung ist immer 5V (messgerät dazwischen). loop duration vllt 2s
[DIAG] b1       0.050s  boot #1 (power-on)
[DIAG] b1       0.052s  digipot at boot: step 63 (~3.74A)
[DIAG] b1       1.796s  rail measurement available (5.14V)
[DIAG] b1       1.798s  status=0x07 STARTED READY NEWPDO
[DIAG] b1       1.799s  opmode 0xFF -> 0x82 CCFLIP PDMOD
[DIAG] b1       1.800s  attach: rail=5.14V opmode=0x82 (STARTED
[DIAG] b1       1.851s  request pdo3 12.0V 1.50A
[DIAG] b1       2.005s  rail ? -> LOW(~5V) (4.88V) contract=12.
[DIAG] b1       2.547s  CHIP RESTARTED (STARTED while in state 
[DIAG] b1       3.807s  opmode 0x82 -> 0x83 CCFLIP PDMOD LGCYMO
[DIAG] b1       3.858s  request TIMEOUT msgrlt=0 rail=4.90V VRE
[DIAG] b1       3.910s  request pdo3 12.0V 1.50A