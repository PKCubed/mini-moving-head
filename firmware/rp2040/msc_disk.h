#ifndef MSC_DISK_H
#define MSC_DISK_H

#include <stdint.h>
#include <stdbool.h>

extern uint16_t cfg_pan_center;
extern uint16_t cfg_tilt_center;
extern uint16_t cfg_pan_range;
extern uint16_t cfg_tilt_range;
extern bool cfg_swap_pan_tilt;
extern uint16_t cfg_dmx_start_address;
extern bool cfg_test_mode;

extern volatile bool disk_dirty;

void msc_disk_init(void);
void msc_disk_task(void); // Call this in main loop to handle flash writes
void parse_config(void);

#endif
