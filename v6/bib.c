/*
 * NASA Docket No. GSC-18,587-1 and identified as “The Bundle Protocol Core Flight
 * System Application (BP) v6.5”
 *
 * Copyright © 2020 United States Government as represented by the Administrator of
 * the National Aeronautics and Space Administration. All Rights Reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/******************************************************************************
 INCLUDES
 ******************************************************************************/

#include "bplib.h"
#include "bplib_os.h"

#include "sdnv.h"
#include "crc.h"
#include "v6.h"
#include "bib.h"

/******************************************************************************
 STATIC FUNCTIONS
 ******************************************************************************/

/*--------------------------------------------------------------------------------------
 * write_crc16 - Converts a uint16_t to big endian representation and writes it to
 *      buffer.
 *
 * crc: The uint16_t crc to convert to big endian. [INPUT]
 * buffer: The buffer to write the converted value to. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static inline void write_crc16(uint16_t crc, uint8_t *buffer)
{
    buffer[0] = (crc >> 8) & 0xFF;
    buffer[1] = (crc)&0xFF;
}

/*--------------------------------------------------------------------------------------
 * write_crc32 - Converts a uint16_t to big endian representation and writes it to
 *      buffer.
 *
 * crc: The uint32_t to convert to big endian. [INPUT]
 * buffer: The buffer to write the converted value to. [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static inline void write_crc32(uint32_t crc, uint8_t *buffer)
{
    buffer[0] = (crc >> 24) & 0xFF;
    buffer[1] = (crc >> 16) & 0xFF;
    buffer[2] = (crc >> 8) & 0xFF;
    buffer[3] = (crc)&0xFF;
}

/*--------------------------------------------------------------------------------------
 * read_crc16 - Reads a big endian representation and converts it to a uint16_t.
 *
 * buffer: The buffer to read the converted value from. [INPUT]
 * return: The uint16_t crc [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static inline uint16_t read_crc16(uint8_t *buffer)
{
    uint16_t crc = 0;
    crc |= (((uint16_t)buffer[0]) << 8);
    crc |= ((uint16_t)buffer[1]);
    return crc;
}

/*--------------------------------------------------------------------------------------
 * read_crc32 - Reads a big endian representation and converts it to a uint32_t.
 *
 * buffer: The buffer to read the converted value from. [OUTPUT]
 * return: The uint16_t crc [OUTPUT]
 *-------------------------------------------------------------------------------------*/
static inline uint32_t read_crc32(uint8_t *buffer)
{
    uint32_t crc = 0;
    crc |= ((uint32_t)buffer[0]) << 24;
    crc |= ((uint32_t)buffer[1]) << 16;
    crc |= ((uint32_t)buffer[2]) << 8;
    crc |= ((uint32_t)buffer[3]);
    return crc;
}

/******************************************************************************
 EXPORTED FUNCTIONS
 ******************************************************************************/

/*--------------------------------------------------------------------------------------
 * bib_init - Inits the crc xor tables for all supported crc specifications.
 *-------------------------------------------------------------------------------------*/
int bib_init(void)
{
    return BP_SUCCESS;
}

/*--------------------------------------------------------------------------------------
 * bib_read -
 *
 *  block - pointer to block holding bundle block [INPUT]
 *  size - size of block [INPUT]
 *  bib - pointer to a bundle integrity block structure to be populated by this function [OUTPUT]
 *  update_indices - boolean, 0: use <sdnv>.index, 1: update <sdnv>.index as you go [INPUT]
 *
 *  Returns:    Number of bytes read
 *-------------------------------------------------------------------------------------*/
int bib_read(const void *block, int size, bp_blk_bib_t *bib, bool update_indices, uint32_t *flags)
{
    uint8_t *buffer     = (uint8_t *)block;
    int      bytes_read = 0;
    uint32_t sdnvflags  = 0;

    /* Check Size */
    if (size < 1)
    {
        return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Invalid size of BIB block: %d\n", size);
    }

    /* Check Block Type */
    if (buffer[0] != BP_BIB_BLK_TYPE)
    {
        return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Invalid BIB block type: %d\n", buffer[0]);
    }

    /* Read Block */
    if (!update_indices)
    {
        sdnv_read(buffer, size, &bib->bf, &sdnvflags);
        sdnv_read(buffer, size, &bib->blklen, &sdnvflags);
        bytes_read = sdnv_read(buffer, size, &bib->security_target_count, &sdnvflags);

        if (bytes_read + 1 > size)
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "BIB block terminated prematurely: %d\n", bytes_read);
        }
        bib->security_target_type = buffer[bytes_read];

        sdnv_read(buffer, size, &bib->cipher_suite_id, &sdnvflags);
        sdnv_read(buffer, size, &bib->cipher_suite_flags, &sdnvflags);
        bytes_read = sdnv_read(buffer, size, &bib->compound_length, &sdnvflags);

        if (bytes_read + 1 > size)
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "BIB block terminated prematurely: %d\n", bytes_read);
        }
        bib->security_result_type = buffer[bytes_read];

        bytes_read = sdnv_read(buffer, size, &bib->security_result_length, &sdnvflags);
    }
    else
    {
        bib->bf.width                     = 0;
        bib->blklen.width                 = 0;
        bib->security_target_count.width  = 0;
        bib->cipher_suite_id.width        = 0;
        bib->cipher_suite_flags.width     = 0;
        bib->compound_length.width        = 0;
        bib->security_result_length.width = 0;

        bib->bf.index                    = 1;
        bib->blklen.index                = sdnv_read(buffer, size, &bib->bf, &sdnvflags);
        bib->security_target_count.index = sdnv_read(buffer, size, &bib->blklen, &sdnvflags);
        bytes_read                       = sdnv_read(buffer, size, &bib->security_target_count, &sdnvflags);

        if (bytes_read + 1 > size)
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "BIB block terminated prematurely: %d\n", bytes_read);
        }
        bib->security_target_type  = buffer[bytes_read];
        bib->cipher_suite_id.index = bytes_read + 1;

        bib->cipher_suite_flags.index = sdnv_read(buffer, size, &bib->cipher_suite_id, &sdnvflags);
        bib->compound_length.index    = sdnv_read(buffer, size, &bib->cipher_suite_flags, &sdnvflags);
        bytes_read                    = sdnv_read(buffer, size, &bib->compound_length, &sdnvflags);

        if (bytes_read + 1 > size)
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "BIB block terminated prematurely: %d\n", bytes_read);
        }
        bib->security_result_type         = buffer[bytes_read];
        bib->security_result_length.index = bytes_read + 1;

        bytes_read = sdnv_read(buffer, size, &bib->security_result_length, &sdnvflags);
    }

    /* Read Integrity Check */
    if (bib->security_target_type != BP_PAY_BLK_TYPE)
    {
        return bplog(flags, BP_FLAG_INVALID_BIB_TARGET_TYPE, "Invalid BIB target type: %d\n",
                     bib->security_target_type);
    }
    else if (bib->security_result_type != BP_BIB_INTEGRITY_SIGNATURE)
    {
        return bplog(flags, BP_FLAG_INVALID_BIB_RESULT_TYPE, "Invalid BIB security result type: %d\n",
                     bib->security_result_type);
    }
    else if (bib->cipher_suite_id.value == BP_BIB_CRC16_X25)
    {
        if ((bib->security_result_length.value != 2) || (bytes_read + 2 > size))
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "BIB block terminated prematurely: %d\n", bytes_read);
        }

        bib->security_result_data.crc16 = read_crc16(buffer + bytes_read);
        bytes_read += 2;
    }
    else if (bib->cipher_suite_id.value == BP_BIB_CRC32_CASTAGNOLI)
    {
        if ((bib->security_result_length.value != 4) || (bytes_read + 4 > size))
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "BIB block terminated prematurely: %d\n", bytes_read);
        }

        bib->security_result_data.crc32 = read_crc32(buffer + bytes_read);
        bytes_read += 4;
    }
    else
    {
        return bplog(flags, BP_FLAG_INVALID_CIPHER_SUITEID, "Invalid BIB cipher suite id: %d\n",
                     bib->cipher_suite_id.value);
    }

    /* Success Oriented Error Checking */
    if (sdnvflags != 0)
    {
        *flags |= sdnvflags;
        return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Flags raised during processing of BIB (%08X)\n", sdnvflags);
    }
    else
    {
        return bytes_read;
    }
}

/*--------------------------------------------------------------------------------------
 * bib_write -
 *
 *  block - pointer to memory that holds bundle block [OUTPUT]
 *  size - size of block [INPUT]
 *  bib - pointer to a bundle integrity block structure used to write the block [INPUT]
 *  update_indices - boolean, 0: use <sdnv>.index, 1: update <sdnv>.index as you go [INPUT]
 *
 *  Returns:    Number of bytes written
 *-------------------------------------------------------------------------------------*/
int bib_write(void *block, int size, bp_blk_bib_t *bib, bool update_indices, uint32_t *flags)
{
    uint8_t *buffer        = (uint8_t *)block;
    int      bytes_written = 0;
    uint32_t sdnvflags     = 0;

    /* Check Parameters */
    if (size < 1)
    {
        return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Insufficient room for BIB block: %d\n", size);
    }
    else if (bib->security_target_type != BP_PAY_BLK_TYPE)
    {
        return bplog(flags, BP_FLAG_INVALID_BIB_TARGET_TYPE, "Invalid BIB target type: %d\n",
                     bib->security_target_type);
    }
    else if (bib->security_result_type != BP_BIB_INTEGRITY_SIGNATURE)
    {
        return bplog(flags, BP_FLAG_INVALID_BIB_RESULT_TYPE, "Invalid BIB security result type: %d\n",
                     bib->security_result_type);
    }

    /* Update BIB Lengths */
    if (bib->cipher_suite_id.value == BP_BIB_CRC16_X25)
    {
        bib->compound_length.value        = 4;
        bib->security_result_length.value = 2;
    }
    else if (bib->cipher_suite_id.value == BP_BIB_CRC32_CASTAGNOLI)
    {
        bib->compound_length.value        = 6;
        bib->security_result_length.value = 4;
    }
    else
    {
        return bplog(flags, BP_FLAG_INVALID_CIPHER_SUITEID, "Invalid BIB cipher suite id: %d\n",
                     bib->cipher_suite_id.value);
    }

    /* Set Block Flags */
    bib->bf.value |= BP_BLK_REPALL_MASK;

    /* Write Block */
    buffer[0] = BP_BIB_BLK_TYPE; /* block type */
    if (!update_indices)
    {
        sdnv_write(buffer, size, bib->bf, &sdnvflags);
        sdnv_write(buffer, size, bib->blklen, &sdnvflags);
        bytes_written = sdnv_write(buffer, size, bib->security_target_count, &sdnvflags);

        if (bytes_written + 1 > size)
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Insufficient room for BIB block at: %d\n", bytes_written);
        }
        buffer[bytes_written] = bib->security_target_type;

        sdnv_write(buffer, size, bib->cipher_suite_id, &sdnvflags);
        sdnv_write(buffer, size, bib->cipher_suite_flags, &sdnvflags);
        bytes_written = sdnv_write(buffer, size, bib->compound_length, &sdnvflags);

        if (bytes_written + 1 > size)
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Insufficient room for BIB block at: %d\n", bytes_written);
        }
        buffer[bytes_written] = bib->security_result_type;

        bytes_written = sdnv_write(buffer, size, bib->security_result_length, &sdnvflags);
    }
    else
    {
        bib->bf.width                     = 0;
        bib->blklen.width                 = 0;
        bib->security_target_count.width  = 0;
        bib->cipher_suite_id.width        = 0;
        bib->cipher_suite_flags.width     = 0;
        bib->compound_length.width        = 0;
        bib->security_result_length.width = 0;

        bib->bf.index                    = 1;
        bib->blklen.index                = sdnv_write(buffer, size, bib->bf, &sdnvflags);
        bib->security_target_count.index = sdnv_write(buffer, size, bib->blklen, &sdnvflags);
        bytes_written                    = sdnv_write(buffer, size, bib->security_target_count, &sdnvflags);

        if (bytes_written + 1 > size)
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Insufficient room for BIB block at: %d\n", bytes_written);
        }
        buffer[bytes_written]      = bib->security_target_type;
        bib->cipher_suite_id.index = bytes_written + 1;

        bib->cipher_suite_flags.index = sdnv_write(buffer, size, bib->cipher_suite_id, &sdnvflags);
        bib->compound_length.index    = sdnv_write(buffer, size, bib->cipher_suite_flags, &sdnvflags);
        bytes_written                 = sdnv_write(buffer, size, bib->compound_length, &sdnvflags);

        if (bytes_written + 1 > size)
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Insufficient room for BIB block at: %d\n", bytes_written);
        }
        buffer[bytes_written]             = bib->security_result_type;
        bib->security_result_length.index = bytes_written + 1;

        bytes_written = sdnv_write(buffer, size, bib->security_result_length, &sdnvflags);
    }

    /* Write Integrity Check */
    if (bib->cipher_suite_id.value == BP_BIB_CRC16_X25)
    {
        write_crc16(bib->security_result_data.crc16, buffer + bytes_written);
        bytes_written += 2;
    }
    else if (bib->cipher_suite_id.value == BP_BIB_CRC32_CASTAGNOLI)
    {
        write_crc32(bib->security_result_data.crc32, buffer + bytes_written);
        bytes_written += 4;
    }

    /* Jam Block Length */
    bib->blklen.value = bytes_written - bib->security_target_count.index;
    sdnv_write(buffer, size, bib->blklen, &sdnvflags);

    /* Success Oriented Error Checking */
    if (sdnvflags != 0)
    {
        *flags |= sdnvflags;
        return BP_ERROR;
    }
    else
    {
        return bytes_written;
    }
}

/*--------------------------------------------------------------------------------------
 * bib_update -
 *
 *  block - pointer to memory that holds bundle block [OUTPUT]
 *  size - size of block [INPUT]
 *  payload - pointer to payload memory buffer [INPUT]
 *  payload_size - number of bytes to crc over [INPUT]
 *  bib - pointer to a bundle integrity block structure used to write the block [INPUT]
 *
 *  Returns:    Number of bytes processed of bundle
 *-------------------------------------------------------------------------------------*/
int bib_update(void *block, int size, const void *payload, int payload_size, bp_blk_bib_t *bib, uint32_t *flags)
{
    assert(bib);
    assert(payload);

    uint8_t *buffer = (uint8_t *)block;

    /* Check Size */
    int room_needed =
        bib->security_result_length.index + bib->security_result_length.width + bib->security_result_length.value;
    if (size < room_needed)
    {
        return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Insufficient room to update BIB block: %d < %d\n", size,
                     room_needed);
    }

    /* Calculate and Write Fragment Payload CRC */
    if (bib->cipher_suite_id.value == BP_BIB_CRC16_X25)
    {
        bib->security_result_data.crc16 = (uint16_t)bplib_crc_get((uint8_t *)payload, payload_size, &BPLIB_CRC16_X25);
        uint8_t *valptr = buffer + bib->security_result_length.index + bib->security_result_length.width;
        write_crc16(bib->security_result_data.crc16, valptr);
    }
    else if (bib->cipher_suite_id.value == BP_BIB_CRC32_CASTAGNOLI)
    {
        bib->security_result_data.crc32 = bplib_crc_get((uint8_t *)payload, payload_size, &BPLIB_CRC32_CASTAGNOLI);
        uint8_t *valptr = buffer + bib->security_result_length.index + bib->security_result_length.width;
        write_crc32(bib->security_result_data.crc32, valptr);
    }
    else
    {
        return bplog(flags, BP_FLAG_INVALID_CIPHER_SUITEID, "Invalid BIB cipher suite id: %d\n",
                     bib->cipher_suite_id.value);
    }

    /* Return Success */
    return BP_SUCCESS;
}

/*--------------------------------------------------------------------------------------
 * bib_verify -
 *
 *  payload - pointer to payload memory buffer [INPUT]
 *  payload_size - number of bytes to crc over [INPUT]
 *  bib - pointer to a bundle integrity block structure used to write the block [INPUT]
 *
 *  Returns:    success or error code
 *-------------------------------------------------------------------------------------*/
int bib_verify(const void *payload, int payload_size, bp_blk_bib_t *bib, uint32_t *flags)
{
    assert(payload);
    assert(bib);

    /* Calculate and Verify Payload CRC */
    if (bib->cipher_suite_id.value == BP_BIB_CRC16_X25)
    {
        uint16_t crc = (uint16_t)bplib_crc_get((uint8_t *)payload, payload_size, &BPLIB_CRC16_X25);
        if (bib->security_result_data.crc16 != crc)
        {
            /* Return Failure */
            return bplog(flags, BP_FLAG_FAILED_INTEGRITY_CHECK, "Failed X25 integrity check, exp=%04X, act=%04X \n",
                         bib->security_result_data.crc16, crc);
        }
    }
    else if (bib->cipher_suite_id.value == BP_BIB_CRC32_CASTAGNOLI)
    {
        uint32_t crc = bplib_crc_get((uint8_t *)payload, payload_size, &BPLIB_CRC32_CASTAGNOLI);
        if (bib->security_result_data.crc32 != crc)
        {
            /* Return Failure */
            return bplog(flags, BP_FLAG_FAILED_INTEGRITY_CHECK,
                         "Failed CASTAGNOLI integrity check, exp=%08Xl, act=%08Xl \n", bib->security_result_data.crc32,
                         crc);
        }
    }
    else
    {
        return bplog(flags, BP_FLAG_INVALID_CIPHER_SUITEID, "Invalid BIB cipher suite id: %d\n",
                     bib->cipher_suite_id.value);
    }

    /* Return Success */
    return BP_SUCCESS;
}
