# iown-homecontrol-us-ww
Documentation of the US/Worldwide version of io-homecontrol(R)

Based on the wonderful work of:
[Velocet/iown-homecontrol](https://github.com/Velocet/iown-homecontrol)

io-homecontrol is a proprietary home-automation protocol used in products from Velux, Somfy, and other companies.  The European version of the protocol is well-documented elsewhere (see above).  In the US and other countries, there are substantial differences in the physical layer, and minor differences in other areas.  This repo provides that information.

## Synopsis
The "WW" (worldwide i.e. non-EU) version of io-homecontrol uses essentially the same payloads as the European version, but the physical layer uses different frequencies, encoding, and packet format.  It uses channels 15, 20, and 25 of IEEE 802.15.4 (2.4 GHz), but uses a non-standard start-of-frame delimiter and does not use the 802.15.4 MAC header.  Find more details in the docs folder.

## Legal Notice & Fair Use Disclaimer
This repository contains independent, clean-room technical documentation detailing the over-the-air (OTA) radio frequencies and non-encrypted packet layouts used by specific home automation systems in the United States market. All information published here consists strictly of uncopyrightable functional facts and physical signal data captured from public airspace using legally purchased hardware. This documentation is provided exclusively for educational, research, and system interoperability purposes under the **Fair Use** doctrine of United States copyright law (17 U.S.C. § 107).

**Trademark Notice**: The term "io-homecontrol" is a registered trademark of VKR Holding A/S and the io-homecontrol alliance. This independent research project is not affiliated with, sponsored by, or endorsed by Velux, Somfy, VKR Holding A/S, or any member of the io-homecontrol alliance. Any reference to the trademarked name is made strictly under **Nominative Fair Use** to identify the functional compatibility of the documented wireless signals. No proprietary software, firmware binaries, encryption keys, or authentication code are hosted or distributed within this project.
