#include <Arduino.h>

#define FEND 0xC0
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD

// KISS commands
#define CMD_UNKNOWN 0xFE
#define CMD_DATA 0x00
#define CMD_TXDELAY 0x01
#define CMD_P 0x02
#define CMD_SLOTTIME 0x03
#define CMD_TXTAIL 0x04
#define CMD_FULLDUPLEX 0x05
#define CMD_SETHARDWARE 0x06
#define CMD_RETURN 0xFF

#define AX25_MAX_FRAME_LEN 330

// types
typedef struct KISSCtx {
    size_t frame_len = 0;
    bool in_escape = false;
    bool in_frame = false;
    uint8_t command = CMD_UNKNOWN;
    uint8_t hdlc_port = 0;
    uint8_t buffer[AX25_MAX_FRAME_LEN]; // http://ax25.net/kiss.aspx recommends 1kb buffer len
} KISSCtx;

/**
 * This is called when a KISS packet is decoded. The data passed here is no
 * longer KISS framed.
 */
extern void on_kiss_packet(uint8_t * data, uint8_t len);

/*
 * Called as each byte of a KISS frame is received. Bytes are buffered
 * in the global KISSCtx buffer which has enough room for the max length AX25
 * frame (330 bytes). ax25.net recommends this be higher (1kb)
 */
void read_kiss(uint8_t b, KISSCtx *kiss) {
  if (kiss->in_frame && b == FEND && kiss->command == CMD_DATA) {
    // end of a data frame
    kiss->in_frame = false;
    on_kiss_packet(kiss->buffer, kiss->frame_len);
  } else if (b == FEND) {
    // beginning of data frmae
    kiss->in_frame = true;
    kiss->command = CMD_UNKNOWN;
    kiss->frame_len = 0;
  } else if (kiss->in_frame && kiss->frame_len < AX25_MAX_FRAME_LEN) {
    // in a frame, check for commands first
    if (kiss->frame_len == 0 && kiss->command == CMD_UNKNOWN) {
      kiss->hdlc_port = b & 0xF0; // multiple HDLC ports are supported by KISS, apparently
      kiss->command = b & 0x0F;
    } else if (kiss->command == CMD_DATA) {
      if (b == FESC) {
        kiss->in_escape = true;
      } else {
        if (kiss->in_escape) {
          if (b == TFEND) b = FEND;
          if (b == TFESC) b = FESC;
          kiss->in_escape = false;
        }
        kiss->buffer[kiss->frame_len++] = b;
      }
    } // TODO implement other commands
  }
}

/**
 * Given a buffer and its length, write the buffer out over Serial encoded
 * with KISS framing
 */
void serial_kiss_wrapper(uint8_t buffer[], size_t len) {
  Serial.write(FEND);
  Serial.write(0x00);
  for (unsigned i = 0; i < len; i++) {
    uint8_t b = buffer[i];
    if (b == FEND) {
      Serial.write(FESC);
      Serial.write(TFEND);
    } else if (b == FESC) {
      Serial.write(FESC);
      Serial.write(TFESC);
    } else {
      Serial.write(b);
    }
  }
  Serial.write(FEND);
}
