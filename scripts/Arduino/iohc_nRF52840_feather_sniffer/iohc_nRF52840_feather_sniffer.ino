// Radio sniffer for 802.15.4 packets with 0x56 sync byte, written for the Adafruit
// nRF82540 Feather.  Scans 802.15.4 channels 15, 20, and 25 looking for packets,
// and prints them to the serial port.  The preamble, SFD and length byte
// are stripped by the radio and thus not included in the output -- the CRC is.

#include <Adafruit_TinyUSB.h> // required for Serial to resolve

const uint8_t scanChannels[] = {15, 20, 25};  // 802.15.4 channels used by io-HC (world-wide version)
const size_t RX_BUFFER_SIZE = 128;  // 802.15.4 maximum packet size is 127 bytes + 1 length byte
const uint8_t sync_byte = 0x56;  // 0xA7 is standard

// Timing -- too short and you spend all your time switching channels, too long and you miss channel changes
// According to the datasheet, switching channels and restarting the radio should take 40.5 us if "fast ramp-up" mode is on.
const unsigned long SCAN_WINDOW_MS = 1;  // time (in ms) to listen on a channel before hopping
const unsigned long LOCK_DURATION_MS = 8; // time (in ms) to stay on a channel after a packet arrives on that channel

int currentChannelIndex = 1;
uint8_t currentChannel = scanChannels[currentChannelIndex];
const int numChannels = sizeof(scanChannels);

unsigned long lastChannelSwitchTime = 0;
unsigned long lastPacketReceivedTime = 0;
bool isLocked = false;

// Create two buffers for Direct Memory Access (DMA) storage
// We alternate between them so the radio can receive a packet while we're printing the last one.
uint8_t rx_packet_buffer_A[RX_BUFFER_SIZE] __attribute__((aligned(4)));
uint8_t rx_packet_buffer_B[RX_BUFFER_SIZE] __attribute__((aligned(4)));

// Pointer to the buffer the radio should fill next
uint8_t* activeRxBuffer = rx_packet_buffer_A;

// Pointer to the buffer we just received and are about to print
uint8_t* completedRxBuffer = nullptr;

void setup() {
  Serial.begin(921600);
  while (!Serial) delay(10); // Wait for Serial Monitor to connect
  Serial.println("--- Initializing ---");

  // Reset/power up the radio peripheral
  NRF_RADIO->POWER = 0;
  delay(10);
  NRF_RADIO->POWER = 1;

  // Set Radio Mode to IEEE 802.15.4 (250kbps, O-QPSK modulation)
  NRF_RADIO->MODE = (RADIO_MODE_MODE_Ieee802154_250Kbit << RADIO_MODE_MODE_Pos);

  // Set radio to fast ramp-up for faster channel changes
  NRF_RADIO->MODECNF0 = (RADIO_MODECNF0_RU_Fast << RADIO_MODECNF0_RU_Pos) | (RADIO_MODECNF0_DTX_Center << RADIO_MODECNF0_DTX_Pos);

  // Customize Packet Engine Configuration 0 (PCNF0)
  // LFLEN = 8 bits (1 byte length field).
  // S0LEN = 0, S1LEN = 0 (Deactivates standard 802.15.4 frame parsing overhead)
  // PLEN = 32-bit zero (Catches your standard 4x 0x00 preambles)
  NRF_RADIO->PCNF0 = (8UL << RADIO_PCNF0_LFLEN_Pos) |
                     (0UL << RADIO_PCNF0_S0LEN_Pos) |
                     (0UL << RADIO_PCNF0_S1LEN_Pos);

  // Customize Packet Engine Configuration 1 (PCNF1)
  // MAXLEN = 127 bytes max payload depth
  // STATLEN = 0 (Payload size is dynamic i.e. controlled by length byte)
  // BALEN = 0 (No proprietary base network address tracking needed)
  NRF_RADIO->PCNF1 = (127UL << RADIO_PCNF1_MAXLEN_Pos) |
                     (0UL << RADIO_PCNF1_STATLEN_Pos) |
                     (0UL << RADIO_PCNF1_BALEN_Pos) |
                     (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) |
                     (0UL << RADIO_PCNF1_WHITEEN_Pos); // Disable whitening

  // Change standard SFD (Sync Word/Byte)
  NRF_RADIO->SFD = sync_byte;

  // Disable CRC hardware filtering
  // IEEE 802.15.4 uses a 2-byte ITU-T CRC
  NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) |
                      (RADIO_CRCCNF_SKIPADDR_Include << RADIO_CRCCNF_SKIPADDR_Pos);
  NRF_RADIO->CRCPOLY = 0x8408;
  NRF_RADIO->CRCINIT = 0;

  // Point the Radio DMA to the first memory buffer array
  NRF_RADIO->PACKETPTR = (uint32_t)activeRxBuffer;

  // Enable the END-START shortcut
  NRF_RADIO->SHORTS = (RADIO_SHORTS_READY_START_Enabled << RADIO_SHORTS_READY_START_Pos);

  // Start the receiver
  setRadioChannel(currentChannel);
  startRadio();
}


void loop() {
  unsigned long currentTime = millis();

  // The radio sets EVENTS_END when a completed packet is received
  if (NRF_RADIO->EVENTS_END) {
    NRF_RADIO->EVENTS_END = 0; // Clear flag

    lastPacketReceivedTime = currentTime;

    // Keep track of the buffer that has our new data
    completedRxBuffer = activeRxBuffer;

    // Toggle activeRxBuffer to the other DMA buffer
    if (activeRxBuffer == rx_packet_buffer_A) {
      activeRxBuffer = rx_packet_buffer_B;
    } else {
      activeRxBuffer = rx_packet_buffer_A;
    }

    // Point the radio DMA at the new buffer and restart right away, so we
    // don't miss the next packet while we're printing this one
    NRF_RADIO->PACKETPTR = (uint32_t)activeRxBuffer;
    NRF_RADIO->TASKS_START = 1;

    // Lock onto this channel and update the watchdog timer
    isLocked = true;

    // Byte 0 in DMA configuration holds the payload length reported by the transmitter
    uint8_t packet_length = completedRxBuffer[0];

    // Defensive clamp in case of malformed/noisy packets
    if (packet_length > 127) packet_length = 127;

    // Write packet content to the serial port
    // if (packet_length != 5) {  // <-- filter for the ~512 wake-up packets that would clutter your logs otherwise
    if (true) {  // <-- use this for cluttered logs
      Serial.print(micros()); Serial.print(" ");
      Serial.print(currentChannel);  Serial.print(": ");  // channel number
      for (int i = 1; i <= packet_length; i++) {
        if (completedRxBuffer[i] < 0x10) Serial.print("0"); // Pad single hex digits
          Serial.print(completedRxBuffer[i], HEX);
          Serial.print(" ");
      }
      // Serial.print(NRF_RADIO->EVENTS_CRCOK ? "CRC VALID" : "CRC INVALID");  // <-- always invalid
      Serial.println();
    }

    NRF_RADIO->EVENTS_CRCOK = 0;
    memset(completedRxBuffer, 0, RX_BUFFER_SIZE); // Flush this buffer
  }

  // Re-start scanning if enough time has passed since the last received packet
  if (isLocked && (currentTime - lastPacketReceivedTime > LOCK_DURATION_MS)) {
    isLocked = false;
    // Serial.println("Lock broken. Resuming channel scan...");
    lastChannelSwitchTime = currentTime;  // reset the channel-switch timer
  }

  // Hop channels if it's time to hop.
  if (!isLocked && (currentTime - lastChannelSwitchTime > SCAN_WINDOW_MS)) {
    // Need to stop before changing channel
    stopRadio();

    // Advance to the next channel in the sequence
    currentChannelIndex = (currentChannelIndex + 1) % numChannels;
    setRadioChannel(scanChannels[currentChannelIndex]);

    // Reset the timer
    lastChannelSwitchTime = currentTime;

    // Restart the radio (now on next channel)
    startRadio();
  }
}

void startRadio() {
  NRF_RADIO->TASKS_RXEN = 1;
  // Radio will START automatically as soon as it ramps up due to the READY -> START shortcut
}

void stopRadio() {
  NRF_RADIO->EVENTS_DISABLED = 0;  // flag to indicate radio rampdown is complete, clear it first
  NRF_RADIO->TASKS_DISABLE = 1;  // tell the radio to disable
  while (NRF_RADIO->EVENTS_DISABLED == 0); // wait until the chip says it's actually disabled before continuing
}

void setRadioChannel(uint8_t channel) {
  currentChannel = channel;
  uint32_t frequencyMhzOffset = 5 + (5 * (channel - 11));
  NRF_RADIO->FREQUENCY = frequencyMhzOffset;  // <-- chip uses offset from 2400 MHz
  // Don't use this with fast scan times:
  // Serial.print("Channel: "); Serial.print(channel); Serial.print(" Frequency: "); Serial.println(2400 + frequencyMhzOffset);
}
