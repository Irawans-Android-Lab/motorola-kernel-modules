#ifndef __LINUX_PLATFORM_DATA_ESWIN_EPH_TS_MMI_H
#define __LINUX_PLATFORM_DATA_ESWIN_EPH_TS_MMI_H

#include <linux/platform_device.h>
#include <linux/touchscreen_u_mmi.h>
#include <linux/mmi_wake_lock.h>

//#define CONFIG_INPUT_TOUCHSCREEN_MMI (1u)

#ifdef CONFIG_INPUT_TOUCHSCREEN_MMI
int eswin_ts_mmi_dev_register(struct comms_device *commsdevice);
void eswin_ts_mmi_dev_unregister(struct comms_device *commsdevice);
#else
static int inline eswin_ts_mmi_dev_register(struct comms_device *commsdevice) {
    return -ENOSYS;
}
static int inline eswin_ts_mmi_dev_unregister(struct comms_device *commsdevice) {
    return -ENOSYS;
}
#endif



#endif
