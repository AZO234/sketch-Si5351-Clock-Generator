sketch Si5351 Clock Generator
=============================
version: v1.0.0(Oct 21, 2019)  
http://domisan.sakura.ne.jp/

High precision Clock Generator with Si5351 module.

![ClockGenerator](http://domisan.sakura.ne.jp/article/si5351/cg000.jpg "ClockGenerator")

Hardware
========

- Arduino UNO R3 (for debug, and ISP writer) & ATmega328P.
- 3.3V regulator
- 10k owm registance x1 (for reset pullup)
- 64x128 OLED LCD with SSD1306(I2C) module
- Si5351 clock generator module
- enough capacitances

Connection
==========

- VCC,GND -> 0.1uF -> 3.3Vreg -> 100uF -> ATmega328P(VCC,AVCC), OLED, Si5351
- [TWI SCL] ATmega328P pin28 -> OLED SCL -> Si5351 SCL
- [TWI SDA] ATmega328P pin27 -> OLED SDA -> Si5351 SDA
- [Reset] 3.3Vreg -> 10k owm -> Switch(to GND) -> ATmega328P pin1
- [Const mode] 2PinHeader(to GND) -> ATmega328P pin4
- [Write mode] 2PinHeader(to GND) -> ATmega328P pin5
- [L key] Switch(to GND) -> ATmega328P pin6
- [R key] Switch(to GND) -> ATmega328P pin11
- [A key] Switch(to GND) -> ATmega328P pin12

Use libraries
=============

This application use follow libraries.

- AZO234/Arduino_Si5351  
https://github.com/AZO234/Arduino_Si5351

- AZO234/Arduino_berkeley-softfloat-3  
https://github.com/AZO234/Arduino_berkeley-softfloat-3

- AZO234/Arduino_SSD1306  
https://github.com/AZO234/Arduino_SSD1306

- AZO234/Arduino_font_JaANK  
https://github.com/AZO234/Arduino_font_JaANK

Write to chip
=============

This program designed for driving on internal 8MHz.

Usage
=====

1. Manual controll mode (pin4=L / pin5=L)

    On this mode, It can controll PLL/Clks.  
    And Save settings to EEPROM.

    To controll, use L R A buttons.

    If the clock is turned on without changing any settings at startup,  
    10MHz clock is outputed from ClkX.

    Select 'SAVE' if you want to save the settings to EEPROM.

2. Harf manual controll mode (pin4=L / pin5=H)

    On this mode, same Manual mode, It can controlls any PLL/Clks.  
    But it can't save settings.

3. Non controll mode (pin4=L / pin5=H)

    On this mode, It can't change settings of PLL/Clks.  
    Setting is loaded from EEPROM and drive.

Reference
---------
- Si5351A/B/C-B  
https://www.silabs.com/documents/public/data-sheets/Si5351-B.pdf

- AN619  
https://www.silabs.com/documents/public/application-notes/AN619.pdf
