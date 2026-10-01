# io-homecontrol link layer specification ("worldwide" (non-Europe) version)

## io protocol frame

This is the io-homecontrol frame for the 802.15.4 "worldwide" (non-Europe) version (byte positions in the header).

|          1 - 3          |            4            |                5                 |      6-8       |      9-11      |     12     | variable |   n-1, n   |
|:-----------------------:|:-----------------------:|:--------------------------------:|:--------------:|:--------------:|:----------:|:--------:|:----------:|
| Header (0x01 0x69 0x6F) |     Control Byte 1      |          Control Byte 2          | Target address | Source address | Command ID |   DATA   | 2-byte CRC |

In my testing, the last two bytes of DATA in every packet were 0x00 0x00 -- this may have been related to allowing the radio hardware to handle the CRC?

## Header

In my testing with Velux products, this was always `0x01 0x69 0x6F`.
* 0x01 -- unknown, but 0x01 is the manufacturer code for Velux
* 0x69 0x6F -- unknown, but in ASCII these bytes spell "io".  Coincidence?

At present, I believe the rest is the same as the Euro version as documented in linklayer.md.
