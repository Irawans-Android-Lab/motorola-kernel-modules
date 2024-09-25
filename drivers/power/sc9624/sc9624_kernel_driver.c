// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2022 Southchip Semiconductor Technology(Shanghai) Co., Ltd.
*/

#define pr_fmt(fmt)	"[sc9624] %s: " fmt, __func__

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

#define SC9624_IRQ_WAKE_TIME    (500) /* ms */

/**********************************IIC API*********************************/
static const struct regmap_config sc9624_regmap_config = {
    .reg_bits = 16,
    .val_bits = 8,
};

static int __sc9624_read_block(struct sc9624 *sc, uint16_t reg,
        uint8_t *data, uint8_t length)
{
    int ret;

    ret = regmap_raw_read(sc->regmap, reg, data, length);
    if (ret < 0) {
        sc_err("i2c read fail: can't read from reg 0x%04X\n", reg);
    }

    return ret;
}

static int __sc9624_write_block(struct sc9624 *sc, uint16_t reg,
        uint8_t *data, uint8_t length)
{
    int ret;

    ret = regmap_raw_write(sc->regmap, reg, data, length);
    if (ret < 0) {
        sc_err("i2c write fail: can't write 0x%04X: %d\n", reg, ret);
    }

    return ret;
}

static int sc9624_read_block(struct sc9624 *sc, uint16_t reg,
        uint8_t *data, uint8_t len)
{
    int ret;

    if (sc->fw_program) {
        sc_err("firmware programming\n");
        return -1;
    }

    mutex_lock(&sc->i2c_rw_lock);
    ret = __sc9624_read_block(sc, reg, data, len);
    mutex_unlock(&sc->i2c_rw_lock);

    return ret;
}

static int sc9624_write_block(struct sc9624 *sc, uint16_t reg,
        uint8_t *data, uint8_t len)
{
    int ret;

    if (sc->fw_program) {
        sc_err("firmware programming\n");
        return -1;
    }

    mutex_lock(&sc->i2c_rw_lock);
    ret = __sc9624_write_block(sc, reg, data, len);
    mutex_unlock(&sc->i2c_rw_lock);

    return ret;
}

//-------------------sc9624 system interface-------------------
static int sc9624_rx_set_cmd(struct sc9624 *sc, RX_CMD cmd);

static int sc9624_get_chipid(struct sc9624 *sc, uint16_t *chip_id)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, ChipID, offset);
    ret = sc9624_read_block(sc, offset, (uint8_t *)chip_id,
            (uint8_t)sizeof(((RXCustType *)0)->ChipID));
    if (ret) {
        sc_err("sc9624 get chip id fail\n");
    }

    return ret;
}

int sc9624_online(struct sc9624 *sc)
{
    uint16_t chip_id;

    return sc9624_get_chipid(sc, &chip_id);
}

static int sc9624_get_custid(struct sc9624 *sc, uint16_t *cust_id)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, CustID, offset);
    ret = sc9624_read_block(sc, offset, (uint8_t *)cust_id,
            (uint8_t)sizeof(((RXCustType *)0)->CustID));
    if (ret) {
        sc_err("sc9624 get cust id fail\n");
    }

    return ret;
}

static int sc9624_get_fwver(struct sc9624 *sc, uint32_t *fw_ver)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, FwVer, offset);
    ret = sc9624_read_block(sc, offset, (uint8_t *)fw_ver,
            (uint8_t)sizeof(((RXCustType *)0)->FwVer));
    if (ret) {
        sc_err("sc9624 get fw ver fail\n");
    }

    return ret;
}

static int sc9624_get_hwver(struct sc9624 *sc, uint32_t *hw_ver)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, HwVer, offset);
    ret = sc9624_read_block(sc, offset, (uint8_t *)hw_ver,
            (uint8_t)sizeof(((RXCustType *)0)->HwVer));
    if (ret) {
        sc_err("sc9624 get hw ver fail\n");
    }

    return ret;
}

static int sc9624_get_gitver(struct sc9624 *sc, uint32_t *git_ver)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, GitVer, offset);
    ret = sc9624_read_block(sc, offset, (uint8_t *)git_ver,
            (uint8_t)sizeof(((RXCustType *)0)->GitVer));
    if (ret) {
        sc_err("sc9624 get git ver fail\n");
    }

    return ret;
}

int sc9624_get_mcode(struct sc9624 *sc, uint32_t *mcode)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, mCode, offset);
    ret = sc9624_read_block(sc, offset, (uint8_t *)mcode,
            (uint8_t)sizeof(((RXCustType *)0)->mCode));
    if (ret) {
        sc_err("sc9624 get mcode fail\n");
    }

    return ret;
}

int sc9624_get_frequecy(struct sc9624 *sc, uint16_t *freq)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, Frequecy, offset);

    //refresh
    ret = sc9624_rx_set_cmd(sc, RX_REFRESH);
    if (ret) {
        sc_err("sc9624 set cmd refresh fail\n");
    }

    ret = sc9624_read_block(sc, offset, (uint8_t *)freq,
            (uint8_t)sizeof(((RXCustType *)0)->Frequecy));
    if (ret) {
        sc_err("sc9624 get mcode fail\n");
    }

    return ret;
}

int sc9624_get_vrect(struct sc9624 *sc, uint16_t *vrect)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, Vrect, offset);

    //refresh
    ret = sc9624_rx_set_cmd(sc, RX_REFRESH);
    if (ret) {
        sc_err("sc9624 set cmd refresh fail\n");
    }

    ret = sc9624_read_block(sc, offset, (uint8_t *)vrect,
            (uint8_t)sizeof(((RXCustType *)0)->Vrect));
    if (ret) {
        sc_err("sc9624 get vrect fail\n");
    }

    return ret;
}

int sc9624_get_voltage(struct sc9624 *sc, uint16_t *volt)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, Vout, offset);

    //refresh
    ret = sc9624_rx_set_cmd(sc, RX_REFRESH);
    if (ret) {
        sc_err("sc9624 set cmd refresh fail\n");
    }

    ret = sc9624_read_block(sc, offset, (uint8_t *)volt,
            (uint8_t)sizeof(((RXCustType *)0)->Vout));
    if (ret) {
        sc_err("sc9624 get voltage fail\n");
    }

    return ret;
}

int sc9624_get_current(struct sc9624 *sc, uint16_t *curr)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, Iout, offset);

    //refresh
    ret = sc9624_rx_set_cmd(sc, RX_REFRESH);
    if (ret) {
        sc_err("sc9624 set cmd refresh fail\n");
    }

    ret = sc9624_read_block(sc, offset, (uint8_t *)curr,
            (uint8_t)sizeof(((RXCustType *)0)->Iout));
    if (ret) {
        sc_err("sc9624 get current fail\n");
    }

    return ret;
}

int sc9624_get_tdie(struct sc9624 *sc, uint16_t *tdie)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, Tdie, offset);

    //refresh
    ret = sc9624_rx_set_cmd(sc, RX_REFRESH);
    if (ret) {
        sc_err("sc9624 set cmd refresh fail\n");
    }

    ret = sc9624_read_block(sc, offset, (uint8_t *)tdie,
            (uint8_t)sizeof(((RXCustType *)0)->Tdie));
    if (ret) {
        sc_err("sc9624 get tdie fail\n");
    }

    return ret;
}
//-------------------sc9624 RX interface-------------------
static int sc9624_rx_set_cmd(struct sc9624 *sc, RX_CMD cmd)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, CMD, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)&cmd,
            (uint8_t)sizeof(((RXCustType *)0)->CMD));
    if (ret) {
        sc_err("sc9624 rx set cmd fail\n");
    }

    return ret;
}

static int sc9624_rx_set_Vout(struct sc9624 *sc, uint16_t vout)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, VoutSet, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)&vout,
            (uint8_t)sizeof(((RXCustType *)0)->VoutSet));
    if (ret) {
        sc_err("sc9624 set vout fail\n");
    }

    ret = sc9624_rx_set_cmd(sc, RX_VOUT_CHANGE);
    if (ret) {
        sc_err("sc9624 set cmd fail\n");
    }

    return ret;
}

static int sc9624_rx_set_Ilimit(struct sc9624 *sc, uint16_t ilimt)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, Ilimit, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)&ilimt,
            (uint8_t)sizeof(((RXCustType *)0)->Ilimit));
    if (ret) {
        sc_err("sc9624 set ilimit fail\n");
    }

    ret = sc9624_rx_set_cmd(sc, RX_ILIMIT_CHANGE);
    if (ret) {
        sc_err("sc9624 set cmd fail\n");
    }

    return ret;
}

int sc9624_rx_set_lvp(struct sc9624 *sc, uint16_t vlvp)
{


    return 0;
}

int sc9624_rx_set_vout_ovp(struct sc9624 *sc, uint16_t vovp)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, OVP, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)&vovp,
            (uint8_t)sizeof(((RXCustType *)0)->OVP));
    if (ret) {
        sc_err("sc9624 set vout ovp fail\n");
    }

    ret = sc9624_rx_set_cmd(sc, RX_OVP_CHANGE);
    if (ret) {
        sc_err("sc9624 set cmd fail\n");
    }

    return ret;
}


int sc9624_rx_set_vout_ocp(struct sc9624 *sc, uint16_t iocp)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, OCP, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)&iocp,
            (uint8_t)sizeof(((RXCustType *)0)->OCP));
    if (ret) {
        sc_err("sc9624 set iout ocp fail\n");
    }

    ret = sc9624_rx_set_cmd(sc, RX_OCP_CHANGE);
    if (ret) {
        sc_err("sc9624 set cmd fail\n");
    }

    return ret;
}

int sc9624_rx_set_otp(struct sc9624 *sc, uint16_t otp)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, OTP, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)&otp,
            (uint8_t)sizeof(((RXCustType *)0)->OTP));
    if (ret) {
        sc_err("sc9624 set otp fail\n");
    }

    ret = sc9624_rx_set_cmd(sc, RX_OTP_CHANGE);
    if (ret) {
        sc_err("sc9624 set cmd fail\n");
    }

    return ret;
}

int sc9624_rx_vdd_enable(struct sc9624 *sc, bool enable)
{
    if (enable) {
        return sc9624_rx_set_cmd(sc, RX_VDD_ENABLE);
    } else {
        return sc9624_rx_set_cmd(sc, RX_VDD_DISABLE);
    }
}

int sc9624_rx_clr_int(struct sc9624 *sc, uint32_t rxint)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, IntClr, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)&rxint,
            (uint8_t)sizeof(((RXCustType *)0)->IntClr));
    if (ret) {
        sc_err("sc9624 clear rx int fail\n");
    }

    ret = sc9624_rx_set_cmd(sc, RX_INT_CLR);
    if (ret) {
        sc_err("sc9624 set cmd fail\n");
    }

    return ret;
}

int sc9624_rx_get_int(struct sc9624 *sc, uint32_t *rxint)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, IntFlag, offset);
    ret = sc9624_read_block(sc, offset, (uint8_t *)rxint,
            (uint8_t)sizeof(((RXCustType *)0)->IntFlag));
    if (ret) {
        sc_err("sc9624 clear rx int fail\n");
    }

    return ret;
}

int sc9624_rx_send_ept(struct sc9624 *sc, EPT_RESON ept_v)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, EPTReson, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)&ept_v,
            (uint8_t)sizeof(((RXCustType *)0)->EPTReson));
    if (ret) {
        sc_err("sc9624 clear rx int fail\n");
    }

    ret = sc9624_rx_set_cmd(sc, RX_SEND_EPT);
    if (ret) {
        sc_err("sc9624 set cmd fail\n");
    }

    return ret;
}

static int sc9624_rx_recv_fsk_pkt(struct sc9624 *sc, FskType *fsk)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, Fsk, offset);
    ret = sc9624_read_block(sc, offset, (uint8_t *)(fsk->buf),
            MAX_FSK_SIZE);
    if (ret) {
        sc_err("sc9624 read recv fsk pkt fail\n");
    }

    return ret;
}

int sc9624_rx_send_ask_pkt(struct sc9624 *sc, AskType *ask)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, Ask, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)(ask->buf),
            MAX_ASK_SIZE);
    if (ret) {
        sc_err("sc9624 write send ask pkt fail\n");
    }

    ret = sc9624_rx_set_cmd(sc, RX_SEND_PPP);
    if (ret) {
        sc_err("sc9624 set cmd fail\n");
    }

    return ret;
}

int sc9624_rx_config_fod(struct sc9624 *sc, FodType *fod)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(RXCustType, FOD, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)fod, 16);
    if (ret) {
        sc_err("sc9624 write send ask pkt fail\n");
    }

    return ret;
}

//-------------------sc9624 RX interface-------------------
static int sc9624_tx_set_cmd(struct sc9624 *sc, TX_CMD cmd)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(TXCustType, CMD, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)&cmd,
            (uint8_t)sizeof(((TXCustType *)0)->CMD));
    if (ret) {
        sc_err("sc9624 tx set cmd fail\n");
    }

    return ret;
}

/*
static int sc9624_tx_func_enable(struct sc9624 *sc)
{
    return sc9624_tx_set_cmd(sc, TX_ENABLE);
}

static int sc9624_tx_fod_enable(struct sc9624 *sc, bool enable)
{
    if (enable) {
        return sc9624_tx_set_cmd(sc, TX_FOD_ENABLE);
    } else {
        return sc9624_tx_set_cmd(sc, TX_FOD_DISABLE);
    }
}
*/

int sc9624_tx_clr_int(struct sc9624 *sc, uint32_t txint)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(TXCustType, IntClr, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)&txint,
            (uint8_t)sizeof(((TXCustType *)0)->IntClr));
    if (ret) {
        sc_err("sc9624 clear tx int fail\n");
    }

    ret = sc9624_tx_set_cmd(sc, TX_INT_CLR);
    if (ret) {
        sc_err("sc9624 set cmd fail\n");
    }


    return ret;
}

int sc9624_tx_get_int(struct sc9624 *sc, uint32_t *txint)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(TXCustType, IntFlag, offset);
    ret = sc9624_read_block(sc, offset, (uint8_t *)txint,
            (uint8_t)sizeof(((TXCustType *)0)->IntFlag));
    if (ret) {
        sc_err("sc9624 clear tx int fail\n");
    }

    return ret;
}

int sc9624_tx_send_fsk_pkt(struct sc9624 *sc, FskType *fsk)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(TXCustType, Fsk, offset);
    ret = sc9624_write_block(sc, offset, (uint8_t *)(fsk->buf),
            MAX_FSK_SIZE);
    if (ret) {
        sc_err("sc9624 write send fsk pkt fail\n");
    }

    ret = sc9624_tx_set_cmd(sc, TX_SEND_PPP);
    if (ret) {
        sc_err("sc9624 set cmd fail\n");
    }

    return ret;
}

static int sc9624_tx_recv_ask_pkt(struct sc9624 *sc, AskType *ask)
{
    int ret;
    uint16_t offset = 0;

    OFFSET(TXCustType, Ask, offset);
    ret = sc9624_read_block(sc, offset, (uint8_t *)(ask->buf),
            MAX_ASK_SIZE);
    if (ret) {
        sc_err("sc9624 read recv ask pkt fail\n");
    }

    return ret;
}

//-------------------Interrupt interface-------------------
static int ocp_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");

    return 0;
}

static int vout_ovp_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int clamp_ovp_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int ngage_ovp_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int vout_lvp_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int otp_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int otp_160c_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int sleep_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int mode_change_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

//rx
static int rx_fsk_recv_irq_handler(struct sc9624 *sc)
{
    int ret;
    FskType fsk_pkt;

    sc_info(":trigger\n");

    ret = sc9624_rx_recv_fsk_pkt(sc, &fsk_pkt);

    return ret;
}

static int rx_ppp_success_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int rx_afc_det_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int rx_epp_det_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int rx_poweron_irq_handler(struct sc9624 *sc)
{
    int ret;
    uint32_t regval;

    sc_info(":trigger\n");

    ret = sc9624_get_custid(sc, (uint16_t *)&regval);
    sc_info(" cust id : 0x%08x\n", regval);
    ret = sc9624_get_fwver(sc, &regval);
    sc_info(" fw ver : 0x%08x\n", regval);
    ret = sc9624_get_hwver(sc, &regval);
    sc_info(" hw ver : 0x%08x\n", regval);
    ret = sc9624_get_gitver(sc, &regval);
    sc_info(" git ver : 0x%08x\n", regval);

    return ret;
}

static int rx_ss_pkt_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int rx_id_pkt_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int rx_config_pkt_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int rx_ready_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int rx_ldo_on_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int rx_ldo_off_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int rx_pldo_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int rx_scp_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

//tx
static int tx_ask_recv_irq_handler(struct sc9624 *sc)
{
    int ret;
    AskType ask_pkt;

    sc_info(":trigger\n");

    ret = sc9624_tx_recv_ask_pkt(sc, &ask_pkt);
    return ret;
}

static int tx_ppp_timeout_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int tx_ppp_success_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int tx_afc_det_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int tx_epp_det_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int tx_detect_rx_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int tx_remove_power_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int tx_fod_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int tx_detect_tx_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int tx_cep_timeout_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

static int tx_rpp_timeout_irq_handler(struct sc9624 *sc)
{
    sc_info(":trigger\n");
    return 0;
}

struct interrupt_handler {
	uint32_t bit_mask;
	int (*handler)(struct sc9624 *sc);
};

#define DECL_INTERRUPT_HANDLER(xbit, xhandler) {\
		.bit_mask = 1 << xbit,\
		.handler = xhandler, \
	}

static const struct interrupt_handler rx_irq_handlers[] = {
    DECL_INTERRUPT_HANDLER(0, ocp_irq_handler),
    DECL_INTERRUPT_HANDLER(1, vout_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(2, clamp_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(3, ngage_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(4, vout_lvp_irq_handler),
    DECL_INTERRUPT_HANDLER(5, otp_irq_handler),
    DECL_INTERRUPT_HANDLER(6, otp_160c_irq_handler),
    DECL_INTERRUPT_HANDLER(7, sleep_irq_handler),
    DECL_INTERRUPT_HANDLER(8, mode_change_irq_handler),
    DECL_INTERRUPT_HANDLER(9, rx_fsk_recv_irq_handler),
    DECL_INTERRUPT_HANDLER(10, rx_ppp_success_irq_handler),
    DECL_INTERRUPT_HANDLER(11, rx_afc_det_irq_handler),
    DECL_INTERRUPT_HANDLER(12, rx_epp_det_irq_handler),
    DECL_INTERRUPT_HANDLER(13, rx_poweron_irq_handler),
    DECL_INTERRUPT_HANDLER(14, rx_ss_pkt_irq_handler),
    DECL_INTERRUPT_HANDLER(15, rx_id_pkt_irq_handler),
    DECL_INTERRUPT_HANDLER(16, rx_config_pkt_irq_handler),
    DECL_INTERRUPT_HANDLER(17, rx_ready_irq_handler),
    DECL_INTERRUPT_HANDLER(18, rx_ldo_on_irq_handler),
    DECL_INTERRUPT_HANDLER(19, rx_ldo_off_irq_handler),
    DECL_INTERRUPT_HANDLER(20, rx_pldo_irq_handler),
    DECL_INTERRUPT_HANDLER(21, rx_scp_irq_handler),
};

static const struct interrupt_handler tx_irq_handlers[] = {
    DECL_INTERRUPT_HANDLER(0, ocp_irq_handler),
    DECL_INTERRUPT_HANDLER(1, vout_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(2, clamp_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(3, ngage_ovp_irq_handler),
    DECL_INTERRUPT_HANDLER(4, vout_lvp_irq_handler),
    DECL_INTERRUPT_HANDLER(5, otp_irq_handler),
    DECL_INTERRUPT_HANDLER(6, otp_160c_irq_handler),
    DECL_INTERRUPT_HANDLER(7, sleep_irq_handler),
    DECL_INTERRUPT_HANDLER(8, mode_change_irq_handler),
    DECL_INTERRUPT_HANDLER(9, tx_ask_recv_irq_handler),
    DECL_INTERRUPT_HANDLER(10, tx_ppp_timeout_irq_handler),
    DECL_INTERRUPT_HANDLER(11, tx_ppp_success_irq_handler),
    DECL_INTERRUPT_HANDLER(12, tx_afc_det_irq_handler),
    DECL_INTERRUPT_HANDLER(13, tx_epp_det_irq_handler),
    DECL_INTERRUPT_HANDLER(14, tx_detect_rx_irq_handler),
    DECL_INTERRUPT_HANDLER(15, tx_remove_power_irq_handler),
    DECL_INTERRUPT_HANDLER(16, tx_fod_irq_handler),
    DECL_INTERRUPT_HANDLER(17, tx_detect_tx_irq_handler),
    DECL_INTERRUPT_HANDLER(18, tx_cep_timeout_irq_handler),
    DECL_INTERRUPT_HANDLER(19, tx_rpp_timeout_irq_handler),
};

/*********************************************************************/
static ssize_t sc9624_show_registers(struct device *dev,
                struct device_attribute *attr, char *buf)
{
    struct sc9624 *sc = dev_get_drvdata(dev);
    int i = 0;
    uint32_t data;
    uint8_t tmpbuf[500];
    int len;
    int idx = 0;
    int ret;

    for (i = 0; i < 0x200 / 4; i++) {
        ret = sc9624_read_block(sc, i * 4, (uint8_t *)&data, 4);
        if (!ret)
        {
            len = snprintf(tmpbuf, PAGE_SIZE - idx,
                    "Reg[%04X] = 0x%08x\n", i * 4, data);
            memcpy(&buf[idx], tmpbuf, len);
            idx += len;
        }
    }

    return idx;
}

static ssize_t sc9624_store_register(struct device *dev,
            struct device_attribute *attr, const char *buf, size_t count)
{
    struct sc9624 *sc = dev_get_drvdata(dev);
    int ret;
    int reg;
    int len;
    uint32_t val;

    ret = sscanf(buf, "%x %x %x", &reg, &val, &len);
    if (ret == 3 && reg >= 0x0000 && reg <= 0x0200)
    {
        sc9624_write_block(sc, reg, (uint8_t *)&val, len);
    }

    return count;
}

static DEVICE_ATTR(registers, 0660, sc9624_show_registers,
        sc9624_store_register);

//-----------------------------reg addr----------------------------------
static ssize_t show_reg_addr(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct sc9624 *sc = dev_get_drvdata(dev);
	return sprintf(buf, "reg addr 0x%08x\n", sc->reg_addr);
}

static ssize_t store_reg_addr(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	struct sc9624 *sc = dev_get_drvdata(dev);

	tmp = simple_strtoul(buf, NULL, 0);
	sc->reg_addr = tmp;

	return count;
}
static DEVICE_ATTR(reg_addr, 0664, show_reg_addr, store_reg_addr);

//-----------------------------reg data----------------------------------
static ssize_t show_reg_data(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int ret = 0;
	uint8_t data[4] = {0x00};
	struct sc9624 *sc = dev_get_drvdata(dev);

	ret = sc9624_read_block(sc, sc->reg_addr, data, 4);
	if (ret == 0)
		sc->reg_data = *((uint32_t *)data);
	return sprintf(buf, "reg addr 0x%08x -> 0x%08x\n", sc->reg_addr, sc->reg_data);
}

static ssize_t store_reg_data(struct device *dev,
			struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	struct sc9624 *sc = dev_get_drvdata(dev);

	tmp = simple_strtoul(buf, NULL, 0);
	sc->reg_data = tmp;
	if (sc->reg_addr >= 0x0000 && sc->reg_addr <= 0x0200)
		sc9624_write_block(sc, sc->reg_addr, (uint8_t *)&sc->reg_data, 4);

	return count;
}
static DEVICE_ATTR(reg_data, 0664, show_reg_data, store_reg_data);

static void sc9624_create_device_node(struct device *dev)
{
    device_create_file(dev, &dev_attr_registers);
    device_create_file(dev, &dev_attr_reg_addr);
    device_create_file(dev, &dev_attr_reg_data);
}


static enum power_supply_property sc9624_charger_props[] = {
    POWER_SUPPLY_PROP_ONLINE,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_CURRENT_NOW,
    POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
    //POWER_SUPPLY_PROP_SC_VOLTAGE,
    //POWER_SUPPLY_PROP_SC_CURRENT,
    //POWER_SUPPLY_PROP_SC_ILIMIT,
    //POWER_SUPPLY_PROP_SC_VRECT,
    //POWER_SUPPLY_PROP_SC_TX_ENABLE,
    //POWER_SUPPLY_PROP_SC_TX_FOD_ENABLE,
    //POWER_SUPPLY_PROP_SC_WORK_MODE,
    //POWER_SUPPLY_PROP_SC_MTP_PROGRAM,
};

static int sc9624_charger_get_property(struct power_supply *psy,
                enum power_supply_property psp,
                union power_supply_propval *val)
{
    struct sc9624 *sc = power_supply_get_drvdata(psy);
    int ret;
    uint16_t regval;

    switch (psp) {
    case POWER_SUPPLY_PROP_ONLINE:
        ret = sc9624_online(sc);
        if (ret) {
            val->intval = 0;
        } else {
            val->intval = 1;
        }
        break;
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        ret = sc9624_get_voltage(sc, &regval);
        if (!ret) {
            val->intval = regval;
        }
        break;
    case POWER_SUPPLY_PROP_CURRENT_NOW:
        ret = sc9624_get_current(sc, &regval);
        if (!ret) {
            val->intval = regval;
        }
        break;
#if 0
    case POWER_SUPPLY_PROP_SC_VRECT:
        ret = sc9624_get_vrect(sc, &regval);
        if (!ret) {
            val->intval = regval;
        }
        break;
    case POWER_SUPPLY_PROP_SC_ILIMIT:
    case POWER_SUPPLY_PROP_SC_TX_ENABLE:
    case POWER_SUPPLY_PROP_SC_TX_FOD_ENABLE:
    case POWER_SUPPLY_PROP_SC_MTP_PROGRAM:
        val->intval = 0;
        break;
    case POWER_SUPPLY_PROP_SC_WORK_MODE:
        val->intval = sc->work_mode;
        break;
#endif
    default:
        return -EINVAL;

    }
    return 0;
}


static int sc9624_charger_set_property(struct power_supply *psy,
                        enum power_supply_property prop,
                        const union power_supply_propval *val)
{
    struct sc9624 *sc = power_supply_get_drvdata(psy);
    int ret = 0;

    switch (prop) {
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
        ret = sc9624_rx_set_Vout(sc, val->intval);
        break;
    case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
        ret = sc9624_rx_set_Ilimit(sc, val->intval);
    break;
#if 0
    case POWER_SUPPLY_PROP_SC_TX_ENABLE:
        ret = sc9624_tx_func_enable(sc);
    break;
    case POWER_SUPPLY_PROP_SC_TX_FOD_ENABLE:
        ret = sc9624_tx_fod_enable(sc, val->intval);
    break;
    case POWER_SUPPLY_PROP_SC_WORK_MODE:
        sc->work_mode = val->intval;
    break;
    case POWER_SUPPLY_PROP_SC_MTP_PROGRAM:
        ret = mtp_program(sc);
    break;
#endif
    default:
        return -EINVAL;
    }

    return ret;
}


static int sc9624_charger_is_writeable(struct power_supply *psy,
                    enum power_supply_property prop)
{
    int ret;

    switch (prop) {
    default:
        ret = 0;
        break;
    }
    return ret;
}

static int sc9624_psy_register(struct sc9624 *sc)
{
    sc->psy_cfg.drv_data = sc;
    sc->psy_cfg.of_node = sc->dev->of_node;

    sc->psy_desc.name = "wireless-sc9624";
    sc->psy_desc.type = POWER_SUPPLY_TYPE_MAINS;
    sc->psy_desc.properties = sc9624_charger_props;
    sc->psy_desc.num_properties = ARRAY_SIZE(sc9624_charger_props);
    sc->psy_desc.get_property = sc9624_charger_get_property;
    sc->psy_desc.set_property = sc9624_charger_set_property;
    sc->psy_desc.property_is_writeable = sc9624_charger_is_writeable;

    sc->wl_psy = devm_power_supply_register(sc->dev,
            &sc->psy_desc, &sc->psy_cfg);
    if (IS_ERR(sc->wl_psy)) {
        sc_err("failed to register wl_psy\n");
        return PTR_ERR(sc->wl_psy);
    }

    sc_info("%s power supply register successfully\n", sc->psy_desc.name);

    return 0;
}

static int sc9624_parse_dt(struct sc9624 *sc, struct device *dev)
{
    //int ret;
    struct device_node *np = dev->of_node;

    if (!np)
        return -ENOMEM;

    /*ret = of_get_named_gpio(np, "sc,sc9624,intr_gpio", 0);
    if (ret < 0) {
        sc_err("no intr_gpio info\n");
        return ret;
    }*/
    sc->irq_gpio = 12;
    /*ret = of_property_read_u32(np, "sc,sc9624-int-enable",
            &sc->cust->IntEn);
    if (ret) {
        sc_err("failed to read intEn\n");
        return ret;
    }*/

    return 0;
}

/*
* interrupt does nothing, just info event chagne, other module could get info
* through power supply interface
*/
static void sc9624_irq_work_handler(struct kthread_work *work)
{
    struct sc9624 *sc =
            container_of(work, struct sc9624, irq_work);
    int ret;
    int i;
    uint32_t regval;

    /* make sure I2C bus had resumed */
    down(&sc->suspend_lock);

    sc_info("irq trigger mode : %s\n", sc->work_mode == RX_MODE ? "RX MODE" : "TX MODE");

    if (sc->work_mode == RX_MODE) {
        ret = sc9624_rx_get_int(sc, &regval);
        if (ret < 0) {
            sc_err("get rx int flag fail\n");
            up(&sc->suspend_lock);
            return;
        }

        for (i = 0; i < ARRAY_SIZE(rx_irq_handlers); i++) {
                if (rx_irq_handlers[i].bit_mask & regval) {
                    if (rx_irq_handlers[i].handler != 0)
                        rx_irq_handlers[i].handler(sc);
            }

            //clear intflag
            ret = sc9624_rx_clr_int(sc, regval);
        }
    } else {
        ret = sc9624_tx_get_int(sc, &regval);
        if (ret < 0) {
            sc_err("get tx int flag fail\n");
            up(&sc->suspend_lock);
            return;
        }

        for (i = 0; i < ARRAY_SIZE(tx_irq_handlers); i++) {
                if (tx_irq_handlers[i].bit_mask & regval) {
                    if (tx_irq_handlers[i].handler != 0)
                        tx_irq_handlers[i].handler(sc);
            }
        }

        //clear intflag
        ret = sc9624_tx_clr_int(sc, regval);
    }

    up(&sc->suspend_lock);

    power_supply_changed(sc->wl_psy);
}

static irqreturn_t sc9624_intr_handler(int irq, void *dev_id)
{
    struct sc9624 *sc = dev_id;

    __pm_wakeup_event(sc->irq_wake_lock, SC9624_IRQ_WAKE_TIME);

    kthread_queue_work(&sc->irq_worker, &sc->irq_work);

    return IRQ_HANDLED;
}

static int sc9624_irq_init(struct sc9624 *sc)
{
    int ret;
    struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };

    ret = devm_gpio_request(sc->dev, sc->irq_gpio, "sc9624-irq-gpio");
    if (ret < 0) {
        sc_err("Error: failed to request GPIO%d (ret = %d)\n",
        sc->irq_gpio, ret);
        return -EINVAL;
    }

    ret = gpio_direction_input(sc->irq_gpio);
    if (ret < 0) {
        sc_err("Error: failed to set GPIO%d as input pin(ret = %d)\n",
        sc->irq_gpio, ret);
        return -EINVAL;
    }

    sc->irq = gpio_to_irq(sc->irq_gpio);
    if (sc->irq <= 0) {
        sc_err("gpio to irq fail, chip->irq(%d)\n",
                        sc->irq);
        return -EINVAL;
    }

    sc_info(" : IRQ number = %d\n", sc->irq);

    kthread_init_worker(&sc->irq_worker);
    sc->irq_worker_task = kthread_run(kthread_worker_fn,
            &sc->irq_worker, "%s", "sc9624_irq_thread");
    if (IS_ERR(sc->irq_worker_task)) {
        sc_err("Error: Could not create tcpc task\n");
        return -EINVAL;
    }

    sched_setscheduler(sc->irq_worker_task, SCHED_FIFO, &param);
    kthread_init_work(&sc->irq_work, sc9624_irq_work_handler);

    ret = request_irq(sc->irq, sc9624_intr_handler,
        IRQF_TRIGGER_FALLING | IRQF_NO_THREAD, "sc9624_irq", sc);
    if (ret < 0) {
        sc_err("Error: failed to request irq%d (gpio = %d, ret = %d)\n",
            sc->irq, sc->irq_gpio, ret);
        return -EINVAL;
    }

    enable_irq_wake(sc->irq);
    return 0;
}

static struct of_device_id sc9624_charger_match_table[] = {
    {
        .compatible = "sc,sc9624-wireless-charger",
        .data = NULL,
    },
    {},
};

static int sc9624_charger_probe(struct i2c_client *client,
                    const struct i2c_device_id *id)
{
    struct sc9624 *sc;
    int ret;

    pr_err("%s start\n", __func__);

    sc = devm_kzalloc(&client->dev, sizeof(struct sc9624), GFP_KERNEL);
    if (!sc)
        return -ENOMEM;

    sc->dev = &client->dev;
    sc->client = client;


    mutex_init(&sc->i2c_rw_lock);
    mutex_init(&sc->data_lock);
    sema_init(&sc->suspend_lock, 1);

    i2c_set_clientdata(client, sc);

    sc->regmap = regmap_init_i2c(client, &sc9624_regmap_config);

    sc9624_create_device_node(&(client->dev));

    sc->irq_wake_lock =
        wakeup_source_register(sc->dev, "sc9624_irq_wake_lock");

    ret = sc9624_parse_dt(sc, &client->dev);
    if (ret)
        return -EIO;

    ret = sc9624_psy_register(sc);
    if (ret)
        goto err_1;

    ret = sc9624_irq_init(sc);

    device_init_wakeup(sc->dev, 1);

    sc_info("sc9624 probe successfully!\n");

    return 0;

err_1:
    wakeup_source_unregister(sc->irq_wake_lock);
    power_supply_unregister(sc->wl_psy);
    return ret;
}

static int sc9624_suspend(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct sc9624 *sc = i2c_get_clientdata(client);

    if (sc)
        down(&sc->suspend_lock);

    sc_err("Suspend successfully!");

    return 0;
}

static int sc9624_resume(struct device *dev)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct sc9624 *sc = i2c_get_clientdata(client);

    if (sc)
        up(&sc->suspend_lock);
	
    sc_err("Resume successfully!");

    return 0;
}
static void sc9624_charger_remove(struct i2c_client *client)
{
    struct sc9624 *sc = i2c_get_clientdata(client);

    power_supply_unregister(sc->wl_psy);

    mutex_destroy(&sc->data_lock);
    mutex_destroy(&sc->i2c_rw_lock);
}

static void sc9624_charger_shutdown(struct i2c_client *client)
{
    //struct sc9624 *sc = i2c_get_clientdata(client);
    return;
}

static const struct dev_pm_ops sc9624_pm_ops = {
    .resume     = sc9624_resume,
    .suspend    = sc9624_suspend,
};

static const struct i2c_device_id sc9624_charger_id[] = {
    {"sc9624-wls-chg", 0},
    {},
};

static struct i2c_driver sc9624_wireless_charger_driver = {
    .driver     = {
        .name   = "sc-wireless-charger",
        .owner  = THIS_MODULE,
        .of_match_table = sc9624_charger_match_table,
        .pm     = &sc9624_pm_ops,
    },
    .id_table   = sc9624_charger_id,

    .probe      = sc9624_charger_probe,
    .remove     = sc9624_charger_remove,
    .shutdown   = sc9624_charger_shutdown,
};

module_i2c_driver(sc9624_wireless_charger_driver);

MODULE_DESCRIPTION("SC SC9624 Wireless Charge Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aiden-yu@southchip.com");
