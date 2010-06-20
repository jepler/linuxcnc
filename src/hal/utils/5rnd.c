/*************************************************************************
*
* bfload - loads xilinx bitfile into the FPGA of
* an Anything I/O board from Mesa Electronics
*
* Copyright (C) 2007 John Kasunich (jmkasunich at fastmail dot fm)
* portions based on m5i20cfg by Peter C. Wallace
*
**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of version 2 of the GNU General
Public License as published by the Free Software Foundation.
This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111 USA

THE AUTHORS OF THIS LIBRARY ACCEPT ABSOLUTELY NO LIABILITY FOR
ANY HARM OR LOSS RESULTING FROM ITS USE.  IT IS _EXTREMELY_ UNWISE
TO RELY ON SOFTWARE ALONE FOR SAFETY.  Any machinery capable of
harming persons must have provisions for completely removing power
from all motors, etc, before persons enter any danger area.  All
machinery must be designed to comply with local and national safety
codes, and the authors of this software can not, and do not, take
any responsibility for such compliance.

This code was written as part of the EMC HAL project.  For more
information, go to www.linuxcnc.org.

**************************************************************************

Info about programming the 5i20:

Board wiring (related to programming):

 unused				<-- 9030 pin 156, GPIO2, bit  8 reg 0x54
 FPGA pin 104 = DONE		--> 9030 pin 157, GPIO3, bit 11 reg 0x54
 FGPA pin 107 = /INIT		--> 9030 pin 137, GPIO4, bit 14 reg 0x54
 STATUS LED CR11 (low = on)	<-- 9030 pin 136, GPIO5, bit 17 reg 0x54
 unused				<-- 9030 pin 135, GPIO6, bit 20 reg 0x54
 FPGA pin 161 = /WRITE		<-- 9030 pin 134, GPIO7, but 23 reg 0x54
 FPGA pin 106 = /PROGRAM,	<-- 9030 pin  94, GPIO8, bit 26 reg 0x54
 FPGA pin 155 = CCLK		<-> FPGA pin 182 (LCLK 33MHz)
 FPGA pin 160 = /CS		<-- /LWR (local bus write strobe)
 FPGA pin 153 = D0		<-> LAD0 (local data bus)
 FPGA pin 146 = D1		<-> LAD1 (local data bus)
 FPGA pin 142 = D2		<-> LAD2 (local data bus)
 FPGA pin 135 = D3		<-> LAD3 (local data bus)
 FPGA pin 126 = D4		<-> LAD4 (local data bus)
 FPGA pin 119 = D5		<-> LAD5 (local data bus)
 FPGA pin 115 = D6		<-> LAD6 (local data bus)
 FPGA pin 108 = D7		<-> LAD7 (local data bus)

Programming sequence:

 set /PROGRAM low for 300nS minimum (resets chip and starts clearing memory)
 /INIT and DONE go low
 set /PROGRAM high
 wait for /INIT to go high (100uS max, when done clearing memory)
 set /WRITE low
 send data bytes (each byte strobes /CS low)
 the last few bytes in the file are dummies, which provide the clocks
 needed to let the device come out of config mode and begin running
 if a CRC error is detected, /INIT will go low
 DONE will go high during the dummy bytes at the end of the file
 set /WRITE high

**************************************************************************

Info about programming the 5i22:

Board wiring (related to programming):

 FPGA pin R15 = DONE		--> 9054 pin 159, USERI/
 FGPA pin U10 = /INIT		--> not accessible from the PC
 FPGA pin V3  = RD_WR/		<-- pulled low?
 FPGA pin E5  = /PROGRAM,	<-- 9054 pin 154, USERO/
 FPGA pin T15 = CCLK		<-> FPGA pin ??? (LCLK 33MHz???)
 FPGA pin V2  = /CS		<-- /LWR (local bus write strobe)
 FPGA pin T12 = D0		<-> LAD0 (local data bus)
 FPGA pin R12 = D1		<-> LAD1 (local data bus)
 FPGA pin N11 = D2		<-> LAD2 (local data bus)
 FPGA pin P11 = D3		<-> LAD3 (local data bus)
 FPGA pin U9  = D4		<-> LAD4 (local data bus)
 FPGA pin V9  = D5		<-> LAD5 (local data bus)
 FPGA pin R7  = D6		<-> LAD6 (local data bus)
 FPGA pin T7  = D7		<-> LAD7 (local data bus)

Programming sequence:

 set /PROGRAM low for 300nS minimum (resets chip and starts clearing memory)
 /INIT and DONE go low (verify that DONE is low, /INIT is inaccessible)
 set /PROGRAM high
 wait at least 100uS for clearing memory (INIT/ goes high but can't sense it)
 set /WRITE low (maybe already pulled low?)
 send data bytes (each byte strobes /CS low)
 the last few bytes in the file are dummies, which provide the clocks
 needed to let the device come out of config mode and begin running
 if a CRC error is detected, /INIT will go low (can't tell) and DONE will
 not go high.
 DONE will go high during the dummy bytes at the end of the file if all is OK
 set /WRITE high (or maybe leave alone)

*************************************************************************/

//#define _GNU_SOURCE /* getline() */

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/io.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/types.h>

#include "epp.h"
#include "upci.h"
#include "bitfile.h"


/************************************************************************/
#define MASK(x)			(1<<(x))	/* gets a bit in position (x) */
#define CHECK_W(w,x)	((w&MASK(x))==MASK(x))	/* true if bit x in w is set */

/* I/O registers */
#define CTRL_STAT_OFFSET	0x0054	/* 9030 GPIO register (region 1) */


//
// the LAS?BRD registers are in the PLX 9030
// HostMot2 firmware needs the #READY bit (0x2) set in order to work, but
// some older eeproms on the 5i20 (and maybe other cards) dont set them
// right, but we can detect the problem and fix it up
//

#define LAS0BRD_OFFSET 0x28
#define LAS1BRD_OFFSET 0x2C
#define LAS2BRD_OFFSET 0x30
#define LAS3BRD_OFFSET 0x34

#define LAS3RR_OFFSET 0xc

#define LASxRR_PREFETCHABLE (1<<3)

#define LASxBRD_READY 0x2
#define LASxBRD_BURST_ENABLE 1
#define LASxBRD_BTERM_ENABLE (1<<2)
#define LASxBRD_PREFETCH_16 ((1<<3) | (1<<4))
#define LASxBRD_PREFETCH_4 ((1<<3))
#define LASxBRD_PREFETCH_ENABLE (1<<5)


#define CNTRL_OFFSET 0x50
#define CNTRL_READ_NO_FLUSH (1<<16)
#define CNTRL_PCI22 (1<<14)


 /* bit number in 9030 GPIO register */
#define GPIO_3_MASK		(1<<11)	/* GPIO 3 */
#define DONE_MASK		(1<<11)	/* GPIO 3 */
#define _INIT_MASK		(1<<14)	/* GPIO 4 */
#define _LED_MASK		(1<<17)	/* GPIO 5 */
#define GPIO_6_MASK		(1<<20)	/* GPIO 6 */
#define _WRITE_MASK		(1<<23)	/* GPIO 7 */
#define _PROGRAM_MASK		(1<<26)	/* GPIO 8 */

/* Exit codes */
#define EC_OK    0   /* Exit OK. */
#define EC_BADCL 100 /* Bad command line. */
#define EC_HDW   101 /* Some sort of hardware failure on the 5I20. */
#define EC_FILE  102 /* File error of some sort. */
#define EC_SYS   103 /* Beyond our scope. */

/* I/O register indices.
*/
#define CTRL_STAT_OFFSET_5I22	0x006C /* 5I22 32-bit control/status register. */

 /* bit number in 9054 GPIO register */
 /* yes, the direction control bits are not in the same order as the I/O bits */
#define DONE_MASK_5I22			(1<<17)	/* GPI */
#define _PROGRAM_MASK_5I22		(1<<16)	/* GPO, active low */
#define DONE_ENABLE_5I22		(1<<18) /* GPI direction control, 1=input */
#define _PROG_ENABLE_5I22		(1<<19) /* GPO direction control, 1=output */

/* how long should we wait for DONE when programming 9054-based cards */
#define DONE_WAIT_5I22			20000

/************************************************************************/


//
// this data structure describes a board we know how to program
//

struct board_info {
    char *board_type;
    char *chip_type;

    enum { IO_TYPE_PCI, IO_TYPE_EPP } io_type;
    union {
        struct {
            unsigned short vendor_id;
            unsigned short device_id;
            unsigned short ss_vendor_id;
            unsigned short ss_device_id;
            int fpga_pci_region;
            int upci_devnum;
        } pci;

        struct epp epp;
    } io;

    int (*program_funct) (struct board_info *bd, struct bitfile_chunk *ch);
};


//
// these are the functions that actually program the FPGA
//

static int program_5i20_fpga(struct board_info *bd, struct bitfile_chunk *ch);
static int program_5i22_fpga(struct board_info *bd, struct bitfile_chunk *ch);
static int program_7i43_fpga(struct board_info *board, struct bitfile_chunk *ch);


// 
// this array describes all the boards we know how to program
//

struct board_info board_info_table[] = {

    {
        .board_type = "5i20",
        .chip_type = "2s200pq208",
        .io_type = IO_TYPE_PCI,
        .io.pci.vendor_id = 0x10B5,
        .io.pci.device_id = 0x9030,
        .io.pci.ss_vendor_id = 0x10B5,
        .io.pci.ss_device_id = 0x3131,
        .io.pci.fpga_pci_region = 5,
        .io.pci.upci_devnum = 0,
        .program_funct = program_5i20_fpga
    },

    {
        .board_type = "5i22-1M",
        .chip_type = "3s1000fg320",
        .io_type = IO_TYPE_PCI,
        .io.pci.vendor_id = 0x10B5,
        .io.pci.device_id = 0x9054,
        .io.pci.ss_vendor_id = 0x10B5,
        .io.pci.ss_device_id = 0x3132,
        .io.pci.fpga_pci_region = 3,
        .io.pci.upci_devnum = 0,
        .program_funct = program_5i22_fpga
    },

    {
        .board_type = "5i22-1.5M",
        .chip_type = "3s1500fg320",
        .io_type = IO_TYPE_PCI,
        .io.pci.vendor_id = 0x10B5,
        .io.pci.device_id = 0x9054,
        .io.pci.ss_vendor_id = 0x10B5,
        .io.pci.ss_device_id = 0x3131,
        .io.pci.fpga_pci_region = 3,
        .io.pci.upci_devnum = 0,
        .program_funct = program_5i22_fpga
    },

    {
        .board_type = "7i43",
        .chip_type = "3s200tq144",
        .io_type = IO_TYPE_EPP,
        .io.epp.io_addr = 0x378,
        .io.epp.io_addr_hi = 0x778,
        .program_funct = program_7i43_fpga
    },

    {
        .board_type = "7i43",
        .chip_type = "3s400tq144",
        .io_type = IO_TYPE_EPP,
        .io.epp.io_addr = 0x378,
        .io.epp.io_addr_hi = 0x778,
        .program_funct = program_7i43_fpga
    }

};


static void errmsg(const char *funct, const char *fmt, ...);
static __u8 bit_reverse (__u8 data);
static int write_fpga_ram(struct board_info *bd, struct bitfile_chunk *ch);

void list_devices(void);
void usage(void);
int parse_program_command(char *cmd);


struct bitfile *open_bitfile_or_die(char *filename) {
    struct bitfile *bf;
    int r;

    fprintf(stderr, "Reading '%s'...\n", filename);

    bf = bitfile_read(filename);
    if (bf == NULL) {
	errmsg(__func__, "reading bitstream file '%s'", filename );
	exit(EC_FILE);
    }

    r = bitfile_validate_xilinx_info(bf);
    if (r != 0) {
	errmsg(__func__, "not a valid Xilinx bitfile");
	exit(EC_FILE);
    }

    bitfile_print_xilinx_info(bf);

    return bf;
}

static int reset_pci9030(int ctrl_region, uint32_t *status_out) {
    uint32_t status, control;
    fprintf(stderr, "Resetting FPGA...\n" );
    /* read current state of register */
    status = upci_read_u32(ctrl_region, CTRL_STAT_OFFSET);
    /* set /PROGRAM bit low  to reset device */
    control = status & ~_PROGRAM_MASK;
    /* set /WRITE and /LED high, (idle state) */
    control |= _WRITE_MASK | _LED_MASK;
    /* and write it back */
    upci_write_u32(ctrl_region, CTRL_STAT_OFFSET, control);
    /* verify that /INIT and DONE went low */
    status = upci_read_u32(ctrl_region, CTRL_STAT_OFFSET);
    if ( status & (DONE_MASK | _INIT_MASK) ) {
	errmsg(__func__, "FPGA did not reset: /INIT = %d, DONE = %d",
	    (status & _INIT_MASK ? 1 : 0), (status & DONE_MASK ? 1 : 0) );
        return 0;
    }
    if(status_out)
        *status_out = status;
    return 1;
}


#define N (4096) // number of u32s to transmit in one write
#define P (256)  // number of consecutive registers to read
static int copy_random_stdout(struct board_info *bd) __attribute__((noinline));
static int copy_random_stdout(struct board_info *bd)
{
    /* open regions for access */
    int region;
    int ctrl_region;
    uint32_t old_LASxBRD, new_LASxBRD;
    uint32_t old_LASxRR, new_LASxRR;
    uint32_t old_CNTRL, new_CNTRL;
    uint32_t buf[N];
    volatile uint32_t *rptr;
    fprintf(stderr, "Opening PCI regions... [devnum=%d]\n",
	bd->io.pci.upci_devnum);
    /* open regions for access */
    ctrl_region = upci_open_region(bd->io.pci.upci_devnum, 1);
    if ( ctrl_region < 0 ) {
	errmsg(__func__, "could not open device %d, region %d (5i20 control port)",
	    bd->io.pci.upci_devnum, 1 );
	goto cleanup0;
    }

    old_LASxRR = upci_read_u32(ctrl_region, LAS3RR_OFFSET);
    new_LASxRR = old_LASxRR | LASxRR_PREFETCHABLE;
    fprintf(stderr, "LASxRR=%08x->%08x\n", old_LASxRR, new_LASxRR);
    upci_write_u32(ctrl_region, LAS3RR_OFFSET, new_LASxRR);

    old_LASxBRD = upci_read_u32(ctrl_region, LAS3BRD_OFFSET);
    new_LASxBRD = old_LASxBRD | LASxBRD_BURST_ENABLE | LASxBRD_PREFETCH_16 | LASxBRD_BTERM_ENABLE;
    fprintf(stderr, "LASxBRD=%08x->%08x\n", old_LASxBRD, new_LASxBRD);
    upci_write_u32(ctrl_region, LAS3BRD_OFFSET, new_LASxBRD);

    old_CNTRL = upci_read_u32(ctrl_region, CNTRL_OFFSET);
    new_CNTRL = old_CNTRL | CNTRL_READ_NO_FLUSH | CNTRL_PCI22;
    fprintf(stderr, "CNTRL=%08x->%08x\n", old_CNTRL, new_CNTRL);
    upci_write_u32(ctrl_region, CNTRL_OFFSET, new_CNTRL);

    region = upci_open_region(bd->io.pci.upci_devnum, 5);
    if(region < 0) {
	perror("udev_open_region 5");
        goto cleanup1;
	return EC_SYS;
    }

    rptr = (volatile uint32_t*)upci_get_read_addr(region, 0xf00);
    signal(SIGPIPE, SIG_IGN);
    while(1) {
        uint32_t *d = buf, *bend=buf+N;
        while(d<bend) {
            volatile uint32_t *s=rptr, *e=rptr+P/sizeof(uint32_t);
            while(s < e) {
                *d++ = *s++;
            }
        }
	if(write(1, buf, sizeof(buf)) < 0) break;
    }

    new_LASxRR = old_LASxRR & ~LASxRR_PREFETCHABLE;
    new_LASxBRD = new_LASxBRD & ~(LASxBRD_BURST_ENABLE | LASxBRD_PREFETCH_ENABLE | LASxBRD_PREFETCH_16 | LASxBRD_BTERM_ENABLE);
    new_CNTRL = new_CNTRL & ~(CNTRL_READ_NO_FLUSH | CNTRL_PCI22);
    upci_write_u32(ctrl_region, LAS3RR_OFFSET, new_LASxRR);
    upci_write_u32(ctrl_region, LAS3BRD_OFFSET, new_LASxBRD);
    upci_write_u32(ctrl_region, CNTRL_OFFSET, new_CNTRL);
    fprintf(stderr, "LASxRR=->%08x\n", new_LASxRR);
    fprintf(stderr, "LASxBRD=->%08x\n", new_LASxBRD);
    fprintf(stderr, "CNTRL=->%08x\n", new_CNTRL);
    reset_pci9030(ctrl_region, 0);
    upci_close_region(region);
cleanup1:
    upci_close_region(ctrl_region);
cleanup0:

    return EC_OK;
}


/***********************************************************************/

int main(int argc, char *argv[])
{
    /* if we are setuid, drop privs until needed */
    seteuid(getuid());


    if (argc != 2) {
        usage();
        exit(EC_BADCL);
    }

    if (strcmp(argv[1], "list") == 0) {
        list_devices();
        exit(EC_OK);
    }

    if (strcmp(argv[1], "help") == 0) {
        usage();
        exit(EC_OK);
    }

    return parse_program_command(argv[1]);
}


// NOTE: can only list PCI devices, no EPP devices
void list_devices(void) {
    int r;
    int pci_index;

    int num_pci_devices;
    int num_boards;

    r = upci_scan_bus();
    if (r < 0) {
        errmsg(__func__,"upci error scanning bus");
        exit(EC_SYS);
    }

    num_pci_devices = r;
    num_boards = sizeof(board_info_table) / sizeof(struct board_info);

    for (pci_index = 0; pci_index < num_pci_devices; pci_index ++) {
        struct upci_dev_info p;
        int board_index;

        upci_get_device_info(&p, pci_index);

        // see if this pci device is in the board table
        for (board_index = 0; board_index < num_boards; board_index ++) {
            struct board_info *board = &board_info_table[board_index];

            if (board->io_type != IO_TYPE_PCI) continue;
            if (board->io.pci.vendor_id != p.vendor_id) continue;
            if (board->io.pci.device_id != p.device_id) continue;
            if (board->io.pci.ss_vendor_id != p.ss_vendor_id) continue;
            if (board->io.pci.ss_device_id != p.ss_device_id) continue;

            fprintf(stderr, "%s at PCI %02x:%02x.%x\n", board->board_type, p.bus, p.dev, p.func);
        }
    }

    exit(EC_OK);
}


/************************************************************************/

static void errmsg(const char *funct, const char *fmt, ...)
{
    va_list vp;

    va_start(vp, fmt);
    fprintf(stderr, "ERROR in %s(): ", funct);
    vfprintf(stderr, fmt, vp);
    fprintf(stderr, "\n");
}


void usage(void) {
    fprintf(stderr, "\n");
    fprintf(stderr, "bfload help - show this help\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "bfload list - list Anything I/O PCI boards found on this system\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "bfload BoardType[:BoardIdentifier]=BitFile\n");
    fprintf(stderr, "    BoardType       - Anything I/O board model name\n");
    fprintf(stderr, "    BoardIdentifier - Specifies which board of that type.  For PCI boards,\n");
    fprintf(stderr, "                      BoardIdentifier is the PCI bus index of the card\n");
    fprintf(stderr, "                      (default 0).  For EPP boards, BoardIdentifier is\n");
    fprintf(stderr, "                      'IOAddr[,IOAddrHigh]' (default IOAddr is 0x378,\n");
    fprintf(stderr, "                      default IOAddrHigh is IOAddr + 0x400.\n");
    fprintf(stderr, "    BitFile         - Name of bitfile to program\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Loads an FPGA configuration from a bitfile into the FPGA of an Anything I/O\n");
    fprintf(stderr, "board from Mesa Electronics.  If the bitfile contains HAL driver config\n");
    fprintf(stderr, "data, writes that data to the FPGA's RAM.\n");
    fprintf(stderr, "\n");
}


int program_pci_board(struct board_info *board, char *device_id, struct bitfile *bf);
int program_epp_board(struct board_info *board, char *device_id, struct bitfile *bf);

int program(char *device_type, char *device_id, char *filename) {
    struct bitfile *bf;
    char *bitfile_chip;
    struct bitfile_chunk *ch;
    struct board_info *board;

    int found_device_type;
    int num_boards;
    int i, r;


    // 
    // open the bitfile
    //

    bf = open_bitfile_or_die(filename);

    // chunk 'b' has the bitfile's target device, the chip type it's for
    ch = bitfile_find_chunk(bf, 'b', 0);
    bitfile_chip = (char *)(ch->body);


    //
    // look up the device type that the caller requested in our table of
    // known device types
    // 

    num_boards = sizeof(board_info_table) / sizeof(struct board_info);
    board = NULL;
    found_device_type = 0;
    for (i = 0; i < num_boards; i ++) {
        if (strcasecmp(board_info_table[i].board_type, device_type) == 0) {
            found_device_type = 1;
            if (strcasecmp(board_info_table[i].chip_type, bitfile_chip) == 0) {
                board = &board_info_table[i];
                break;
            }
        }
    }
    if (!found_device_type) {
        fprintf(stderr, "board type '%s' is unknown\n", device_type);
        bitfile_free(bf);
        return EC_BADCL;
    }
    if (board == NULL) {
        fprintf(stderr, "chip type incompatibility\n");
        fprintf(stderr, "board type '%s' is not available with bitfile's FPGA type '%s'\n", device_type, bitfile_chip);
        bitfile_free(bf);
        return EC_BADCL;
    }


    // 
    // At this point the program has identified the board type (it's in
    // "board") and read in the bitfile (it's in "bf").
    //
    // Next we need to parse the device_id to find out *which* board of the
    // required type we're supposed to program.
    //

    switch (board->io_type) {
        case IO_TYPE_PCI:
            r = program_pci_board(board, device_id, bf);
            break;

        case IO_TYPE_EPP:
            r = program_epp_board(board, device_id, bf);
            break;

        default:
            fprintf(stderr, "don't know how to parse %s device id '%s'\n", board->board_type, device_id);
            bitfile_free(bf);
            r = EC_BADCL;
    }

    bitfile_free(bf);
    copy_random_stdout(board);
    
    return r;
}


int program_pci_board(struct board_info *board, char *device_id, struct bitfile *bf) {
    struct bitfile_chunk *ch;
    int board_num;
    struct upci_dev_info info;
    int r;


    if (device_id == NULL) {
        board_num = 0;
    } else {
        char *endp;
        board_num = strtol(device_id, &endp, 0);
        if (*endp != '\0') {
            fprintf(stderr, "error parsing board number from '%s'\n", device_id);
            return EC_BADCL;
        }
    }


    //
    // find the PCI board
    //

    r = upci_scan_bus();
    if (r < 0) {
	errmsg(__func__, "PCI bus data missing");
	return EC_SYS;
    }

    info.vendor_id = board->io.pci.vendor_id;
    info.device_id = board->io.pci.device_id;
    info.ss_vendor_id = board->io.pci.ss_vendor_id;
    info.ss_device_id = board->io.pci.ss_device_id;
    info.instance = board_num;

    board->io.pci.upci_devnum = upci_find_device(&info);
    if (board->io.pci.upci_devnum < 0) {
	errmsg(__func__, "%s board #%d not found",
	    board->board_type, board_num);
	return EC_HDW;
    }

    upci_print_device_info(board->io.pci.upci_devnum);


    //
    // program the board with the bitfile
    //

    /* chunk 'e' has the bitstream */
    ch = bitfile_find_chunk(bf, 'e', 0);

    fprintf(stderr, "Loading configuration %s into %s:%d board...\n", bf->filename, board->board_type, board_num);
    r = board->program_funct(board, ch);
    if (r != 0) {
	errmsg(__func__, "configuration did not load");
	return EC_HDW;
    }

    /* do we need to HAL driver data to the FGPA RAM? */
    // HostMot2 does not use this 
    ch = bitfile_find_chunk(bf, 'r', 0);
    if (ch != NULL) {
	fprintf(stderr, "Writing data to FPGA RAM\n");
	r = write_fpga_ram(board, ch);
	if (r != 0) {
	    errmsg(__func__, "RAM data could not be loaded");
	    return EC_HDW;
	}
    }

    return 0;
}


int program_epp_board(struct board_info *board, char *device_id, struct bitfile *bf) {
    struct bitfile_chunk *ch;
    int r;

    // this snippet of code is nasty
    // i hate doing text parsing in c
    if (device_id == NULL) {
        // use default io addr for parallel port
    } else {
        // Format: io_addr[,io_addr_lo]
        char *endp;
        char *endp2;

        board->io.epp.io_addr = strtol(device_id, &endp, 0);
        if (endp == device_id) {
            fprintf(stderr, "cannot parse EPP address from '%s'\n", device_id);
            return EC_BADCL;
        }
        if (*endp == '\0') {
            board->io.epp.io_addr_hi = board->io.epp.io_addr + 0x400;
        } else {
            if (*endp != ',') {
                fprintf(stderr, "cannot parse EPP address from '%s'\n", device_id);
                return EC_BADCL;
            }
            endp ++;
            if (*endp == '\0') {
                board->io.epp.io_addr_hi = board->io.epp.io_addr + 0x400;
            } else {
                board->io.epp.io_addr_hi = strtol(endp, &endp2, 0);
                if (*endp2 != '\0') {
                    fprintf(stderr, "cannot parse EPP address from '%s'\n", device_id);
                    return EC_BADCL;
                }
            }
        }
    }
    fprintf(stderr, "%s board at 0x%04x,0x%04x\n", board->board_type, board->io.epp.io_addr, board->io.epp.io_addr_hi);


    // get access the the parport i/o addresses
    if (seteuid(0) != 0) {
        fprintf(stderr, "error setting euid: %s\n", strerror(errno));
        fprintf(stderr, "you have to be root or bfload has to be setuid root\n");
        return EC_SYS;
    }

    if (iopl(3) != 0) {
        fprintf(stderr, "error getting I/O port access: %s\n", strerror(errno));
        return EC_SYS;
    }

    // drop privilege
    seteuid(getuid());


    //
    // program the board with the bitfile
    //

    /* chunk 'e' has the bitstream */
    ch = bitfile_find_chunk(bf, 'e', 0);

    fprintf(stderr, 
        "Loading configuration %s into %s:0x%04x,0x%04x board...\n",
        bf->filename,
        board->board_type,
        board->io.epp.io_addr,
        board->io.epp.io_addr_hi
    );
    r = board->program_funct(board, ch);
    if (r != 0) {
	errmsg(__func__, "configuration did not load");
	return EC_HDW;
    }


    return 0;
}


// try to execute a programming command: CardType[:CardID]=FileName
int parse_program_command(char *cmd) {
    char *filename;

    char *device_type;
    char *device_id;

    struct stat stat_buf;
    int r;

    // first parse out the filename
    filename = strchr(cmd, '=');
    if (filename == NULL) {
        errmsg(__func__, "error parsing program command '%s'\n", cmd);
        return EC_BADCL;
    }

    *filename = '\0';
    filename ++;

    r = stat(filename, &stat_buf);
    if (r != 0) {
        errmsg(__func__, "error stating '%s': %s\n", filename, strerror(errno));
        return EC_BADCL;
    }


    // parse out the device id
    device_id = strchr(cmd, ':');
    if (device_id != NULL) {
        *device_id = '\0';
        device_id ++;
        if (*device_id == '\0') {
            device_id = NULL;
        }
    }

    device_type = cmd;
    if (*device_type == '\0') {
        errmsg(__func__, "no device type specified\n");
        return EC_BADCL;
    }

    return program(device_type, device_id, filename);
}

/* program FPGA on PCI board 'devnum', with data from bitfile chunk 'ch' */
static int program_5i20_fpga(struct board_info *bd, struct bitfile_chunk *ch)
{
    int ctrl_region, data_region, count;
    __u32 status, control;
    __u8 *dp;

    fprintf(stderr, "Opening PCI regions... [devnum=%d]\n",
	bd->io.pci.upci_devnum);
    /* open regions for access */
    ctrl_region = upci_open_region(bd->io.pci.upci_devnum, 1);
    if ( ctrl_region < 0 ) {
	errmsg(__func__, "could not open device %d, region %d (5i20 control port)",
	    bd->io.pci.upci_devnum, 1 );
	goto cleanup0;
    }
    data_region = upci_open_region(bd->io.pci.upci_devnum, 2);
    if ( data_region < 0 ) {
	errmsg(__func__, "could not open device %d, region %d (5i20 data port)",
	    bd->io.pci.upci_devnum, 2 );
	goto cleanup1;
    }


    //
    // fix up LASxBRD READY if needed
    //
    {
        int offsets[] = { LAS0BRD_OFFSET, LAS1BRD_OFFSET, LAS2BRD_OFFSET, LAS3BRD_OFFSET };
        int i;

        fprintf(stderr, "checking #READY in EEPROM:\n" );

        for (i = 0; i < 4; i ++) {
            __u32 val;
            int offset = offsets[i];

            val = upci_read_u32(ctrl_region, offset);
            fprintf(stderr, "    LAS%dBRD (0x%04x): 0x%08x", i, offset, val);
            if (val & LASxBRD_READY) {
                fprintf(stderr, " ok\n");
            } else {
                fprintf(stderr, "    *** #READY is OFF, i'll fix it for you just this once but you should upgrade your ancient EEPROM\n");
                val |= LASxBRD_READY;
                upci_write_u32(ctrl_region, offset, val);
            }
        }
    }


    if(!reset_pci9030(ctrl_region, &status)) goto cleanup2;

    /* set /PROGRAM high, let FPGA come out of reset */
    control = status | _PROGRAM_MASK;
    upci_write_u32(ctrl_region, CTRL_STAT_OFFSET, control);
    /* wait for /INIT to go high when it finishes clearing memory
	This should take no more than 100uS.  If we assume each PCI read
	takes 30nS (one PCI clock), that is 3300 reads.  Reads actually
	take several clocks, but even at a microsecond each, 3.3mS is not
	an excessive timeout value */
    count = 3300;
    do {
	status = upci_read_u32(ctrl_region, CTRL_STAT_OFFSET);
	if ( status & _INIT_MASK ) break;
    } while ( --count > 0 );
    if ( count == 0 ) {
	errmsg(__func__, "FPGA did not come out of /INIT" );
	goto cleanup3;
    }
    /* set /WRITE low for data transfer, and turn on LED */
    control = status & ~_WRITE_MASK & ~_LED_MASK;
    upci_write_u32(ctrl_region, CTRL_STAT_OFFSET, control);

    /* Program the card */
    count = ch->len;
    dp = ch->body;
    fprintf(stderr, "Writing data to FPGA....\n" );
    while ( count-- > 0 ) {
	upci_write_u8(data_region, 0, bit_reverse(*(dp++)));
    }
    /* all bytes transferred, clear "bytes to go" indicator */
    fprintf(stderr, "Data transfer complete...\n");
    status = upci_read_u32(ctrl_region, CTRL_STAT_OFFSET);
    if ( ! (status & _INIT_MASK) ) {
	/* /INIT goes low on CRC error */
	errmsg(__func__, "FPGA asserted /INIT: CRC error" );
	goto cleanup3;
    }
    if ( ! (status & DONE_MASK) ) {
	errmsg(__func__, "FPGA did not assert DONE" );
	goto cleanup3;
    }
    /* turn off write enable and LED */
    control = status | _WRITE_MASK | _LED_MASK;
    upci_write_u32(ctrl_region, CTRL_STAT_OFFSET, control);
    fprintf(stderr, "Successfully programmed %u bytes\n", ch->len);
    return 0;
cleanup3:
    /* set /PROGRAM low (reset device), /WRITE high and LED off */
    status = upci_read_u32(ctrl_region, CTRL_STAT_OFFSET);
    control = status & ~_PROGRAM_MASK;
    control |= _WRITE_MASK | _LED_MASK;
    upci_write_u32(ctrl_region, CTRL_STAT_OFFSET, control);
cleanup2:
    upci_close_region(data_region);
cleanup1:
    upci_close_region(ctrl_region);
cleanup0:
    return -1;
}

static int program_5i22_fpga(struct board_info *bd, struct bitfile_chunk *ch)
{
    int ctrl_region, data_region, count;
    __u32 status, control;
    __u8 *dp;


    fprintf(stderr, "Opening PCI regions...\n");
    /* open regions for access */
    ctrl_region = upci_open_region(bd->io.pci.upci_devnum, 1);
    if ( ctrl_region < 0 ) {
		errmsg(__func__, "could not open device %d, region %d (5i22 control port)",
		    bd->io.pci.upci_devnum, 1 );
		goto cleanup22_0;
    }
    data_region = upci_open_region(bd->io.pci.upci_devnum, 2);
    if ( data_region < 0 ) {
		errmsg(__func__, "could not open device %d, region %d (5i22 data port)",
		    bd->io.pci.upci_devnum, 2 );
		goto cleanup22_1;
    }
    fprintf(stderr, "Resetting FPGA...\n" );
	/* Enable programming.
	*/
	fprintf(stderr, "\nProgramming...\n") ;

	/* set GPIO bits to GPIO function */
	status = upci_read_u32(ctrl_region, CTRL_STAT_OFFSET_5I22);
	control = status | (DONE_ENABLE_5I22 | _PROG_ENABLE_5I22);
	upci_write_u32(ctrl_region, CTRL_STAT_OFFSET_5I22, control) ;
	/* Turn off /PROGRAM bit and insure that DONE isn't asserted */
	upci_write_u32(ctrl_region, CTRL_STAT_OFFSET_5I22, control & ~_PROGRAM_MASK_5I22) ;
	status = upci_read_u32(ctrl_region, CTRL_STAT_OFFSET_5I22);
	if ((status & DONE_MASK_5I22) == DONE_MASK_5I22) {
		/* Note that if we see DONE at the start of programming, it's most likely due
			 to an attempt to access the FPGA at the wrong I/O location.  */
		errmsg(__func__, "<DONE> status bit indicates busy at start of programming.") ;
		return EC_HDW;
	}
	/* turn on /PROGRAM output bit */
	upci_write_u32(ctrl_region, CTRL_STAT_OFFSET_5I22, control | _PROGRAM_MASK_5I22) ;

	/* Delay for at least 100 uS. to allow the FPGA to finish its reset
		 sequencing.  3300 reads is at least 100 us, could be as long as a few ms
	*/
    for (count=0;count<3300;count++) {
		status = upci_read_u32(ctrl_region, CTRL_STAT_OFFSET);
    }

	/* Program the card. */
	count = ch->len;
	dp = ch->body;
	while (count-- > 0) {
		upci_write_u8(data_region, 0, bit_reverse(*dp++));
	};

	/* Wait for completion of programming. */
	for (count=0;count<DONE_WAIT_5I22;count++) {
			status = upci_read_u32(ctrl_region, CTRL_STAT_OFFSET_5I22);
			if ((status & DONE_MASK_5I22) == DONE_MASK_5I22)
				break;
		}
	if (count >= DONE_WAIT_5I22) {
		errmsg(__func__, "Error: Not <DONE>; programming not completed.") ;
		return EC_HDW;
	}

	fprintf(stderr, "\nSuccessfully programmed 5i22.");
	return EC_OK;

    upci_close_region(data_region);
cleanup22_1:
    upci_close_region(ctrl_region);
cleanup22_0:
    return -1;
}


// returns TRUE if the FPGA reset, FALSE on error
static int m7i43_cpld_reset(struct board_info *board) {
    uint8_t byte;

    // select the control register
    epp_addr8(&board->io.epp, 1);

    // bring the Spartan3's PROG_B line low for 1 us (the specs require 300-500 ns or longer)
    epp_write(&board->io.epp, 0x00);
    usleep(1);

    // bring the Spartan3's PROG_B line high and wait for 2 ms before sending firmware (required by spec)
    epp_write(&board->io.epp, 0x01);
    usleep(2 * 1000);

    // make sure the FPGA is not asserting its DONE bit
    byte = epp_read(&board->io.epp);
    if ((byte & 0x01) != 0) {
        fprintf(stderr, "error: DONE is not low after CPLD reset!\n");
        return 0;
    }

    return 1;
}


// this function resets the FPGA *only* if it's currently configured with the HostMot2 firmware
static void m7i43_hm2_reset(struct board_info *board) {
    epp_addr16(&board->io.epp, 0x7F7F);
    epp_write(&board->io.epp, 0x5A);
}


// returns FPGA size in K-gates
static int m7i43_cpld_get_fpga_size(struct board_info *board) {
    uint8_t byte;

    //  select data register
    epp_addr8(&board->io.epp, 0);

    byte = epp_read(&board->io.epp);
    if ((byte & 0x01) == 0x01) {
        return 400;
    } else {
        return 200;
    }
}


static int m7i43_cpld_send_firmware(struct board_info *board, struct bitfile_chunk *ch) {
    int i;
    uint8_t *dp;

    // select the CPLD's data address
    epp_addr8(&board->io.epp, 0);

    dp = ch->body;
    for (i = 0; i < ch->len; i ++) {
        epp_write(&board->io.epp, bit_reverse(*dp));
        dp ++;
    }

    // see if it worked
    if (epp_check_for_timeout(&board->io.epp)) {
        fprintf(stderr, "EPP Timeout while sending firmware!\n");
        return 0;
    }

    return 1;
}


static int program_7i43_fpga(struct board_info *board, struct bitfile_chunk *ch) {
    int fpga_size;


    // set up the parport for EPP
    epp_init(&board->io.epp);


    // 
    // reset the FPGA, then send appropriate firmware
    //

    m7i43_hm2_reset(board);

    if (!m7i43_cpld_reset(board)) {
        fprintf(stderr, "error resetting FPGA, aborting load\n");
        return -1;
    }

    fpga_size = m7i43_cpld_get_fpga_size(board);
    if (
        ((strcmp(board->chip_type, "3s400tq144") == 0) && (fpga_size == 400))
        || ((strcmp(board->chip_type, "3s200tq144") == 0) && (fpga_size == 200))
    ) {
        if (!m7i43_cpld_send_firmware(board, ch)) {
            fprintf(stderr, "error sending FPGA firmware\n");
            return -1;
        }
        return 0;
    } else {
        fprintf(stderr, "FPGA part mismatch\n");
        fprintf(stderr, "The selected board reports fpga chip size %d K gates\n", fpga_size);
        fprintf(stderr, "The requested firmware is for the other 7i43 FPGA\n");
        return -1;
    }
}


/* the fpga was originally designed to be programmed serially... even
   though we are doing it using a parallel interface, the bit ordering
   is based on the serial interface, and the data needs to be reversed
*/

static __u8 bit_reverse (__u8 data)
{
    static const __u8 swaptab[256] = {
	0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
	0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
	0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
	0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
	0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
	0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
	0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
	0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
	0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
	0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
	0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
	0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
	0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
	0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
	0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
	0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF };

    return swaptab[data];
}

/* write data from bitfile chunk 'ch' to FPGA on board 'devnum' */

static int write_fpga_ram(struct board_info *bd, struct bitfile_chunk *ch)
{
    int mem_region, n;
    __u32 data;

    fprintf(stderr, "Opening PCI region %d...\n", bd->io.pci.fpga_pci_region);
    mem_region = upci_open_region(bd->io.pci.upci_devnum, bd->io.pci.fpga_pci_region);
    if ( mem_region < 0 ) {
	errmsg(__func__, "could not open device %d, region %d (FPGA memory)",
	    bd->io.pci.upci_devnum, bd->io.pci.fpga_pci_region );
	return -1;
    }
    fprintf(stderr, "Writing data to FPGA...\n");
    data = 0;
    for ( n = 0 ; n < ch->len ; n++ ) {
	data |= ch->body[n] << ((n & 3) * 8);
	if ( (n & 3) == 3 ) {
	    /* have 32 bits, write to the RAM */
	    upci_write_u32(mem_region, (n & ~3), data);
	    /* prep for next 32 */
	    data = 0;
	}
    }
    if ( (n & 3) != 0 ) {
	/* have residual bits, write to the RAM */
	upci_write_u32(mem_region, (n & ~3), data);
    }
    fprintf(stderr, "Transferred %d bytes\n", n);

    upci_close_region(mem_region);
    return 0;
}
