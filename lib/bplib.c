/************************************************************************
 * File: bplib.c
 *
 *  Copyright 2019 United States Government as represented by the 
 *  Administrator of the National Aeronautics and Space Administration. 
 *  All Other Rights Reserved.  
 *
 *  This software was created at NASA's Goddard Space Flight Center.
 *  This software is governed by the NASA Open Source Agreement and may be 
 *  used, distributed and modified only pursuant to the terms of that 
 *  agreement.
 * 
 * Maintainer(s):
 *  Joe-Paul Swinski, Code 582 NASA GSFC
 *
 *************************************************************************/

/******************************************************************************
 INCLUDES
 ******************************************************************************/

#include "bplib.h"
#include "bplib_os.h"
#include "sdnv.h"
#include "pri.h"
#include "cteb.h"
#include "bib.h"
#include "pay.h"
#include "custody.h"
#include "bundle.h"
#include "crc.h"

/******************************************************************************
 DEFINES
 ******************************************************************************/

#ifndef LIBID
#define LIBID                           "unversioned"
#endif

#ifndef BP_WRAP_TIMEOUT
#define BP_WRAP_TIMEOUT                 1000    /* milliseconds */
#endif

/******************************************************************************
 TYPEDEFS
 ******************************************************************************/

/* Active Table */
typedef struct {
    bp_sid_t            sid;
    bp_val_t            retx;
} bp_active_table_t;

/* Channel Control Block */
typedef struct {
    bp_attr_t           attributes;
    bp_bundle_t         bundle;
    bp_custody_t        custody;
    bp_val_t            oldest_active_cid;
    bp_val_t            current_active_cid;
    int                 active_table_signal;
    bp_active_table_t*  active_table;
    bp_stats_t          stats;
} bp_channel_t;

/******************************************************************************
 CONSTANT DATA
 ******************************************************************************/

static const bp_attr_t default_attributes = {
    .lifetime               = BP_DEFAULT_LIFETIME,
    .request_custody        = BP_DEFAULT_REQUEST_CUSTODY,
    .admin_record           = BP_DEFAULT_ADMIN_RECORD,
    .integrity_check        = BP_DEFAULT_INTEGRITY_CHECK,
    .allow_fragmentation    = BP_DEFAULT_ALLOW_FRAGMENTATION,
    .cipher_suite           = BP_DEFAULT_CIPHER_SUITE,
    .timeout                = BP_DEFAULT_TIMEOUT,
    .max_length             = BP_DEFAULT_MAX_LENGTH,
    .wrap_response          = BP_DEFAULT_WRAP_RESPONSE,
    .cid_reuse              = BP_DEFAULT_CID_REUSE,
    .dacs_rate              = BP_DEFAULT_DACS_RATE,
    .active_table_size      = BP_DEFAULT_ACTIVE_TABLE_SIZE,
    .max_fills_per_dacs     = BP_DEFAULT_MAX_FILLS_PER_DACS,
    .max_gaps_per_dacs      = BP_DEFAULT_MAX_GAPS_PER_DACS,
    .storage_service_parm   = BP_DEFAULT_STORAGE_SERVICE_PARM
};

/******************************************************************************
 LOCAL FUNCTIONS
 ******************************************************************************/

/*--------------------------------------------------------------------------------------
 * acknowledge
 *-------------------------------------------------------------------------------------*/
static int acknowledge(void* parm, bp_val_t cid)
{
    bp_channel_t* ch = (bp_channel_t*)parm;
    int status = BP_FAILEDRESPONSE;

    int ati = cid % ch->attributes.active_table_size;
    bp_sid_t sid = ch->active_table[ati].sid;
    if(sid != BP_SID_VACANT)
    {
        status = ch->bundle.store.relinquish(ch->bundle.bundle_handle, sid);
        ch->active_table[ati].sid = BP_SID_VACANT;        
    }

    return status;
}

/******************************************************************************
 EXPORTED FUNCTIONS
 ******************************************************************************/

/*--------------------------------------------------------------------------------------
 * bplib_init - initializes bp library
 *-------------------------------------------------------------------------------------*/
void bplib_init(void)
{
    /* Initialize OS Interface */
    bplib_os_init();
    
    /* Initialize the Bundle Integrity Block Module */
    bib_init();
}

/*--------------------------------------------------------------------------------------
 * bplib_open -
 *-------------------------------------------------------------------------------------*/
bp_desc_t bplib_open(bp_route_t route, bp_store_t store, bp_attr_t* attributes)
{
    assert(store.create);
    assert(store.destroy);
    assert(store.enqueue);
    assert(store.dequeue);
    assert(store.retrieve);
    assert(store.relinquish);
    assert(store.getcount);
    
    /* Allocate Channel */
    bp_channel_t* ch = (bp_channel_t*)malloc(sizeof(bp_channel_t));
    if(ch == NULL)
    {
        bplog(BP_FAILEDMEM, "Cannot open channel: not enough memory\n");
        return NULL;
    }

    /* Clear Channel Memory and Initialize to Defaults */
    memset(ch, 0, sizeof(bp_channel_t));
    ch->active_table_signal = BP_INVALID_HANDLE;

    /* Set Initial Attributes */
    if(attributes)  ch->attributes = *attributes;
    else            ch->attributes = default_attributes;

    /* Initialize Bundle and Custody Modules 
     *  NOTE: this must occur first and together so that future calls to 
     *  un-initialize are safe to make.
     */
    uint16_t flags = 0;
    int bundle_status = bundle_initialize(&ch->bundle, route, store, &ch->attributes, true, &flags);
    int custody_status = custody_initialize(&ch->custody, route, store, &ch->attributes, &flags);
    if(bundle_status != BP_SUCCESS || custody_status != BP_SUCCESS)
    {
        bplog(BP_ERROR, "Failed to initialize channel, flags=%0X\n", flags);
        bplib_close(ch);
        return NULL;
    }

    /* Initialize Active Table */
    ch->active_table_signal = bplib_os_createlock();
    if(ch->active_table_signal < 0)
    {
        bplib_close(ch);
        bplog(BP_FAILEDOS, "Failed to create lock for active table\n");
        return NULL;
    }

    /* Allocate Memory for Active Table */
    ch->active_table = (bp_active_table_t*)malloc(sizeof(bp_active_table_t) * ch->attributes.active_table_size);
    if(ch->active_table == NULL)
    {
        bplib_close(ch);
        bplog(BP_FAILEDMEM, "Failed to allocate memory for channel active table\n");
        return NULL;
    }
    else
    {
        memset(ch->active_table, 0, sizeof(bp_active_table_t) * ch->attributes.active_table_size);
    }

    /* Initialize Data */
    ch->oldest_active_cid   = 1;
    ch->current_active_cid  = 1;

    /* Return Channel */
    return ch;
}

/*--------------------------------------------------------------------------------------
 * bplib_close -
 *-------------------------------------------------------------------------------------*/
void bplib_close(bp_desc_t channel)
{
    /* Check Parameters */
    if(channel == NULL) return;
    
    /* Get Channel */
    bp_channel_t* ch = (bp_channel_t*)channel;
    
    /* Un-initialize Active Table */
    if(ch->active_table_signal != BP_INVALID_HANDLE) bplib_os_destroylock(ch->active_table_signal);
    if(ch->active_table) free(ch->active_table);

    /* Un-initialize Bundle and Custody Modules */
    bundle_uninitialize(&ch->bundle);
    custody_uninitialize(&ch->custody);
    
    /* Free Channel */
    free(ch);
}

/*--------------------------------------------------------------------------------------
 * bplib_flush -
 *-------------------------------------------------------------------------------------*/
int bplib_flush(bp_desc_t channel)
{
    /* Check Parameters */
    if(channel == NULL) return BP_PARMERR;
    
    /* Get Channel */
    bp_channel_t* ch = (bp_channel_t*)channel;

    /* Send Data Bundle */
    bp_store_t* store = &ch->bundle.store;
    int handle = ch->bundle.bundle_handle;

    /* Lock Active Table */
    bplib_os_lock(ch->active_table_signal);
    {
        /* Relinquish All Active Bundles */
        while(ch->oldest_active_cid != ch->current_active_cid)
        {
            /* Get Storage ID of Oldest Active Bundle */
            int ati = ch->oldest_active_cid % ch->attributes.active_table_size;
            bp_sid_t sid = ch->active_table[ati].sid;

            /* Relinquish Bundle */
            if(sid != BP_SID_VACANT)
            {
                store->relinquish(handle, sid);
                ch->active_table[ati].sid = BP_SID_VACANT;
                ch->stats.lost++;
            }

            /* Go To Next Active Table Entry */
            ch->oldest_active_cid++;
        }
    }
    bplib_os_unlock(ch->active_table_signal);

    /* Return Success */
    return BP_SUCCESS;
}

/*--------------------------------------------------------------------------------------
 * bplib_config -
 *-------------------------------------------------------------------------------------*/
int bplib_config(bp_desc_t channel, int mode, int opt, void* val, int len)
{
    /* Check Parameters */
    if(channel == NULL)     return BP_PARMERR;
    else if(val == NULL)    return BP_PARMERR;
    
    /* Get Channel */
    bp_channel_t* ch = (bp_channel_t*)channel;

    /* Set Mode */
    bool setopt = mode == BP_OPT_MODE_WRITE ? true : false;
    
    /* Select and Process Option */
    switch(opt)
    {
        case BP_OPT_LIFETIME:
        {
            if(len != sizeof(int)) return BP_PARMERR;
            int* lifetime = (int*)val;
            if(setopt)  ch->attributes.lifetime = *lifetime;
            else        *lifetime = ch->attributes.lifetime;
            break;
        }
        case BP_OPT_REQUEST_CUSTODY:
        {
            if(len != sizeof(int)) return BP_PARMERR;
            int* enable = (int*)val;
            if(setopt && *enable != true && *enable != false) return BP_PARMERR;
            if(setopt)  ch->attributes.request_custody = *enable;
            else        *enable = ch->attributes.request_custody;
            break;
        }
        case BP_OPT_ADMIN_RECORD:
        {
            if(len != sizeof(int)) return BP_PARMERR;
            int* enable = (int*)val;
            if(setopt && *enable != true && *enable != false) return BP_PARMERR;
            if(setopt)  ch->attributes.admin_record = *enable;
            else        *enable = ch->attributes.admin_record;
            break;
        }
        case BP_OPT_INTEGRITY_CHECK:
        {
            if(len != sizeof(int)) return BP_PARMERR;
            int* enable = (int*)val;
            if(setopt && *enable != true && *enable != false) return BP_PARMERR;
            if(setopt)  ch->attributes.integrity_check = *enable;
            else        *enable = ch->attributes.integrity_check;
            break;
        }
        case BP_OPT_ALLOW_FRAGMENTATION:
        {
            if(len != sizeof(int)) return BP_PARMERR;
            int* enable = (int*)val;
            if(setopt && *enable != true && *enable != false) return BP_PARMERR;
            if(setopt)  ch->attributes.allow_fragmentation = *enable;
            else        *enable = ch->attributes.allow_fragmentation;
            break;
        }
        case BP_OPT_CIPHER_SUITE:
        {
            if(len != sizeof(int)) return BP_PARMERR;
            int* type = (int*)val;
            if(setopt)  ch->attributes.cipher_suite = *type;
            else        *type = ch->attributes.cipher_suite;
            break;
        }
        case BP_OPT_TIMEOUT:
        {
            if(len != sizeof(int)) return BP_PARMERR;
            int* timeout = (int*)val;
            if(setopt)  ch->attributes.timeout = *timeout;
            else        *timeout = ch->attributes.timeout;
            break;
        }
        case BP_OPT_MAX_LENGTH:
        {
            if(len != sizeof(int)) return BP_PARMERR;
            int* maxlen = (int*)val;
            if(setopt)  ch->attributes.max_length = *maxlen;
            else        *maxlen = ch->attributes.max_length;
            break;
        }
        case BP_OPT_WRAP_RESPONSE:
        {
            if(len != sizeof(int)) return BP_PARMERR;
            int* wrap = (int*)val;
            if(setopt && *wrap != BP_WRAP_RESEND && *wrap != BP_WRAP_BLOCK && *wrap != BP_WRAP_DROP) return BP_PARMERR;
            if(setopt)  ch->attributes.wrap_response = *wrap;
            else        *wrap = ch->attributes.wrap_response;
            break;
        }
        case BP_OPT_CID_REUSE:
        {
            if(len != sizeof(int)) return BP_PARMERR;
            int* enable = (int*)val;
            if(setopt && *enable != true && *enable != false) return BP_PARMERR;
            if(setopt)  ch->attributes.cid_reuse = *enable;
            else        *enable = ch->attributes.cid_reuse;
            break;
        }
        case BP_OPT_DACS_RATE:
        {
            if(len != sizeof(int)) return BP_PARMERR;
            int* rate = (int*)val;
            if(setopt)  ch->attributes.dacs_rate = *rate;
            else        *rate = ch->attributes.dacs_rate;
            break;
        }
        default:
        {
            /* Option Not Found */
            return bplog(BP_PARMERR, "Config. Option Not Found (%d)\n", opt);
        }
    }

    /* Re-initialize Bundles */
    if(setopt) ch->bundle.prebuilt = true;

    /* Return Status */
    return BP_SUCCESS;
}

/*--------------------------------------------------------------------------------------
 * bplib_latchstats -
 *-------------------------------------------------------------------------------------*/
int bplib_latchstats(bp_desc_t channel, bp_stats_t* stats)
{
     /* Check Parameters */
    if(channel == NULL)     return BP_PARMERR;
    else if(stats == NULL)  return BP_PARMERR;

    /* Get Channel */
    bp_channel_t* ch = (bp_channel_t*)channel;

    /* Update Store Counts */
    ch->stats.bundles = ch->bundle.store.getcount(ch->bundle.bundle_handle);
    ch->stats.payloads = ch->bundle.store.getcount(ch->bundle.payload_handle);
    ch->stats.records = ch->custody.bundle.store.getcount(ch->custody.bundle.bundle_handle);
    
    /* Update Active Statistic */
    ch->stats.active = ch->current_active_cid - ch->oldest_active_cid;

    /* Latch Statistics */
    *stats = ch->stats;

    /* Return Success */
    return BP_SUCCESS;
}

/*--------------------------------------------------------------------------------------
 * bplib_store -
 *-------------------------------------------------------------------------------------*/
int bplib_store(bp_desc_t channel, void* payload, int size, int timeout, uint16_t* flags)
{
    int status;

     /* Check Parameters */
    if(channel == NULL)         return BP_PARMERR;
    else if(payload == NULL)    return BP_PARMERR;
    else if(flags == NULL)  return BP_PARMERR;

    /* Get Channel */
    bp_channel_t* ch = (bp_channel_t*)channel;

    /* Lock Data Bundle */
    status = bundle_send(&ch->bundle, payload, size, timeout, flags);
    if(status == BP_SUCCESS) ch->stats.generated++;

    /* Return Status */
    return status;
}

/*--------------------------------------------------------------------------------------
 * bplib_load -
 *-------------------------------------------------------------------------------------*/
int bplib_load(bp_desc_t channel, void** bundle, int size, int timeout, uint16_t* flags)
{
    int status = BP_SUCCESS; /* size of bundle returned or error code */

    /* Check Parameters */
    if(channel == NULL)     return BP_PARMERR;
    else if(bundle == NULL) return BP_PARMERR;
    else if(flags == NULL)  return BP_PARMERR;

    /* Get Channel */
    bp_channel_t* ch = (bp_channel_t*)channel;
    
    /* Setup State */
    unsigned long       sysnow          = 0;                /* current system time used for timeouts (seconds) */
    bp_bundle_data_t*   data            = NULL;             /* start out assuming nothing to send */
    bp_sid_t            sid             = BP_SID_VACANT;    /* store id points to nothing */
    int                 ati             = -1;               /* active table index */
    bool                newcid          = true;             /* whether to assign new custody id and active table entry */
    bp_store_t*         store           = NULL;             /* which storage service to used */
    int                 handle          = -1;               /* handle for store service being loaded */

    /* Get Current Time */
    if(bplib_os_systime(&sysnow) == BP_OS_ERROR)
    {
        *flags |= BP_FLAG_UNRELIABLETIME;
    }
    
    /*-------------------------*/
    /* Try to Send DACS Bundle */
    /*-------------------------*/
    if(data == NULL)
    {
        /* Store any DACS currently being accumulated */
        custody_send(&ch->custody, ch->attributes.dacs_rate, sysnow, BP_CHECK, flags);

        /* Dequeue any stored DACS */
        store = &ch->custody.bundle.store;
        handle = ch->custody.bundle.bundle_handle;    
        if(store->dequeue(handle, (void**)&data, NULL, &sid, BP_CHECK) == BP_SUCCESS)
        {
            /* Set Route Flag */
            *flags |= BP_FLAG_ROUTENEEDED;
        }
    }
    
    /*------------------------------------------------*/
    /* Try to Send Active Bundle (if nothing to send) */
    /*------------------------------------------------*/
    if(data == NULL)
    {
        /* Send Data Bundle */
        store = &ch->bundle.store;
        handle = ch->bundle.bundle_handle;

        /* Process Active Table for Timeouts */
        bplib_os_lock(ch->active_table_signal);
        {
            /* Try to Send Timed-out Bundle */
            while((data == NULL) && (ch->oldest_active_cid != ch->current_active_cid))
            {
                ati = ch->oldest_active_cid % ch->attributes.active_table_size;
                sid = ch->active_table[ati].sid;
                if(sid == BP_SID_VACANT) /* entry vacant */
                {
                    ch->oldest_active_cid++;
                }
                else if(store->retrieve(handle, (void**)&data, NULL, sid, BP_CHECK) == BP_SUCCESS)
                {
                    if(data->exprtime != 0 && sysnow >= data->exprtime) /* check lifetime */
                    {
                        /* Bundle Expired - Clear Entry */
                        store->relinquish(handle, sid);
                        ch->active_table[ati].sid = BP_SID_VACANT;
                        ch->oldest_active_cid++;
                        ch->stats.expired++;
                        data = NULL;
                    }
                    else if(ch->attributes.timeout != 0 && sysnow >= (ch->active_table[ati].retx + ch->attributes.timeout)) /* check timeout */
                    {
                        /* Retransmit Bundle */
                        ch->oldest_active_cid++;
                        ch->stats.retransmitted++;

                        /* Handle Active Table and Custody ID */
                        if(ch->attributes.cid_reuse)
                        {
                            /* Set flag to reuse custody id and active table entry, 
                             * active table entry is not cleared, since CID is being reused */
                            newcid = false;
                        }
                        else
                        {
                            /* Clear Entry (it will be reinserted below at the current CID) */
                            ch->active_table[ati].sid = BP_SID_VACANT;
                        }
                    }
                    else /* oldest active bundle still active */
                    {
                        /* Bundle Not Ready to Retransmit */
                        data = NULL;

                        /* Check Active Table Has Room
                         * Since next step is to dequeue from store, need to make
                         * sure that there is room in the active table since we don't
                         * want to dequeue a bundle from store and have no place
                         * to put it.  Note that it is possible that even if the
                         * active table was full, if the bundle dequeued did not
                         * request custody transfer it could still go out, but the
                         * current design requires that at least one slot in the active
                         * table is open at all times. */
                        ati = ch->current_active_cid % ch->attributes.active_table_size;
                        sid = ch->active_table[ati].sid;
                        if(sid != BP_SID_VACANT) /* entry vacant */
                        {
                            *flags |= BP_FLAG_ACTIVETABLEWRAP;

                            if(ch->attributes.wrap_response == BP_WRAP_RESEND)
                            {
                                /* Bump Oldest Custody ID */
                                ch->oldest_active_cid++;

                                /* Retrieve Bundle from Storage */
                                if(store->retrieve(handle, (void**)&data, NULL, sid, BP_CHECK) != BP_SUCCESS)
                                {
                                    /* Failed to Retrieve - Clear Entry (and loop again) */
                                    store->relinquish(handle, sid);
                                    ch->active_table[ati].sid = BP_SID_VACANT;
                                    *flags |= BP_FLAG_STOREFAILURE;
                                    ch->stats.lost++;
                                }
                                else
                                {
                                    /* Force Retransmit - Do Not Reuse Custody ID */
                                    ch->stats.retransmitted++;
                                    bplib_os_waiton(ch->active_table_signal, BP_WRAP_TIMEOUT );
                                }
                            }
                            else if(ch->attributes.wrap_response == BP_WRAP_BLOCK)
                            {
                                /* Custody ID Wrapped Around to Occupied Slot */                            
                                status = BP_OVERFLOW;                   
                                bplib_os_waiton(ch->active_table_signal, BP_WRAP_TIMEOUT );
                            }
                            else /* if(ch->wrap_response == BP_WRAP_DROP) */
                            {
                                /* Bump Oldest Custody ID */
                                ch->oldest_active_cid++;

                                /* Clear Entry (and loop again) */
                                store->relinquish(handle, sid);
                                ch->active_table[ati].sid = BP_SID_VACANT;
                                ch->stats.lost++;
                            }
                        }

                        /* Break Out of Loop */
                        break;
                    }
                }
                else
                {
                    /* Failed to Retrieve Bundle from Storage */
                    store->relinquish(handle, sid);
                    ch->active_table[ati].sid = BP_SID_VACANT;
                    *flags |= BP_FLAG_STOREFAILURE;
                    ch->stats.lost++;
                }
            }
        }
        bplib_os_unlock(ch->active_table_signal);
    }

    /*------------------------------------------------*/
    /* Try to Send Stored Bundle (if nothing to send) */
    /*------------------------------------------------*/
    while(data == NULL)
    {
        /* Dequeue Bundle from Storage Service */
        int deq_status = store->dequeue(handle, (void**)&data, NULL, &sid, timeout);
        if(deq_status == BP_SUCCESS)
        {
            if(data->exprtime != 0 && sysnow >= data->exprtime)
            {
                /* Bundle Expired Clear Entry (and loop again) */
                store->relinquish(handle, sid);
                ch->stats.expired++;
                sid = BP_SID_VACANT;
                data = NULL;
            }
        }
        else if(deq_status == BP_TIMEOUT)
        {
            /* No Bundles in Storage to Send */
            status = BP_TIMEOUT;
            break;
        }
        else
        {
            /* Failed Storage Service */
            status = BP_FAILEDSTORE;
            *flags |= BP_FLAG_STOREFAILURE;
            break;
        }
    }
    
    /*------------------------------*/
    /* Send Bundle if Ready to Send */
    /*------------------------------*/
    if(data != NULL)
    {
        /* Check Buffer Size */
        if(*bundle == NULL || size >= data->bundlesize)
        {
            /* Check/Allocate Bundle Memory */
            if(*bundle == NULL) *bundle = malloc(data->bundlesize);

            /* Successfully Load Bundle to Application and Relinquish Memory */                
            if(*bundle != NULL)
            {
                /* If Custody Transfer */
                if(data->cteboffset != 0)
                {
                    bplib_os_lock(ch->active_table_signal);
                    {
                        /* Assign Custody ID and Active Table Entry */
                        if(newcid)
                        {
                            ati = ch->current_active_cid % ch->attributes.active_table_size;
                            ch->active_table[ati].sid = sid;
                            bundle_update(data, ch->current_active_cid++, flags);
                        }

                        /* Update Retransmit Time */
                        ch->active_table[ati].retx = sysnow;
                    }
                    bplib_os_unlock(ch->active_table_signal);
                }

                /* Load Bundle */
                memcpy(*bundle, data->header, data->bundlesize);
                status = data->bundlesize;
                ch->stats.transmitted++;
 
                /* If No Custody Transfer - Free Bundle Memory */
                if(data->cteboffset == 0)
                {
                    store->relinquish(handle, sid);
                }
            }
            else
            {
                status = bplog(BP_FAILEDMEM, "Unable to acquire memory for bundle of size %d\n", data->bundlesize);
                store->relinquish(handle, sid);
                ch->stats.lost++;                    
            }
        }
        else
        {
            status = bplog(BP_BUNDLETOOLARGE, "Bundle too large to fit inside buffer (%d %d)\n", size, data->bundlesize);
            store->relinquish(handle, sid);
            ch->stats.lost++;
        }
    }

    /* Return Size in Bytes or Status on Error */
    return status;
}

/*--------------------------------------------------------------------------------------
 * bplib_process -
 *-------------------------------------------------------------------------------------*/
int bplib_process(bp_desc_t channel, void* bundle, int size, int timeout, uint16_t* flags)
{
    int status;

    /* Check Parameters */
    if(channel == NULL)     return BP_PARMERR;
    else if(bundle == NULL) return BP_PARMERR;
    else if(flags == NULL)  return BP_PARMERR;

    /* Get Channel */
    bp_channel_t* ch = (bp_channel_t*)channel;

    /* Count Reception */
    ch->stats.received++;
    
    /* Get Time */
    unsigned long sysnow = 0;
    if(bplib_os_systime(&sysnow) == BP_OS_ERROR)
    {
        *flags |= BP_FLAG_UNRELIABLETIME;
    }

    /* Receive Bundle */
    bp_custodian_t custodian;
    status = bundle_receive(&ch->bundle, bundle, size, sysnow, &custodian, timeout, flags);
    if(status == BP_EXPIRED)
    {
        ch->stats.expired++;
    }
    else if(status == BP_PENDINGACKNOWLEDGMENT)
    {
        /* Process Aggregate Custody Signal - Process DACS */
        bplib_os_lock(ch->active_table_signal);
        {
            status = custody_acknowledge(&ch->custody, &custodian, acknowledge, (void*)ch, flags);
            if(status > 0)
            {
                int acknowledgment_count = status;
                status = BP_SUCCESS;
                ch->stats.acknowledged += acknowledgment_count;
                bplib_os_signal(ch->active_table_signal);
            }
        }
        bplib_os_unlock(ch->active_table_signal);
    }
    else if(status == BP_PENDINGCUSTODYTRANSFER)
    {
        /* Acknowledge Custody Transfer - Update DACS */
        status = custody_receive(&ch->custody, &custodian, sysnow, BP_CHECK, flags);
    }
    
    /* Return Status */
    return status;
}

/*--------------------------------------------------------------------------------------
 * bplib_accept -
 *
 *  Returns success if payload copied, or error code (zero, negative)
 *-------------------------------------------------------------------------------------*/
int bplib_accept(bp_desc_t channel, void** payload, int size, int timeout, uint16_t* flags)
{
    (void)flags;
    
    int status, deqstat;

    /* Check Parameters */
    if(channel == NULL)         return BP_PARMERR;
    else if(payload == NULL)    return BP_PARMERR;
    else if(flags == NULL)      return BP_PARMERR;

    /* Get Channel */
    bp_channel_t* ch = (bp_channel_t*)channel;

    /* Setup Variables */
    uint8_t*        payptr  = NULL;
    int             paylen  = 0;
    bp_sid_t        sid     = BP_SID_VACANT;

    /* Dequeue Payload from Storage */
    deqstat = ch->bundle.store.dequeue(ch->bundle.payload_handle, (void**)&payptr, &paylen, &sid, timeout);
    if(deqstat > 0)
    {
        /* Return Payload to Application */
        if(*payload == NULL || size >= paylen)
        {
            /* Check/Allocate Memory for Payload */
            if(*payload == NULL) *payload = malloc(paylen);

            /* Copy Payload and Set Status */
            if(*payload != NULL)
            {
                memcpy(*payload, payptr, paylen);
                status = paylen;
                ch->stats.delivered++;
            }
            else
            {
                status = bplog(BP_FAILEDMEM, "Unable to acquire memory for payload of size %d\n", paylen);
                ch->stats.lost++;
            }
        }
        else
        {
            status = bplog(BP_PAYLOADTOOLARGE, "Payload too large to fit inside buffer (%d %d)\n", size, paylen);
            ch->stats.lost++;
        }

        /* Relinquish Memory */
        ch->bundle.store.relinquish(ch->bundle.payload_handle, sid);
    }
    else 
    {
        status = deqstat;
    }

    /* Return Size of Payload in Bytes or Status on Error */
    return status;
}

/*--------------------------------------------------------------------------------------
 * bplib_routeinfo -
 *
 *  bundle -                pointer to a bundle (byte array) [INPUT]
 *  size -                  size of bundle being pointed to [INPUT]
 *  destination_node -      read from bundle [OUTPUT]
 *  destination_service -   as read from bundle [OUTPUT]
 *  Returns:                BP_SUCCESS or error code
 *-------------------------------------------------------------------------------------*/
int bplib_routeinfo(void* bundle, int size, bp_route_t* route)
{
    int status;
    bp_blk_pri_t pri_blk;
    uint8_t* buffer = (uint8_t*)bundle;
    uint16_t* flags = 0;

    /* Check Parameters */
    assert(buffer);

    /* Parse Primary Block */
    status = pri_read(buffer, size, &pri_blk, true, flags);
    if(status <= 0) return status;

    /* Set Addresses */
    if(route)
    {
        route->local_node           = (bp_ipn_t)pri_blk.srcnode.value;
        route->local_service        = (bp_ipn_t)pri_blk.srcserv.value;
        route->destination_node     = (bp_ipn_t)pri_blk.dstnode.value;
        route->destination_service  = (bp_ipn_t)pri_blk.dstserv.value;
        route->report_node          = (bp_ipn_t)pri_blk.rptnode.value;
        route->report_service       = (bp_ipn_t)pri_blk.rptserv.value;
    }

    /* Return Success */
    return BP_SUCCESS;
}

/*--------------------------------------------------------------------------------------
 * bplib_eid2ipn -
 *
 *  eid -                   null-terminated string representation of End Point ID [INPUT]
 *  len -                   size in bytes of above string [INPUT]
 *  node -                  node number as read from eid [OUTPUT]
 *  store -               store number as read from eid [OUTPUT]
 *  Returns:                BP_SUCCESS or error code
 *-------------------------------------------------------------------------------------*/
int bplib_eid2ipn(const char* eid, int len, bp_ipn_t* node, bp_ipn_t* store)
{
    char eidtmp[BP_MAX_EID_STRING];
    int tmplen;
    char* node_ptr;
    char* service_ptr;
    char* endptr;
    unsigned long node_result;
    unsigned long service_result;

    /* Sanity Check EID Pointer */
    if(eid == NULL)
    {
        return bplog(BP_INVALIDEID, "EID is null\n");
    }

    /* Sanity Check Length of EID */
    if(len < 7)
    {
        return bplog(BP_INVALIDEID, "EID must be at least 7 characters, act: %d\n", len);
    }
    else if(len > BP_MAX_EID_STRING)
    {
        return bplog(BP_INVALIDEID, "EID cannot exceed %d bytes in length, act: %d\n", BP_MAX_EID_STRING, len);
    }

    /* Check IPN Scheme */
    if(eid[0] != 'i' || eid[1] != 'p' || eid[2] != 'n' || eid[3] != ':')
    {
        return bplog(BP_INVALIDEID, "EID (%s) must start with 'ipn:'\n", eid);
    }

    /* Copy EID to Temporary Buffer and Set Pointers */
    tmplen = len - 4;
    memcpy(eidtmp, &eid[4], tmplen);
    eidtmp[tmplen] = '\0';
    node_ptr = eidtmp;
    service_ptr = strchr(node_ptr, '.');
    if(service_ptr != NULL)
    {
        *service_ptr = '\0';
        service_ptr++;
    }
    else
    {
        return bplog(BP_INVALIDEID, "Unable to find dotted notation in EID (%s)\n", eid);
    }

    /* Parse Node Number */
    errno = 0;
    node_result = strtoul(node_ptr, &endptr, 10); /* assume IPN node and store numbers always written in base 10 */
    if( (endptr == node_ptr) ||
        ((node_result == ULONG_MAX || node_result == 0) && errno == ERANGE) )
    {
        return bplog(BP_INVALIDEID, "Unable to parse EID (%s) node number\n", eid);
    }

    /* Parse Service Number */
    errno = 0;
    service_result = strtoul(service_ptr, &endptr, 10); /* assume IPN node and store numbers always written in base 10 */
    if( (endptr == service_ptr) ||
        ((service_result == ULONG_MAX || service_result == 0) && errno == ERANGE) )
    {
        return bplog(BP_INVALIDEID, "Unable to parse EID (%s) store number\n", eid);
    }

    /* Set Outputs */
    *node = (bp_ipn_t)node_result;
    *store = (bp_ipn_t)service_result;
    return BP_SUCCESS;
}

/*--------------------------------------------------------------------------------------
 * bplib_ipn2eid -
 *
 *  eid -                   buffer that will hold null-terminated string representation of End Point ID [OUTPUT]
 *  len -                   size in bytes of above buffer [INPUT]
 *  node -                  node number to be written into eid [INPUT]
 *  store -               store number to be written into eid [INPUT]
 *  Returns:                BP_SUCCESS or error code
 *-------------------------------------------------------------------------------------*/
int bplib_ipn2eid(char* eid, int len, bp_ipn_t node, bp_ipn_t store)
{
    /* Sanity Check EID Buffer Pointer */
    if(eid == NULL)
    {
        return bplog(BP_INVALIDEID, "EID buffer is null\n");
    }

    /* Sanity Check Length of EID Buffer */
    if(len < 7)
    {
        return bplog(BP_INVALIDEID, "EID buffer must be at least 7 characters, act: %d\n", len);
    }
    else if(len > BP_MAX_EID_STRING)
    {
        return bplog(BP_INVALIDEID, "EID buffer cannot exceed %d bytes in length, act: %d\n", BP_MAX_EID_STRING, len);
    }

    /* Write EID */
    bplib_os_format(eid, len, "ipn:%lu.%lu", (unsigned long)node, (unsigned long)store);

    return BP_SUCCESS;
}

/*--------------------------------------------------------------------------------------
 * bplib_attrinit -         initializes a channel attribute struct with default values
 *
 *  attr -                  pointer to attribute structure that needs to be initialized
 *  Returns:                BP_SUCCESS or error code
 *-------------------------------------------------------------------------------------*/
int bplib_attrinit(bp_attr_t* attr)
{
    if(attr)
    {
        *attr = default_attributes;
        return BP_SUCCESS;
    }
    else
    {
        return BP_PARMERR;
    }
}
