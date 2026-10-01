# io-homecontrol radio protocol description -- "Worldwide" (non-EU) Version

## Overview
The "WW" (worldwide i.e. non-EU) version of io-homecontrol uses different frequencies, encoding, and packet format than the Euro version.  While based on 2.4 GHz 801.15.4, it has the following modifications which make it non-standard and thus incompatible with some common 2.4 GHz 801.15.4 radios:
- non-standard start-of-frame delimiter
- 802.14.5 MAC header not used

More details below.

## Physical specifications

- IEEE 801.15.4 channels used:
  - 15: 2425 MHz
  - 20: 2450 MHz
  - 25: 2475 MHz
- Data/Baud Rate: 250 kbps
- Modulation: O-QPSK
- Standards: Modified IEEE 802.15.4
- Checks: CRC

## Raw data sending
The physical level (PHY) is based on 802.15.4.  In accordance with 802.15.4, data is transmitted as 8-bit octets (no start bits or stop bits). Bytes are transmitted in order, LSB first.

## PHY Frame Format
|   Synchronization Header    |    Frame Length     | Payload (including CRC) |
|:---------------------------:|:-------------------:|:-----------------------:|
|  0x00 0x00 0x00 0x00 0x56   |       1 byte        |       <=127 bytes       |

Messages start with a synchronization header consisting of four 0x00 bytes followed by a single-byte start-of-frame delimiter (SFD), also known as the sync byte.  The SFD used by io-HC is a non-standard value of 0x56 -- 802.15.4 radios which strictly follow the standard (such as the popular the popular Sonoff Zigbee 3.0 USB Dongle-E) will expect 0xA7 in this position and thus fail to sync on io-HC frames.

Following the synchronization header is a single message-length byte.  The length calculation doesn't include the header or length byte itself, but does include the two CRC bytes (see below).  The highest bit of this byte is reserved 0, resulting in a maximum payload length (including CRC) of 127 bytes.

The payload is transmitted next.  However, the 802.14.5 MAC header is not used -- the two control bytes used in the Euro version are used instead.  As a result, radio IC's which require these bytes to follow the 802.15.4 standard may fail to pass along io-HC packets unless the filtering can be disabled (often via "promiscuous mode").

After the payload, a two-byte CRC is transmitted.  See LinkLayer-US for more information.

## Wake-Up Packets (Low-Power)

When addressing low-power devices (e.g. solar blinds), before sending the "real" packets, the remote sends up to 512 five-byte "wake-up" packets, at a rate of one per ms, with payloads in the form `0x00 [0x00 | 0x01] 0xnn` (+ 2x CRC bytes), where the second and third bytes count down from 0x01 0xFF by (approximately) 1 until reaching 0x00 0x00, at which point the "real" packet is sent.  This may be because the target devices' radios only receive intermittently (to save power) -- if a device sees any one of these messages, it will either keep its radio on long enough to receive the "real" packet, or schedule a radio-on time to receive it.

It's not known what the first byte is used for, but a possible reason is to produce a five-byte payload, which happens to be the length of an 802.14.5 "acknowledgement" frame.  Lengths 0-4 and 6-8 are reserved in the 802.15.4 standard, and attempting to send such packets may cause issues with either sending or receiving radios which enforce this.  Padding the message with an extra leading 0x00 makes it a valid length.

## Radio Hardware

The radio transceiver used in the US versions of (at least) the KLR200 touch-screen remote and the KLF200 gateway is the Atmel AT86RF233.  While this device supports compliant 802.15.4 frames, it also allows changing the sync byte and ignoring "invalid" 802.15.4 MAC headers, so it works for the non-standard io-HC frames too.

Since io-HC doesn't use the standard's MAC header, the chip's frame filtering and address filtering can't be used, and the TRX_END interrupt is used to detect incoming frames instead of the usual AMI interrupt.  However, the automatic CRC generation and checking built into the chip are used.

While some hardware allows ignoring the MAC header via "promiscuous mode", because the SFD is used for byte sync, radios which are hard-coded to the 802.15.4 standard's SFD value of 0xA7 (such as the popular Sonoff Zigbee 3.0 USB Dongle-E) will _not_ sync on io-HC messages and either produce gibberish or (more often) not return any data at all.

Below is a list of some known working and non-working 2.4GHz radios for US io-HomeControl packets:

|    Radio (Chip or Product)     | Works? |                                             Comment                                             |
|:------------------------------:|:------:|:-----------------------------------------------------------------------------------------------:|
|        Atmel AT86RF233         |  Yes   | Allows custom sync byte and ignoring invalid MAC headers.  Used by Velux for US-market devices. |
| Sonoff Zigbee 3.0 USB Dongle-E |   No   |       "Proprietary Mode" required to change 0xA7 sync byte not supported on this variant        |
|            nRF52840            |  Yes   |                           Supports custom sync byte, no MAC filtering                           |
|             CC2500             |   No   |                                Doesn't support O-QPSK modulation                                |

