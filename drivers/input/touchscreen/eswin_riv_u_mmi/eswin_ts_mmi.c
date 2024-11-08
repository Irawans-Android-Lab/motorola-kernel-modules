/*
 * Copyright (C) 2019 Motorola Mobility LLC
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/input/mt.h>
#include <linux/platform_device.h>
#include "eswin_eph861x.h"
#include "eswin_eph861x_types.h"
#include "eswin_eph861x_comms.h"
#include "eswin_ts_mmi.h"
#include "eswin_eph861x_tlv_command.h"
#include "eswin_eph861x_eswin.h"

#define GET_ESWIN_DATA(dev) { \
    ephdata = (struct eph_data*)dev_get_drvdata(dev); \
    if (!ephdata) { \
        ts_err("Failed to get driver data"); \
        return -ENODEV; \
    } \
}

static int eswin_ts_mmi_methods_get_vendor(struct device *dev, void *cdata)
{
    return scnprintf((char*)cdata, TS_MMI_MAX_VENDOR_LEN, "%s", "eswin");
}

static int eswin_ts_mmi_methods_get_productinfo(struct device *dev, void *cdata)
{
    struct eph_data *ephdata;
    char ic_info[TS_MMI_MAX_VENDOR_LEN];

    GET_ESWIN_DATA(dev);

    switch(ephdata->ephdeviceinfo.product_id)
    {
        case 0x13:
        strcpy(ic_info,"EPH8611");
        break;
        case 0x23:
        strcpy(ic_info,"EPH8621");
        break;
        default:
        strcpy(ic_info,"UNKNOWE");
        break;
    }
    ts_info("eswin_ts_mmi_methods_get_productinfo > [%d] [%s] <", ephdata->ephdeviceinfo.product_id, ic_info);
    return scnprintf(TO_CHARP(cdata), TS_MMI_MAX_VENDOR_LEN, "%s", ic_info);
}

static int eswin_ts_mmi_methods_get_build_id(struct device *dev, void *cdata)
{
    struct eph_data *ephdata;
    char fw_build_version[32];

    GET_ESWIN_DATA(dev);
    snprintf(fw_build_version, 16, "%u.%u.%u",
            ephdata->ephdeviceinfo.application_version_major,
            ephdata->ephdeviceinfo.application_version_minor,
            ephdata->ephdeviceinfo.bootloader_version);
    ts_info("eswin_ts_mmi_methods_get_build_id %s", fw_build_version);

    return scnprintf((char*)(cdata), TS_MMI_MAX_ID_LEN, "%s", fw_build_version);
}

static int eswin_ts_mmi_methods_get_config_id(struct device *dev, void *cdata)
{
    int ret;
    struct eph_data *ephdata;
    struct eph_device_control_config_read_command command_request;
    char buf[16];

    GET_ESWIN_DATA(dev);
    ts_info("eswin_ts_mmi_methods_get_config_id");
    command_request.header.type = 0xB;
    command_request.header.length = 0x6;
    command_request.command_id = 0xC;
    command_request.read_length = 0x2;
    ret = eph_read_control_config(ephdata, &command_request, buf);
    if (ret) {
        ts_err("failed get fw version data, %d", ret);
        return -EINVAL;
    }

    return snprintf((char*)(cdata), TS_MMI_MAX_ID_LEN, "%04x", le32_to_cpu(buf[0]));
}

static int eswin_ts_mmi_methods_get_bus_type(struct device *dev, void *idata)
{
    struct eph_data *ephdata;

    GET_ESWIN_DATA(dev);
    ts_info("eswin_ts_mmi_methods_get_bus_type %x", ephdata->inputdev->id.bustype);
    TO_INT(idata) = ephdata->inputdev->id.bustype == BUS_I2C ?
            TOUCHSCREEN_MMI_BUS_TYPE_I2C : TOUCHSCREEN_MMI_BUS_TYPE_SPI;
    return 0;
}

static int eswin_ts_mmi_methods_get_irq_status(struct device *dev, void *idata)
{
    struct eph_data *ephdata;

    GET_ESWIN_DATA(dev);
    ts_info("eswin_ts_mmi_methods_get_irq_status");
    TO_INT(idata) = eph_read_chg(ephdata);
    return 0;
}

static int eswin_ts_mmi_methods_get_drv_irq(struct device *dev, void *idata)
{
    struct eph_data *ephdata;

    GET_ESWIN_DATA(dev);
    ts_info("eswin_ts_mmi_methods_get_drv_irq");
    TO_INT(idata) = ephdata->irq_wake;
    return 0;
}

static int eswin_ts_mmi_methods_get_poweron(struct device *dev, void *idata)
{
    struct eph_data *ephdata;

    GET_ESWIN_DATA(dev);
    TO_INT(idata) = ephdata->power_on;
    return 0;
}

static int eswin_ts_mmi_methods_get_flashprog(struct device *dev, void *idata)
{
    struct eph_data *ephdata;

    GET_ESWIN_DATA(dev);
    TO_INT(idata) = ephdata->updating_device_settings;
    return 0;
}

static int eswin_ts_mmi_methods_drv_irq(struct device *dev, int state)
{
    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    if (state)
    {
        enable_irq(ephdata->chg_irq);
    }
    else
    {
        disable_irq(ephdata->chg_irq);
    }
    return 0;
}

static int eswin_ts_mmi_methods_power(struct device *dev, int on)
{
    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    if (on == TS_MMI_POWER_ON)
        return eph_power_on(ephdata);
    else if(on == TS_MMI_POWER_OFF)
        return eph_power_off(ephdata);
    else {
        ts_err("Invalid power parameter %d.\n", on);
        return -EINVAL;
    }
    return 0;
}

static int eswin_ts_mmi_charger_mode(struct device *dev, int mode)
{
    int ret = 0;

    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);
    //TODO: what process eswin need to do in charger mode??
    return ret;
}

static int eswin_ts_mmi_refresh_rate(struct device *dev, int freq)
{
    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    mutex_lock(&ephdata->comms_mutex);
    /* set report rate */
    ephdata->refresh_rate = freq;
    //TODO: How ESWIN change Report rate??
    mutex_unlock(&ephdata->comms_mutex);

    return 0;
}

#define Y_MIN_ID 2
#define Y_MAX_ID 3
#define SCREEN_X_MAX 1080
#define SCREEN_Y_MAX 2992
#define SCREEN_EXTENDED 2600
#define SCREEN_PRIM_COMPACT 1980
#define DISP_MODE_REAR 3
#define DISP_MODE_FULL 4
#define DISP_MODE_EXTENDED 0
#define DISP_MODE_PRIM_COMPACT 1
#define DISP_MODE_PRIM_PEEK 2

static int rdArray[4];

static int eswin_ts_mmi_active_region(struct device *dev, int *reg_data)
{
    int dmode = -1;

    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    memcpy(rdArray, reg_data, sizeof(rdArray));
    if (rdArray[Y_MIN_ID] > 0) { /* REAR */
        dmode = DISP_MODE_REAR; /* Mode4 */
    } else { /* FULL or PRIMARY */
        if (rdArray[Y_MAX_ID] == SCREEN_Y_MAX)
            dmode = DISP_MODE_FULL; /* Mode5 */
        else if (rdArray[Y_MAX_ID] > SCREEN_EXTENDED)
            dmode = DISP_MODE_EXTENDED; /* Mode1 */
        else if (rdArray[Y_MAX_ID] > SCREEN_PRIM_COMPACT)
            dmode = DISP_MODE_PRIM_COMPACT; /* Mode2 */
        else
            dmode = DISP_MODE_PRIM_PEEK; /* Mode3 */
    }
    if (dmode >= 0 ) {
        // ret = core_data->hw_ops->display_mode(core_data, dmode);
        //TODO: display mode used in eswin!!!
        // if (!ret)
        ts_info("set active region: %d %d %d %d; dmode=%d\n",
                rdArray[0], rdArray[1], rdArray[Y_MIN_ID], rdArray[Y_MAX_ID], dmode);
    } else
        ts_err("Invalid display mode; reqion %d %d %d %d\n",
            rdArray[0], rdArray[1], rdArray[Y_MIN_ID], rdArray[Y_MAX_ID]);

    return 0;
}

static int eswin_ts_mmi_methods_get_active_region(struct device *dev, void *uidata)
{
    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    memcpy((int *)uidata, rdArray, sizeof(rdArray));
    return 0;
}

/* reset chip
 * type：can control software reset and hardware reset
 * currently reset use GPIO pull low 
 */
static int eswin_ts_mmi_methods_reset(struct device *dev, int type)
{
    int ret = -ENODEV;

    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    ts_info("%s start gpio_reset \n", __func__);
    eph_reset_device(ephdata);

    return ret;
}

static int eswin_ts_firmware_update(struct device *dev, char *fwname)
{
    int ret;

    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    //ret = eph_update_fw(dev, fwname);
    ret = 0;
    return ret;
}

#ifdef ESWIN_PALM_SENSOR_EN
int eswin_ts_mmi_palm_set_enable(struct device *dev, unsigned int enable)
{
    int ret = 0;
    struct eph_data *ephdata;

    GET_ESWIN_DATA(dev);

    mutex_lock(&ephdata->comms_mutex);
    // core_data->get_mode.palm_detection= enable;
    // if (core_data->set_mode.palm_detection == enable) {
    //     ts_info("The value = %d is same, so not to write", enable);
    //     goto exit;
    // }

    // if (core_data->power_on == 0) {
    //     ts_info("The touch is in sleep state, restore the value when resume\n");
    //     goto exit;
    // }

    //clear palm status before enable detection
    // if (enable)
    //     atomic_set(&core_data->palm_status, 0);
    // else {
    //     if (core_data->imports && core_data->imports->report_palm) {
    //         core_data->imports->report_palm(0);
    //         ts_info("Disable palm detection, report far\n");
    //     }
    //     del_timer(&core_data->palm_release_timer);
    // }

    // ret = goodix_ts_send_cmd(core_data, PALM_DETECTION_SWITCH_CMD, 5,
    //                     core_data->get_mode.palm_detection, 0x00);
    // if (ret < 0) {
    //     ts_err("failed to set palm detection, enable = %d", enable);
    //     goto exit;
    // }

    // core_data->set_mode.palm_detection = enable;
    // msleep(20);
    // ts_info("Success set palm detection to %d\n", enable);

exit:
    mutex_unlock(&ephdata->comms_mutex);
    return ret;
}
#endif

static int eswin_ts_mmi_panel_state(struct device *dev,
    enum ts_mmi_pm_mode from, enum ts_mmi_pm_mode to)
{
    int ret = 0;
#if defined(CONFIG_BOARD_USES_DOUBLE_TAP_CTRL)
    unsigned char gesture_type = 0;
#endif
    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    switch (to) {
        case TS_MMI_PM_GESTURE:
#if defined(CONFIG_BOARD_USES_DOUBLE_TAP_CTRL)
            if (ephdata->imports && ephdata->imports->get_gesture_type) {
                ret = ephdata->imports->get_gesture_type(&ephdata->commsdevice->dev, &gesture_type);
            }
            if (gesture_type & TS_MMI_GESTURE_ZERO) {
                // NOTE: must to check
                gesture_type |= BIT(0);
                ts_info("enable zero gesture mode cmd 0x%04x\n", gesture_type);
            }
            if (gesture_type & TS_MMI_GESTURE_SINGLE) {
                gesture_type |= BIT(1);
                ts_info("enable single gesture mode cmd 0x%04x\n", gesture_type);
            }
            if (gesture_type & TS_MMI_GESTURE_DOUBLE) {
                gesture_type |= BIT(2);
                ts_info("enable double gesture mode cmd 0x%04x\n", gesture_type);
            }

            ephdata->gesture_mode = gesture_type;
            /* gesture enable */
            gesture_type |= BIT(0);
            ret = eph_gesture_mode_enable(&ephdata->commsdevice->dev, (u8)gesture_type);

            enable_irq(ephdata->chg_irq);
            enable_irq_wake(ephdata->chg_irq);
            ephdata->gesture_wakeup_enable = 1;
            ts_info("Send enable gesture mode 0x%04x \n", gesture_type);
#endif
            break;
        case TS_MMI_PM_DEEPSLEEP:
            ret = eph_deepsleep_enable(&ephdata->commsdevice->dev, 1);
            ts_info("eph deepsleep %d\n", ret);
            break;
        case TS_MMI_PM_ACTIVE:
            break;
        default:
            ts_err("Invalid power state parameter %d.\n", to);
            return -EINVAL;
    }

    return 0;
}

static int eswin_ts_mmi_pre_resume(struct device *dev)
{
    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    ts_info("Resume start");

    return 0;
}

static int eswin_ts_mmi_post_resume(struct device *dev)
{
    int ret;
    u8 gesture_type;

    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

#if defined(CONFIG_BOARD_USES_DOUBLE_TAP_CTRL)
    if (ephdata->gesture_wakeup_enable) {
        gesture_type = ephdata->gesture_mode & 0xFE;
        /* disable gesture */
        ret = eph_gesture_mode_enable(&ephdata->commsdevice->dev, (u8)gesture_type);
        if (ret)
            ts_err("gesture disbale fail %d.\n", ret);
        disable_irq_wake(ephdata->chg_irq);
        ephdata->gesture_wakeup_enable = 0;
    }
#endif
    /* open esd */
    //goodix_ts_blocking_notify(NOTIFY_RESUME, NULL);
    /* TODO - grip, report rate change, pocket mode. */
    ts_info("Resume end");

    return 0;
}

static int eswin_ts_mmi_pre_suspend(struct device *dev)
{
    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    ts_info("Suspend start");
    disable_irq(ephdata->chg_irq);
     cancel_work_sync(&ephdata->force_baseline_work);
#if 0
    atomic_set(&core_data->suspended, 1);

    /*
     * notify suspend event, inform the esd protector
     * and charger detector to turn off the work
     */
    ts_blocking_notify(NOTIFY_SUSPEND, NULL);
#endif
    return 0;
}

static int eswin_ts_mmi_post_suspend(struct device *dev)
{
    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    ephdata->suspended = true;
     //eph_clear_all_host_touch_slots(ephdata);
    ts_info("Suspend end");

    return 0;
}

#ifdef CONFIG_ESWIN_FOD
static int eswin_ts_mmi_update_fod_mode(struct device *dev, int mode)
{
    int ret;

    struct eph_data *ephdata;
    GET_ESWIN_DATA(dev);

    ret = eph_fod_mode_enable(&ephdata->commsdevice->dev, ((mode >0) ? 0x01 : 0x00));
    ts_info("update_fod_mode %d\n", mode);

    return ret;
}
#endif

static struct ts_mmi_methods eswin_ts_mmi_methods = {
    .get_vendor = eswin_ts_mmi_methods_get_vendor,
    .get_productinfo = eswin_ts_mmi_methods_get_productinfo,
    .get_build_id = eswin_ts_mmi_methods_get_build_id,
    .get_config_id = eswin_ts_mmi_methods_get_config_id,
    .get_bus_type = eswin_ts_mmi_methods_get_bus_type,
    .get_irq_status = eswin_ts_mmi_methods_get_irq_status,
    .get_drv_irq = eswin_ts_mmi_methods_get_drv_irq,
    .get_poweron = eswin_ts_mmi_methods_get_poweron,
    .get_flashprog = eswin_ts_mmi_methods_get_flashprog,
    .get_active_region = eswin_ts_mmi_methods_get_active_region,
    /* SET methods */
    .reset =  eswin_ts_mmi_methods_reset,
    .drv_irq = eswin_ts_mmi_methods_drv_irq,
    .power = eswin_ts_mmi_methods_power,
    .charger_mode = eswin_ts_mmi_charger_mode,
    .refresh_rate = eswin_ts_mmi_refresh_rate,
    .active_region = eswin_ts_mmi_active_region,
#ifdef ESWIN_PALM_SENSOR_EN
    .palm_set_enable = eswin_ts_mmi_palm_set_enable,
#endif
    /* Firmware */
    .firmware_update = eswin_ts_firmware_update,
#if 0
    /* vendor specific attribute group */
    .extend_attribute_group = eswin_ts_mmi_extend_attribute_group,
#endif
    /* PM callback */
    .panel_state = eswin_ts_mmi_panel_state,
    .pre_resume = eswin_ts_mmi_pre_resume,
    .post_resume = eswin_ts_mmi_post_resume,
    .pre_suspend = eswin_ts_mmi_pre_suspend,
    .post_suspend = eswin_ts_mmi_post_suspend,
#ifdef CONFIG_ESWIN_FOD
    .update_fod_mode = eswin_ts_mmi_update_fod_mode,
#endif
};

int eswin_ts_mmi_dev_register(struct comms_device *commsdevice)
{
    int ret;
    struct eph_data *ephdata = eph_comms_driver_data_get(commsdevice);

    mutex_init(&ephdata->comms_mutex);
    ret = ts_mmi_dev_register(&ephdata->commsdevice->dev, &eswin_ts_mmi_methods);
    if (ret)
    {
        dev_err(&commsdevice->dev, "Failed to register ts mmi\n");
        mutex_destroy(&ephdata->comms_mutex);
        return ret;
    }
    ephdata->imports = &eswin_ts_mmi_methods.exports;

    return 0;
}

void eswin_ts_mmi_dev_unregister(struct comms_device *commsdevice)
{
    struct eph_data *ephdata = eph_comms_driver_data_get(commsdevice);

    if (!ephdata)
    {
        dev_err(&commsdevice->dev,"Failed to get driver data");
    }
    mutex_destroy(&ephdata->comms_mutex);
    ts_mmi_dev_unregister(&ephdata->commsdevice->dev);

    return;
}
