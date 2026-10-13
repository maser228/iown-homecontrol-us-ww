# KLF200 Radio Behavior

## Radio Init
On startup, the KLR200 micro initializes the AT86RF233 with the following packets set at a rate ~10us between commands (all values in hex):
- `C3 00` -- turns off CLKM output
- `C4 20` -- turns off IRQ_MASK_MODE
- `C2 03` -- turns off radio (sets status to FORCE_TX_OFF) 
- `81 FF` (reply `1F`) -- reads radio status -> STATE_TRANSITION_IN_PROGRESS 
- [2x more of same]
- `81 FF` (reply `08`) -- reads radio status -> "TRX_OFF (CLK Mode)"
- `CB 56` -- write register 0x0B -- SFD_VALUE (sync byte) to 0x56  <-- non-standard! 
- `D2 F7` -- enable crystal oscillator (to excite the external crystal) and trim it
- `C2 06` -- set radio state to RX_ON
- `81 FF` (reply `1F`) -- read radio status -> STATE_TRANSITION_IN_PROGRESS
- [5x more of same]
- `81 FF` (reply `06`) -- read radio status -> RX_ON
- `CE 0C` -- arm these interrupts: `RX_START` (i.e. message has started arriving), `TRX_END` (complete message has arrived)
- `C8 2F` -- sets channel 15

With this complete, the radio begins scanning channels.

## Scanning

When there's no activity, the KLF200 scans across the three channels used (15, 20, and 25).  To do this, it performs the following steps:
1. Turn off radio
2. Confirm radio is off
3. Set the new frequency
4. Turn the radio on
5. Poll the radio status until the chip confirms it's in the RX_ON state

The AT86RF233 supports changing channels "on-the-fly" while the radio is on (including any required recalibration), so it's not clear why the KLF200 turns the radio off to change channels.  This may be a case of reusing code from the Euro radio chip.

In the idle state (no messages being sent or detected), the KLF changes channels every 2 ms.  Since "wake-up" messages arrive every 1 ms, holding on each channel for 2 ms ensures that at least one will be heard before switching channels.

With the `RX_START` and `TRX_END` interrupts set, the radio will assert an output pin high if either of these conditions occur.  At that point the micro can read the `IRQ_STATUS` register to see which interrupt(s) fired.  (In most cases, this also resets the interrupt and is required to do so.)

#### SPI Trace
```
Format: `> [MOSI] < [MISO]`  MISO omitted for register writes.

> C2 03 -- write FORCE_TRX_OFF (turn off radio)
> 81 FF < 00 08 -- read TRX_STATUS, response is TRX_OFF (radio is off)
> C8 39 -- set frequency
> C2 06 -- write RX_ON (turn radio on to receive mode)
> 81 FF < 00 1F -- check radio status, status is STATE_TRANSITION_IN_PROGRESS
> 81 FF < 00 1F -- check radio status, status is STATE_TRANSITION_IN_PROGRESS
> 81 FF < 00 1F -- check radio status, status is STATE_TRANSITION_IN_PROGRESS
> 81 FF < 00 06 -- check radio status, status is RX_ON
```

## Receiving a Message

1. When the radio detects a packet arriving, it raises the `RX_START` interrupt an asserts the interrupt output pin.
2. Seeing this pin go high, the micro reads the `IRQ_STATUS` register to know which interrupt fired (and reset it).
3. Once the entire packet has arrived, the `TRX_END` interrupt fires and the pin is asserted again.  Again this is read out by the micro.  The time between `RX_START` and `TRX_END` is dependent on the length of the packet but appears to be in the range of 200~900 us.
4. About 10 us after this, the micro checks the `PHY_RSSI` register, which provides the `RX_CRC_VALID` bit.
5. If the CRC is valid, the micro reads out the message payload from the frame buffer (though it does not read the actual CRC bytes).  If CRC is not valid, the payload is not read out.
6. If packet is a wake-up packet with a counter value less than `00 04`, the KLF stops channel-hopping and waits for any remaining wake-up frames and the "real" packet. 
7. If the packet is a "normal" (non-wakeup) packet, the micro also checks the ED value once.  (The radio does an ED calculation automatically when a packet is received.)
8. If no further packets arrive on the current channel for ~30 ms, the micro returns to scanning mode.

> [!Note]
> When the KLF receives a wake-up packet, it does **not** immediately lock onto that packet's channel.  Instead it continues scanning until a few ms before the "real" packet is scheduled to arrive.  Since the wake-ups are in flight for 500x longer than the "real" message, it's very possible for the wake-ups of multiple messages to occur at once (on different channels).  The continued-scanning behavior could allow the KLF to track and receive both payloads, as long as the "real" packets don't overlap in time.  It could also allow receipt of non-low-power messages on other channels while wake-ups are in progress.

## Sending a Message

The KLF200's idle state is the scanning mode described above.  When it's time to transmit a message, the micro stops scanning and performs the following:

1. Sets the desired channel (without turning off the radio first) then waits ~1 ms.
2. Takes several ED measurements and changes the channel as needed (see below for details).
3. Sets the radio power level to +1 dBm (might vary based on measured ED??).
4. For wakeup packets, clears all interrupts.  For non-wakeup packets, enables the `TRX_END` interrupt, allowing it to be notified when the transmit is complete.  (The interrupt isn't needed for wake-up packets because they're always transmitted 1ms apart and take much less than this to transmit.)
5. Puts the radio into TX mode, and confirms that it's in TX mode.  This causes the radio to immediately start transmitting the preamble and sync byte.  (Starting TX before copying the payload into the buffer is supported as long as the message gets written to the frame buffer in time.)
6. Writes the message payload into the frame buffer.
7. Repeats steps 3-6 for the remaining wake-up packets (if used).
8. For normal packets, the `TRX_END` interrupt is active so the micro waits for the interrupt pin to go high.  Once this happens, the micro checks the interrupt (which also clears it).
9. Turns on the radio in RX mode and verifies this state.
10. Enables the `RX_START` and `TRX_END` interrupts.  The time from catching the `TRX_END` to enabling the RX interrupts is about 42 us, so in theory there's a "dead zone" there where replies could be lost.  In practice, the replies appear to come in after 2-3 ms, so this is not a concern.
11. Waits for up to 50 ms for a reply.  If none arrives, changes to a different channel (see below) and repeats the process.
12. Once a reply arrives, remembers that channel for the next communication.
13. Returns to channel scanning mode approximately 5 ms after the reply arrives.

For low-power devices, the micro precedes a message packet with ~510 wake-up packets, whose payload consists of `0x00` followed by a descending counter that starts at `01 FD`.  The packets are sent at a rate of one packet every ms, so the counter also serves as an indicator of when the message packet will arrive.

## ED Measurement and Channel Selection Behavior
Before sending a message (including the burst of wake-up packets if needed), the micro performs an ED measurement.  Each measurement consists of sending the radio a command to start the measurement, waiting ~150us, and then reading out the result.  The datasheet says the measurement takes 128us, with a valid return range of 0~83dB.  The return value is added to the radio's base RSSI sensitivity of -94dBm (dB milliwatts) to get the measured dBm value in the range -94 to -11 dBm.

Roughly, speaking, -94 dBm is the noise floor, indicating a clear channel.  Typical ED values when receiving from blinds in my house are -75 to -61 dBm, with one at -37 dBm (likely a control within a few feet of the receiver).  So we would expect the KLF to avoid transmitting on channels where the ED is higher than approximately -80 to -75 dBm.  While averaging or other statistical methods could be used, a simple threshold of -75 dBm is consistent with all of the examples below.

We always observe the micro performing ED measurements in groups of two measurements made in rapid succession.  After this, the micro may do additional groups of two measurements, ~1 ms apart.  In clear-channel operation, the micro appears to make six two-measurement groups (12 measurements total) and then start transmission.  However in cases where the micro decides to change channels, it will often stop short of the full six groups. 

### Examples

#### Clear Channel Transmissions
- Set ch ??; perform six groups of measurements (return values: -87,-87; -87,-87; -87,-82; -87,-87; -87,-87; -87,-87 dBm) --> transmit (and reply)
- Set ch 20; perform six groups of measurements (return values: -87,-87; -87,-87; -87,-87; -87,-88; -87,-87; -87,-87 dBm) --> transmit (and reply)
- Set ch 15; perform six groups of measurements (return values: -89,-90; -89,-89; -89,-89; -89,-89; -76,-76; -89,-90 dBm) --> transmit

#### One Channel Change
- No reply to previous transmit on 15
- Set ch 25; perform two groups of measurements (return values: -76,-76; -75,-75 dBm) 
- Set ch 15, perform six groups of measurements (return values: -88,-89; -86,-86; -85,-86; -87,-89; -86,-86; -85,-78 dBm) --> transmit

In this case, -76 and -75 dBm values caused the micro to stop measuring after two groups and change channels.  It's not clear why channel 20 was skipped.

#### One Channel Change
- Received reply on ch 15
- Set ch 15, perform three groups of measurements (return values: -89,-82; -89,-81; -89,-61 dBm)
- Change channel to 25, perform -88 groups of ED measurements (return values: -92,-91; -93,-81; -85,-92; -89,-81; -92,-92; -92,-92) --> transmit

Here the single -61 dBm reading likely causes the KLF to stop measuring and change channels.  Again channel 20 was skipped for unknown reason.

#### Two Channel Changes
- No reply to message sent on ch 25
- Set ch 15, perform five groups of two measurements (return values in dBm: -89,-89;  -89,-89;  -90,-89;  -89,-89;  -90,70 dBm)
- Change to ch 20, perform one group of two measurements (return values: -41,-87 dBm)
- Change to ch 15, perform six groups of two measurements (return values: -89,-89;  -89,-90;  -89,-89;  -89,-89;  -89,-90;  -89,-89) --> transmit.

Here the -70 dBm probably cut short measurements on ch 15.  Likewise ch 20 (not skipped this time) had a -41 dBm reading and was also cut short after one measurement.


## Behavior on No Response

When the KLF200 sends a message and does not receive a reply after 50 ms, it tries another channel.  I thought this was as simple as trying the next channel numerically (i.e. 15 -> 20 -> 25), but that might not be the case.

> Before starting the retry, the micro actually runs one "standard" channel-change routine (as if "Scanning" above), turning off the radio, setting a new frequency, and turning it back on again. I'm assuming this is simply scanning code starting to run before being interrupted by the transmitting code again. 

Transcript of KLF checking blinds:
1. Sends on ch 20 --> no reply
2. Resends on ch 25 --> reply OK

1. Sends on 25 --> no reply
2. Resends on 15 --> no reply
3. Resends on 25 --> reply OK     <-- I expected 20 here, not 25 again

1. Sends on 25 --> no reply
2. Resends on 20 --> reply OK     <-- I expected 15 here, not 20

1. Sends on 20 --> no reply
2. Resends on 25 --> reply OK

In these cases, all ED tests were successful i.e. the KLF did not change channels after the ED calculation.

From this data it seems like the KLR always tries the last channel that worked, however if a channel doesn't work, the channel it tries next is not always the next channel in numeric order.

There's a possibility that the KLR is remembering the last channel that worked for each device, but in that case you wouldn't expect the next message to always be on the last channel that worked (although there aren't many data points here).

Since it does an ED check before sending, it's also possible that it's remembering the channel with the lowest ED as its "go-to" alternate channel.  Or keeping a count of the number of successful sends per channel?
