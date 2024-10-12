/*
 *Copyright (c) 2024, Sensortek.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __STK501XX_MTK_I2C_H__
#define __STK501XX_MTK_I2C_H__

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/version.h>

#include "stk501xx.h"
#include "common_define.h"

#ifdef CONFIG_CAPSENSE_FLIP_CAL
#include <linux/extcon.h>
#endif

#ifdef STK_SENSORS_DEV
    #include <linux/sensors.h>
#endif // STK_SENSORS_DEV

#define STK_HEADER_VERSION          "0.0.7"//C+D+E+F
#define STK_C_VERSION               "0.0.3"
#define STK_DRV_I2C_VERSION         "0.0.2"
#define STK_MTK_VERSION             "0.0.1"
#define STK_ATTR_VERSION            "0.0.1"

#define SAR_STATE_NEAR  1
#define SAR_STATE_FAR   2
#define SAR_STATE_NONE  0

#define INPUT_DEVICE_CH0 "Moto CapSense Ch0"
#define INPUT_DEVICE_CH1 "Moto CapSense Ch1"
#define INPUT_DEVICE_CH2 "Moto CapSense Ch2"
#define INPUT_DEVICE_CH3 "Moto CapSense Ch3"
#define INPUT_DEVICE_CH4 "Moto CapSense Ch4"
#define INPUT_DEVICE_CH5 "Moto CapSense Ch5"
#define INPUT_DEVICE_CH6 "Moto CapSense Ch6"
#define INPUT_DEVICE_CH7 "Moto CapSense Ch7"

char input_dev_name[8][20] = {INPUT_DEVICE_CH0, INPUT_DEVICE_CH1, INPUT_DEVICE_CH2, INPUT_DEVICE_CH3,
                              INPUT_DEVICE_CH4, INPUT_DEVICE_CH5, INPUT_DEVICE_CH6, INPUT_DEVICE_CH7
                             };


typedef struct _stk501xx_sensor_dev
{
    char    name[20];
    int     type;
} _stk501xx_sensor_dev;

_stk501xx_sensor_dev stk_sensord_dev[] =
{
    {
        .name = INPUT_DEVICE_CH0,
        .type = 65530,
    },
    {
        .name = INPUT_DEVICE_CH1,
        .type = 65631,
    },
    {
        .name = INPUT_DEVICE_CH2,
        .type = 65632,
    },
    {
        .name = INPUT_DEVICE_CH3,
        .type = 65633,
    },
    {
        .name = INPUT_DEVICE_CH4,
        .type = 65634,
    },
    {
        .name = INPUT_DEVICE_CH5,
        .type = 65635,
    },
    {
        .name = INPUT_DEVICE_CH6,
        .type = 65636,
    },
    {
        .name = INPUT_DEVICE_CH7,
        .type = 65637,
    },

};

typedef struct channels_t
{
    struct input_dev        *input_dev;   /* data */
#ifdef STK_SENSORS_DEV
    struct sensors_classdev  sar_cdev;
#endif // STK_SENSORS_DEV

    ktime_t                 timestamp;
    bool                    enabled;
} channels_t;

typedef struct stk501xx_wrapper
{
    struct i2c_manager      i2c_mgr;
    struct stk_data         stk;
    channels_t              channels[8];

#ifdef CONFIG_CAPSENSE_USB_CAL
    struct work_struct ps_notify_work;
    struct notifier_block ps_notif;
    bool ps_is_present;
#ifdef CONFIG_CAPSENSE_ATTACH_CAL
    bool phone_is_present;
#endif

#ifdef CONFIG_CAPSENSE_FLIP_CAL
    struct notifier_block flip_notif;
    struct extcon_dev *ext_flip_det;
    bool phone_flip_state;
    bool phone_flip_update_regs;
    int phone_flip_open_val;
    int num_flip_closed_regs;
    int num_flip_open_regs;
    struct stk501xx_register_table *flip_open_regs;
    struct stk501xx_register_table *flip_closed_regs;
#endif
#endif
} stk501xx_wrapper;

#endif //__STK501XX_MTK_I2C_H__
