/************************************************************************
 * File: dacs.c
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
#include "rb_tree.h"
#include "sdnv.h"
#include "dacs.h"
#include "v6.h"

/******************************************************************************
 EXPORTED FUNCTIONS
 ******************************************************************************/

/*--------------------------------------------------------------------------------------
 * dacs_write -
 *
 *  rec - buffer containing the ACS record [OUTPUT]
 *  size - size of buffer [INPUT]
 *  max_fills_per_dacs - the maximum number of allowable fills for each dacs
 *  tree - a rb_tree ptr containing the cid ranges for the bundle. The tree nodes will 
 *      be deleted as they are written to the dacs. [OUTPUT]
 *  iter - a ptr to a ptr the next rb_node in the tree to extract the fill information
 *      and then delete. [OUTPUT]
 * 
 *  Returns:    Number of bytes processed of bundle
 *-------------------------------------------------------------------------------------*/
int dacs_write(uint8_t* rec, int size, int max_fills_per_dacs, rb_tree_t* tree, rb_node_t** iter, uint16_t* sdnvflags)
{
    uint16_t flags = 0;
    bp_sdnv_t cid = { 0, 2, 4 };
    bp_sdnv_t fill = { 0, 0, 2 };
 
    /* Write Record Information */
    rec[BP_ACS_REC_TYPE_INDEX] = BP_ACS_REC_TYPE; /* record type */
    rec[BP_ACS_REC_STATUS_INDEX] = BP_ACS_ACK_MASK;

    /* Write First CID and Fills */
    int count_fills = 0; /* The number of fills that have occured so far. */

    /* Store the previous and next range fills. */
    rb_range_t range;
    rb_range_t prev_range;

    /* Get the first available range from the rb tree and fill it. */
    rb_tree_get_next(tree, iter, &range, true, false);
    cid.value = range.value;
    fill.index = sdnv_write(rec, size, cid, &flags);
    fill.value = range.offset + 1;
    fill.index = sdnv_write(rec, size, fill, &flags);    
    count_fills += 2;

    /* Traverse tree in order and write out fills to dacs. */
    while (count_fills < max_fills_per_dacs && *iter != NULL)
    {
        prev_range = range;
        rb_tree_get_next(tree, iter, &range, true, false);        

        /* Write range of missing cid.
           Calculate the missing values between the current and previous node. */
        fill.value = range.value - (prev_range.value + prev_range.offset + 1);
        fill.index = sdnv_write(rec, size, fill, &flags);

        /* Write range of received cids. */
        fill.value = range.offset + 1;
        fill.index = sdnv_write(rec, size, fill, &flags);    
        count_fills += 2;        
    }

    /* Success Oriented Error Checking */
    if(flags != 0)
    {
        *sdnvflags |= flags;
        return bplog(BP_BUNDLEPARSEERR, "Flags raised during processing of DACS (%08X)\n", flags); 
    } 

    /* Return Block Size */
    return fill.index;
}

/*--------------------------------------------------------------------------------------
 * dacs_read -
 *-------------------------------------------------------------------------------------*/
int dacs_read(uint8_t* rec, int rec_size, bp_acknowledge_t ack, void* ack_parm, uint16_t* sdnvflags)
{
    bp_val_t i;
    uint16_t flags = 0;
    bp_sdnv_t cid = { 0, 2, 0 };
    bp_sdnv_t fill = { 0, 0, 0 };
    int cidin = true;
    uint8_t acs_status = rec[BP_ACS_REC_STATUS_INDEX];
    bool ack_success = (acs_status & BP_ACS_ACK_MASK) == BP_ACS_ACK_MASK;
    int ack_count = 0;
    
    /* Read First Custody ID */
    fill.index = sdnv_read(rec, rec_size, &cid, &flags);
    if(flags != 0)
    {
        return bplog(BP_BUNDLEPARSEERR, "Failed to read first custody ID (%08X)\n", flags);
    }

    /* Process Fills */
    while((int)fill.index < rec_size)
    {
        /* Read Fill */
        fill.index = sdnv_read(rec, rec_size, &fill, &flags);
        if(flags != 0)
        {
            return bplog(BP_BUNDLEPARSEERR, "Failed to read fill (%08X)\n", flags);
        }

        /* Process Custody IDs */
        if(cidin == true && ack_success)
        {
            /* Free Bundles */
            cidin = false;
            for(i = 0; i < fill.value; i++)
            {
                if(ack(ack_parm, cid.value + i) == BP_SUCCESS)
                {
                    ack_count++;
                }
            }
        }
        else
        {
            /* Skip Bundles */
            cidin = true;
        }

        /* Set Next Custody ID */
        cid.value += fill.value;
    }

    /* Success Oriented Error Checking */
    if(flags != 0)
    {
        *sdnvflags |= flags;
        return bplog(BP_BUNDLEPARSEERR, "Flags raised during processing of DACS (%08X)\n", flags); 
    } 

    /* Return Number of Acknowledgments */
    return ack_count;
}