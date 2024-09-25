// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2022 Southchip Semiconductor Technology(Shanghai) Co., Ltd.
*/

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/semaphore.h>
#include <linux/sched/clock.h>
#include <uapi/linux/sched/types.h>

#include "sc9624_kernel_reg.h"
#include "sc9624_kernel_program.h"

//#define FIRMWARE_FILE_PATH      "/data/misc/sc/TigerH.BIN"
#define MTP_SIZE                32 * 1024
#define MTP_SECTOR              256

static int __sc9624_read_block(struct sc9624 *sc, uint16_t reg, uint8_t length, uint8_t *data)
{
    int ret;

    ret = regmap_raw_read(sc->regmap, reg, data, length);
    if (ret < 0) {
        sc_err("i2c read fail: can't read from reg 0x%04X\n", reg);
    }

    return ret;
}

static int ___sc9624_write_block(struct sc9624 *sc, uint16_t reg, uint8_t length, uint8_t *data)
{
    int ret;

    ret = regmap_raw_write(sc->regmap, reg, data, length);
    if (ret < 0) {
        sc_err("i2c write fail: can't write 0x%04X: %d\n", reg, ret);
    }

    return ret;
}

static int sc9624_read_block(struct sc9624 *sc, uint16_t reg, uint8_t *data, uint8_t len)
{
    int ret;

    mutex_lock(&sc->i2c_rw_lock);
    ret = __sc9624_read_block(sc, reg, len, data);
    mutex_unlock(&sc->i2c_rw_lock);

    return ret;
}

static int sc9624_write_block(struct sc9624 *sc, uint16_t reg, uint8_t *data, uint8_t len)
{
    int ret;

    mutex_lock(&sc->i2c_rw_lock);
    ret = ___sc9624_write_block(sc, reg, len, data);
    mutex_unlock(&sc->i2c_rw_lock);

    return ret;
}

static int sc9624_read_byte(struct sc9624 *sc, uint16_t reg, uint8_t *data)
{
    int ret;

    mutex_lock(&sc->i2c_rw_lock);
    ret = __sc9624_read_block(sc, reg, 1, data);
    mutex_unlock(&sc->i2c_rw_lock);

    return ret;
}

static int sc9624_write_byte(struct sc9624 *sc, uint16_t reg, uint8_t data)
{
    int ret;

    mutex_lock(&sc->i2c_rw_lock);
    ret = ___sc9624_write_block(sc, reg, 1, &data);
    mutex_unlock(&sc->i2c_rw_lock);

    return ret;
}

#if 0
static int fp_size(struct file *f)
{
    int error = -EBADF;
    struct kstat stat;

    error = vfs_getattr(&f->f_path, &stat, STATX_SIZE, AT_STATX_FORCE_SYNC);

    if (error == 0) {
        return stat.size;
    }
    else {
        sc_err("get file file stat error\n");
        return error;
    }
}

static int file_read(char *filename, char **buf)
{
    struct file *fp;
    mm_segment_t fs;
    int size = 0;
    loff_t pos = 0;

    fp = filp_open(filename, O_RDONLY, 0);
    if (IS_ERR(fp)) {
        sc_err("open %s file error\n", filename);
        goto end;
    }

    fs = get_fs();
    set_fs(KERNEL_DS);
    size = fp_size(fp);
    if (size <= 0) {
        sc_err("load file:%s error\n", filename);
        goto error;
    }

    *buf = kzalloc(size + 1, GFP_KERNEL);
    vfs_read(fp, *buf, size, &pos);

error:
    filp_close(fp, NULL);
    set_fs(fs);
end:
    return size;
}

static int sc9624_read_bin(struct sc9624 *sc, char *firmware_buf, uint32_t *firmware_len)
{
    char *buf = NULL;
    int size = 0;

    size = file_read(FIRMWARE_FILE_PATH, &buf);
    if (size > 0){
        memcpy(firmware_buf, buf, size);
        *firmware_len = size;

        kfree(buf);
        return 0;
    }

    return -1;
}
#endif

static uint32_t endian_conversion(uint32_t value)
{
    return (uint32_t)(((value & 0xFF) << 24) | ((value & 0xFF00) << 8) | ((value & 0xFF0000) >> 8) | ((value & 0xFF000000) >> 24));
}

static uint8_t sc9624_func_crc8(uint8_t data, uint8_t crc_init)
{
    uint32_t polynomial = 0x39;
    uint32_t crc_temp = 0;
    uint8_t i = 0;

    crc_temp = data ^ crc_init;
    for (i = 0; i < 8; i++) {
        crc_temp = (crc_temp & 0x80) ?
            ((crc_temp << 1) ^ polynomial) : (crc_temp << 1);
    }

    return (uint8_t)crc_temp;
}

static uint32_t sc9624_func_crc32(uint32_t data, uint32_t crc_init)
{
    uint32_t crc_poly = 0x04c11db7;
    uint8_t i = 0;


    for(i = 0; i < 32; i++) {
        crc_init = (crc_init << 1) ^ ((((crc_init >> 31) & 0x01) ^ ((data >> i) & 0x01))
                        == 0x01 ? 0xFFFFFFFF & crc_poly : 0x00000000 & crc_poly);
    }

    return (uint32_t)crc_init;
}

static uint8_t sc9624_get_crc8(uint8_t *data)
{
    uint8_t crc_data = 0xFF;
    uint8_t i;

    for (i = 0; i < 16; i++) {
        crc_data = sc9624_func_crc8(data[i], crc_data);
    }

    return crc_data;
}


static bool dig_tm_status(struct sc9624 *sc)
{
    int ret;
    SC9624_tm_st_e tm_st;

    ret = sc9624_read_byte(sc, SC9624_TM_ST, &tm_st.value);
    if (ret) {
        sc_err("read 0xFF7F fail\n");
        return false;
    }

    return tm_st.dig_tm ? true : false;
}

static int dig_tm_entry(struct sc9624 *sc, bool enable)
{
    int ret;

    if (enable) {
        if (dig_tm_status(sc)) {
            return 0;
        }

        ret = sc9624_write_byte(sc, SC9624_PASSWD, SC9624_PASSWD1);
        ret |= sc9624_write_byte(sc, SC9624_PASSWD, SC9624_PASSWD2);
        ret |= sc9624_write_byte(sc, SC9624_PASSWD, SC9624_PASSWD3);
        ret |= sc9624_write_byte(sc, SC9624_PASSWD, SC9624_PASSWD4);
        if (ret) {
            sc_err("enter dig tm fail\n");
            return ret;
        }
        sc_info("enter dig tm success\n");
    }
    else {
        ret = sc9624_write_byte(sc, SC9624_PASSWD, SC9624_PASSWD0);
        sc_info("exit dig tm success\n");
    }

    return ret;
}

static int wait_warmup_done(struct sc9624 *sc)
{
    int i;
    SC9624_st_e st;

    for (i = 0; i < 10; i++) {
        if (sc9624_read_byte(sc, SC9624_ST, &st.value)) {
            sc_err("read 0xFFFF fail\n");
            return -1;
        }
        if (st.warmup_done) {
            return 0;
        }
        msleep(10);
    }
    return -1;
}

static int ate_mode_ctrl(struct sc9624 *sc, bool enable)
{
    int ret;
    SC9624_ate_en_e ate_en;

    ret = sc9624_read_byte(sc, SC9624_ATE_EN, &ate_en.value);
    if (ret) {
        sc_err("read 0xFFB0 fail\n");
        return ret;
    }

    if (enable) {
        ate_en.en_ate = 1;
    }
    else {
        ate_en.en_ate = 0;
    }

    return sc9624_write_byte(sc, SC9624_ATE_EN, ate_en.value);
}

static int iic_send_por(struct sc9624 *sc)
{
    return sc9624_write_byte(sc, SC9624_ATE_ST, 0x69);
}

static int mcu_ctrl(struct sc9624 *sc, bool enable)
{
    int ret;
    SC9624_st_e st;

    ret = sc9624_read_byte(sc, SC9624_ST, &st.value);
    if (ret) {
        sc_err("read 0xFFFF fail\n");
        return ret;
    }

    if (enable) {
        if (!st.mcu_en) {
            ret = sc9624_write_byte(sc, SC9624_SRAM_BIST_CTRL0, 0x02);
        }
    }
    else {
        if (st.mcu_en) {
            ret = sc9624_write_byte(sc, SC9624_SRAM_BIST_CTRL0, 0x02);
        }
    }

    return ret;
}

static int hirc_ctrl(struct sc9624 *sc, bool enable)
{
    int ret;
    SC9624_st_e st;

    ret = sc9624_read_byte(sc, SC9624_ST, &st.value);
    if (ret) {
        sc_err("read 0xFFFF fail\n");
        return ret;
    }

    if (enable) {
        if (!st.ate_hirc_en) {
            ret = sc9624_write_byte(sc, SC9624_SRAM_BIST_CTRL0, 0x04);
        }
    }
    else {
        if (st.ate_hirc_en) {
            ret = sc9624_write_byte(sc, SC9624_SRAM_BIST_CTRL0, 0x04);
        }
    }

    return ret;
}

static int write_amba(struct sc9624 *sc, uint32_t reg, uint32_t data)
{
    int ret;
    uint16_t reg_addr = (reg >> 16) & 0xFFFF;

    //write high addr
    reg_addr = (uint16_t)(((reg_addr & 0xFF) << 8) | ((reg_addr & 0xFF00) >> 8));
    ret = sc9624_write_block(sc, SC9624_HADDR_MSB, (uint8_t *)&reg_addr, 2);
    if (ret) {
        return ret;
    }

    ret = sc9624_write_block(sc, (uint16_t)(reg_addr & 0xFFFF), (uint8_t *)&data, 4);
    if (ret) {
        return ret;
    }

    reg_addr = 0x2000;
    reg_addr = (uint16_t)(((reg_addr & 0xFF) << 8) | ((reg_addr & 0xFF00) >> 8));
    return sc9624_write_block(sc, SC9624_HADDR_MSB, (uint8_t *)&reg_addr, 2);
}

static int iic_mtp_ctrl(struct sc9624 *sc, bool enable)
{
    int ret;
    SC9624_ate_en_e ate_en;

    ret = sc9624_read_byte(sc, SC9624_ATE_EN, &ate_en.value);
    if (ret) {
        sc_err("read 0xFFB0 fail\n");
        return ret;
    }

    if (enable) {
        ate_en.iic_mtp_en = 1;
    }
    else {
        ate_en.iic_mtp_en = 0;
    }
    return sc9624_write_byte(sc, SC9624_ATE_EN, ate_en.value);
}

static int mtp_power_ctrl(struct sc9624 *sc, bool enable)
{
    int ret;
    SC9624_ctrl_e ctrl;

    ret = sc9624_read_byte(sc, SC9624_CTRL, &ctrl.value);
    if (ret) {
        sc_err("read 0xFFA9 fail\n");
        return ret;
    }

    if (enable) {
        ctrl.pdn = 1;
    }
    else {
        ctrl.pdn = 0;
    }
    return sc9624_write_byte(sc, SC9624_CTRL, ctrl.value);
}

static int mtp_iic_cfg(struct sc9624 *sc)
{
    int ret;
    SC9624_ctrl_e ctrl;

    ret = sc9624_read_byte(sc, SC9624_CTRL, &ctrl.value);
    if (ret) {
        sc_err("read 0xFFA9 fail\n");
        return ret;
    }

    ctrl.iic_cfg = 1;
    return sc9624_write_byte(sc, SC9624_CTRL, ctrl.value);
}

static int mtp_cp_vol_set(struct sc9624 *sc, uint8_t vol)
{
    int ret;
    SC9624_ctrl_e ctrl;

    ret = sc9624_read_byte(sc, SC9624_CTRL, &ctrl.value);
    if (ret) {
        sc_err("read 0xFFA9 fail\n");
        return ret;
    }

    switch(vol){
        case CP_VOL_97:
            ctrl.cp_vol = 3;
        break;
        case CP_VOL_100:
            ctrl.cp_vol = 2;
        break;
        case CP_VOL_103:
            ctrl.cp_vol = 0;
        break;
        case CP_VOL_106:
            ctrl.cp_vol = 1;
        break;
        default:
            ctrl.cp_vol = 2;
        break;
    }
    return sc9624_write_byte(sc, SC9624_CTRL, ctrl.value);
}

static int mtp_write_unlock(struct sc9624 *sc, bool enable)
{
    int ret;
    SC9624_ctrl_e ctrl;

    ret = sc9624_read_byte(sc, SC9624_CTRL, &ctrl.value);
    if (ret) {
        sc_err("read 0xFFA9 fail\n");
        return ret;
    }

    if (enable) {
        ctrl.lckn = 1;
    }
    else {
        ctrl.lckn = 0;
    }

    return sc9624_write_byte(sc, SC9624_CTRL, ctrl.value);
}

static int mtp_bist_ctrl(struct sc9624 *sc, bool enable)
{
    int ret;
    SC9624_ctrl_e ctrl;

    ret = sc9624_read_byte(sc, SC9624_CTRL, &ctrl.value);
    if (ret) {
        sc_err("read 0xFFA9 fail\n");
        return ret;
    }

    if (enable) {
        ctrl.mtp_en = 1;
    }
    else {
        ctrl.mtp_en = 0;
    }

    return sc9624_write_byte(sc, SC9624_CTRL, ctrl.value);
}

static int mtp_op_time_set(struct sc9624 *sc, int time)
{
    return sc9624_write_byte(sc, SC9624_TPGHF, time & 0x1F);
}

static int mtp_mode_set(struct sc9624 *sc, uint8_t mode)
{
    return sc9624_write_byte(sc, SC9624_MTP_OP_SEL, (uint8_t)mode);
}

static int mtp_addr_set(struct sc9624 *sc, uint16_t bks, uint16_t addr)
{
    uint16_t val = (bks << 5) | addr;

    val = (uint16_t)(((val & 0xFF) << 8) | ((val & 0xFF00) >> 8));
    return sc9624_write_block(sc, SC9624_AD_MSB, (uint8_t *)&val, 2);
}

static int mtp_opnum_set(struct sc9624 *sc, uint16_t number)
{
    uint16_t val = number & 0x7FFF;

    val = (uint16_t)(((val & 0xFF) << 8) | ((val & 0xFF00) >> 8));
    return sc9624_write_block(sc, SC9624_OP_NUM_MSB, (uint8_t *)&val, 2);
}

static int bist_start_ctrl(struct sc9624 *sc, bool enable)
{
    if (enable) {
        return sc9624_write_byte(sc, SC9624_REG_RSV, 0x01);
    }
    else {
        return sc9624_write_byte(sc, SC9624_REG_RSV, 0x00);
    }
}

static bool mtp_set_margin(struct sc9624 *sc, uint8_t margin)
{
    int ret;
    SC9624_cp_e cp;

    ret = sc9624_read_byte(sc, SC9624_CP, &cp.value);
    if (ret) {
        sc_err("read 0xFFAC fail\n");
        return false;
    }

    cp.marrd = margin;

    return sc9624_write_byte(sc, SC9624_CP, cp.value);
}

static bool mtp_cp_ctrl(struct sc9624 *sc, bool enable)
{
        int ret;
    SC9624_cp_e cp;

    ret = sc9624_read_byte(sc, SC9624_CP, &cp.value);
    if (ret) {
        sc_err("read 0xFFAC fail\n");
        return false;
    }

    if (enable) {
        cp.cp_en = 1;
    }
    else {
        cp.cp_en = 0;
    }

    return sc9624_write_byte(sc, SC9624_CP, cp.value);
}

static bool read_sector_crc(struct sc9624 *sc, uint32_t *r_crc)
{
    int ret;
    uint32_t data;

    ret = sc9624_read_block(sc, SC9624_DOUT_CRC_B3, (uint8_t *)&data, 4);
    if (ret) {
        return false;
    }

    *r_crc = endian_conversion(data);

    return true;
}

static int write_pdin(struct sc9624 *sc, uint8_t *data_in, uint8_t len)
{
    int ret;
    int i;
    uint8_t w_data[17] = {0};
    SC9624_mtp_st_e mtp_st;

    if (len != 16) {
        sc_err("Length is not 16-byte aligned\n");
        return -1;
    }

    for (i = 0; i < len; i += 4) {
        *(uint32_t *)(w_data + i) = endian_conversion(*(uint32_t *)(data_in + i));
    }

    w_data[16] = sc9624_get_crc8(w_data);
    ret = sc9624_write_block(sc, SC9624_DIN0_B3, w_data, 17);
    if (ret) {
        sc_err("write data fail\n");
        return -1;
    }

    //check crc
    ret = sc9624_read_byte(sc, SC9624_MTP_ST, &mtp_st.value);
    if (ret) {
        sc_err("read 0xFFAD fail\n");
        return -1;
    }

    if (mtp_st.din_crc_fail) {
        sc_err("check crc fail\n");
        return -1;
    }

    return 0;
}

static bool mtp_erase_chip(struct sc9624 *sc)
{
    int ret;
    int i;
    SC9624_mtp_st_e mtp_st;

    sc_info("start erase chip\n");
    //chip erase
    ret = mtp_power_ctrl(sc, true);
    ret |= mtp_iic_cfg(sc);
    ret |= mtp_cp_vol_set(sc, CP_VOL_106);
    ret |= mtp_op_time_set(sc, ERASE_OP_TIME);
    ret |= mtp_mode_set(sc, MTP_CERS_MODE);
    ret |= mtp_addr_set(sc, MAIN_BKS, MTP_START_ADDR);
    ret |= mtp_write_unlock(sc, true);
    ret |= mtp_bist_ctrl(sc, true);
    ret |= bist_start_ctrl(sc, true);
    if (ret){
        sc_err("erase init fail\n");
        goto erase_fail;
    }

    for (i = 0; i < 100; i++){
        ret = sc9624_read_byte(sc, SC9624_MTP_ST, &mtp_st.value);
        if (ret) {
            sc_err("read 0xFFAD fail\n");
            goto erase_fail;
        }

        if (!mtp_st.mtp_busy) {
            break;
        }

        if (i > 90) {
            sc_err("erase timeout\n");
            goto erase_fail;
        }
        msleep(10);
    }

    ret = mtp_mode_set(sc, MTP_CLR_MODE);
    ret |= mtp_write_unlock(sc, false);
    ret |= mtp_power_ctrl(sc, false);
    ret |= mtp_bist_ctrl(sc, false);

    if (ret) {
        sc_err("erase deinit fail\n");
        goto erase_fail;
    }

    sc_info("erase successful\n");
    return true;

erase_fail:
    sc_err("erase fail\n");
    return false;
}

static bool mtp_erase_check(struct sc9624 *sc)
{
    int ret;
    int i;
    SC9624_mtp_st_e mtp_st;
    uint8_t w_data[16] = {0};

    sc_info("start erase check\n");
    ret = mtp_power_ctrl(sc, true);
    ret |= mtp_iic_cfg(sc);
    ret |= mtp_set_margin(sc, MARGIN3);
    ret |= mtp_addr_set(sc, MAIN_BKS, MTP_START_ADDR);
    ret |= mtp_mode_set(sc, MTP_RV_MODE);
    ret |= mtp_opnum_set(sc, (MTP_SIZE / MTP_SECTOR) * 16 - 1);
    ret |= write_pdin(sc, w_data, 16);
    ret |= mtp_bist_ctrl(sc, true);
    ret |= bist_start_ctrl(sc, true);
    if (ret) {
        sc_err("erase check init fail\n");
        goto erase_check_fail;
    }

    for (i = 0; i< 100; i++) {
        ret = sc9624_read_byte(sc, SC9624_MTP_ST, &mtp_st.value);
        if (ret) {
            sc_err("read 0xFFAD fail\n");
            goto erase_check_fail;
        }

        if (!mtp_st.mtp_busy) {
            break;
        }

        if (i > 90) {
            sc_err("erase check timeout\n");
            goto erase_check_fail;
        }
        msleep(10);
    }

    ret = mtp_mode_set(sc, MTP_CLR_MODE);
    ret |= mtp_power_ctrl(sc, false);
    ret |= mtp_bist_ctrl(sc, false);

    ret = sc9624_read_byte(sc, SC9624_MTP_ST, &mtp_st.value);
    if (ret) {
        sc_err("read 0xFFAD fail\n");
        goto erase_check_fail;
    }

    if (mtp_st.mtp_fail) {
        goto erase_check_fail;
    }

    sc_info("erase check successful\n");
    return true;

erase_check_fail:
    sc_err("erase check fail\n");
    return false;
}

static bool mtp_write(struct sc9624 *sc, uint8_t *buf, uint32_t buf_len)
{
    int ret;
    int i, timeout;
    SC9624_mtp_st_e mtp_st;

    sc_info("mtp write start\n");

    if (buf == NULL || (buf_len % MTP_SECTOR) != 0) {
        sc_err("buf is NULL or Length is not sector aligned\n");
        goto mtp_write_fail;
    }

    ret = mtp_power_ctrl(sc, true);
    ret |= mtp_iic_cfg(sc);
    ret |= mtp_cp_vol_set(sc, CP_VOL_106);
    ret |= mtp_op_time_set(sc, WRITE_OP_TIME);
    ret |= mtp_addr_set(sc, MAIN_BKS, MTP_START_ADDR);
    ret |= mtp_mode_set(sc, MTP_WV_MODE);
    ret |= mtp_write_unlock(sc, true);
    ret |= mtp_opnum_set(sc, (buf_len >> 4) - 1);
    ret |= mtp_bist_ctrl(sc, true);
    ret |= bist_start_ctrl(sc, true);
    if (ret) {
        sc_err("mtp write init fail\n");
        goto mtp_write_fail;
    }

    for (i = 0; i < buf_len; i += 16) {
        //wait wr_avb
        for (timeout = 0; timeout < 100; timeout++) {
            ret = sc9624_read_byte(sc, SC9624_MTP_ST, &mtp_st.value);
            if (ret) {
                sc_err("read 0xFFAD fail\n");
                goto mtp_write_fail;
            }

            if (mtp_st.wr_avb) {
                break;
            }

            if (timeout > 90) {
                sc_err("mtp write wait wr avb timeout\n");
                goto mtp_write_fail;
            }

            msleep(10);
        }

        if (write_pdin(sc, buf + i, 16)) {
            sc_err("mtp write pdin fail\n");
            goto mtp_write_fail;
        }
    }

    for (timeout = 0; timeout < 10; timeout++) {
        ret = sc9624_read_byte(sc, SC9624_MTP_ST, &mtp_st.value);
        if (ret) {
            sc_err("read 0xFFAD fail\n");
            goto mtp_write_fail;
        }

        if (!mtp_st.mtp_busy) {
            break;
        }

        if (mtp_st.mtp_fail || timeout > 8) {
            sc_err("mtp write fail or timeout\n");
            goto mtp_write_fail;
        }
        msleep(10);
    }

    ret = mtp_write_unlock(sc, false);
    ret |= mtp_cp_ctrl(sc, false);
    ret |= mtp_mode_set(sc, MTP_CLR_MODE);
    ret |= mtp_power_ctrl(sc, false);
    ret |= bist_start_ctrl(sc, false);
    ret |= mtp_bist_ctrl(sc, false);
    if (ret) {
        sc_err("mtp write deinit fail\n");
        goto mtp_write_fail;
    }

    sc_info("mtp write successful\n");
    return true;

mtp_write_fail:
    sc_err("mtp write fail\n");
    return false;
}

static bool mtp_crc_check(struct sc9624 *sc, uint8_t margin, uint32_t crc_start, uint32_t *crc_stop, uint8_t *buf, uint32_t buf_len)
{
    int ret;
    int i, j;
    int timeout;
    uint32_t data32 = crc_start;
    uint32_t r_crc;
    SC9624_mtp_st_e mtp_st;

    sc_info("mtp crc check margin : %d start\n", margin);
    ret = mtp_power_ctrl(sc, true);
    ret |= mtp_set_margin(sc, margin);
    ret |= mtp_iic_cfg(sc);
    ret |= mtp_addr_set(sc, MAIN_BKS, MTP_START_ADDR);
    ret |= mtp_mode_set(sc, MTP_CRC_MODE);
    ret |= mtp_opnum_set(sc, (buf_len >> 4) - 1);
    ret |= mtp_bist_ctrl(sc, true);
    ret |= bist_start_ctrl(sc, true);
    if (ret) {
        sc_err("mtp crc check init fail\n");
        goto mtp_crc_check_fail;
    }

    for (i = 0; i < (buf_len / MTP_SECTOR); i++) {
        //buf crc
        for (j = 0; j < (MTP_SECTOR >> 2); j++) {
            data32 = sc9624_func_crc32(*(uint32_t *)(buf + i * MTP_SECTOR + j * 4), data32);
        }

        for (timeout = 0; timeout < 10; timeout++) {
            ret = sc9624_read_byte(sc, SC9624_MTP_ST, &mtp_st.value);
            if (ret) {
                sc_err("read 0xFFAD fail\n");
                goto mtp_crc_check_fail;
            }

            if (mtp_st.rd_avb) {
                break;
            }

            if (timeout > 8) {
                sc_err("mtp crc check timeout\n");
                goto mtp_crc_check_fail;
            }
            msleep(10);
        }

        if (!read_sector_crc(sc, &r_crc)) {
            sc_err("mtp crc check read crc fail\n");
            goto mtp_crc_check_fail;
        }

        if (data32 != r_crc) {
            sc_err("sector %d check crc fail, w : 0x%08x  r: 0x%08x\n",
                i, data32, r_crc);
            goto mtp_crc_check_fail;
        }
        *crc_stop = r_crc;
        data32 = 0xFFFFFFFF;
    }

    ret = mtp_mode_set(sc, MTP_CLR_MODE);
    ret |= mtp_power_ctrl(sc, false);
    ret |= bist_start_ctrl(sc, false);
    ret |= mtp_bist_ctrl(sc, false);
    if (ret) {
        sc_err("mtp crc check deinit fail\n");
        goto mtp_crc_check_fail;
    }

    sc_info("mtp crc check margin : %d successful\n", margin);
    return true;

mtp_crc_check_fail:
    sc_err("mtp crc check margin : %d fail\n", margin);
    return false;
}

int mtp_program(struct sc9624 *sc)
{
    int ret = 0;
    uint32_t crc_start = 0xFFFFFFFF;
    uint32_t crc_stop;
    uint32_t firmware_length = 0;
    uint8_t *firmware_buf = NULL;

    sc_info("program start\n");
    sc->fw_program = true;

    //read bin
    firmware_buf = kzalloc(MTP_SIZE, GFP_KERNEL);  // 32K buffer
    memset(firmware_buf, 0x00, MTP_SIZE);
    //ret = sc9624_read_bin(sc, firmware_buf, &firmware_length);
	if (ret != 0 || firmware_buf == NULL) {
		sc_err("firmware get error %d\n", ret);
		goto program_fail;
	}

    sc_err("firmware len ---> %d", firmware_length);

    //sector alignment
    firmware_length = firmware_length + (MTP_SECTOR - (firmware_length % MTP_SECTOR));

    //op init
    ret = dig_tm_entry(sc, true);
    ret |= wait_warmup_done(sc);
    if (ret) {
        sc_err("wait_warmup_done error %d\n", ret);
		goto program_fail;
    }

    ret = ate_mode_ctrl(sc, true);
    ret |= mcu_ctrl(sc, false);
    ret |= hirc_ctrl(sc, true);
    ret |= write_amba(sc, 0x4000D008,0x7FFFFFFF);//open clk
    ret |= iic_mtp_ctrl(sc, true);
    if (ret) {
        sc_err("op init fail\n");
        goto program_fail;
    }

    if (!mtp_erase_chip(sc)) {
        goto program_fail;
    }

    if (!mtp_erase_check(sc)) {
        goto program_fail;
    }

    if (!mtp_write(sc, firmware_buf, firmware_length)) {
        goto program_fail;
    }

    if (!mtp_crc_check(sc, MARGIN1, crc_start, &crc_stop, firmware_buf, firmware_length)) {
        goto program_fail;
    }

    if (!mtp_crc_check(sc, MARGIN3, crc_stop, &crc_start, firmware_buf, firmware_length)) {
        goto program_fail;
    }

    ret = iic_mtp_ctrl(sc, false);
    ret |= ate_mode_ctrl(sc, false);
    ret |= iic_send_por(sc);
    ret |= dig_tm_entry(sc, false);
    if (ret) {
        sc_err("op deinit fail\n");
        goto program_fail;
    }

    sc_info("program successful\n");
    kfree(firmware_buf);
    sc->fw_program = false;
    return 0;

program_fail:
    sc_err("program fail\n");
    kfree(firmware_buf);
    sc->fw_program = false;
    return -1;
}
