/* SPDX-License-Identifier: Apache-2.0 */
/*
 * EVRe.cpp
 *
 *  Created on: Aug 5, 2024
 *      Author: Ragab
 */

#include "EVRe.h"

//nLength: the length of the full packet minus 3 bytes (2-byte for CRC and 1-byte for end bytes).
//pData: first nLength bytes in the packet.
// calculate the 16-bit CRC of data with predetermined length.
#if EVRE_CRC_TABLE_RUNTIME
uint16_t crctab16[256];

/* CRC-16/X-25 table, reflected polynomial 0x8408. Idempotent. */
void EVRe_CrcTableInit(void) {
	for (unsigned i = 0; i < 256; ++i) {
		uint16_t v = (uint16_t) i;
		for (unsigned b = 0; b < 8; ++b) {
			v = (v & 1U) ? (uint16_t) ((v >> 1) ^ 0x8408U) : (uint16_t) (v >> 1);
		}
		crctab16[i] = v;
	}
}
#endif

uint16_t GetCrc16(const uint8_t *pData, int nLength) {
	uint16_t fcs = 0xffff; // initialization
	while (nLength > 0) {
		fcs = (fcs >> 8) ^ crctab16[(fcs ^ *pData) & 0xff];
		nLength--;
		pData++;
	}
	return ~fcs; // negated
}

/* The decoder proper. decodePacket() below wraps it and turns a rejection
 * into an ERROR_RESP frame. */
static uint8_t decodeCore(base_t *device, uint8_t *PACKET, uint16_t pSize, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen) {
	if (device == nullptr) {
		return INSTANCE_IS_NULL;
	}
	if (PACKET == nullptr || pSize < PACKET_BASE_SIZE) {
		return INVALID_PACKET_ERR; /* pSize - 3 would underflow below */
	}

	(*outLen) = 0;

	uint16_t PACKET_CRC = 0;
	uint8_t *CRC_Byte = (uint8_t*) &PACKET_CRC;

	PACKET_CRC = GetCrc16(PACKET, pSize - 0x03);

	if (PACKET[CRC0(pSize)] == CRC_Byte[0] && PACKET[CRC1(pSize)] == CRC_Byte[1]) {

		uint8_t isBroadcast = (PACKET[SLAVE_ID] == BROADCAST_ID) ? 1U : 0U;
		if (!isBroadcast && PACKET[SLAVE_ID] != device->SALVE_ID_REG) {
			return SLAVE_ID_MISMATCHED;
		}
		/* Every slave hears a broadcast, so nothing may answer one: only the
		 * functions that produce no response are allowed. */
		if (isBroadcast && PACKET[FN_CODE] != WRITE) {
			return FUNCTION_CODE_ERR;
		}

		uint16_t REG_OFFSET = 0;
		uint8_t *REG_OFFSET_Byte = (uint8_t*) &REG_OFFSET;

		REG_OFFSET_Byte[0] = PACKET[OFFSET0];
		REG_OFFSET_Byte[1] = PACKET[OFFSET1];

		uint16_t REG_CNT = 0;
		uint8_t *REG_CNT_Byte = (uint8_t*) &REG_CNT;

		REG_CNT_Byte[0] = PACKET[REG_CNT0];
		REG_CNT_Byte[1] = PACKET[REG_CNT1];

		/* The declared register count must match the bytes that actually
		 * arrived. Without this a short packet claiming a large REG_CNT makes
		 * the data loops below read past the end of PACKET and write whatever
		 * follows it into live registers. */
		if (PACKET[FN_CODE] == WRITE || PACKET[FN_CODE] == WRITE_ACK
				|| PACKET[FN_CODE] == READ_RESP) {
			if (pSize != (uint16_t) (PACKET_BASE_SIZE + REG_CNT)) {
				return LENGTH_MISMATCH;
			}
		} else if (pSize != PACKET_BASE_SIZE) {
			return LENGTH_MISMATCH;
		}

		switch (PACKET[FN_CODE]) {
			case READ:
			{
				switch ((REG_OFFSET & 0xF000)) {
					case 0xA000:
					{
						if (!(REG_OFFSET >= 0xA000 && REG_OFFSET <= device->RESERVED_REG_READ_MAX)) {
							return REG_OFFSET_OUT_OF_RANGE;
						} else if ((REG_OFFSET + REG_CNT - 1) > device->RESERVED_REG_READ_MAX) {
							return REG_CNT_OUT_OF_RANGE;
						}
						break;
					}
					case 0xD000:
					{
						if (!(REG_OFFSET >= 0xD000 && REG_OFFSET <= device->DEVICE_REG_READ_MAX)) {
							return REG_OFFSET_OUT_OF_RANGE;
						} else if ((REG_OFFSET + REG_CNT - 1) > device->DEVICE_REG_READ_MAX) {
							return REG_CNT_OUT_OF_RANGE;
						}
						break;
					}
					default:
						return PERMISSION_DENIED;
				}

				(*outLen) = PACKET_BASE_SIZE + REG_CNT;

				if (outBuf == nullptr || outMax < (*outLen)) {
					(*outLen) = 0;
					return BUFFER_TOO_SMALL;
				} else {
					outBuf[START] = 0x7B;

					outBuf[SLAVE_ID] = device->SALVE_ID_REG;

					outBuf[FN_CODE] = READ_RESP;

					outBuf[OFFSET0] = PACKET[OFFSET0];
					outBuf[OFFSET1] = PACKET[OFFSET1];

					outBuf[REG_CNT0] = PACKET[REG_CNT0];
					outBuf[REG_CNT1] = PACKET[REG_CNT1];

					switch ((REG_OFFSET & 0xF000)) {
						case 0xA000:
						{
							for (uint16_t OFFSET_IND = REG_OFFSET; OFFSET_IND < (REG_OFFSET + REG_CNT); ++OFFSET_IND) {
								outBuf[DATAx(OFFSET_IND - REG_OFFSET)] = *(device->A000[OFFSET_IND & 0x0FFF]);
							}
							break;
						}
						case 0xD000:
						{
							for (uint16_t OFFSET_IND = REG_OFFSET; OFFSET_IND < (REG_OFFSET + REG_CNT); ++OFFSET_IND) {
								outBuf[DATAx(OFFSET_IND - REG_OFFSET)] = *(device->D000[OFFSET_IND & 0x0FFF]);
							}
							break;
						}
					}

					PACKET_CRC = GetCrc16(outBuf, (*outLen) - 0x03);

					outBuf[CRC0((*outLen))] = CRC_Byte[0];
					outBuf[CRC1((*outLen))] = CRC_Byte[1];

					outBuf[END((*outLen))] = 0x7D;
				}

				break;
			}

			case READ_RESP:
			{
				switch ((REG_OFFSET & 0xF000)) {
					case 0xA000:
					{
						if (!(REG_OFFSET >= 0xA000 && REG_OFFSET <= device->RESERVED_REG_READ_MAX)) {
							return REG_OFFSET_OUT_OF_RANGE;
						} else if ((REG_OFFSET + REG_CNT - 1) > device->RESERVED_REG_READ_MAX) {
							return REG_CNT_OUT_OF_RANGE;
						}
						break;
					}
					case 0xD000:
					{
						if (!(REG_OFFSET >= 0xD000 && REG_OFFSET <= device->DEVICE_REG_READ_MAX)) {
							return REG_OFFSET_OUT_OF_RANGE;
						} else if ((REG_OFFSET + REG_CNT - 1) > device->DEVICE_REG_READ_MAX) {
							return REG_CNT_OUT_OF_RANGE;
						}
						break;
					}
					default:
						return PERMISSION_DENIED;
				}

				switch ((REG_OFFSET & 0xF000)) {
					case 0xA000:
					{
						for (uint16_t OFFSET_IND = REG_OFFSET; OFFSET_IND < (REG_OFFSET + REG_CNT); ++OFFSET_IND) {
							*(device->A000[OFFSET_IND & 0x0FFF]) = PACKET[DATAx(OFFSET_IND - REG_OFFSET)];
						}
						break;
					}
					case 0xD000:
					{
						for (uint16_t OFFSET_IND = REG_OFFSET; OFFSET_IND < (REG_OFFSET + REG_CNT); ++OFFSET_IND) {
							*(device->D000[OFFSET_IND & 0x0FFF]) = PACKET[DATAx(OFFSET_IND - REG_OFFSET)];
						}
						break;
					}
				}
				break;
			}

			case WRITE_ACK:
			{
				switch ((REG_OFFSET & 0xF000)) {
					case 0xA000:
					{
						if (!(REG_OFFSET >= device->RESERVED_REG_WRITE_MIN && REG_OFFSET <= device->RESERVED_REG_READ_MAX)) {
							return PERMISSION_DENIED;
						} else if ((REG_OFFSET + REG_CNT - 1) > (device->RESERVED_REG_READ_MAX - 0x0FF + device->MSG_CNT)) {
							return REG_CNT_OUT_OF_RANGE;
						}
						break;
					}
					case 0xD000:
					{
						if (!(REG_OFFSET >= device->DEVICE_REG_WRITE_MIN && REG_OFFSET <= device->DEVICE_REG_READ_MAX)) {
							return PERMISSION_DENIED;
						} else if ((REG_OFFSET + REG_CNT - 1) > device->DEVICE_REG_READ_MAX) {
							return REG_CNT_OUT_OF_RANGE;
						}
						break;
					}
				}

				(*outLen) = PACKET_BASE_SIZE;

				if (outBuf == nullptr || outMax < (*outLen)) {
					(*outLen) = 0;
					return BUFFER_TOO_SMALL;
				} else {
					outBuf[START] = 0x7B;

					outBuf[SLAVE_ID] = device->SALVE_ID_REG;

					outBuf[FN_CODE] = WRITE_ACK_RESP;

					outBuf[OFFSET0] = PACKET[OFFSET0];
					outBuf[OFFSET1] = PACKET[OFFSET1];

					outBuf[REG_CNT0] = PACKET[REG_CNT0];
					outBuf[REG_CNT1] = PACKET[REG_CNT1];

					PACKET_CRC = GetCrc16(outBuf, (*outLen) - 0x03);

					outBuf[CRC0((*outLen))] = CRC_Byte[0];
					outBuf[CRC1((*outLen))] = CRC_Byte[1];

					outBuf[END((*outLen))] = 0x7D;
				}
				goto AFTER_WRITE_ACK;
				break;
			}

			case WRITE:
			{
				switch ((REG_OFFSET & 0xF000)) {
					case 0xA000:
					{
						if (!(REG_OFFSET >= device->RESERVED_REG_WRITE_MIN && REG_OFFSET <= device->RESERVED_REG_READ_MAX)) {
							return PERMISSION_DENIED;
						} else if ((REG_OFFSET + REG_CNT - 1) > (device->RESERVED_REG_READ_MAX - 0x0FF + device->MSG_CNT)) {
							return REG_CNT_OUT_OF_RANGE;
						}
						break;
					}
					case 0xD000:
					{
						if (!(REG_OFFSET >= device->DEVICE_REG_WRITE_MIN && REG_OFFSET <= device->DEVICE_REG_READ_MAX)) {
							return PERMISSION_DENIED;
						} else if ((REG_OFFSET + REG_CNT - 1) > device->DEVICE_REG_READ_MAX) {
							return REG_CNT_OUT_OF_RANGE;
						}
						break;
					}
				}

				AFTER_WRITE_ACK: {

					switch ((REG_OFFSET & 0xF000)) {
						case 0xA000:
						{
							uint8_t isACK = false;
							for (uint16_t OFFSET_IND = REG_OFFSET; OFFSET_IND < (REG_OFFSET + REG_CNT); ++OFFSET_IND) {
								if (OFFSET_IND == MSG_CNT_BASE_ADDR) {
									device->MSG_CNT = 0;
									if (device->MSG_ACK_HANDLER[0x00] != nullptr) {
										(*(device->MSG_ACK_HANDLER[0x00]))();
									}
								} else if (OFFSET_IND >= MSG_BUFFER_BASE_ADDR && OFFSET_IND < (MSG_BUFFER_BASE_ADDR + 0x0FF)) {
									isACK = true;
									if (device->MSG_ACK_HANDLER[(*(device->A000[OFFSET_IND & 0x0FFF]))] != nullptr) {
										(*(device->MSG_ACK_HANDLER[(*(device->A000[OFFSET_IND & 0x0FFF]))]))();
									}
									*(device->A000[OFFSET_IND & 0x0FFF]) = 0x00;
								} else {
									*(device->A000[OFFSET_IND & 0x0FFF]) = PACKET[DATAx(OFFSET_IND - REG_OFFSET)];
								}
							}

							if (isACK) {
								uint8_t NEW_MSG_CNT = 0;
								uint8_t NEW_MSG_BUFFER[255] = { 0 };

								for (uint8_t ind = 0; ind < device->MSG_CNT; ++ind) {
									if (device->MSG_BUFFER[ind]) {
										NEW_MSG_BUFFER[NEW_MSG_CNT] = device->MSG_BUFFER[ind];
										++NEW_MSG_CNT;
									}
								}

								device->MSG_CNT = NEW_MSG_CNT;

								for (uint8_t ind = 0; ind < device->MSG_CNT; ++ind) {
									device->MSG_BUFFER[ind] = NEW_MSG_BUFFER[ind];
								}
							}
							break;
						}
						case 0xD000:
						{
							for (uint16_t OFFSET_IND = REG_OFFSET; OFFSET_IND < (REG_OFFSET + REG_CNT); ++OFFSET_IND) {
								*(device->D000[OFFSET_IND & 0x0FFF]) = PACKET[DATAx(OFFSET_IND - REG_OFFSET)];
							}
							break;
						}
					}
				}
				break;
			}

			case WRITE_ACK_RESP:
			{
				switch ((REG_OFFSET & 0xF000)) {
					case 0xA000:
					{
						if (!(REG_OFFSET >= device->RESERVED_REG_WRITE_MIN && REG_OFFSET <= device->RESERVED_REG_READ_MAX)) {
							return PERMISSION_DENIED;
						} else if ((REG_OFFSET + REG_CNT - 1) > (device->RESERVED_REG_READ_MAX - 0x0FF + device->MSG_CNT)) {
							return REG_CNT_OUT_OF_RANGE;
						}
						break;
					}
					case 0xD000:
					{
						if (!(REG_OFFSET >= device->DEVICE_REG_WRITE_MIN && REG_OFFSET <= device->DEVICE_REG_READ_MAX)) {
							return PERMISSION_DENIED;
						} else if ((REG_OFFSET + REG_CNT - 1) > device->DEVICE_REG_READ_MAX) {
							return REG_CNT_OUT_OF_RANGE;
						}
						break;
					}
				}

				break;
			}

			default:
				return FUNCTION_CODE_ERR;
		}
	} else {
		return INVALID_PACKET_ERR;
	}

	setBit(device->CONFIG, HEARTBEAT);

	return NO_ERROR;
}

/* ERROR_RESP: echoes the offset and count of the request and carries one
 * ERR_CODE_ENUM byte, so the host can tie the failure to the exact request. */
static uint8_t buildErrorFrame(uint8_t slaveId, uint16_t regOffset, uint16_t regCount,
		uint8_t errCode, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen) {
	(*outLen) = PACKET_BASE_SIZE + 0x01;

	if (outBuf == nullptr || outMax < (*outLen)) {
		(*outLen) = 0;
		return BUFFER_TOO_SMALL;
	}

	uint16_t PACKET_CRC = 0;
	uint8_t *CRC_Byte = (uint8_t*) &PACKET_CRC;
	uint8_t *REG_OFFSET_Byte = (uint8_t*) &regOffset;
	uint8_t *REG_CNT_Byte = (uint8_t*) &regCount;

	outBuf[START] = 0x7B;
	outBuf[SLAVE_ID] = slaveId;
	outBuf[FN_CODE] = ERROR_RESP;
	outBuf[OFFSET0] = REG_OFFSET_Byte[0];
	outBuf[OFFSET1] = REG_OFFSET_Byte[1];
	outBuf[REG_CNT0] = REG_CNT_Byte[0];
	outBuf[REG_CNT1] = REG_CNT_Byte[1];
	outBuf[DATAx(0)] = errCode;

	PACKET_CRC = GetCrc16(outBuf, (*outLen) - 0x03);

	outBuf[CRC0((*outLen))] = CRC_Byte[0];
	outBuf[CRC1((*outLen))] = CRC_Byte[1];
	outBuf[END((*outLen))] = 0x7D;

	return NO_ERROR;
}

uint8_t decodePacketInto(base_t *device, uint8_t *PACKET, uint16_t pSize, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen) {
	uint8_t err = decodeCore(device, PACKET, pSize, outBuf, outMax, outLen);

	if (err == NO_ERROR || device == nullptr) {
		return err;
	}
	/* Stay silent when the slave id cannot be trusted. A bad CRC means the id
	 * field is suspect too, so answering could put several slaves on the bus
	 * at once; a mismatched id was never ours to answer. */
	if (err == INVALID_PACKET_ERR || err == SLAVE_ID_MISMATCHED) {
		return err;
	}
	if (pSize < PACKET_BASE_SIZE || PACKET == nullptr) {
		return err;
	}
	/* Never answer a broadcast, and never answer a response: two devices
	 * trading ERROR_RESP frames would never stop. */
	if (PACKET[SLAVE_ID] == BROADCAST_ID) {
		return err;
	}
	if (PACKET[FN_CODE] == READ_RESP || PACKET[FN_CODE] == WRITE_ACK_RESP
			|| PACKET[FN_CODE] == ERROR_RESP) {
		return err;
	}

	uint16_t off = (uint16_t) (PACKET[OFFSET0] | (PACKET[OFFSET1] << 8));
	uint16_t cnt = (uint16_t) (PACKET[REG_CNT0] | (PACKET[REG_CNT1] << 8));
	buildErrorFrame(device->SALVE_ID_REG, off, cnt, err, outBuf, outMax, outLen);
	return err;
}

uint8_t encodePacketInto(base_t *device, uint8_t slaveId, uint8_t fnCode, uint16_t regOffset, uint16_t regCount, uint8_t *pData, uint8_t *outBuf, uint16_t outMax, uint16_t *outLen) {
	if (device == nullptr) {
		return INSTANCE_IS_NULL;
	}

	(*outLen) = 0;

	uint16_t PACKET_CRC = 0;
	uint8_t *CRC_Byte = (uint8_t*) &PACKET_CRC;

	uint16_t REG_OFFSET = regOffset;
	uint8_t *REG_OFFSET_Byte = (uint8_t*) &REG_OFFSET;

	uint16_t REG_CNT = regCount;
	uint8_t *REG_CNT_Byte = (uint8_t*) &REG_CNT;

	switch (fnCode) {
		case READ:
		{
			switch ((REG_OFFSET & 0xF000)) {
				case 0xA000:
				{
					if (!(REG_OFFSET >= 0xA000 && REG_OFFSET <= device->RESERVED_REG_READ_MAX)) {
						return REG_OFFSET_OUT_OF_RANGE;
					} else if ((REG_OFFSET + REG_CNT - 1) > device->RESERVED_REG_READ_MAX) {
						return REG_CNT_OUT_OF_RANGE;
					}
					break;
				}
				case 0xD000:
				{
					if (!(REG_OFFSET >= 0xD000 && REG_OFFSET <= device->DEVICE_REG_READ_MAX)) {
						return REG_OFFSET_OUT_OF_RANGE;
					} else if ((REG_OFFSET + REG_CNT - 1) > device->DEVICE_REG_READ_MAX) {
						return REG_CNT_OUT_OF_RANGE;
					}
					break;
				}
				default:
					return PERMISSION_DENIED;
			}

			(*outLen) = PACKET_BASE_SIZE;

			if (outBuf == nullptr || outMax < (*outLen)) {
				(*outLen) = 0;
				return BUFFER_TOO_SMALL;
			} else {
				outBuf[START] = 0x7B;

				outBuf[SLAVE_ID] = slaveId;

				outBuf[FN_CODE] = fnCode;

				outBuf[OFFSET0] = REG_OFFSET_Byte[0];
				outBuf[OFFSET1] = REG_OFFSET_Byte[1];

				outBuf[REG_CNT0] = REG_CNT_Byte[0];
				outBuf[REG_CNT1] = REG_CNT_Byte[1];

				PACKET_CRC = GetCrc16(outBuf, (*outLen) - 0x03);

				outBuf[CRC0((*outLen))] = CRC_Byte[0];
				outBuf[CRC1((*outLen))] = CRC_Byte[1];

				outBuf[END((*outLen))] = 0x7D;
			}
			break;
		}

		/* A READ_RESP carries data like a WRITE, but its range is checked
		 * against the READ limits, not the write limits: it reports registers,
		 * it does not modify them. The body is shared with WRITE / WRITE_ACK
		 * below. Lets a device originate a response, which unsolicited and
		 * streaming modes need. */
		case READ_RESP:
		{
			switch ((REG_OFFSET & 0xF000)) {
				case 0xA000:
				{
					if (!(REG_OFFSET >= 0xA000 && REG_OFFSET <= device->RESERVED_REG_READ_MAX)) {
						return REG_OFFSET_OUT_OF_RANGE;
					} else if ((REG_OFFSET + REG_CNT - 1) > device->RESERVED_REG_READ_MAX) {
						return REG_CNT_OUT_OF_RANGE;
					}
					break;
				}
				case 0xD000:
				{
					if (!(REG_OFFSET >= 0xD000 && REG_OFFSET <= device->DEVICE_REG_READ_MAX)) {
						return REG_OFFSET_OUT_OF_RANGE;
					} else if ((REG_OFFSET + REG_CNT - 1) > device->DEVICE_REG_READ_MAX) {
						return REG_CNT_OUT_OF_RANGE;
					}
					break;
				}
				default:
					return PERMISSION_DENIED;
			}
			goto BUILD_DATA_PACKET;
		}

		case WRITE:
		case WRITE_ACK:
		{
			switch ((REG_OFFSET & 0xF000)) {
				case 0xA000:
				{
					if (!(REG_OFFSET >= device->RESERVED_REG_WRITE_MIN && REG_OFFSET <= device->RESERVED_REG_READ_MAX)) {
						return PERMISSION_DENIED;
					} else if ((REG_OFFSET + REG_CNT - 1) > (device->RESERVED_REG_READ_MAX - 0x0FF + device->MSG_CNT)) {
						return REG_CNT_OUT_OF_RANGE;
					}
					break;
				}
				case 0xD000:
				{
					if (!(REG_OFFSET >= device->DEVICE_REG_WRITE_MIN && REG_OFFSET <= device->DEVICE_REG_READ_MAX)) {
						return PERMISSION_DENIED;
					} else if ((REG_OFFSET + REG_CNT - 1) > device->DEVICE_REG_READ_MAX) {
						return REG_CNT_OUT_OF_RANGE;
					}
					break;
				}
			}

			BUILD_DATA_PACKET: {
			(*outLen) = PACKET_BASE_SIZE + REG_CNT;

			if (outBuf == nullptr || outMax < (*outLen)) {
				(*outLen) = 0;
				return BUFFER_TOO_SMALL;
			} else {
				outBuf[START] = 0x7B;

				outBuf[SLAVE_ID] = slaveId;

				outBuf[FN_CODE] = fnCode;

				outBuf[OFFSET0] = REG_OFFSET_Byte[0];
				outBuf[OFFSET1] = REG_OFFSET_Byte[1];

				outBuf[REG_CNT0] = REG_CNT_Byte[0];
				outBuf[REG_CNT1] = REG_CNT_Byte[1];

				if (pData != nullptr) {
					for (uint16_t ind = 0; ind < REG_CNT; ++ind) {
						outBuf[DATAx(ind)] = pData[ind];
					}
				} else {
					switch ((REG_OFFSET & 0xF000)) {
						case 0xA000:
						{
							for (uint16_t OFFSET_IND = REG_OFFSET; OFFSET_IND < (REG_OFFSET + REG_CNT); ++OFFSET_IND) {
								outBuf[DATAx(OFFSET_IND - REG_OFFSET)] = *(device->A000[OFFSET_IND & 0x0FFF]);
							}
							break;
						}
						case 0xD000:
						{
							for (uint16_t OFFSET_IND = REG_OFFSET; OFFSET_IND < (REG_OFFSET + REG_CNT); ++OFFSET_IND) {
								outBuf[DATAx(OFFSET_IND - REG_OFFSET)] = *(device->D000[OFFSET_IND & 0x0FFF]);
							}
							break;
						}
					}
				}

				PACKET_CRC = GetCrc16(outBuf, (*outLen) - 0x03);

				outBuf[CRC0((*outLen))] = CRC_Byte[0];
				outBuf[CRC1((*outLen))] = CRC_Byte[1];

				outBuf[END((*outLen))] = 0x7D;
			}
			}
			break;
		}

		default:
			return FUNCTION_CODE_ERR;
	}
	return NO_ERROR;
}

/* ---------------------------------------------------------------------- */
/* Allocating forms, kept for compatibility: work out how big the response
/* can be, allocate once, and hand the buffer to the *Into form above.      */
/* ---------------------------------------------------------------------- */

uint8_t decodePacket(base_t *device, uint8_t *PACKET, uint16_t pSize, uint8_t **RESPONSE, uint16_t *rSize) {
	(*RESPONSE) = nullptr;
	(*rSize) = 0;

	uint32_t want = PACKET_BASE_SIZE + 0x01U; /* an ERROR_RESP always fits */
	if (PACKET != nullptr && pSize >= PACKET_BASE_SIZE && PACKET[FN_CODE] == READ) {
		uint32_t cnt = (uint32_t) (PACKET[REG_CNT0] | (PACKET[REG_CNT1] << 8));
		if ((PACKET_BASE_SIZE + cnt) > want) {
			want = PACKET_BASE_SIZE + cnt;
		}
	}
	if (want > 0xFFFFU) {
		want = 0xFFFFU;
	}

	uint8_t *buf = (uint8_t*) calloc(want, sizeof(uint8_t));
	if (buf == nullptr) {
		return MEM_ALLOCATION_FAILED;
	}

	uint16_t len = 0;
	uint8_t err = decodePacketInto(device, PACKET, pSize, buf, (uint16_t) want, &len);

	if (len == 0U) {
		free(buf); /* nothing to answer */
	} else {
		(*RESPONSE) = buf;
		(*rSize) = len;
	}
	return err;
}

uint8_t encodePacket(base_t *device, uint8_t slaveId, uint8_t fnCode, uint16_t regOffset, uint16_t regCount, uint8_t *pData, uint8_t **PACKET, uint16_t *pSize) {
	(*PACKET) = nullptr;
	(*pSize) = 0;

	uint32_t want = (fnCode == READ) ? (uint32_t) PACKET_BASE_SIZE
			: (PACKET_BASE_SIZE + (uint32_t) regCount);
	if (want > 0xFFFFU) {
		want = 0xFFFFU;
	}

	uint8_t *buf = (uint8_t*) calloc(want, sizeof(uint8_t));
	if (buf == nullptr) {
		return MEM_ALLOCATION_FAILED;
	}

	uint16_t len = 0;
	uint8_t err = encodePacketInto(device, slaveId, fnCode, regOffset, regCount, pData,
			buf, (uint16_t) want, &len);

	if (len == 0U) {
		free(buf);
	} else {
		(*PACKET) = buf;
		(*pSize) = len;
	}
	return err;
}

uint8_t addMsg(base_t *device, uint8_t MSG) {
	if (device == nullptr) {
		return INSTANCE_IS_NULL;
	}

	if (!MSG) {
		return MSG_NULL;
	} else if (device->MSG_CNT == 0xFF) {
		return MSG_BUFFER_FULL;
	} else {
		device->MSG_BUFFER[device->MSG_CNT] = MSG;
		++(device->MSG_CNT);
		return NO_ERROR;
	}
}

uint8_t protocolInit(base_t *device) {
	if (device == nullptr) {
		return INSTANCE_IS_NULL;
	}

#if EVRE_CRC_TABLE_RUNTIME
	EVRe_CrcTableInit();
#endif

	device->A000 = new uint8_t*[(device->RESERVED_REG_READ_MAX + 1) & 0x0FFF];

	if (!(device->A000)) {
		return MEM_ALLOCATION_FAILED;
	}

	device->A000[0x000] = getByteAddress(&(device->DEVICE_ID), BYTE0);
	device->A000[0x001] = getByteAddress(&(device->DEVICE_ID), BYTE1);

	device->A000[0x002] = getByteAddress(&(device->STATUS), BYTE0);
	device->A000[0x003] = getByteAddress(&(device->STATUS), BYTE1);

	device->A000[0x004] = getByteAddress(&(device->CONFIG), BYTE0);
	device->A000[0x005] = getByteAddress(&(device->CONFIG), BYTE1);

	device->A000[0x006] = getByteAddress(&(device->MSG_CNT), BYTE0);

	for (uint16_t ind = MSG_BUFFER_BASE_ADDR; ind < (MSG_BUFFER_BASE_ADDR + 0x0FF); ++ind) {
		device->A000[ind & 0x0FFF] = &(device->MSG_BUFFER[(ind - MSG_BUFFER_BASE_ADDR)]);
	}

	for (uint16_t ind = 0; ind < 0x0FF; ++ind) {
		device->MSG_ACK_HANDLER[ind] = nullptr;
	}

	/* What this library provides. protocolConfigure() adds the bits that are
	 * the device's own (AUTO_SEND, DFU, ...). */
	device->STATUS = (uint16_t) (EVRE_PROTOCOL_VERSION | CAP_ERROR_FRAME
			| CAP_BROADCAST | CAP_MSG | CAP_STATIC);

	return protocolConfigure(device);
}

uint8_t __attribute__((weak)) protocolConfigure(base_t *device) {
	return NO_ERROR;
}

