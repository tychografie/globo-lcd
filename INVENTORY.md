# Globo — Parts in the device

| Part | Details | Where it connects |
|---|---|---|
| LilyGo T-Display-S3 (non-touch) | ESP32-S3R8, 8 MB PSRAM, 170×320 ST7789 LCD (8-bit parallel) | main board |
| MAX98357 I2S amplifier breakout | 3W Class D | same header as the encoder: BCLK 11, LRC 12, DIN 10; SD tied to VIN |
| Speaker | 3525, 4Ω 3W | amp output |
| EC11 rotary encoder | with push switch | P2 header top, next to 3V: A 1, B 2, SW 3 |
| 14500 Li-Ion battery | 3.7 V 1600 mAh, XH2.54 2-pin plug | TP4057 board B+/B− |
| TP4057 charge board | USB-C (charge only, no data), 1 A, with cell protection | OUT+ through the power switch, OUT− direct, to the board's 1.25mm battery connector |
| SS12F15 slide switch | SPDT 50V 0.3A, used as hard power switch | in the OUT+ line between TP4057 and the board; off still charges the cell |
