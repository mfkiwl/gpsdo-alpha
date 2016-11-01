#include <Arduino.h>
#include "eventLoop.h"
#include "protocol.h"

typedef enum _GSIP_STATE {
  GSIP_INIT = 0,
  GSIP_READ_HEADER,
  GSIP_READ_TYPE,
  GSIP_READ_OPERATION,
  GSIP_READ_PAYLOAD,
  GSIP_READ_CRC
} GSIP_STATE;

GSIP_STATE state = GSIP_INIT;
unsigned char header[4] = { 0x55, 0x55, 0xaa, 0xaa };
int headerPos = 3;
int splitFlag = 0;
GSIP_TYPE type = Cmd;
GSIP_OPERATION operation = ReadVersion;
GSIP_PAYLOAD payload;
unsigned char* payloadBuf = (unsigned char*)malloc(4);
int payloadPos = 3;
unsigned short crc = 0;

void resetState() {
  headerPos = 3;
  splitFlag = 0;
  type = Cmd;
  operation = ReadVersion;
  payload.l = 0;
  memset(payloadBuf, 0, 4);
  payloadPos = 3;
  crc = 0; 
}

void initGSIP() {
  resetState();
  on("cmd", (EventCallback)execCmd);
  on("writeMsg", (EventCallback)writeMsg);
}

void getMsg() {
  GSIP_MSG* msg;
  unsigned char next = 0;
  // find cmd from serial buffer with FSM
  while (Serial.available()) {
    next = Serial.read();
    
    switch(state) {
      case GSIP_INIT:
        if (next == header[headerPos]) {
          headerPos--;
          state = GSIP_READ_HEADER;
        }
      break;

      case GSIP_READ_HEADER:
        // x86 and arduino are both little-endian
        if (header[headerPos] == next) {
          if (headerPos == 0) {
            headerPos = 3;
            state = GSIP_READ_TYPE;
          }
          else
            headerPos--;
        }
        else {
          resetState();
          state = GSIP_INIT;
        }
      break;

      case GSIP_READ_TYPE:
        if (splitFlag == 0 && next == '|') {
          splitFlag = 1;
        }
        else if (splitFlag == 1) {
          type = (GSIP_TYPE)next;
          splitFlag = 0;
          state = GSIP_READ_OPERATION;
        }
        else {
          resetState();
          state = GSIP_INIT;
        }
      break;

      case GSIP_READ_OPERATION:
        if (splitFlag == 0 && next == '|') {
          splitFlag = 1;
        }
        else if (splitFlag == 1) {
          operation = (GSIP_OPERATION)next;
          splitFlag = 0;
          state = GSIP_READ_PAYLOAD;
        }
        else {
          resetState();
          state = GSIP_INIT;
        }
      break;

      case GSIP_READ_PAYLOAD:
        if (splitFlag == 0 && next == '|') {
          splitFlag = 1;
        }
        else if (splitFlag == 1) {
          // on Arduino side, the max length of payload is 4 bytes, so it's simpler to read payload
          // on PC side, we must consider string with unknown max length
          if (next == '|') {
            // ok, let's go to read crc...
            state = GSIP_READ_CRC;
          }
          else {
            if (payloadPos == -1) {
              resetState();
              state = GSIP_INIT;
            }
            else {
              payloadBuf[payloadPos] = next;
              payloadPos--;
            }
          }
        }
        else {
          resetState();
          state = GSIP_INIT;
        }
      break;

      case GSIP_READ_CRC:
        crc = next;
        
        // create msg and trigger
        msg = (GSIP_MSG*)malloc(sizeof(GSIP_MSG));
        msg->type = type;
        msg->operation = operation;
        memcpy((void*)&(payload.l), payloadBuf, 4);
        msg->crc7 = crc;
        trigger("cmd", NULL, (void*)msg);
        
        resetState();
        state = GSIP_INIT;
      break;

      default:
        resetState();
        state = GSIP_INIT;
    }
  }
}


void writeMsgImpl(GSIP_TYPE type, GSIP_OPERATION operation, GSIP_PAYLOAD payload) {
  // for arduoni, we'll only send data
  
  int len = 7 + payload; // header 4 + type 1 + operation 1 + payload n + crc 1
  // TODO : create msg buffer
  
  
  // TODO : create crc7
  // TODO : write to serial
}

void writeMsg(void* error, void* param) {
  GSIP_MSG* msg = (GSIP_MSG*)param;
  
  if (error == NULL && msg != NULL) {
    writeMsgImpl(msg->type, msg->operation, msg->payload);
  }

  // TODO : free error and param
}


void execCmd(void* error, void* param) {
  GSIP_MSG* msg = (GSIP_MSG*)param;
  if (msg->type == Cmd) {
    switch(msg->operation) {
      case ReadVersion:
      // TODO : return firmware version
      break;

      case ReadFreq:
      // TODO : read freq and return
      break;
      
      default: 
      // unknown operation
      break;
    }
  }

  free(msg);
}

// the implementation is not optimized for speed, but it's readable and easy to understand
// initial reminder = 0x0
// polynominal = 0x89 = 10001001 = x^7 + x^3 + 1
// both input and polynominal are NOT reflected
// NO final xor
unsigned char calcCrc7(unsigned char* src, int length) {
  unsigned char crc7 = src[0]; // initial reminder = 0x0
  unsigned char pn = 0x89; // polynominal

  int bitCount = 0;
  int byteCount = 1;

  unsigned char nextByte = 0;
  if (length != 1)
    nextByte = src[byteCount];

  while (bitCount < length * 8) {
    // 1. xor if MSB = 1
    if ((crc7 & 0x80) != 0)
      crc7 ^= pn;

    // 2. move next bit into LSB of crc7
    // store MSB of nextByte in nextBit
    unsigned char nextBit = nextByte | 0x7f;
    nextBit = nextBit >> 7;
    // now, nextBit goes into crc7
    crc7 = crc7 << 1;
    crc7 = crc7 | nextBit;

    bitCount++;

    // 3. prepare nextByte
    // move next bit to MSB of nextByte
    nextByte = nextByte << 1;
    // move next byte of src into nextByte if all 8 bits are used
    if (bitCount % 8 == 0) {
      byteCount++;
      if (byteCount == length)
        nextByte = 0;
      else
        nextByte = src[byteCount];
    }
  }

  // move 1 bit right since the resulted 7 bits should be stored in lower bits
  return crc7 >> 1;
}

