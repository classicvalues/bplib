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

#include <string.h>

#include "bplib.h"
#include "bplib_os.h"
#include "v6.h"
#include "pri.h"
#include "bib.h"
#include "pay.h"
#include "cteb.h"
#include "dacs.h"
#include "sdnv.h"

/******************************************************************************
 DEFINES
 ******************************************************************************/

#define BP_NUM_EXCLUDE_REGIONS 16

/******************************************************************************
 TYPEDEFS
 ******************************************************************************/

typedef struct
{
    bp_blk_pri_t  primary_block;
    bp_blk_cteb_t custody_block;
    bp_blk_bib_t  integrity_block;
    bp_blk_pay_t  payload_block;
} bp_v6blocks_t;

/******************************************************************************
 FILE DATA
 ******************************************************************************
 * Notes:
 *    The block length field for every blocks block MUST be set to a positive
 *    integer.  The option to update the fields of the blocks reserves the width
 *    of the blklen field and goes back and writes the value after the entire
 *    block is written.  If the blklen field was variable, the code would have
 *    to make a first pass to calculate the block length and then a second pass
 *    to use that block length - that would be too much processing.
 */

static const bp_blk_pri_t bundle_pri_blk = {.version = BP_PRI_VERSION,
                                            /* Value   Index   Width */
                                            .pcf          = {0, 1, 3},
                                            .blklen       = {0, 4, 1},
                                            .dstnode      = {0, 5, 4},
                                            .dstserv      = {0, 9, 2},
                                            .srcnode      = {0, 11, 4},
                                            .srcserv      = {0, 15, 2},
                                            .rptnode      = {0, 17, 4},
                                            .rptserv      = {0, 21, 2},
                                            .cstnode      = {0, 23, 4},
                                            .cstserv      = {0, 27, 2},
                                            .createsec    = {0, 29, 6},
                                            .createseq    = {0, 35, 2},
                                            .lifetime     = {0, 37, 6},
                                            .dictlen      = {0, 43, 1},
                                            .fragoffset   = {0, 44, 4},
                                            .paylen       = {0, 48, 4},
                                            .is_admin_rec = false,
                                            .is_frag      = false,
                                            .allow_frag   = false,
                                            .cst_rqst     = true,
                                            .ack_app      = false,
                                            .cos          = BP_DEFAULT_CLASS_OF_SERVICE};

static const bp_blk_cteb_t bundle_cteb_blk = {
    /* Value   Index   Width */
    .bf = {0, 1, 1}, .blklen = {0, 2, 1}, .cid = {0, 3, 4}, .csteid = {'\0'}, .cstnode = 0, .cstserv = 0};

static const bp_blk_bib_t bundle_bib_blk = {
    /* Value    Index  Width */
    .bf                     = {0, 1, 1},
    .blklen                 = {0, 2, 4},
    .security_target_count  = {1, 6, 1},
    .security_target_type   = BP_PAY_BLK_TYPE,
    .cipher_suite_id        = {0, 8, 1},
    .cipher_suite_flags     = {0, 9, 1},
    .compound_length        = {0, 10, 1},
    .security_result_type   = BP_BIB_INTEGRITY_SIGNATURE,
    .security_result_length = {0, 12, 1},
};

static const bp_blk_pay_t bundle_pay_blk = {
    /* Value     Index   Width */
    .bf      = {0, 1, 1},
    .blklen  = {0, 2, 4},
    .payptr  = NULL,
    .paysize = 0};

/******************************************************************************
 LOCAL FUNCTIONS
 ******************************************************************************/

/*--------------------------------------------------------------------------------------
 * v6_build -
 *
 *  This builds the bundle
 *-------------------------------------------------------------------------------------*/
int v6_build(bp_bundle_t *bundle, bp_blk_pri_t *pri, uint8_t *hdr_buf, int hdr_len, uint32_t *flags)
{
    int bytes_written;
    int hdr_index;

    bp_bundle_data_t *data            = &bundle->data;
    bp_v6blocks_t    *blocks          = (bp_v6blocks_t *)bundle->blocks;
    bp_val_t          custody_service = bundle->route.local_service;

    /* Initialize Data Storage Memory */
    hdr_index = 0;
    memset(data, 0, sizeof(bp_bundle_data_t));

/* Handle Global Custody ID */
#if BPLIB_GLOBAL_CUSTODY_ID
    custody_service = 0;
#endif

    /* Initialize Primary Block */
    if (pri)
    {
        /* User Provided Primary Block */
        blocks->primary_block = *pri;

        /* Set Pre-Built Flag to FALSE */
        bundle->prebuilt = false;
    }
    else
    {
        /* Library Provided Primary Block */
        blocks->primary_block               = bundle_pri_blk;
        blocks->primary_block.dstnode.value = bundle->route.destination_node;
        blocks->primary_block.dstserv.value = bundle->route.destination_service;
        blocks->primary_block.srcnode.value = bundle->route.local_node;
        blocks->primary_block.srcserv.value = bundle->route.local_service;
        blocks->primary_block.rptnode.value = bundle->route.report_node;
        blocks->primary_block.rptserv.value = bundle->route.report_service;
        if (bundle->attributes.request_custody)
        {
            blocks->primary_block.cstnode.value = bundle->route.local_node;
            blocks->primary_block.cstserv.value = custody_service;
        }
        else
        {
            blocks->primary_block.cstnode.value = 0;
            blocks->primary_block.cstserv.value = 0;
        }
        blocks->primary_block.lifetime.value = bundle->attributes.lifetime;
        blocks->primary_block.is_admin_rec   = bundle->attributes.admin_record;
        blocks->primary_block.allow_frag     = bundle->attributes.allow_fragmentation;
        blocks->primary_block.cst_rqst       = bundle->attributes.request_custody;
        if ((unsigned)bundle->attributes.class_of_service > BP_COS_EXPEDITED)
        {
            blocks->primary_block.cos = BP_COS_EXTENDED;
        }
        else
        {
            blocks->primary_block.cos = bundle->attributes.class_of_service;
        }

        /* Set Pre-Built Flag to TRUE */
        bundle->prebuilt = true;
    }

    /* Write Primary Block */
    bytes_written = pri_write(data->header, BP_BUNDLE_HDR_BUF_SIZE, &blocks->primary_block, false, flags);
    if (bytes_written < 0)
    {
        return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed (%d) to write primary block of bundle\n", bytes_written);
    }
    hdr_index += bytes_written;

    /* Write Custody Block */
    if (blocks->primary_block.cst_rqst)
    {
        /* Initialize Block */
        blocks->custody_block           = bundle_cteb_blk;
        blocks->custody_block.cid.value = 0; /* Set Initial Custody ID to Zero */
        bplib_ipn2eid(blocks->custody_block.csteid, BP_MAX_EID_STRING, bundle->route.local_node,
                      custody_service); /* Set Custodian EID */

        /* Populate Data with Block */
        data->cidfield   = blocks->custody_block.cid;
        data->cteboffset = hdr_index;
        bytes_written = cteb_write(&data->header[hdr_index], BP_BUNDLE_HDR_BUF_SIZE - hdr_index, &blocks->custody_block,
                                   false, flags);

        /* Check Status */
        if (bytes_written < 0)
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed (%d) to write custody block of bundle\n",
                         bytes_written);
        }
        hdr_index += bytes_written;
    }
    else
    {
        data->cteboffset = 0;
    }

    /* Write Integrity Block */
    if (bundle->attributes.integrity_check)
    {
        /* Initialize Block */
        blocks->integrity_block                       = bundle_bib_blk;
        blocks->integrity_block.cipher_suite_id.value = bundle->attributes.cipher_suite;

        /* Populate Data */
        data->biboffset = hdr_index;
        bytes_written   = bib_write(&data->header[hdr_index], BP_BUNDLE_HDR_BUF_SIZE - hdr_index,
                                    &blocks->integrity_block, false, flags);

        /* Check Status */
        if (bytes_written < 0)
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed (%d) to write integrity block of bundle\n",
                         bytes_written);
        }
        hdr_index += bytes_written;
    }
    else
    {
        data->biboffset = 0;
    }

    /* Copy Non-excluded Header Regions */
    if (hdr_index + hdr_len < BP_BUNDLE_HDR_BUF_SIZE)
    {
        memcpy(&data->header[hdr_index], hdr_buf, hdr_len);
        hdr_index += hdr_len;
    }
    else
    {
        return bplog(flags, BP_FLAG_BUNDLE_TOO_LARGE, "Non-excluded forwarded bundle exceed maximum header size (%d)\n",
                     hdr_index);
    }

    /* Initialize Payload Block */
    blocks->payload_block = bundle_pay_blk;

    /* Initialize Payload Block Offset */
    data->payoffset = hdr_index;

    /* Return Success */
    return BP_SUCCESS;
}

/******************************************************************************
 EXPORTED FUNCTIONS
 ******************************************************************************/

/*--------------------------------------------------------------------------------------
 * v6_initialize -
 *
 *  This initializes the v6 module
 *-------------------------------------------------------------------------------------*/
int v6_initialize(void)
{
    int status;

    /* Initialize the Bundle Integrity Block Module */
    status = bib_init();

    /* Return Status */
    return status;
}

/*--------------------------------------------------------------------------------------
 * v6_create -
 *
 *  This initializes a bundle structure
 *-------------------------------------------------------------------------------------*/
int v6_create(bp_bundle_t *bundle, bp_route_t route, bp_attr_t attributes)
{
    int status = BP_SUCCESS;

    /* Initialize Route and Attributes */
    bundle->route      = route;
    bundle->attributes = attributes;

    /* Initialize Blocks */
    bundle->blocks = NULL;

    /* Allocate Blocks */
    bundle->blocks = (bp_v6blocks_t *)bplib_os_calloc(sizeof(bp_v6blocks_t));
    if (bundle->blocks == NULL)
    {
        status = BP_ERROR;
    }

    /* Mark Bundle to Prebuild */
    bundle->prebuilt = false;

    /* Return Status */
    return status;
}

/*--------------------------------------------------------------------------------------
 * v6_destroy -
 *
 *  This initializes a bundle structure
 *-------------------------------------------------------------------------------------*/
int v6_destroy(bp_bundle_t *bundle)
{
    if (bundle->blocks)
    {
        bplib_os_free(bundle->blocks);
    }
    bundle->blocks = NULL;
    return BP_SUCCESS;
}

/*--------------------------------------------------------------------------------------
 * v6_populate_bundle -
 *
 *  This populates a new bundle's fields
 *-------------------------------------------------------------------------------------*/
int v6_populate_bundle(bp_bundle_t *bundle, uint32_t *flags)
{
    return v6_build(bundle, NULL, NULL, 0, flags);
}

/*--------------------------------------------------------------------------------------
 * v6_send_bundle -
 *-------------------------------------------------------------------------------------*/
int v6_send_bundle(bp_bundle_t *bundle, const uint8_t *buffer, int size, bp_create_func_t create, void *parm,
                   int timeout, uint32_t *flags)
{
    int               payload_offset = 0;
    bp_bundle_data_t *data           = &bundle->data;
    bp_v6blocks_t    *blocks         = (bp_v6blocks_t *)bundle->blocks;
    bp_blk_pri_t     *pri            = &blocks->primary_block;
    bp_blk_bib_t     *bib            = &blocks->integrity_block;
    bp_blk_pay_t     *pay            = &blocks->payload_block;

    /* Update Payload Block */
    pay->payptr  = buffer;
    pay->paysize = size;

    /* Check Fragmentation */
    int max_paysize = bundle->attributes.max_length - data->headersize;
    if (pay->paysize > max_paysize)
    {
        if (bundle->attributes.allow_fragmentation)
        {
            pri->is_frag = true;
        }
        else
        {
            return bplog(flags, BP_FLAG_BUNDLE_TOO_LARGE, "Unable to fragment forwarded bundle (%d > %d)\n",
                         pay->paysize, max_paysize);
        }
    }
    else if (max_paysize <= 0)
    {
        return bplog(flags, BP_FLAG_BUNDLE_TOO_LARGE, "Bundle header blocks exceed maximum size of bundle (%d > %d)\n",
                     data->headersize, bundle->attributes.max_length);
    }

    /* Check if Time Needs to be Set  */
    bp_field_t lifetime = pri->lifetime;
    if (bundle->prebuilt)
    {
        /* Get Current Time */
        unsigned long sysnow = 0;
        if (bplib_os_systime(&sysnow) == BP_ERROR)
        {
            /* Unreliable Time Detected */
            bplog(flags, BP_FLAG_UNRELIABLE_TIME, "Unreliable time detected: %ld\n", sysnow);
            pri->createsec.value = BP_UNKNOWN_CREATION_TIME;

            /* Lifetime hardcoded in this special case to protect against unintended bundle expiration */
            lifetime.value = BP_BEST_EFFORT_LIFETIME;
            sdnv_write(data->header, BP_BUNDLE_HDR_BUF_SIZE, lifetime, flags);
        }
        else
        {
            /* Creation time set to current system time */
            pri->createsec.value = sysnow;
        }

        /* Set Creation Time and Sequence */
        sdnv_write(data->header, BP_BUNDLE_HDR_BUF_SIZE, pri->createsec, flags);
        sdnv_write(data->header, BP_BUNDLE_HDR_BUF_SIZE, pri->createseq, flags);
    }

    /* Set Expiration Time of Bundle */
    if (pri->createsec.value == BP_TTL_CREATION_TIME)
    {
        data->exprtime = BP_TTL_CREATION_TIME;
    }
    else if (pri->createsec.value == BP_UNKNOWN_CREATION_TIME)
    {
        data->exprtime = BP_UNKNOWN_CREATION_TIME;
    }
    else
    {
        data->exprtime = pri->createsec.value + lifetime.value;
        if (data->exprtime < pri->createsec.value)
        {
            /* Rollover Detected */
            bplog(flags, BP_FLAG_SDNV_OVERFLOW, "Calculation of expiration time of bundle rolled over\n");

            /* Set expiration time to maximum value as a best effort attempt to handle rollver */
            data->exprtime = BP_MAX_ENCODED_VALUE;
        }
    }

    /* Enqueue Bundle */
    while (payload_offset < pay->paysize)
    {
        /* Calculate Storage Header Size and Fragment Size */
        int payload_remaining = pay->paysize - payload_offset;
        int fragment_size     = max_paysize < payload_remaining ? max_paysize : payload_remaining;

        /* Update Primary Block Fragmentation */
        if (pri->is_frag)
        {
            pri->fragoffset.value = payload_offset;
            pri->paylen.value     = pay->paysize;
            sdnv_write(data->header, BP_BUNDLE_HDR_BUF_SIZE, pri->fragoffset, flags);
            sdnv_write(data->header, BP_BUNDLE_HDR_BUF_SIZE, pri->paylen, flags);
        }

        /* Update Integrity Block */
        if (data->biboffset != 0)
        {
            bib_update(&data->header[data->biboffset], BP_BUNDLE_HDR_BUF_SIZE - data->biboffset,
                       &pay->payptr[payload_offset], fragment_size, bib, flags);
        }

        /* Write Payload Block (static portion) */
        pay->blklen.value = fragment_size;
        int bytes_written =
            pay_write(&data->header[data->payoffset], BP_BUNDLE_HDR_BUF_SIZE - data->payoffset, pay, false, flags);
        if (bytes_written < 0)
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE,
                         "Failed (%d) to write payload block (static portion) of bundle\n", bytes_written);
        }
        data->headersize = data->payoffset + bytes_written;
        data->bundlesize = data->headersize + fragment_size;

        /* Enqueue Bundle */
        int status = create(parm, pri->is_admin_rec, &pay->payptr[payload_offset], fragment_size, timeout);
        if (status != BP_SUCCESS)
        {
            return bplog(flags, BP_FLAG_STORE_FAILURE, "Failed (%d) to store bundle in storage system\n", status);
        }

        payload_offset += fragment_size;
    }

    /* Increment Sequence Count (done here since now bundle successfully stored) */
    if (bundle->prebuilt)
    {
        pri->createseq.value++;
        sdnv_mask(&pri->createseq);
    }

    /* Return Payload Bytes Stored */
    return BP_SUCCESS;
}

/*--------------------------------------------------------------------------------------
 * v6_receive_bundle -
 *-------------------------------------------------------------------------------------*/
int v6_receive_bundle(bp_bundle_t *bundle, const uint8_t *buffer, int size, bp_payload_t *payload, uint32_t *flags)
{
    int status = BP_SUCCESS;

    int index      = 0;
    int bytes_read = 0;

    int ei = 0;
    int exclude[BP_NUM_EXCLUDE_REGIONS];

    bp_blk_pri_t pri_blk;

    bool          cteb_present = false;
    int           cteb_index;
    bp_blk_cteb_t cteb_blk;

    bool         bib_present = false;
    int          bib_index;
    bp_blk_bib_t bib_blk;

    int          pay_index;
    bp_blk_pay_t pay_blk;

    /* Parse Primary Block */
    exclude[ei++] = index;
    bytes_read    = pri_read(buffer, size, &pri_blk, true, flags);
    if (bytes_read < 0)
    {
        return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed to parse primary block of size %d\n", size);
    }

    index += bytes_read;
    exclude[ei++] = index;

    /* Check Unsupported */
    if (pri_blk.dictlen.value != 0)
    {
        return bplog(flags, BP_FLAG_NONCOMPLIANT, "Unsupported bundle attempted to be processed (%d)\n",
                     pri_blk.dictlen.value);
    }

    /* Calculate Bundle's Expiration Time */
    bp_val_t exprtime = pri_blk.createsec.value + pri_blk.lifetime.value;
    if (pri_blk.createsec.value == BP_UNKNOWN_CREATION_TIME)
    {
        /* Bundle Source has Unreliable Time */
        exprtime = BP_UNKNOWN_CREATION_TIME;
    }
    else if (pri_blk.createsec.value == BP_TTL_CREATION_TIME)
    {
        /* Request for Time-To-Live Extension */
        exprtime = BP_TTL_CREATION_TIME;
    }
    else if (exprtime < pri_blk.createsec.value)
    {
        /* Rollover Detected */
        bplog(flags, BP_FLAG_SDNV_OVERFLOW, "Calculation of expiration time of bundle rolled over\n");

        /* Set expiration time to maximum value as a best effort attempt to handle rollver */
        exprtime = BP_MAX_ENCODED_VALUE;
    }

    /* Get Current Time */
    bool          unrelt = false;
    unsigned long sysnow = 0;
    if (bplib_os_systime(&sysnow) == BP_ERROR)
    {
        unrelt = true; /* time is unreliable */
        bplog(flags, BP_FLAG_UNRELIABLE_TIME, "Unreliable time detected: %ld\n", sysnow);
    }

    /* Check Expiration */
    if (v6_is_expired(bundle, sysnow, exprtime, unrelt))
    {
        return BP_PENDING_EXPIRATION;
    }

    /* Parse and Process Remaining Blocks */
    while (status == BP_SUCCESS && index < size)
    {
        /* Read Block Information */
        uint8_t blk_type = buffer[index];

        /* Check for Room in Exclude Region Array */
        if (ei >= (BP_NUM_EXCLUDE_REGIONS - 2))
        {
            return bplog(flags, BP_FLAG_NONCOMPLIANT, "Bundle has too many extension blocks, %d\n",
                         (BP_NUM_EXCLUDE_REGIONS / 2) - 2);
        }

        /* Check Block Type */
        if (blk_type == BP_BIB_BLK_TYPE)
        {
            /* Mark Start of BIB Region */
            bib_present   = true;
            bib_index     = index;
            exclude[ei++] = index;

            /* Read BIB */
            bytes_read = bib_read(&buffer[bib_index], size - bib_index, &bib_blk, true, flags);
            if (bytes_read < 0)
            {
                return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed to parse BIB block at offset %d\n", bib_index);
            }

            index += bytes_read;

            /* Mark End of BIB Region */
            exclude[ei++] = index;
        }
        else if (blk_type == BP_CTEB_BLK_TYPE)
        {
            /* Mark Start of CTEB Region */
            cteb_present = true;
            cteb_index   = index;

            /* Read CTEB */
            bytes_read = cteb_read(&buffer[cteb_index], size - cteb_index, &cteb_blk, true, flags);
            if (bytes_read < 0)
            {
                return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed to parse CTEB block at offset %d\n", cteb_index);
            }

            index += bytes_read;
        }
        else if (blk_type != BP_PAY_BLK_TYPE) /* skip over block */
        {
            bp_field_t blk_flags   = {0, 1, 0};
            bp_field_t blk_len     = {0, 0, 0};
            int        start_index = index;
            int        data_index  = 0; /* start of the block after the block length, set below */

            blk_len.index = sdnv_read(&buffer[start_index], size - start_index, &blk_flags, flags);
            data_index    = sdnv_read(&buffer[start_index], size - start_index, &blk_len, flags);

            /* Check Parsing Status */
            if (*flags & (BP_FLAG_SDNV_OVERFLOW | BP_FLAG_SDNV_INCOMPLETE))
            {
                status = bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed (%0X) to parse block at index %d\n", *flags,
                               start_index);
            }
            else
            {
                index += data_index + blk_len.value;
            }

            /* Mark Processing as Incomplete (unrecognized extension block) */
            bplog(flags, BP_FLAG_INCOMPLETE, "Unrecognized extension block of type %d skipped\n", blk_type);

            /* Should transmit status report that block cannot be processed */
            if (blk_flags.value & BP_BLK_NOTIFYNOPROC_MASK)
            {
                bplog(flags, BP_FLAG_NONCOMPLIANT, "Request to notify on unprocessed extension block ignored\n");
            }

            /* Delete bundle since block not recognized */
            if (blk_flags.value & BP_BLK_DELETENOPROC_MASK)
            {
                status = bplog(flags, BP_FLAG_DROPPED, "Dropping bundle with unrecognized block\n");
            }

            /* Check if Block Should be Included */
            if (blk_flags.value & BP_BLK_DROPNOPROC_MASK)
            {
                /* Exclude Block */
                exclude[ei++] = start_index;
                exclude[ei++] = index;
            }
            else
            {
                /* Mark As Forwarded without Processed */
                blk_flags.value |= BP_BLK_FORWARDNOPROC_MASK;

                /*
                 * NOTE: This is updating the block flags inside of the buffer which is supposed to be "const"
                 * Somewhat ironic that BP flags need to be changed to indicate that nothing was done.
                 *
                 * This is certainly not ideal, but this is the way it has worked up to this point.
                 * This should be addressed in the BPv7 implementation.
                 */
                sdnv_write((uint8_t *)buffer + start_index, size - start_index, blk_flags, flags);
            }
        }
        else /* payload block */
        {
            pay_index     = index;
            exclude[ei++] = index; /* start of payload header */
            bytes_read    = pay_read(&buffer[pay_index], size - pay_index, &pay_blk, true, flags);
            if (bytes_read < 0)
            {
                return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed (%d) to read payload block\n", status);
            }

            index += bytes_read;
            exclude[ei++] = index + pay_blk.paysize;

            /* Set Returned Payload */
            payload->data.exprtime    = exprtime;
            payload->data.ackapp      = pri_blk.ack_app;
            payload->data.payloadsize = pay_blk.paysize;
            payload->memptr           = pay_blk.payptr;

            /* Perform Integrity Check */
            if (bib_present)
            {
                status = bib_verify(pay_blk.payptr, pay_blk.paysize, &bib_blk, flags);
                if (status != BP_SUCCESS)
                {
                    return status;
                }
            }

            /* Check Size of Payload */
            if (pri_blk.is_admin_rec && pay_blk.paysize < 2)
            {
                return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Invalid block length: %d\n", pay_blk.paysize);
            }

            /* Process Payload */
            if (pri_blk.dstnode.value != bundle->route.local_node) /* forward bundle (dst node != local node) */
            {
                /* Handle Custody Request */
                if (pri_blk.cst_rqst)
                {
                    pri_blk.rptnode.value = 0;
                    pri_blk.rptserv.value = 0;
                    pri_blk.cstnode.value = bundle->route.local_node;
                    pri_blk.cstserv.value = bundle->route.local_service;
                }

                /* Copy Non-excluded Header Regions */
                uint8_t hdr_buf[BP_BUNDLE_HDR_BUF_SIZE];
                int     hdr_index = 0;
                int     i;
                for (i = 1; (i + 1) < ei; i += 2)
                {
                    int start_index   = exclude[i];
                    int stop_index    = exclude[i + 1];
                    int bytes_to_copy = stop_index - start_index;
                    if ((hdr_index + bytes_to_copy) >= BP_BUNDLE_HDR_BUF_SIZE)
                    {
                        return bplog(flags, BP_FLAG_BUNDLE_TOO_LARGE,
                                     "Non-excluded forwarded blocks exceed maximum header size (%d)\n", hdr_index);
                    }
                    else
                    {
                        memcpy(&hdr_buf[hdr_index], &buffer[start_index], bytes_to_copy);
                        hdr_index += bytes_to_copy;
                    }
                }

                /* Initialize Forwarded Bundle */
                status = v6_build(bundle, &pri_blk, hdr_buf, hdr_index, flags);
                if (status == BP_SUCCESS)
                {
                    /* Indicate Bundle Ready for Forwarding */
                    status = BP_PENDING_FORWARD;

                    /* Handle Custody Transfer */
                    payload->node    = BP_IPN_NULL;
                    payload->service = BP_IPN_NULL;
                    if (pri_blk.cst_rqst)
                    {
                        if (cteb_present)
                        {
                            payload->node    = cteb_blk.cstnode;
                            payload->service = cteb_blk.cstserv;
                            payload->cid     = cteb_blk.cid.value;
                        }
                        else
                        {
                            status = bplog(flags, BP_FLAG_NONCOMPLIANT, "Only aggregate custody supported\n");
                        }
                    }
                }
            }
            else if ((pri_blk.dstserv.value != 0) && (pri_blk.dstserv.value != bundle->route.local_service))
            {
                return bplog(flags, BP_FLAG_ROUTE_NEEDED, "Wrong channel to process bundle (%lu, %lu)\n",
                             (unsigned long)pri_blk.dstserv.value, (unsigned long)bundle->route.local_service);
            }
            else if (pri_blk.is_admin_rec) /* Administrative Record */
            {
                /* Read Record Information */
                uint8_t rec_type = buffer[index];

                /* Process Record */
                if (rec_type == BP_ACS_REC_TYPE)
                {
                    /* Indicate Aggregate Custody Signal Ready for Custody Processing */
                    payload->node    = pri_blk.cstnode.value;
                    payload->service = pri_blk.cstserv.value;
                    status           = BP_PENDING_ACKNOWLEDGMENT;
                }
                else if (rec_type == BP_CS_REC_TYPE)
                {
                    status = bplog(flags, BP_FLAG_NONCOMPLIANT, "Custody signal bundles are not supported\n");
                }
                else if (rec_type == BP_STAT_REC_TYPE)
                {
                    status = bplog(flags, BP_FLAG_NONCOMPLIANT, "Status report bundles are not supported\n");
                }
                else
                {
                    status =
                        bplog(flags, BP_FLAG_UNKNOWNREC, "Unknown administrative record: %u\n", (unsigned int)rec_type);
                }
            }
            else /* bundle with payload destined for local node */
            {
                /* Indicate Payload Ready for Acceptance */
                status = BP_PENDING_ACCEPTANCE;

                /* Handle Custody Transfer */
                payload->node    = BP_IPN_NULL;
                payload->service = BP_IPN_NULL;
                if (pri_blk.cst_rqst)
                {
                    if (cteb_present)
                    {
                        payload->node    = cteb_blk.cstnode;
                        payload->service = cteb_blk.cstserv;
                        payload->cid     = cteb_blk.cid.value;
                    }
                    else
                    {
                        status = bplog(flags, BP_FLAG_NONCOMPLIANT,
                                       "Bundle requesting custody, but only aggregate custody supported\n");
                    }
                }
            }

            /* Force Exit After Payload Block */
            break;
        }
    }

    /* Return Status */
    return status;
}

/*--------------------------------------------------------------------------------------
 * v6_update_bundle -
 *-------------------------------------------------------------------------------------*/
int v6_update_bundle(bp_bundle_data_t *data, bp_val_t cid, uint32_t *flags)
{
    data->cidfield.value = cid;
    sdnv_mask(&data->cidfield);
    return sdnv_write(&data->header[data->cteboffset], data->bundlesize - data->cteboffset, data->cidfield, flags);
}

/*--------------------------------------------------------------------------------------
 * v6_populate_acknowledgment -
 *-------------------------------------------------------------------------------------*/
int v6_populate_acknowledgment(uint8_t *rec, int size, int max_fills, rb_tree_t *tree, uint32_t *flags)
{
    return dacs_write(rec, size, max_fills, tree, flags);
}

/*--------------------------------------------------------------------------------------
 * v6_receive_acknowledgment -
 *-------------------------------------------------------------------------------------*/
int v6_receive_acknowledgment(const uint8_t *rec, int size, int *num_acks, bp_delete_func_t remove, void *parm,
                              uint32_t *flags)
{
    return dacs_read(rec, size, num_acks, remove, parm, flags);
}

/*--------------------------------------------------------------------------------------
 * v6_is_expired
 *-------------------------------------------------------------------------------------*/
int v6_is_expired(bp_bundle_t *bundle, unsigned long sysnow, unsigned long exprtime, bool unrelt)
{
    if (!unrelt &&                               /* time must be reliable */
        !bundle->attributes.ignore_expiration && /* expiration cannot be ignored */
        exprtime != BP_UNKNOWN_CREATION_TIME &&  /* the expiration time must be set */
        exprtime !=
            BP_TTL_CREATION_TIME && /* the expiration time cannot indicate use of a time-to-live extension block */
        sysnow >= exprtime)         /* the current time must equal or exceed the expiration time */
    {
        return (int)true;
    }
    else
    {
        return (int)false;
    }
}

/*--------------------------------------------------------------------------------------
 * v6_routeinfo -
 *-------------------------------------------------------------------------------------*/
int v6_routeinfo(const void *bundle, int size, bp_route_t *route)
{
    bp_blk_pri_t pri_blk;
    uint32_t    *flags = 0;

    /* Check Parameters */
    if (bundle == NULL)
    {
        return BP_ERROR;
    }

    /* Parse Primary Block */
    int bytes_read = pri_read((uint8_t *)bundle, size, &pri_blk, true, flags);
    if (bytes_read < 0)
    {
        return bytes_read;
    }

    /* Set Addresses */
    if (route)
    {
        route->local_node          = (bp_ipn_t)pri_blk.srcnode.value;
        route->local_service       = (bp_ipn_t)pri_blk.srcserv.value;
        route->destination_node    = (bp_ipn_t)pri_blk.dstnode.value;
        route->destination_service = (bp_ipn_t)pri_blk.dstserv.value;
        route->report_node         = (bp_ipn_t)pri_blk.rptnode.value;
        route->report_service      = (bp_ipn_t)pri_blk.rptserv.value;
    }

    /* Return Success */
    return BP_SUCCESS;
}

/*--------------------------------------------------------------------------------------
 * v6_display -
 *-------------------------------------------------------------------------------------*/
int v6_display(const void *bundle, int size, uint32_t *flags)
{
    uint8_t     *buffer     = (uint8_t *)bundle;
    int          index      = 0;
    int          bytes_read = 0;
    bp_blk_pri_t pri_blk;

    /* Parse Primary Block */
    bytes_read = pri_read(buffer, size, &pri_blk, true, flags);
    if (bytes_read < 0)
    {
        return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed to parse primary block of size %d\n", size);
    }

    index += bytes_read;

    /* Display Primary Block */
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "@@@@\n");
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Bundle of Size %d, Version %d\n", size, pri_blk.version);
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Primary Block Length:          %d\n", pri_blk.blklen.value);
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Adminstrtive Record:           %s\n", pri_blk.is_admin_rec ? "TRUE" : "FALSE");
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Fragmented:                    %s\n", pri_blk.is_frag ? "TRUE" : "FALSE");
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Fragmentation Allowed:         %s\n", pri_blk.allow_frag ? "TRUE" : "FALSE");
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Custody Requested:             %s\n", pri_blk.cst_rqst ? "TRUE" : "FALSE");
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Application Acknowledgement:   %s\n", pri_blk.ack_app ? "TRUE" : "FALSE");
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Processing Control Flags:      0x%X\n", (unsigned int)pri_blk.pcf.value);
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Destination EID:               %ld.%ld\n", (long)pri_blk.dstnode.value,
          (long)pri_blk.dstserv.value);
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Source EID:                    %ld.%ld\n", (long)pri_blk.srcnode.value,
          (long)pri_blk.srcserv.value);
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Report To EID:                 %ld.%ld\n", (long)pri_blk.rptnode.value,
          (long)pri_blk.rptserv.value);
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Custody EID:                   %ld.%ld\n", (long)pri_blk.cstnode.value,
          (long)pri_blk.cstserv.value);
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Creation Timestamp:            %ld\n", (long)pri_blk.createsec.value);
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Creation Sequence:             %ld\n", (long)pri_blk.createseq.value);
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Lifetime:                      %ld\n", (long)pri_blk.lifetime.value);
    bplog(NULL, BP_FLAG_DIAGNOSTIC, "Dictionary Length:             %ld\n", (long)pri_blk.dictlen.value);
    if (pri_blk.dictlen.value != 0)
    {
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "Fragmentation Offset;          %ld\n", (long)pri_blk.fragoffset.value);
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "Payload Length:                %ld\n", (long)pri_blk.paylen.value);
    }

    /* Parse Remaining Blocks */
    while (index < size)
    {
        /* Read Block Information */
        uint8_t    blk_type    = buffer[index];
        bp_field_t blk_flags   = {0, 1, 0};
        bp_field_t blk_len     = {0, 0, 0};
        int        start_index = index;
        int        data_index  = 0; /* start of the block after the block length, set below */

        blk_len.index = sdnv_read(&buffer[start_index], size - start_index, &blk_flags, flags);
        data_index    = sdnv_read(&buffer[start_index], size - start_index, &blk_len, flags);

        /* Check Parsing Status */
        if (*flags & (BP_FLAG_SDNV_OVERFLOW | BP_FLAG_SDNV_INCOMPLETE))
        {
            return bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed (%0X) to parse block at index %d\n", *flags,
                         start_index);
        }

        index += data_index + blk_len.value;

        /* Display Extension Block */
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "---------------------------------\n");
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "Block Type 0x%02X at Offset %d\n", blk_type, start_index);
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "Block Data Size:               %d\n", blk_len.value);
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "Must Replicate in Fragments:   %s\n",
              (blk_flags.value & BP_BLK_REPALL_MASK) ? "SET" : "CLEAR");
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "Status on Failure:             %s\n",
              (blk_flags.value & BP_BLK_NOTIFYNOPROC_MASK) ? "SET" : "CLEAR");
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "Delete Bundle on Failure:      %s\n",
              (blk_flags.value & BP_BLK_DELETENOPROC_MASK) ? "SET" : "CLEAR");
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "Last Block:                    %s\n",
              (blk_flags.value & BP_BLK_LASTBLOCK_MASK) ? "SET" : "CLEAR");
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "Drop Block on Failure:         %s\n",
              (blk_flags.value & BP_BLK_DROPNOPROC_MASK) ? "SET" : "CLEAR");
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "Forwarded w/o Processing:      %s\n",
              (blk_flags.value & BP_BLK_FORWARDNOPROC_MASK) ? "SET" : "CLEAR");
        bplog(NULL, BP_FLAG_DIAGNOSTIC, "EID Reference:                 %s\n",
              (blk_flags.value & BP_BLK_EIDREF_MASK) ? "SET" : "CLEAR");

        /* Payload Block */
        if (blk_type == BP_PAY_BLK_TYPE)
        {
            bp_blk_pay_t pay_blk;
            int          pay_index = start_index;

            bytes_read = pay_read(&buffer[pay_index], size - pay_index, &pay_blk, true, flags);
            if (bytes_read < 0)
            {
                bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed (%d) to read payload block\n", bytes_read);
                continue;
            }
            else
            {
                pay_index += bytes_read;
            }

            /* Display Payload Block */
            bplog(NULL, BP_FLAG_DIAGNOSTIC, "#################################\n");
            if (pri_blk.is_admin_rec)
            {
                uint8_t rec_type = buffer[pay_index++];
                bplog(NULL, BP_FLAG_DIAGNOSTIC, "Administrative Record - 0x%02X (", rec_type);
                if (rec_type == BP_ACS_REC_TYPE)
                {
                    bplog(NULL, BP_FLAG_DIAGNOSTIC, "ACS) - ");
                }
                else if (rec_type == BP_CS_REC_TYPE)
                {
                    bplog(NULL, BP_FLAG_DIAGNOSTIC, "CS) - ");
                }
                else if (rec_type == BP_STAT_REC_TYPE)
                {
                    bplog(NULL, BP_FLAG_DIAGNOSTIC, "STATUS) - ");
                }
                else
                {
                    bplog(NULL, BP_FLAG_DIAGNOSTIC, "UNKNOWN) - ");
                }
            }
            else
            {
                bplog(NULL, BP_FLAG_DIAGNOSTIC, "User Data Payload - ");
            }

            /* Display Payload Data */
            while (pay_index < size)
            {
                bplog(NULL, BP_FLAG_DIAGNOSTIC, "%02X", buffer[pay_index++]);
            }
            bplog(NULL, BP_FLAG_DIAGNOSTIC, "\n");
        }
        else if (blk_type == BP_CTEB_BLK_TYPE)
        {
            bp_blk_cteb_t cteb_blk;
            int           cteb_index = start_index;

            /* Display Custody Transfer Enhancement Block */
            bplog(NULL, BP_FLAG_DIAGNOSTIC, "#################################\n");
            bytes_read = cteb_read(&buffer[cteb_index], size - cteb_index, &cteb_blk, true, flags);
            if (bytes_read < 0)
            {
                bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed to parse CTEB block at offset %d\n", cteb_index);
                continue;
            }

            bplog(NULL, BP_FLAG_DIAGNOSTIC, "Custody ID:        %d\n", cteb_blk.cid.value);
            bplog(NULL, BP_FLAG_DIAGNOSTIC, "Custodian IPN:     %d.%d\n", cteb_blk.cstnode, cteb_blk.cstserv);
        }
        else if (blk_type == BP_BIB_BLK_TYPE)
        {
            bp_blk_bib_t bib_blk;
            int          bib_index = start_index;

            /* Display Bundle Integrity Block */
            bplog(NULL, BP_FLAG_DIAGNOSTIC, "#################################\n");
            bytes_read = bib_read(&buffer[bib_index], size - bib_index, &bib_blk, true, flags);
            if (bytes_read < 0)
            {
                bplog(flags, BP_FLAG_FAILED_TO_PARSE, "Failed to parse BIB block at offset %d\n", bib_index);
                continue;
            }

            bplog(NULL, BP_FLAG_DIAGNOSTIC, "Bundle Payload CRC: %04X\n", bib_blk.security_result_data.crc16);
        }
    }

    /* Return Success */
    return BP_SUCCESS;
}
