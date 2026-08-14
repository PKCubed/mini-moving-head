#include "bsp/board.h"
#include "tusb.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "msc_disk.h"
#include "disk_image.h" // Generated FAT12 disk image

#define DISK_BLOCK_NUM  16
#define DISK_BLOCK_SIZE 512
#define DISK_SIZE       (DISK_BLOCK_NUM * DISK_BLOCK_SIZE)

// We'll store the disk at the very end of flash (assuming 2MB flash minimum).
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

#define FLASH_DISK_OFFSET (PICO_FLASH_SIZE_BYTES - DISK_SIZE)
#define FLASH_DISK_ADDR   (XIP_BASE + FLASH_DISK_OFFSET)

// RAM copy of the disk
uint8_t msc_disk[DISK_SIZE];

// Globals for config
uint16_t cfg_pan_center = 1500;
uint16_t cfg_tilt_center = 1500;
uint16_t cfg_pan_range = 1000;
uint16_t cfg_tilt_range = 1000;
bool cfg_swap_pan_tilt = false;
uint16_t cfg_dmx_start_address = 1;
bool cfg_test_mode = false;

volatile bool disk_dirty = false;

static void parse_line(const char* line) {
    int val;
    if (sscanf(line, "pan_center=%d", &val) == 1) cfg_pan_center = val;
    else if (sscanf(line, "tilt_center=%d", &val) == 1) cfg_tilt_center = val;
    else if (sscanf(line, "pan_range=%d", &val) == 1) cfg_pan_range = val;
    else if (sscanf(line, "tilt_range=%d", &val) == 1) cfg_tilt_range = val;
    else if (sscanf(line, "swap_pan_tilt=%d", &val) == 1) cfg_swap_pan_tilt = (val != 0);
    else if (sscanf(line, "dmx_start_address=%d", &val) == 1) cfg_dmx_start_address = val;
    else if (sscanf(line, "test_mode=%d", &val) == 1) cfg_test_mode = (val != 0);
}

void parse_config(void) {
    // File data starts at sector 3 in our generated FAT12 image
    char* data = (char*)&msc_disk[3 * DISK_BLOCK_SIZE];
    
    // Create a local copy to safely parse using strtok
    char buf[1024];
    strncpy(buf, data, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';
    
    char* line = strtok(buf, "\r\n");
    while (line != NULL) {
        parse_line(line);
        line = strtok(NULL, "\r\n");
    }
}

void msc_disk_init(void) {
    // Check if flash has a valid boot sector (starts with 0xEB 0x3C 0x90 or similar)
    const uint8_t* flash_disk = (const uint8_t*)FLASH_DISK_ADDR;
    
    if (flash_disk[0] == 0xEB && flash_disk[510] == 0x55 && flash_disk[511] == 0xAA) {
        // Valid disk found in flash, copy to RAM
        memcpy(msc_disk, flash_disk, DISK_SIZE);
    } else {
        // Uninitialized, copy the default disk image
        memcpy(msc_disk, default_disk_image, DISK_SIZE);
        disk_dirty = true; // Force write to flash
    }
    
    // Parse config once on boot
    parse_config();
}

void msc_disk_task(void) {
    if (disk_dirty) {
        disk_dirty = false;
        
        uint32_t ints = save_and_disable_interrupts();
        
        // Erase flash sectors
        for (int i = 0; i < DISK_SIZE; i += FLASH_SECTOR_SIZE) {
            flash_range_erase(FLASH_DISK_OFFSET + i, FLASH_SECTOR_SIZE);
        }
        
        // Program flash pages
        for (int i = 0; i < DISK_SIZE; i += FLASH_PAGE_SIZE) {
            flash_range_program(FLASH_DISK_OFFSET + i, msc_disk + i, FLASH_PAGE_SIZE);
        }
        
        restore_interrupts(ints);
        
        // Re-parse config in case it changed
        parse_config();
    }
}

//--------------------------------------------------------------------+
// TinyUSB MSC callbacks
//--------------------------------------------------------------------+

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
  (void) lun;
  const char vid[] = "TinyUSB";
  const char pid[] = "Mass Storage";
  const char rev[] = "1.0";

  memcpy(vendor_id  , vid, strlen(vid));
  memcpy(product_id , pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
  (void) lun;
  return true;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size)
{
  (void) lun;
  *block_count = DISK_BLOCK_NUM;
  *block_size  = DISK_BLOCK_SIZE;
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject)
{
  (void) lun;
  (void) power_condition;
  (void) start;
  (void) load_eject;
  return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize)
{
  (void) lun;

  if ( lba >= DISK_BLOCK_NUM ) return -1;

  uint32_t addr = lba * DISK_BLOCK_SIZE + offset;
  memcpy(buffer, &msc_disk[addr], bufsize);

  return bufsize;
}

bool tud_msc_is_writable_cb (uint8_t lun)
{
  (void) lun;
  return true;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize)
{
  (void) lun;

  if ( lba >= DISK_BLOCK_NUM ) return -1;

  uint32_t addr = lba * DISK_BLOCK_SIZE + offset;
  memcpy(&msc_disk[addr], buffer, bufsize);
  
  // Mark disk as dirty to be saved in the main loop
  disk_dirty = true;

  return bufsize;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb (uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize)
{
  void const* response = NULL;
  int32_t resplen = 0;

  switch (scsi_cmd[0])
  {
    default:
      // Set Sense = Invalid Command Operation
      tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
      resplen = -1;
    break;
  }

  if ( resplen > 0 && response != NULL )
  {
    if(resplen > bufsize) resplen = bufsize;
    memcpy(buffer, response, resplen);
  }

  return resplen;
}
