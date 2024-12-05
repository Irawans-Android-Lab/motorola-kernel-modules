/*
 * ESWIN EPH861X series Touchscreen driver
 *
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

// uncomment to enable the dev_dbg prints to dmesg
#define DEBUG
// uncomment to test with input forced open
//#define INPUT_DEVICE_ALWAYS_OPEN
#include <linux/types.h>



#include <uapi/asm-generic/errno-base.h>

#include <linux/mutex.h>
#include <linux/kernel.h>
#include <uapi/linux/input-event-codes.h>


#include <linux/module.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/input/mt.h>

#include "eswin_eph861x_project_config.h"
#include "eswin_eph861x_tlv.h"
#include "eswin_eph861x_types.h"
#include "eswin_eph861x_comms.h"
#include "eswin_eph861x_tlv_report.h"

#define EPH_MT_PRESSURE_CONTACTING     1
#define EPH_MT_PRESSURE_HOVER          0

#define KEY_GESTURE_U                           KEY_U
#define KEY_GESTURE_UP                          KEY_UP
#define KEY_GESTURE_DOWN                        KEY_DOWN
#define KEY_GESTURE_LEFT                        KEY_LEFT
#define KEY_GESTURE_RIGHT                       KEY_RIGHT
#define KEY_GESTURE_O                           KEY_O
#define KEY_GESTURE_E                           KEY_E
#define KEY_GESTURE_M                           KEY_M
#define KEY_GESTURE_L                           KEY_L
#define KEY_GESTURE_W                           KEY_W
#define KEY_GESTURE_S                           KEY_S
#define KEY_GESTURE_V                           KEY_V
#define KEY_GESTURE_C                           KEY_C
#define KEY_GESTURE_Z                           KEY_Z
#define KEY_GESTURE_F1                         KEY_F1

u8 sysfs_report_buf[PAGE_SIZE]={0};
u8 sysfs_packetised_eng_buf[PAGE_SIZE]={0};
static struct wakeup_source *gesture_wakelock;
static u16 stored_touches = 0;
static u16 prev_stored_touches = 0;

static void eph_recv_touch_report(struct eph_data* ephdata, u8* message);




const char *get_touch_type_str(u8 touch_type)
{
    switch (touch_type)
    {
        case CONTACT_TYPE:
            return "Contact touch type";
        case RELEASE_TYPE:
            return "Release touch type";
        case HOVER_TYPE:
            return "Hover touch type";
        case STYLUS_POSITION_TYPE:
            return "Pen touch type";
        case GESTURE_TYPE:
            return "Gesture touch type";
        case STYLUS_RELEASE_TYPE:
            return "Pen touch release type";
        default:
            break;
    }
    return "???";
}

static void eph_gesture_event_process(struct eph_data *ephdata, u8 *message)
{
    u8 gesture_id = message[1];
    int gesture;
    int ret = 0;
    u16 position_x;
    u16 position_y;
    position_x = message[2] | ((u16)message[3] << 8);
    position_y = message[4] | ((u16)message[5] << 8);

    ts_info("gesture_id:0x%x", gesture_id);
    switch (gesture_id) {
    case GESTURE_SWIPE_LEFT:
        gesture = KEY_GESTURE_LEFT;
        break;
    case GESTURE_SWIPE_RIGHT:
        gesture = KEY_GESTURE_RIGHT;
        break;
    case GESTURE_SWIPE_UP:
        gesture = KEY_GESTURE_UP;
        break;
    case GESTURE_SWIPE_DOWN:
        gesture = KEY_GESTURE_DOWN;
        break;
    case GESTURE_DOUBLE_TAP:
        gesture = KEY_GESTURE_U;
        break;
    case GESTURE_TAP:
        gesture = KEY_GESTURE_F1;
        break;
    default:
        gesture = -1;
        break;
    }
    /* report event key */
    if (gesture != -1)
    {
        ts_info("Gesture Code=%d", gesture);

#ifdef CONFIG_BOARD_USES_DOUBLE_TAP_CTRL
        /* report double tap */
        if (gesture == KEY_GESTURE_U)
        {
            if (ephdata->imports && ephdata->imports->report_gesture)
            {
                struct gesture_event_data event;
                ts_info("invoke imported report double tap gesture function\n");
                event.evcode = 4;
                event.evdata.x = le16_to_cpup((__le16 *)(&position_x));
                event.evdata.y = le16_to_cpup((__le16 *)(&position_y));
                /* call class method */
                ret = ephdata->imports->report_gesture(&event);
            }
        }
	/* report single tap */
        else if (gesture == KEY_GESTURE_F1)
        {
            if (ephdata->imports && ephdata->imports->report_gesture)
            {
                struct gesture_event_data event;
                ts_info("invoke imported report single tap gesture function\n");
                event.evcode = 1;
                event.evdata.x = le16_to_cpup((__le16 *)(&position_x));
                event.evdata.y = le16_to_cpup((__le16 *)(&position_y));
                /* call class method */
                ret = ephdata->imports->report_gesture(&event);
            }
        }
#else
        /* report single tap */
        if (gesture == KEY_GESTURE_U)
        {
            if (ephdata->imports && ephdata->imports->report_gesture)
            {
                struct gesture_event_data event;
                ts_info("invoke imported report gesture function\n");
                event.evcode = 1;
                /* call class method */
                ret = ephdata->imports->report_gesture(&event);
            }
        }
#endif
        if (!ret)
        {
            PM_WAKEUP_EVENT(gesture_wakelock, 5000);
        } else {
            ts_err("import-report_gesture failed %d\n", ret);
	    }
    }
}

void eph_recv_event_report_contianer(struct eph_data *ephdata, u8 *message)
{

    struct device *dev = &ephdata->commsdevice->dev;
    struct tlv_header tlvheader;
    u16 message_offset = TLV_HEADER_SIZE;
    u8 event_type;
    u8 event_length;

    tlvheader = eph_get_tl_header_info(ephdata, message);
#ifdef CONFIG_ENABLE_ESWIN_PALM_CANCEL
    ephdata->palm_on = false;
#endif
    while (message_offset < tlvheader.length)
    {

        /* Get the event report type and length */
        event_type = (message[message_offset] & EVENT_REPORT_TYPE_MASK) >> EVENT_REPORT_TYPE_OFFSET;
        event_length = (message[message_offset] & EVENT_REPORT_LENGTH_MASK);
#ifdef CONFIG_ENABLE_ESWIN_PALM_CANCEL
        if (SUPPRESSED_TOUCH_TYPE == event_type)
        {
            ephdata->palm_on = true;
            dev_info(dev, "Touch palm on %d", event_type);
        }
#endif
#if DEBUG_LOG
        dev_info(dev,
             "report - offset: %u event_type: %u event_length: %u ",
             message_offset, event_type, event_length);
#endif
        switch (event_type)
        {

            case CONTACT_TYPE:
            case RELEASE_TYPE:
            case HOVER_TYPE:
            case STYLUS_POSITION_TYPE:
            case STYLUS_RELEASE_TYPE:
            {
                eph_recv_touch_report(ephdata, &message[message_offset]);
                break;
            }
            case GESTURE_TYPE:
                eph_gesture_event_process(ephdata, &message[message_offset]);
                break;
            default:
            {
                dev_err(dev, "Unsupported event report %d\n", event_type);
                return;
            }

        }
        message_offset = message_offset + (event_length + 1);

    }
    input_sync(ephdata->inputdev);
    return;
}

void eph_recv_off_event_report_contianer(struct eph_data *ephdata, u8 *message)
{
    struct input_dev *input_dev = ephdata->inputdev;
    eph_read_report_fod_event(ephdata, message);

    if (ephdata->fod_event) {
        input_report_key(input_dev, ephdata->fod_event, 1);
        input_sync(input_dev);
        input_report_key(input_dev, ephdata->fod_event, 0);
        input_sync(input_dev);
        ts_info("report BTN_TRIGGER_HAPPY code(%d)", ephdata->fod_event);
        ephdata->fod_event = 0;
    }

    return;
}

void eph_read_report_fod_event(struct eph_data *ephdata, u8 *message)
{
    int ret = 0;
    static bool last_fod_down = false;
    struct gesture_event_data event;
    int fod_down_interval = 0;
    static unsigned long start = 0;
    u8 finger_event_type;

    finger_event_type = message[9];
#if DEBUG_LOG
    u8 gesture_event_type;
    gesture_event_type = message[11];
    ts_info("finger_event_type: %u gesture_event_type: %u",
            finger_event_type, gesture_event_type);
#endif
    if (ephdata->suspended)
    {
        if (FINGER_REGION_FINGER_DOWN == finger_event_type)
        {
            fod_down_interval = (int)jiffies_to_msecs(jiffies - start);
            event.evcode = 2;
            event.evdata.x = 0;
            event.evdata.y = 0;
            ts_info("Get FOD-DOWN gesture:%d interval:%d", ephdata->zerotap_data[0], fod_down_interval);
            if (fod_down_interval > 2000)
                ephdata->zerotap_data[0] = 0;

            if (fod_down_interval > 0 && fod_down_interval < 250 && ephdata->zerotap_data[0])
            {
                return;
            }
            start = jiffies;
            ephdata->fod_jiffies = jiffies;
            if (ephdata->zerotap_data[0] > 6)
            {
                ts_info("FOD-DOWN too many times %d", ephdata->zerotap_data[0]);
            }
            else
            {
                ret = ephdata->imports->report_gesture(&event);
                ts_info("report fod down event %d", ephdata->zerotap_data[0]);
                if (!ret)
                {
                    ts_err("report_gesture event FOD-DOWN failed");
                    PM_WAKEUP_EVENT(gesture_wakelock, 3000);
                }
                ephdata->zerotap_data[0]++;
            }
        }
        else if (FINGER_REGION_FINGER_UP == finger_event_type)
        {
            ts_info("Get FOD-UP gesture");
            event.evcode = 3;
            event.evdata.x = 0;
            event.evdata.y = 0;
            ret = ephdata->imports->report_gesture(&event);
            if (!ret)
            {
                ts_err("report_gesture event FOD-UP failed");
                PM_WAKEUP_EVENT(gesture_wakelock, 3000);
            }
            ephdata->zerotap_data[0] = 0;
        }
    }
    else
    {
        if (FINGER_REGION_FINGER_UP == finger_event_type)
        {
            ts_info("report BTN_TRIGGER_HAPPY2");
            ephdata->fod_event = BTN_TRIGGER_HAPPY2;
            last_fod_down = false;
        }
        else if (FINGER_REGION_FINGER_DOWN == finger_event_type)
        {
            ts_info("Get FOD-DOWN normal");
            if (last_fod_down == false)
            {
                ephdata->fod_event = BTN_TRIGGER_HAPPY1;
                last_fod_down = true;
                ts_info("report BTN_TRIGGER_HAPPY1");
            }
        }
    }
}

/* https://www.kernel.org/doc/Documentation/input/multi-touch-protocol.txt */
static void eph_recv_touch_report(struct eph_data *ephdata, u8 *message)
{
    struct device *dev = &ephdata->commsdevice->dev;
    const struct eph_platform_data *ephplatform = ephdata->ephplatform;
    u8 touch_id_slot;
    u8 touch_type;
    u16 position_x;
    u16 position_y;
    u8 width;
    u8 height;
    int touch_pressure = 0;
    int touch_tool_type = 0;
    u8 touch_major_axis = 0;
    u8 touch_minor_axis = 0;
    bool is_active = false;
    static bool debug_report_touch_down_en = false;

    prev_stored_touches = stored_touches;

    touch_type = (message[0] & EVENT_REPORT_TYPE_MASK) >> EVENT_REPORT_TYPE_OFFSET;
    touch_id_slot = message[1];

    position_x = message[2] | ((u16)message[3] << 8);
    position_y = message[4] | ((u16)message[5] << 8);

    width = message[6];
    height = message[7];

    switch (touch_type)
    {

        case CONTACT_TYPE:
        {
            touch_tool_type = MT_TOOL_FINGER;
            touch_pressure = EPH_MT_PRESSURE_CONTACTING;
            is_active = true;

            touch_major_axis = height;
            touch_minor_axis = width;
            break;
        }

        case RELEASE_TYPE:
        {
            touch_tool_type = MT_TOOL_FINGER;
            touch_pressure = EPH_MT_PRESSURE_HOVER;
            is_active = false;

            touch_major_axis = height;
            touch_minor_axis = width;
            break;
        }

        case HOVER_TYPE:
        {
            touch_tool_type = MT_TOOL_FINGER;
            touch_pressure = EPH_MT_PRESSURE_HOVER;
            is_active = true;
            break;
        }

        case STYLUS_POSITION_TYPE:
        {
            is_active = true;
            touch_tool_type = MT_TOOL_PEN;
            touch_pressure = EPH_MT_PRESSURE_CONTACTING;

            touch_major_axis = height;
            touch_minor_axis = width;

            break;
        }
        case STYLUS_RELEASE_TYPE:
        {
            touch_tool_type = MT_TOOL_PEN;
            touch_pressure = EPH_MT_PRESSURE_HOVER;
            is_active = false;

            touch_major_axis = height;
            touch_minor_axis = width;
            break;
        }

        default:
        {
            dev_err(dev, "Unexpected touch_type %d\n", touch_type);
            return;
        }
    }

#ifdef CONFIG_ENABLE_ESWIN_PALM_CANCEL
    touch_tool_type = ephdata->palm_on ? MT_TOOL_PALM : touch_tool_type;
#endif

    input_mt_slot(ephdata->inputdev, touch_id_slot);


    if (RELEASE_TYPE == touch_type)
    {
        input_mt_report_slot_state(ephdata->inputdev, touch_tool_type, is_active);
        input_report_abs(ephdata->inputdev, ABS_MT_PRESSURE, touch_pressure);

        dev_dbg(dev,
                "[%u] %s,  RELEASE_TYPE,  ABS_MT_PRESSURE:%d touch_tool_type:%d is_active:%d BTN_TOUCH:%d\n",
                touch_id_slot,
                get_touch_type_str(touch_type), touch_pressure, touch_tool_type, is_active, 0);
        debug_report_touch_down_en = true;
        stored_touches &= (~(1 << touch_id_slot));
    }
    else
    {
        if (debug_report_touch_down_en) {
            dev_dbg(dev,
                    "[%u] %s POSTITION:(%u, %u) ABS_MT_TOUCH_MAJOR:%d ABS_MT_TOUCH_MINOR:%d ABS_MT_PRESSURE:%d touch_tool_type:%d is_active:%d\n",
                    touch_id_slot,
                    get_touch_type_str(touch_type),
                    position_x, position_y, touch_major_axis, touch_minor_axis, touch_pressure, touch_tool_type, is_active);
            debug_report_touch_down_en = false;
        }


        input_mt_report_slot_state(ephdata->inputdev, touch_tool_type, is_active);

        if (ephplatform->panel_invert_x)
            input_report_abs(ephdata->inputdev, ABS_MT_POSITION_X, (ephplatform->panel_max_x - position_x));
        else
            input_report_abs(ephdata->inputdev, ABS_MT_POSITION_X, (position_x));

        if (ephplatform->panel_invert_y)
            input_report_abs(ephdata->inputdev, ABS_MT_POSITION_Y, (ephplatform->panel_max_y - position_y));
        else
            input_report_abs(ephdata->inputdev, ABS_MT_POSITION_Y, position_y);

        input_report_abs(ephdata->inputdev, ABS_MT_TOUCH_MAJOR, touch_major_axis);
        input_report_abs(ephdata->inputdev, ABS_MT_TOUCH_MINOR, touch_minor_axis);
        input_report_abs(ephdata->inputdev, ABS_MT_PRESSURE, touch_pressure);

        stored_touches |= 1 << touch_id_slot;

    }

    if ((stored_touches) && (!prev_stored_touches))
    {
        input_report_key(ephdata->inputdev, BTN_TOUCH, 1);
#ifdef CONFIG_ESWIN_LAST_TIME
        ephdata->last_event_time = ktime_get_boottime();
        ts_info("TOUCH: [%d] logged timestamp\n", touch_id_slot);
#endif
    }
    else if ((!stored_touches) && (prev_stored_touches))
    {
        input_report_key(ephdata->inputdev, BTN_TOUCH, 0u);
    }
    else
    {
    }

    return;

}

void eph_clear_all_host_touch_slots(struct eph_data *ephdata)
{
    int id;

    dev_dbg(&ephdata->commsdevice->dev, "%s >\n", __func__);

    if (!ephdata->inputdev)
    {
        return;
    }

    mutex_lock(&ephdata->inputdev->mutex);

    for (id = 0; id < CONFIG_SUPPORTED_TOUCHES; id++)
    {
        input_mt_slot(ephdata->inputdev, id);
        input_mt_report_slot_state(ephdata->inputdev, MT_TOOL_FINGER, false);
    }
    input_report_key(ephdata->inputdev, BTN_TOUCH, 0u);
    input_sync(ephdata->inputdev);

    stored_touches = 0;
    prev_stored_touches = 0;
    mutex_unlock(&ephdata->inputdev->mutex);
}

static void eph_recv_device_state_report(struct eph_data *ephdata, u8 *message)
{
    struct device *dev = &ephdata->commsdevice->dev;
    u8 device_status_flags;
    u16 component_id;

    device_status_flags = message[(TLV_HEADER_SIZE+TLV_DEVICE_STATUS_FLAGS_FIELD)];
    component_id = message[(TLV_HEADER_SIZE+TLV_DEVICE_STATUS_COMPONENT_FIELD)] | ((u16)message[(TLV_HEADER_SIZE+TLV_DEVICE_STATUS_COMPONENT_FIELD+1)] << 8);

    if (0 != (TLV_DEVICE_STATUS_RESET_MASK & device_status_flags))
    {
        complete(&ephdata->reset_completion);
        dev_dbg(dev,
                "Reset completed. STATUS_FLAG:%d COMPONENT_ID:%d\n", device_status_flags, component_id);
    }
}

bool eph_proc_report(struct eph_data *ephdata, u8 *message)
{
    u8 type = message[0];
    bool buffer_report = false;

    switch (type)
    {
        case TLV_SCREEN_OFF_REPORT_DATA:
        {
            if (ephdata->inputdev)
            {
                eph_recv_off_event_report_contianer(ephdata, message);
                buffer_report = true;
            }
            break;
        }
        case TLV_REPORT_DATA:
        {
            if (ephdata->inputdev)
            {
                eph_recv_event_report_contianer(ephdata, message);
                buffer_report = true;
            }
            break;
        }
        case TLV_DEVICE_STATUS_DATA:
        {
            eph_recv_device_state_report(ephdata, message);
            buffer_report = true;
            break;
        }
        case TLV_PACKETISED_DATA:
        case TLV_ENG_DEBUG_DATA:
        {
            buffer_report = true;
            break;
        }
        default:
        {
            break;
        }
    }

    return buffer_report;
}

int eph_buffer_report(struct eph_data *ephdata, u8 *message)
{
    struct tlv_header tlvheader;

    tlvheader = eph_get_tl_header_info(ephdata, message);

    if(PAGE_SIZE <= tlvheader.length)
    {
        return -ENOMEM;
    }

    udelay(200);
    mutex_lock(&ephdata->sysfs_report_buffer_lock);

    memcpy(&sysfs_report_buf[0],message,tlvheader.length);

    mutex_unlock(&ephdata->sysfs_report_buffer_lock);

    return 0;
}

int eph_handle_report(struct eph_data *ephdata, u8 *message)
{
    bool buffer_report;
    int ret_val = 0;
    buffer_report = eph_proc_report(ephdata, message);
    if(buffer_report)
    {
        ret_val = eph_buffer_report(ephdata, message);
    }
    return ret_val;
}

int eph_gesture_init(struct eph_data *ephdata)
{
    struct input_dev *input_dev = ephdata->inputdev;
    static bool initialized_sensor;

    ts_info("eph_gesture_init >\n");
    input_set_capability(input_dev, EV_KEY, KEY_POWER);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_U);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_UP);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_DOWN);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_LEFT);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_RIGHT);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_O);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_E);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_M);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_L);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_W);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_S);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_V);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_Z);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_C);

    __set_bit(KEY_GESTURE_RIGHT, input_dev->keybit);
    __set_bit(KEY_GESTURE_LEFT, input_dev->keybit);
    __set_bit(KEY_GESTURE_UP, input_dev->keybit);
    __set_bit(KEY_GESTURE_DOWN, input_dev->keybit);
    __set_bit(KEY_GESTURE_U, input_dev->keybit);
    __set_bit(KEY_GESTURE_O, input_dev->keybit);
    __set_bit(KEY_GESTURE_E, input_dev->keybit);
    __set_bit(KEY_GESTURE_M, input_dev->keybit);
    __set_bit(KEY_GESTURE_W, input_dev->keybit);
    __set_bit(KEY_GESTURE_L, input_dev->keybit);
    __set_bit(KEY_GESTURE_S, input_dev->keybit);
    __set_bit(KEY_GESTURE_V, input_dev->keybit);
    __set_bit(KEY_GESTURE_C, input_dev->keybit);
    __set_bit(KEY_GESTURE_Z, input_dev->keybit);

    if (!initialized_sensor) {
        PM_WAKEUP_REGISTER(&ephdata->commsdevice->dev, gesture_wakelock, "poll-wake-lock");
        if (!gesture_wakelock) {
            ts_err("failed to allocate wakeup source\n");
            return -ENOMEM;
        }
        initialized_sensor = true;
    }

    ts_info("eph_gesture_init <\n");
    return 0;
}
