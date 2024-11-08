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
#define DEBUG_LOG (1u)
// uncomment to test with input forced open
//#define INPUT_DEVICE_ALWAYS_OPEN
#include <linux/types.h>



#include <uapi/asm-generic/errno-base.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/dma-mapping.h>

#include "eswin_eph861x_tlv.h"
#include "eswin_eph861x_types.h"
#if (ESWIN_EPH861X_SPI)
#include "eswin_eph861x_spi.h"

#define TLV_RESERVED_INVALID_TYPE  0xFF



u8 spi_tx_dummy_buf[SPI_APP_BUF_SIZE_READ];

#if DEBUG_LOG
static void EPH_LOG_BUFFER(struct eph_data *ephdata, u8* data, bool rx_data)
{
    dev_info(&ephdata->commsdevice->dev, "[%d] - %02x %02x %02x %02x %02x %02x\n", rx_data, data[0], data[1], data[2], data[3], data[4], data[5]);
    return;
}
#endif

int __eph_spi_read(struct eph_data *ephdata, u16 len, u8 *val)
{
    int ret_val;
    struct spi_message  spimsg;
    struct spi_transfer spitr;

    spi_message_init(&spimsg);
    memset(&spitr, 0,  sizeof(struct spi_transfer));

    if ((SPI_APP_BUF_SIZE_READ) < len)
    {
        dev_err(&ephdata->commsdevice->dev, "Error reading from spi: length greater than buffer size(%d)", len);
        return -ENOMEM;
    }

    memcpy(ephdata->comms_send_buf, &spi_tx_dummy_buf[0], len);

    spitr.tx_buf = ephdata->comms_send_buf;
    spitr.rx_buf = ephdata->comms_receive_buf;
    spitr.len = len;
#if (ESWIN_EPH861X_SPI_USE_DMA)
    spitr.tx_dma = ephdata->comms_dma_handle_tx;
    spitr.rx_dma = ephdata->comms_dma_handle_rx;
    spimsg.is_dma_mapped = 1;
#endif
    spi_message_add_tail(&spitr, &spimsg);
    ret_val = spi_sync(ephdata->commsdevice, &spimsg);

    if (ret_val < 0)
    {
        dev_err(&ephdata->commsdevice->dev, "Error reading from spi (%d)", ret_val);
        return ret_val;
    }
#if DEBUG_LOG
    else
    {
        EPH_LOG_BUFFER(ephdata, spitr.rx_buf, 1);
    }
#endif

    memcpy(val, ephdata->comms_receive_buf, len);
    return 0;
}


int __eph_spi_write(struct eph_data *ephdata, u16 len, const u8 *val)
{
    int ret_val;
    struct spi_message  spimsg;
    struct spi_transfer spitr;

    if ((SPI_APP_BUF_SIZE_WRITE) < len)
    {
        dev_err(&ephdata->commsdevice->dev, "Error writing to spi: length greater than buffer size(%d)", len);
        return -ENOMEM;
    }

    memcpy(ephdata->comms_send_buf, val, len);
    spi_message_init(&spimsg);
    memset(&spitr, 0,  sizeof(struct spi_transfer));
    spitr.tx_buf = ephdata->comms_send_buf;
    spitr.rx_buf = ephdata->comms_receive_buf;
    spitr.len = len;
#if (ESWIN_EPH861X_SPI_USE_DMA)
    spitr.tx_dma = ephdata->comms_dma_handle_tx;
    spitr.rx_dma = ephdata->comms_dma_handle_rx;
    spimsg.is_dma_mapped = 1;
#endif
    spi_message_add_tail(&spitr, &spimsg);
    ret_val = spi_sync(ephdata->commsdevice, &spimsg);
    if (ret_val < 0)
    {
        dev_err(&ephdata->commsdevice->dev, "Error writing to spi (%d)", ret_val);
        return ret_val;
    }
#if DEBUG_LOG
    else
    {
        EPH_LOG_BUFFER(ephdata, (u8*)spitr.tx_buf, 0);
    }
#endif
    return 0;
}

int eph_comms_read(struct eph_data *ephdata, u16 len, u8 *buf)
{
    int ret_val = 0;


    if ((SPI_APP_BUF_SIZE_READ) < len)
    {
        dev_err(&ephdata->commsdevice->dev, "Error reading from spi: length greater than buffer size(%d)", len);
        return -ENOMEM;
    }

    ret_val = __eph_spi_read(ephdata,
                             len,
                             buf);
    if (ret_val)
    {
        dev_err(&ephdata->commsdevice->dev, "Error occured(%d)", ret_val);
        return ret_val;
    }

    if (true == eph_is_report_null_report(buf) || (TLV_RESERVED_INVALID_TYPE == buf[0]))
    {
        udelay(200);
        ret_val = __eph_spi_read(ephdata,
                                 len,
                                 buf);
        if (ret_val)
        {
            return ret_val;
        }

        if (true == eph_is_report_null_report(buf) || (TLV_RESERVED_INVALID_TYPE == buf[0]))
        {
            dev_err(&ephdata->commsdevice->dev,
                    "Failed to read message --- No response type: %u ",
                    buf[0]);
            return -EIO;
        }

    }
    return ret_val;
}


int eph_comms_write(struct eph_data *ephdata, u16 len, u8 *buf)
{
    int ret_val = 0;

    ret_val = __eph_spi_write(ephdata,
                                  len,
                                  buf);
    return ret_val;
}


int eph_comms_specific_checks_spi(struct comms_device *commsdevice)
{

    if ( (commsdevice->bits_per_word && commsdevice->bits_per_word != 8) ||
         !(commsdevice->mode & SPI_CPHA) ||
         !(commsdevice->mode & SPI_CPOL) )
    {
        dev_err(&commsdevice->dev, "unexpected spi device setup: SPI mode(%d), bits_per_word(%d) \n", commsdevice->mode, commsdevice->bits_per_word);
        return -EINVAL;
    }

    memset(&spi_tx_dummy_buf[0], 0xFF, SPI_APP_BUF_SIZE_READ);
    return 0;
}

void eph_comms_driver_data_set_spi(struct comms_device *commsdevice, struct eph_data *ephdata)
{

    snprintf(ephdata->phys, sizeof(ephdata->phys), "spi-%d/input0", commsdevice->master->bus_num);
    dev_info(&commsdevice->dev, "%s %s\n", __func__, ephdata->phys);

    spi_set_drvdata(commsdevice, ephdata);
    if (0xff != spi_tx_dummy_buf[0])
    {
        memset(spi_tx_dummy_buf, 0xff, SPI_APP_BUF_SIZE_READ);
    }
    return;
}


struct eph_data* eph_comms_driver_data_get_spi(struct comms_device *commsdevice)
{

    struct eph_data *ephdata = (struct eph_data *)spi_get_drvdata(commsdevice);
    return ephdata;
}


struct comms_device* eph_comms_device_get_spi(struct device *dev)
{

    struct comms_device *commsdevice = to_spi_device(dev);
    return commsdevice;
}

int eph_comms_specific_bootloader_checks_spi(struct eph_data *ephdata)
{
    (void)ephdata;
    return 0;
}

bool eph_is_report_null_report(u8 *buff)
{

    bool is_null_report = false;

    if (0 == buff[0])
    {
        if((0 == buff[1]) && (0 == buff[2]))
        {
            is_null_report = true;
        }

    }

    return is_null_report;
}

#endif /*   ESWIN_EPH861X_SPI  */










