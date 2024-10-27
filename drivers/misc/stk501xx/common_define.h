/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 */
/* MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

/*
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
/*
 * Definitions for stk501xx SAR sensor chip.
 */
#ifndef __DEFINE_COMMON_H__
#define __DEFINE_COMMON_H__

#include "platform_config.h"

#ifndef UNUSED_VAR
    #define UNUSED_VAR(x) (void)(x)
#endif

#define STK_ABS(x)               ((x < 0)?(-(x)):(x))

static inline uint32_t STK_POW(uint32_t base, uint32_t exp) {
    uint32_t result = 1;
    while (exp > 0) {
        if (exp % 2 == 1)
            result *= base;
        exp /= 2;
        base *= base;
    }
    return result;
}

static inline int STK_LOG_CAL(uint32_t val, uint32_t base) {
    int sqrtCounter = 0;
    uint32_t value = val;

    if (value <= 1) {
        sqrtCounter = 0;
    } else {
        while (value > 1) {
            value = value >> base;
            sqrtCounter++;
        }
    }

    return sqrtCounter;
}

#define STK_TAG                 "[STK_SAR] "
#define STK_FUN()               printk(KERN_INFO STK_TAG" %s\n", __FUNCTION__)
#if 1
#define STK_LOG(fmt, args...)    pr_info(STK_TAG "[INFO]" "<%s><%d>"fmt, __FUNCTION__, __LINE__, ##args)
#define STK_DBG(fmt, args...)    pr_debug(STK_TAG "[DBG]" "<%s><%d>"fmt, __FUNCTION__, __LINE__, ##args)
#define STK_ERR(fmt, args...)    pr_err(STK_TAG "[ERR]" "<%s><%d>"fmt, __FUNCTION__, __LINE__, ##args)
#else
#define STK_ERR(...)            \
    do {                        \
        printk(KERN_ERR STK_TAG " %s %d: ", __FUNCTION__, __LINE__);    \
        printk(__VA_ARGS__);    \
        printk("\n");           \
    } while (0)
#define STK_LOG(...)            \
    do {                        \
        printk(KERN_INFO STK_TAG " %s %d: ", __FUNCTION__, __LINE__);    \
        printk(__VA_ARGS__);    \
        printk("\n");           \
    } while (0)
#define STK_DBG(...)            \
    do {                        \
        printk(KERN_INFO STK_TAG " %s %d: ", __FUNCTION__, __LINE__);    \
        printk(__VA_ARGS__);    \
        printk("\n");           \
    } while (0)
#endif

typedef struct stk_timer_info stk_timer_info;
typedef struct stk_gpio_info stk_gpio_info;

struct stk_bus_ops
{
    int bustype;
    int (*init)(void *);
    int (*read)(int, unsigned int, unsigned char *);
    int (*read_block)(int, unsigned int, int, void *);
    int (*write)(int, unsigned int, unsigned char);
    int (*write_block)(int, unsigned int, void *, int);
    int (*read_modify_write)(int, unsigned int, unsigned char, unsigned char);
    int (*remove)(void *);
};

typedef enum
{
    SECOND,
    M_SECOND,
    U_SECOND,
    N_SECOND,
} TIMER_UNIT;

typedef enum
{
    US_DELAY,
    MS_DELAY,
} BUSY_WAIT_TYPE;

struct stk_timer_info
{
    char            wq_name[4096];
    uint32_t        interval_time;
    TIMER_UNIT      timer_unit;
    void            (*timer_cb)(void *user_data);
    bool            is_active;
    bool            is_exist;
    bool            is_periodic;
    bool            change_interval_time;
    void            *user_data;
} ;

struct stk_timer_ops
{
    int (*register_timer)(stk_timer_info *);
    int (*start_timer)(stk_timer_info *);
    int (*stop_timer)(stk_timer_info *);
    int (*remove)(stk_timer_info *);
    void (*busy_wait)(unsigned long, BUSY_WAIT_TYPE);
};

typedef enum
{
    TRIGGER_RISING,
    TRIGGER_FALLING,
    TRIGGER_HIGH,
    TRIGGER_LOW,
} GPIO_TRIGGER_TYPE;

typedef enum
{
    PUSHPULL_NONE,
    PUSHPULL_UP,
    PUSHPULL_DOWN,
} GPIO_PUSHPULL_TYPE;

struct stk_gpio_info
{
    char                wq_name[4096];
    char                device_name[4096];
    void                (*gpio_cb)(void *user_data);
    GPIO_TRIGGER_TYPE   trig_type;
    GPIO_PUSHPULL_TYPE  push_pull_type;
    int                 int_pin;
    int32_t             irq;
    bool                is_active;
    bool                is_exist;
    void                *user_data;
} ;

struct stk_gpio_ops
{
    int (*register_gpio_irq)(stk_gpio_info *);
    int (*start_gpio_irq)(stk_gpio_info *);
    int (*stop_gpio_irq)(stk_gpio_info *);
    int (*remove)(stk_gpio_info *);
};

struct stk_storage_ops
{
    int (*init_storage)(void);
    int (*write_to_storage)(char *, uint8_t *, int);
    int (*read_from_storage)(char *, uint8_t *, int);
    int (*remove)(void);
};

struct common_function
{
    const struct stk_bus_ops *bops;
    const struct stk_timer_ops *tops;
    const struct stk_gpio_ops *gops;
};

typedef struct stk_register_table
{
    uint8_t address;
    uint8_t value;
    uint8_t mask_bit;
} stk_register_table;


#define STK_REG_READ(stk_data, reg, val)                    ((stk_data)->bops->read((stk_data)->bus_idx, reg, val))
#define STK_REG_READ_BLOCK(stk_data, reg, count, buf)       ((stk_data)->bops->read_block((stk_data)->bus_idx, reg, count, buf))
#define STK_REG_WRITE(stk_data, reg, val)                   ((stk_data)->bops->write((stk_data)->bus_idx, reg, val))
#define STK_REG_WRITE_BLOCK(stk_data, reg, val, len)        ((stk_data)->bops->write_block((stk_data)->bus_idx, reg, val, len))
#define STK_REG_READ_MODIFY_WRITE(stk_data, reg, val, mask) ((stk_data)->bops->read_modify_write((stk_data)->bus_idx, reg, val, mask))

#define STK_TIMER_REGISTER(stk_data, t_info)                (stk_data)->tops->register_timer? ((stk_data)->tops->register_timer(t_info)) : 0
#define STK_TIMER_START(stk_data, t_info)                   (stk_data)->tops->start_timer? ((stk_data)->tops->start_timer(t_info)) : 0
#define STK_TIMER_STOP(stk_data, t_info)                    (stk_data)->tops->stop_timer? ((stk_data)->tops->stop_timer(t_info)) : 0
#define STK_TIMER_REMOVE(stk_data, t_info)                  (stk_data)->tops->remove? ((stk_data)->tops->remove(t_info)) : 0
#define STK_TIMER_BUSY_WAIT(stk_data, delay, mode)          if ((stk_data)->tops->busy_wait) ((stk_data)->tops->busy_wait(delay, mode))

#define STK_GPIO_IRQ_REGISTER(stk_data, g_info)             (stk_data)->gops->register_gpio_irq? ((stk_data)->gops->register_gpio_irq(g_info)) : 0
#define STK_GPIO_IRQ_START(stk_data, g_info)                (stk_data)->gops->start_gpio_irq? ((stk_data)->gops->start_gpio_irq(g_info)) : 0
#define STK_GPIO_IRQ_STOP(stk_data, g_info)                 (stk_data)->gops->stop_gpio_irq? ((stk_data)->gops->stop_gpio_irq(g_info)) : 0
#define STK_GPIO_IRQ_REMOVE(stk_data, g_info)               (stk_data)->gops->remove? ((stk_data)->gops->remove(g_info)) : 0

#endif // __DEFINE_COMMON_H__
