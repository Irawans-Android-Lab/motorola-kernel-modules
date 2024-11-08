#ifndef __LINUX_PLATFORM_DATA_ESWIN_EPH_BOOTLOADER_H
#define __LINUX_PLATFORM_DATA_ESWIN_EPH_BOOTLOADER_H

#include "eswin_eph861x_types.h"

extern int eph_chg_force_bootloader(struct eph_data *ephdata);
extern int eph_bootloader_release_chg(struct eph_data *ephdata);
extern int eph_send_frames(struct eph_data *ephdata, struct eph_flash *ephflash);
extern int eph_app_force_bootloader_mode(struct eph_data *ephdata);
extern int eph_check_bootloader(struct eph_data *ephdata, unsigned int expected_next_state);
extern void eph_get_bin_firmware_version(struct eph_data *ephdata, const struct firmware *fw);
extern int eph_firmware_version_compare(struct eph_data *ephdata);
#endif /* __LINUX_PLATFORM_DATA_ESWIN_EPH_ESWIN_ */
