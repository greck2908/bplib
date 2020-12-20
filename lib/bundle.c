 /************************************************************************
 * File: bundle.c
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
#include "bundle.h"

/******************************************************************************
 EXPORTED FUNCTIONS
 ******************************************************************************/

/*--------------------------------------------------------------------------------------
 * bundle_initialize -
 *-------------------------------------------------------------------------------------*/
int bundle_initialize(bp_bundle_t* bundle, bp_route_t route, bp_store_t store, bp_attr_t* attributes, bool with_payload, uint16_t* flags)
{
    /* Initialize Bundle Parameters */
    bundle->route               = route;
    bundle->store               = store;
    bundle->attributes          = attributes;
    bundle->bundle_handle       = BP_INVALID_HANDLE;
    bundle->payload_handle      = BP_INVALID_HANDLE;
    
    /* Initialize Bundle Store */
    bundle->bundle_handle = bundle->store.create(bundle->attributes->storage_service_parm);
    if(bundle->bundle_handle < 0)
    {
        bundle_uninitialize(bundle);
        return bplog(BP_FAILEDSTORE, "Failed to create storage handle for bundles\n");
    }
    
    /* Initialize Payload Store */
    if(with_payload)
    {
        bundle->payload_handle = bundle->store.create(bundle->attributes->storage_service_parm);
        if(bundle->payload_handle < 0)
        {
            bundle_uninitialize(bundle);
            return bplog(BP_FAILEDSTORE, "Failed to create storage handle for payloads\n");
        }
    }
    else
    {
        bundle->payload_handle = BP_INVALID_HANDLE;
    }

    /* Initialize New Bundle */
    return v6_build(bundle, NULL, NULL, 0, flags);
}

/*--------------------------------------------------------------------------------------
 * bundle_uninitialize -
 *-------------------------------------------------------------------------------------*/
void bundle_uninitialize(bp_bundle_t* bundle)
{
    if(bundle->bundle_handle >= 0)
    {
        bundle->store.destroy(bundle->bundle_handle);
        bundle->bundle_handle = BP_INVALID_HANDLE;
    }
    
    if(bundle->payload_handle >= 0)
    {
        bundle->store.destroy(bundle->payload_handle);
        bundle->payload_handle = BP_INVALID_HANDLE;
    }
}

/*--------------------------------------------------------------------------------------
 * bundle_send -
 *-------------------------------------------------------------------------------------*/
int bundle_send(bp_bundle_t* bundle, uint8_t* pay, int pay_size, int timeout, uint16_t* flags)
{
    /* Check if Re-initialization Needed */
    if(bundle->prebuilt == false)
    {
        v6_build(bundle, NULL, NULL, 0, flags);
    }
    
    /* Store Bundle */
    return v6_write(bundle, true, pay, pay_size, timeout, flags);
}

/*--------------------------------------------------------------------------------------
 * bundle_receive -
 *-------------------------------------------------------------------------------------*/
int bundle_receive(bp_bundle_t* bundle, uint8_t* block, int block_size, bp_val_t sysnow, bp_custodian_t* custodian, int timeout, uint16_t* flags)
{
    /* Read Bundle */
    return v6_read(bundle, block, block_size, sysnow, custodian, timeout, flags);
}

/*--------------------------------------------------------------------------------------
 * bundle_update -
 *-------------------------------------------------------------------------------------*/
int bundle_update (bp_bundle_data_t* data, bp_val_t cid, uint16_t* flags)
{
    return v6_update(data, cid, flags);

}
