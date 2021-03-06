/*
 * $Log:	rbc_private.h,v $
 * Revision 1.2  91/06/03  12:26:26  configtw
 * Tab conversion.
 * 
 * Revision 1.1  91/03/26  10:34:39  pls
 * Initial revision
 * 
*/

/*
 * Copyright (C) 1991, Integrated Parallel Technology, All Rights Reserved.
 * U. S. Government Sponsorship under SBIR Contract NAS7-1102 is acknowledged.
 */

/* rbc_private.h
 *
 * This is the driver code for the rollback chip
 * It assumes:
 *      32 bit size for longs
 *      integers are at least 16 bits
 *      unsigned longs can be used for addresses
 */

#ifndef STATE_ADDRESS   /* must be of form 0xYYY0 0000 */
#define STATE_ADDRESS   0x90000000
#endif

#ifndef CMD_ADDRESS     /* must be of form 0xXXX0 0000 */
#define CMD_ADDRESS     0x80000000
#endif
#define REG_OFFSET                      0x0
#define SEG_DEF_OFFSET                  0x0100
#define SEG_FRAME_COUNT_OFFSET  0x0800
#define SEG_ENCODE_OFFSET               0x01000

typedef struct
{
		unsigned long   system_commands;        /* write-only */
#define SET_TEST                0x001
#define SET_RUN         0x002
#define SET_SID_STATUS  0x004
#define CLEAR_WB_ALL    0x008
#define CLEAR_TABLES    0x010
#define RESET_MFC_ALL   0x020

#define FRAME_MASK              0x3f
#define SEG_SHIFT               16
		unsigned long   rollback;               /* rollback operation */
		unsigned long   advance;                /* advance operation */
		unsigned long   mark;                   /* mark operation */
		unsigned long   clear_wb;               /* clear written bits */
		unsigned long   reset_mfc;              /* reset frame counters */
		unsigned long   status;         /* read status info */
#define COMMAND_ABORT   0x0001
#define HARDWARE_ERROR  0x0002
#define ADVANCE_MASK    0x0004
#define ROLLBACK_MASK   0x0008
#define MARK_MASK               0x0010
#define TAG_N_PROGRESS  0x0020

#define MEM_CODE_MASK   0x01c0
#define MEM_CODE_SHIFT  6

#define RB_N_PROGRESS   0x0200
#define ADV_N_PROGRESS  0x0400
#define CLRING_WB_ALL   0x1000
#define CLRING_MFC_ALL  0x2000
#define CLRING_WB_SEG   0x4000
#define CLRING_WB_TABLES        0x8000

#define SEG_MASK                0xFF0000

		unsigned long   set_prefix;             /* for memory read/write */
		unsigned long   write_cmf;              /* current mark frame count */
		unsigned long   write_omf;              /* oldest mark frame count */
		unsigned long   set_wb_address; /* seek into written bit */
#define WB_MSB 0x0100000

		unsigned long   read_wb;                /* written bits @ wb_address*/
} RBC_register;


/*
unsigned long   seg_def_table[NUM_SEGS];
unsigned long   seg_frame_counters[NUM_SEGS];
#define MAX_ENCODE_SIZE 1024    /? one for each K of memory possible ?/
unsigned long   seg_encode_table[MAX_ENCODE_SIZE];
*/

extern RBC_register *cmd_register;

extern unsigned long rbc_memory_end;

extern unsigned long * seg_def_table;           /* seg_def_table_entry's */
#define LOW_MASK 0x03ff
#define HIGH_MASK 0x0ffc00
#define HIGH_SHIFT 10

extern unsigned long * seg_frame_counters;      /* six bits low CMF, next OMF */
#define CFC_MASK 0x03f
#define OFC_MASK 0x0fc0
#define OFC_SHIFT 6

extern unsigned long * seg_encode_table;        /* seg_no's */
#define ENCODE_MASK 0x0ff

