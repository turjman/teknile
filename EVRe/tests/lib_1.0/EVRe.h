/* SPDX-License-Identifier: Apache-2.0 */
/*
 * EVRe.h
 *
 *  Created on: Aug 5, 2024
 *      Author: Ragab
 */

#ifndef EVRE_H
#define EVRE_H

#include <stdint.h>
#include <stdbool.h>
#include <cstdlib>

#define PACKET_BASE_SIZE  (0x0AU)

/* This implementation's protocol revision, reported in STATUS[7:0]. */
#define EVRE_PROTOCOL_VERSION (0x01U)

/* Slave 0 addresses every slave on the link. No slave answers it, so only
 * WRITE is accepted; READ and WRITE_ACK would make every slave transmit at
 * once. 0 must never be used as a real device address. */
#define BROADCAST_ID      (0x00U)

#define START   		  (0x00U)

#define SLAVE_ID   		  (0x01U)

#define FN_CODE 		  (0x02U)

#define OFFSET0 		  (0x03U)
#define OFFSET1 		  (0x04U)

#define REG_CNT0 		  (0x05U)
#define REG_CNT1 		  (0x06U)

#define DATAx(ind)  	  (ind+0x07)  > 0 ? (ind+0x07)  : 0 // Index is zero based

#define CRC0(pLEN)  	  (pLEN-0x03) > 0 ? (pLEN-0x03) : 0 // (LSB)
#define CRC1(pLEN)   	  (pLEN-0x02) > 0 ? (pLEN-0x02) : 0 // (MSB)
#define END(pLEN)  		  (pLEN-0x01) > 0 ? (pLEN-0x01) : 0

#define setBit(VAL, BIT) (VAL |= (1<<BIT))
#define clearBit(VAL, BIT) (VAL &= ~(1<<BIT))
#define toggleBit(VAL, BIT) (VAL ^= (1<<BIT))
#define getBit(VAL, BIT) ((VAL&(1<<BIT))>>BIT)

#define getByteAddress(VAL, BYTE) (((uint8_t *)(VAL)) + BYTE)

typedef struct protocol_base {
		uint8_t SALVE_ID_REG = 0;

		uint16_t DEVICE_REG_READ_MAX = 0;
		uint16_t DEVICE_REG_WRITE_MIN = 0;

		const uint16_t RESERVED_REG_READ_MAX = 0xA105;
		const uint16_t RESERVED_REG_WRITE_MIN = 0xA004;

		uint8_t **A000 = nullptr;
		uint8_t **D000 = nullptr;

		uint16_t DEVICE_ID = 0;

		uint16_t STATUS = 0;

		uint16_t CONFIG = 0;

		uint8_t MSG_CNT = 0;

		uint8_t MSG_BUFFER[255] = { 0 };

		void (*MSG_ACK_HANDLER[255])(void) = {0};
} base_t;

/* EVRE_CRC_TABLE_RUNTIME: 0 = 512-byte constant table in flash (default).
 *                        1 = table built in RAM by EVRe_CrcTableInit(),
 *                            called from protocolInit(): frees 512 bytes of
 *                            flash and spends 512 bytes of RAM. Behaviour is
 *                            identical either way. */
#ifndef EVRE_CRC_TABLE_RUNTIME
#define EVRE_CRC_TABLE_RUNTIME 0
#endif

#if EVRE_CRC_TABLE_RUNTIME
extern uint16_t crctab16[256];
void EVRe_CrcTableInit(void);
#else
static const uint16_t crctab16[] = { 0X0000, 0X1189, 0X2312, 0X329B, 0X4624, 0X57AD, 0X6536, 0X74BF, 0X8C48, 0X9DC1, 0XAF5A, 0XBED3, 0XCA6C, 0XDBE5, 0XE97E, 0XF8F7, 0X1081, 0X0108, 0X3393, 0X221A, 0X56A5, 0X472C, 0X75B7, 0X643E, 0X9CC9, 0X8D40, 0XBFDB, 0XAE52, 0XDAED, 0XCB64, 0XF9FF, 0XE876, 0X2102, 0X308B, 0X0210, 0X1399, 0X6726, 0X76AF, 0X4434, 0X55BD, 0XAD4A, 0XBCC3, 0X8E58, 0X9FD1, 0XEB6E, 0XFAE7, 0XC87C, 0XD9F5, 0X3183, 0X200A, 0X1291, 0X0318, 0X77A7, 0X662E, 0X54B5, 0X453C, 0XBDCB, 0XAC42, 0X9ED9,
		0X8F50, 0XFBEF, 0XEA66, 0XD8FD, 0XC974, 0X4204, 0X538D, 0X6116, 0X709F, 0X0420, 0X15A9, 0X2732, 0X36BB, 0XCE4C, 0XDFC5, 0XED5E, 0XFCD7, 0X8868, 0X99E1, 0XAB7A, 0XBAF3, 0X5285, 0X430C, 0X7197, 0X601E, 0X14A1, 0X0528, 0X37B3, 0X263A, 0XDECD, 0XCF44, 0XFDDF, 0XEC56, 0X98E9, 0X8960, 0XBBFB, 0XAA72, 0X6306, 0X728F, 0X4014, 0X519D, 0X2522, 0X34AB, 0X0630, 0X17B9, 0XEF4E, 0XFEC7, 0XCC5C, 0XDDD5, 0XA96A, 0XB8E3, 0X8A78, 0X9BF1, 0X7387, 0X620E, 0X5095, 0X411C, 0X35A3, 0X242A, 0X16B1, 0X0738, 0XFFCF, 0XEE46,
		0XDCDD, 0XCD54, 0XB9EB, 0XA862, 0X9AF9, 0X8B70, 0X8408, 0X9581, 0XA71A, 0XB693, 0XC22C, 0XD3A5, 0XE13E, 0XF0B7, 0X0840, 0X19C9, 0X2B52, 0X3ADB, 0X4E64, 0X5FED, 0X6D76, 0X7CFF, 0X9489, 0X8500, 0XB79B, 0XA612, 0XD2AD, 0XC324, 0XF1BF, 0XE036, 0X18C1, 0X0948, 0X3BD3, 0X2A5A, 0X5EE5, 0X4F6C, 0X7DF7, 0X6C7E, 0XA50A, 0XB483, 0X8618, 0X9791, 0XE32E, 0XF2A7, 0XC03C, 0XD1B5, 0X2942, 0X38CB, 0X0A50, 0X1BD9, 0X6F66, 0X7EEF, 0X4C74, 0X5DFD, 0XB58B, 0XA402, 0X9699, 0X8710, 0XF3AF, 0XE226, 0XD0BD, 0XC134, 0X39C3,
		0X284A, 0X1AD1, 0X0B58, 0X7FE7, 0X6E6E, 0X5CF5, 0X4D7C, 0XC60C, 0XD785, 0XE51E, 0XF497, 0X8028, 0X91A1, 0XA33A, 0XB2B3, 0X4A44, 0X5BCD, 0X6956, 0X78DF, 0X0C60, 0X1DE9, 0X2F72, 0X3EFB, 0XD68D, 0XC704, 0XF59F, 0XE416, 0X90A9, 0X8120, 0XB3BB, 0XA232, 0X5AC5, 0X4B4C, 0X79D7, 0X685E, 0X1CE1, 0X0D68, 0X3FF3, 0X2E7A, 0XE70E, 0XF687, 0XC41C, 0XD595, 0XA12A, 0XB0A3, 0X8238, 0X93B1, 0X6B46, 0X7ACF, 0X4854, 0X59DD, 0X2D62, 0X3CEB, 0X0E70, 0X1FF9, 0XF78F, 0XE606, 0XD49D, 0XC514, 0XB1AB, 0XA022, 0X92B9, 0X8330,
		0X7BC7, 0X6A4E, 0X58D5, 0X495C, 0X3DE3, 0X2C6A, 0X1EF1, 0X0F78 };
#endif

uint16_t GetCrc16(const uint8_t *pData, int nLength);

/* Allocating forms: the response is calloc'ed and the caller must free it.
 * Thin wrappers around the *Into forms below. */
uint8_t decodePacket(base_t *device, uint8_t *PACKET, uint16_t pSize, uint8_t **RESPONSE, uint16_t *rSize);

uint8_t encodePacket(base_t *device, uint8_t slaveId, uint8_t fnCode, uint16_t regOffset, uint16_t regCount, uint8_t *pData, uint8_t **PACKET, uint16_t *pSize);

/* No-heap forms: the caller supplies the buffer. outLen is the number of
 * bytes written (0 when the function produces no response). Returns
 * BUFFER_TOO_SMALL if outMax cannot hold the frame. Use these on small MCUs
 * and anywhere the call happens in an interrupt: they never allocate. */
uint8_t decodePacketInto(base_t *device, uint8_t *PACKET, uint16_t pSize, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen);

uint8_t encodePacketInto(base_t *device, uint8_t slaveId, uint8_t fnCode, uint16_t regOffset, uint16_t regCount, uint8_t *pData, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen);

uint8_t addMsg(base_t *device, uint8_t MSG);

uint8_t protocolInit(base_t *device);
uint8_t protocolConfigure(base_t *device);

enum ERR_CODE_ENUM {
	NO_ERROR = 0U,
	INVALID_PACKET_ERR,
	FUNCTION_CODE_ERR,
	PERMISSION_DENIED,
	REG_OFFSET_OUT_OF_RANGE,
	REG_CNT_OUT_OF_RANGE,
	MEM_ALLOCATION_FAILED,
	SLAVE_ID_MISMATCHED,
	MSG_BUFFER_FULL,
	MSG_NULL,
	INSTANCE_IS_NULL,
	BUFFER_TOO_SMALL, /* caller's buffer too small for the response */
	LENGTH_MISMATCH, /* received length does not match the declared register count */
};

enum FN_CODE_ENUM {
	READ = 0xAA,
	READ_RESP = 0xAB,
	WRITE = 0xEA,
	WRITE_ACK = 0xEB,
	WRITE_ACK_RESP = 0xEC,
	ERROR_RESP = 0xEE /* data section: one ERR_CODE_ENUM byte */
};

enum PERMISSION_ENUM {
	READ_ONLY = 0U,
	READ_WRITE
};

enum RESERVED_MAP_ENUM {
	DEVICE_ID_BASE_ADDR = 0xA000,
	STATUS_BASE_ADDR = 0xA002,
	CONFIG_BASE_ADDR = 0xA004,
	MSG_CNT_BASE_ADDR = 0xA006,
	MSG_BUFFER_BASE_ADDR = 0xA007
};

/* STATUS (0xA002), read only: protocol revision and what this device
 * implements, so a host can detect features instead of assuming them. */
enum STATUS_ENUM {
	STATUS_VERSION_MASK = 0x00FFU, /* [7:0] EVRE_PROTOCOL_VERSION */
	CAP_ERROR_FRAME = 0x0100U, /* answers a rejected packet with ERROR_RESP */
	CAP_BROADCAST = 0x0200U, /* accepts slave 0 for WRITE */
	CAP_MSG = 0x0400U, /* MSG_CNT / MSG_BUFFER implemented */
	CAP_AUTO_SEND = 0x0800U, /* CONFIG bit AUTO_SEND implemented */
	CAP_DFU = 0x1000U, /* CONFIG bit DFU_MODE implemented */
	CAP_STATIC = 0x2000U, /* built without the heap (the *Into functions) */
};

enum CFG_ENUM {
	HEARTBEAT = 0U,
	SYSTEM_RESET,
	MSG_ENABLE,
	AUTO_SEND,
	DFU_MODE
};

enum BYTE_IND_ENUM {
	BYTE0 = 0U,
	BYTE1,
	BYTE2,
	BYTE3,
	BYTE4,
	BYTE5,
	BYTE6,
	BYTE7,
};

#endif

