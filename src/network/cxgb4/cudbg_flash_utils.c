#ifdef __KERNEL__
#include <platdef.h>
#else
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#endif
#include <t4_regs.h>
#ifndef CUDBG_LITE
#include <fcntl.h>
#include <unistd.h>
#include <adap_util.h>
#include <common.h>
#include <t4_hw.h>
#include <t4_chip_type.h>
#include <adapter.h>
#endif
#include <cudbg_if.h>
#include <cudbg_lib_common.h>

enum {
	SF_ATTEMPTS = 10,		/* max retries for SF operations */

	/* flash command opcodes */
	SF_PROG_PAGE	= 2,	/* program page */
	SF_WR_DISABLE	= 4,	/* disable writes */
	SF_RD_STATUS	= 5,	/* read status register */
	SF_WR_ENABLE	= 6,	/* enable writes */
	SF_RD_DATA_FAST = 0xb,	/* read flash */
	SF_RD_ID	= 0x9f, /* read ID */
	SF_ERASE_SECTOR = 0xd8, /* erase sector */
};

#if 0
/*** Flash Layout	***/

	OPROM_START_SEC			0  /* First flash sector for
					      Option-ROM */
	NUM_OPROM_SEC			7  /* Number of flash sectors
					      for Option-ROM */
	FW_START_SEC			8  /* First flash sector for Firmware */

	NUM_FW_SEC			16 /* Number of flash sectors
					      for Firmware */
	BOOT_CONFIG_START_SEC		7  /* First flash sector for config
					      utility i params */
	NUM_BOOT_CONFIG_SEC		1  /* Number of flash sectors for config
					      utility params */
	HW_CONFIG_START_SEC		31 /* First flash sector for hardware
					      config file */
	NUM_HW_CONFIG_SEC		1  /* Number of flash sectors for
					      hardware config file */
	UTIL_START_SEC			7  /* First flash sector for Config
					      Utility */
	FCOE_BOOT_INFO_SEC		30 /* First flash sector for FCOE BOOT
					      INFO to OS */
	NUM_FCOE_BOOT_INFO_SEC		1  /* Number of flash sectors for FCOE
					      BOOT INFO to OS */
	ISCSI_BOOT_INFO_SEC		29 /* First flash sector for ISCSI BOOT
					      INFO to OS */
	NUM_ISCSI_BOOT_INFO_SEC		1  /* Number of flash sectors for ISCSI
					      BOOT INFO to OS */
	VPDINIT_BOOT_INFO_SEC		26 /* First flash sector for VPD INIT */

	NUM_VPDINIT_BOOT_SEC		1  /* Number of flash sectors for VPD
					      INIT */
#endif

/* cudbg is writing to second half of the flash 2 MB to 4 MB */

int write_flash(struct adapter *adap, u32 start_sec, void *data, u32 size);

void update_skip_size(struct cudbg_flash_sec_info *psec_info, u32 size)
{
	psec_info->skip_size += size;
}

u32 get_skip_size(struct cudbg_flash_sec_info *psec_info)
{
	return psec_info->skip_size;
}

void set_sector_availability(struct cudbg_flash_sec_info *psec_info,
							 int sector_nu, int avail)
{
	sector_nu -= CUDBG_START_SEC;
	if (avail)
		set_dbg_bitmap(psec_info->sec_bitmap, sector_nu);
	else
		reset_dbg_bitmap(psec_info->sec_bitmap, sector_nu);
}

/* This function will return empty sector available for filling */
int find_empty_sec(struct cudbg_flash_sec_info *psec_info)
{
	int i, index, bit;

	for (i = CUDBG_START_SEC; i < CUDBG_SF_MAX_SECTOR; i++) {
		index = (i - CUDBG_START_SEC) / 8;
		bit = (i - CUDBG_START_SEC) % 8;
		if (!(psec_info->sec_bitmap[index] & (1 << bit)))
			return i;
	}
	return CUDBG_STATUS_FLASH_FULL;
}

/* This function will get header initially. If header is already there
 * then it will update that header */
void update_headers(void *handle, struct cudbg_buffer *dbg_buff,
		    u64 timestamp, u32 cur_entity_hdr_offset,
		    u32 start_offset, u32 ext_size)
{
	void *sec_hdr;
	struct cudbg_hdr *cudbg_hdr;
	struct cudbg_flash_hdr *flash_hdr;
	struct cudbg_entity_hdr *entity_hdr;
	struct cudbg_flash_sec_info *psec_info;
	u32 hdr_offset;
	u32 data_hdr_size;
	u32 total_hdr_size;
	u32 sec_hdr_start_addr;

	psec_info = ((struct cudbg_private *)handle)->psec_info; 
	data_hdr_size = CUDBG_MAX_ENTITY * sizeof(struct cudbg_entity_hdr) +
				sizeof(struct cudbg_hdr);
	total_hdr_size = data_hdr_size + sizeof(struct cudbg_flash_hdr);
	sec_hdr_start_addr = CUDBG_SF_SECTOR_SIZE - total_hdr_size;
	sec_hdr  = psec_info->sec_data + sec_hdr_start_addr;

	flash_hdr = (struct cudbg_flash_hdr *)(sec_hdr);
	cudbg_hdr = (struct cudbg_hdr *)dbg_buff->data;

	/* initially initialize flash hdr and copy all data headers and
	 * in next calling (else part) copy only current entity header
	 */
	if ((start_offset - psec_info->skip_size) == data_hdr_size) {
		flash_hdr->signature = CUDBG_FL_SIGNATURE;
		flash_hdr->major_ver = CUDBG_FL_MAJOR_VERSION;
		flash_hdr->minor_ver = CUDBG_FL_MINOR_VERSION;
		flash_hdr->build_ver = CUDBG_FL_BUILD_VERSION;
		flash_hdr->hdr_len = sizeof(struct cudbg_flash_hdr);
		hdr_offset =  sizeof(struct cudbg_flash_hdr);

		memcpy((void *)((char *)sec_hdr + hdr_offset),
		       (void *)((char *)dbg_buff->data), data_hdr_size);
	} else
		memcpy((void *)((char *)sec_hdr +
			sizeof(struct cudbg_flash_hdr) +
			cur_entity_hdr_offset),
			(void *)((char *)dbg_buff->data +
			cur_entity_hdr_offset),
			sizeof(struct cudbg_entity_hdr));

	hdr_offset = data_hdr_size + sizeof(struct cudbg_flash_hdr);
	flash_hdr->data_len = cudbg_hdr->data_len - psec_info->skip_size;
	flash_hdr->timestamp = timestamp;

	entity_hdr = (struct cudbg_entity_hdr *)((char *)sec_hdr +
		      sizeof(struct cudbg_flash_hdr) +
		      cur_entity_hdr_offset);
	/* big entity like mc need to be skipped */
	entity_hdr->start_offset -= psec_info->skip_size;

	cudbg_hdr = (struct cudbg_hdr *)((char *)sec_hdr +
			sizeof(struct cudbg_flash_hdr));
	cudbg_hdr->data_len = flash_hdr->data_len;
	flash_hdr->data_len += ext_size;
}

/* Write CUDBG data into serial flash */
int cudbg_write_flash(void *handle, u64 timestamp, void *data,
		      u32 start_offset, u32 cur_entity_hdr_offset,
		      u32 cur_entity_size,
		      u32 ext_size)
{
	struct cudbg_init *cudbg_init = NULL;
	struct adapter *adap = NULL;
	struct cudbg_flash_hdr *flash_hdr = NULL;
	struct cudbg_buffer *dbg_buff = (struct cudbg_buffer *)data;
	struct cudbg_flash_sec_info *psec_info;
	struct cudbg_private *context;
	u32 data_hdr_size;
	u32 total_hdr_size;
	u32 tmp_size;
	u32 sec_data_offset;
	u32 sec_hdr_start_addr;
	u32 sec_data_size;
	u32 space_left;
	int rc = 0;
	int sec;

	context = (struct cudbg_private *)handle;
	cudbg_init = &(context->dbg_init);
	psec_info = context->psec_info;
	adap = cudbg_init->adap;

	data_hdr_size = CUDBG_MAX_ENTITY * sizeof(struct cudbg_entity_hdr) +
			sizeof(struct cudbg_hdr);
	total_hdr_size = data_hdr_size + sizeof(struct cudbg_flash_hdr);
	sec_hdr_start_addr = CUDBG_SF_SECTOR_SIZE - total_hdr_size;
	sec_data_size = sec_hdr_start_addr;

	cudbg_init->print("\tWriting %u bytes to flash\n",
			  cur_entity_size);

	/* this function will get header if psec_info->sec_data does not
	 * have any header and
	 * will update the header if it has header
	 */
	update_headers(handle, dbg_buff, timestamp,
		       cur_entity_hdr_offset,
		       start_offset, ext_size);

	if (ext_size) {
		cur_entity_size += sizeof(struct cudbg_entity_hdr);
		start_offset = dbg_buff->offset - cur_entity_size;
	}

	flash_hdr = (struct cudbg_flash_hdr *)(psec_info->sec_data +
			sec_hdr_start_addr);

	if (flash_hdr->data_len > CUDBG_FLASH_SIZE) {
		rc = CUDBG_STATUS_FLASH_FULL;
		goto out;
	}

	space_left = CUDBG_FLASH_SIZE - flash_hdr->data_len;

	if (cur_entity_size > space_left) {
		rc = CUDBG_STATUS_FLASH_FULL;
		goto out;
	}

	while (cur_entity_size > 0) {
		sec = find_empty_sec(psec_info);
		if (psec_info->par_sec) {
			sec_data_offset = psec_info->par_sec_offset;
			set_sector_availability(psec_info, psec_info->par_sec, 0);
			psec_info->par_sec = 0;
			psec_info->par_sec_offset = 0;

		} else {
			psec_info->cur_seq_no++;
			flash_hdr->sec_seq_no = psec_info->cur_seq_no;
			sec_data_offset = 0;
		}

		if (cur_entity_size + sec_data_offset > sec_data_size) {
			tmp_size = sec_data_size - sec_data_offset;
		} else {
			tmp_size = cur_entity_size;
			psec_info->par_sec = sec;
			psec_info->par_sec_offset = cur_entity_size +
						  sec_data_offset;
		}

		memcpy((void *)((char *)psec_info->sec_data + sec_data_offset),
		       (void *)((char *)dbg_buff->data + start_offset),
		       tmp_size);

		rc = write_flash(adap, sec, psec_info->sec_data,
				CUDBG_SF_SECTOR_SIZE);
		if (rc)
			goto out;

		cur_entity_size -= tmp_size;
		set_sector_availability(psec_info, sec, 1);
		start_offset += tmp_size;
	}
out:
	return rc;
}

int write_flash(struct adapter *adap, u32 start_sec, void *data, u32 size)
{
	unsigned int addr;
	unsigned int i, n;
	unsigned int sf_sec_size;
	int rc = 0;
	u8 *ptr = (u8 *)data;

	sf_sec_size = adap->params.sf_size/adap->params.sf_nsec;
	addr =  start_sec * CUDBG_SF_SECTOR_SIZE;
	i = DIV_ROUND_UP(size,/* # of sectors spanned */
			sf_sec_size);

	rc = t4_flash_erase_sectors(adap, start_sec,
		   start_sec + i - 1);
	/*
	 * If size == 0 then we're simply erasing the FLASH sectors associated
	 * with the on-adapter OptionROM Configuration File.
	 */
	if (rc || size == 0)
		goto out;

	/* this will write to the flash up to SF_PAGE_SIZE at a time */
	for (i = 0; i < size; i += SF_PAGE_SIZE) {
		if ((size - i) <  SF_PAGE_SIZE)
			n = size - i;
		else
			n = SF_PAGE_SIZE;
		rc = t4_write_flash(adap, addr, n, ptr, 0);
		if (rc)
			goto out;

		addr += n;
		ptr += n;
	}

	return 0;
out:
	return rc;
}
