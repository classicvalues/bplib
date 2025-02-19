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

#ifndef BUNDLE_TYPES_H
#define BUNDLE_TYPES_H

/******************************************************************************
 INCLUDES
 ******************************************************************************/

#include "bplib.h"

/******************************************************************************
 DEFINES
 ******************************************************************************/

#define BP_BUNDLE_HDR_BUF_SIZE 128

#define BP_DUPLICATE              (-100)
#define BP_FULL                   (-101)
#define BP_PENDING_ACKNOWLEDGMENT (-102)
#define BP_PENDING_FORWARD        (-103)
#define BP_PENDING_ACCEPTANCE     (-104)
#define BP_PENDING_APPLICATION    (-105)
#define BP_PENDING_EXPIRATION     (-106)

/******************************************************************************
 TYPEDEFS
 ******************************************************************************/

/* Call-Backs */
typedef int (*bp_create_func_t)(void *parm, bool is_record, const uint8_t *payload, int size, int timeout);
typedef int (*bp_delete_func_t)(void *parm, bp_val_t cid, uint32_t *flags);

/* Bundle Field (fixed size) */
typedef struct
{
    bp_val_t value; /* value of field */
    int      index; /* offset into memory block to write value */
    int      width; /* number of bytes in memory block value uses */
} bp_field_t;

/* Active Bundle */
typedef struct
{
    bp_sid_t sid;  /* storage id */
    bp_val_t retx; /* retransmit time */
    bp_val_t cid;  /* custody id */
} bp_active_bundle_t;

/* Payload Data */
typedef struct
{
    bp_val_t exprtime;    /* absolute time when payload expires */
    bool     ackapp;      /* acknowledgement by application is requested */
    int      payloadsize; /* size of payload */
} bp_payload_data_t;

/* Pending Structure */
typedef struct
{
    bp_val_t          cid;     /* custody id of payload */
    bp_ipn_t          node;    /* custody node of payload */
    bp_ipn_t          service; /* custody service of payload */
    bp_payload_data_t data;    /* serialized and stored payload data */
    const uint8_t    *memptr;  /* pointer to payload */
} bp_payload_t;

/* Bundle Data */
typedef struct
{
    bp_val_t   exprtime;                       /* absolute time when bundle expires */
    bp_field_t cidfield;                       /* SDNV of custody id field of bundle */
    int        cteboffset;                     /* offset of the CTEB block of bundle */
    int        biboffset;                      /* offset of the BIB block of bundle */
    int        payoffset;                      /* offset of the payload block of bundle */
    int        headersize;                     /* size of the header (portion of buffer below used) */
    int        bundlesize;                     /* total size of the bundle (header and payload) */
    uint8_t    header[BP_BUNDLE_HDR_BUF_SIZE]; /* header portion of bundle */
} bp_bundle_data_t;

/* Bundle Structure */
typedef struct
{
    bp_route_t       route;      /* addressing information */
    bp_attr_t        attributes; /* bundle attributes */
    bp_bundle_data_t data;       /* serialized and stored bundle data */
    bool             prebuilt;   /* does pre-built bundle header need initialization */
    void            *blocks;     /* populated in initialization function */
} bp_bundle_t;

#endif /* BUNDLE_TYPES_H */
