#include <Arduino.h>
#include <cc1101.h>
#include <ccpacket.h>
#include <CircularBuffer.h>

#define CC1101Interrupt 0 // Pin 2
#define CC1101_GDO0 2

CC1101 radio;

byte syncWord[2] = {199, 10};

#define TX_COUNTDOWN_MS 20

#define DEBUG false

#if DEBUG
#define PRINTS(s)   { Serial.print(F(s)); }
#define PRINT(v)    { Serial.print(v); }
#define PRINTLN(v)  { Serial.println(v); }
#define PRINTLNS(s)  { Serial.println(F(s)); }
#define PRINT2(s,v)  { Serial.print(F(s)); Serial.print(v); }
#define PRINT3(s,v,b) { Serial.print(F(s)); Serial.print(v, b); }
#else
#define PRINTS(s)
#define PRINT(v)
#define PRINTLN(v)
#define PRINTLNS(s)
#define PRINT2(s,v)
#define PRINT3(s,v,b)
#endif

#define EXP_BACKOFF(a,b,x) (a + b * (2^x))

// Get signal strength indicator in dBm.
// See: http://www.ti.com/lit/an/swra114d/swra114d.pdf
int rssi(char raw) {
    uint8_t rssi_dec;
    // TODO: This rssi_offset is dependent on baud and MHz; this is for 38.4kbps and 433 MHz.
    uint8_t rssi_offset = 74;
    rssi_dec = (uint8_t) raw;
    if (rssi_dec >= 128)
        return ((int)( rssi_dec - 256) / 2) - rssi_offset;
    else
        return (rssi_dec / 2) - rssi_offset;
}

// Get link quality indicator.
int lqi(char raw) {
    return 0x3F - raw;
}

// input and output buffers
CircularBuffer<uint8_t, 400> input_buffer;
CircularBuffer<uint8_t, 100> output_buffer;

// Used for sending and receiving data from the CC1101
CCPACKET ccPacket;

bool packetWaiting;

void messageReceived() {
  PRINTLN("In messageReceived");
  packetWaiting = true;
}

unsigned long last_tx = 0;
const uint8_t max_packet_len = 48;
uint8_t tx_packet_buffer[max_packet_len];

void setup() {
  radio.init();
  radio.setSyncWord(syncWord);
  radio.setCarrierFreq(CFREQ_433);
  radio.disableAddressCheck();
  radio.setTxPowerAmp(PA_LowPower);
  // Set other registers to change frequency, modulation, baud rate, set
  // See SmartRF Studio for more info
  //radio.writeReg(CC1101_MDMCFG2, 0x03);
  //radio.writeReg(CC1101_MDMCFG3, 0x83);
  //radio.writeReg(CC1101_MDMCFG4, 0xF5);
  //radio.writeReg(CC1101_DEVIATN, 0x40);

  attachInterrupt(CC1101Interrupt, messageReceived, FALLING);

  // Could go faster than 9600 if desired
  Serial.begin(9600);
  PRINTLN("CC1101_PARTNUM ");
  PRINTLN(radio.readReg(CC1101_PARTNUM, CC1101_STATUS_REGISTER));
  PRINTLN("CC1101_VERSION ");
  PRINTLN(radio.readReg(CC1101_VERSION, CC1101_STATUS_REGISTER));
  PRINTLN("CC1101_MARCSTATE ");
  PRINTLN(radio.readReg(CC1101_MARCSTATE, CC1101_STATUS_REGISTER) & 0x1f);

  PRINTLN("CC1101 radio initialized.");
  PRINTLN("Begin!");
}

void loop() {
  //PRINT("loop "); PRINTLN(packetWaiting);
  if(packetWaiting) {
    // If there's a packet waiting, read it into the buffer
    detachInterrupt(CC1101Interrupt);
    packetWaiting = false;
    if (radio.receiveData(&ccPacket) > 0) {
      if (ccPacket.crc_ok && ccPacket.length > 0) {
        PRINT("Got Packet, CRC OK "); PRINTLN(ccPacket.length);
        for(uint8_t i=0; i<ccPacket.length; i++) {
          PRINT(ccPacket.data[i]); PRINT(" ");
          bool res = output_buffer.push(ccPacket.data[i]);
          if(!res) {
            PRINTLN("!!! Output Buffer Overrun !!!");
            break;
          }
        }
      } else {
        PRINT("Got Packet, CRC not OK "); PRINTLN(ccPacket.length);
      }
    }
    attachInterrupt(CC1101Interrupt, messageReceived, FALLING);
  }

  // Check incoming serial data
  while (Serial.available() && input_buffer.available() > 0) {
    PRINTLN("Reading Serial input");
    uint8_t byte = Serial.read();
    bool res = input_buffer.push(byte);
    if(!res) {
      // Shouldn't happen
      PRINTLN("!!!! Input Buffer Overrun !!!!");
      break;
    }
  }

  // Check if we can TX
  unsigned long now = millis();
  if(now - last_tx > TX_COUNTDOWN_MS) {
    // If there is any data to send, send it
    uint8_t n = min(input_buffer.size(), max_packet_len);
    if(n > 0) {
      for(uint8_t i=0; i<n; i++) {
        ccPacket.data[i] = input_buffer.shift();
      }
      ccPacket.length = n;
      PRINTLN("Sending packet");
      //delayMicroseconds(75); // settling time for other receiver, is this necessary?
      detachInterrupt(CC1101Interrupt);
      radio.sendData(ccPacket);
      attachInterrupt(CC1101Interrupt, messageReceived, FALLING);
      last_tx = now;
    }
  }

  // Output to Serial, if anything is waiting
  while(output_buffer.size() > 0) {
    Serial.write(output_buffer.shift());
  }
}
