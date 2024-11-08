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
#include <linux/kernel.h>
#include <uapi/linux/input-event-codes.h>

#include <linux/sysfs.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>

#include "eswin_eph861x_tlv.h"
#include "eswin_eph861x_types.h"
#include "eswin_eph861x_project_config.h"
#include "eswin_eph861x_comms.h"
#include "eswin_eph861x_tlv_report.h"
#include "eswin_eph861x_tlv_command.h"




int eph_read_device_information(struct eph_data *ephdata)
{
    int ret_val = 0;
    u8 retry = 0;
    u8 type_match = false;
    const struct tlv_header tlvheader = {1, 0};


    ret_val = eph_comms_write(ephdata,
                              TLV_HEADER_SIZE,
                              (u8*)&tlvheader);
    if (ret_val)
    {
        return ret_val;
    }

    udelay(100);

    while (false ==  type_match)
    {
        memset(ephdata->comms_receive_buf, 0, COMMS_BUF_SIZE);
        memset(&ephdata->ephdeviceinfo, 0 , sizeof(struct eph_device_info));

        ret_val = eph_comms_two_stage_read(ephdata, ephdata->comms_receive_buf);

        if (!ret_val)
        {
            u8 calc_crc;

            type_match = (TLV_DEVICE_INFO_DATA ==  ephdata->comms_receive_buf[0]);

            if(true == type_match)
            {
                memcpy((u8*)&ephdata->ephdeviceinfo, ephdata->comms_receive_buf, sizeof(struct eph_device_info));
            }

            calc_crc = eph_get_data_crc((u8*)&ephdata->ephdeviceinfo.product_id, (sizeof(struct eph_device_info) - (TLV_HEADER_SIZE + sizeof(u8))));
            if (ephdata->ephdeviceinfo.crc != calc_crc)
            {
                dev_err(&ephdata->commsdevice->dev, "CRC mismatch - expected: %d, got: %d", calc_crc, ephdata->ephdeviceinfo.crc);
                type_match= false;
            }

            if (false == type_match) {
                ret_val = eph_handle_report(ephdata, ephdata->comms_receive_buf);
                dev_info(&ephdata->commsdevice->dev, "Process unexpected response %d\n", ret_val);
            }
        }

        if(ret_val || ((retry > COMMS_READ_RETRY_NUM) && (false ==  type_match)))
        {
            dev_err(&ephdata->commsdevice->dev, "Failed to read expected response on second retry");
            return -EAGAIN;
        }
        else
        {
            retry++;
        }
    }


    return ret_val;
}


int eph_write_control_config(struct eph_data *ephdata, u16 len, u8 *buf)
{
    int ret_val = 0;
    struct eph_device_control_config_write_response response;
    bool type_match = false;
    u8 retry = 0;
    u16 payload_length = (len - (TLV_HEADER_SIZE + TLV_WRITE_HEADER_SIZE));

    udelay(50);

    memcpy(ephdata->comms_send_crc_buf, buf, len);

    if(ephdata->ephdeviceinfo.protocol_version > CONFIG_ESWIN_TOUCH_PROTOCOL_VERSION_2)
    {
        u8 calc_crc;
        struct tlv_header* header;
        header = (struct tlv_header*)ephdata->comms_send_crc_buf;
        header->length = header->length + cpu_to_le16(1);
        calc_crc = eph_get_data_crc((u8*)ephdata->comms_send_crc_buf, len);
        memcpy(&ephdata->comms_send_crc_buf[len], &calc_crc, sizeof(u8));
        len++;
    }

    ret_val = eph_comms_write(ephdata, len, ephdata->comms_send_crc_buf);

    udelay(200);

    if (ret_val)
    {
        return ret_val;
    }

    while (false ==  type_match)
    {
        memset(ephdata->comms_receive_buf, 0, COMMS_BUF_SIZE);

        ret_val = eph_comms_two_stage_read(ephdata, ephdata->comms_receive_buf);

        if (!ret_val)
        {
            memcpy((u8*)&response, ephdata->comms_receive_buf, sizeof(struct eph_device_control_config_write_response));

            if (le16_to_cpu(response.bytes_written) != payload_length)
            {
                dev_err(&ephdata->commsdevice->dev, "Error writing to control config bytes written did not match what was sent");
                dev_err(&ephdata->commsdevice->dev,
                        "Sent the following number of bytes (%d) but (%d) bytes where written",
                        payload_length, le16_to_cpu(response.bytes_written));
            }

            type_match = (TLV_CONFIG_DATA_WRITE ==  response.header.type) || (TLV_CONTROL_DATA_WRITE ==  response.header.type);

            if (type_match == false) {
                ret_val = eph_handle_report(ephdata, ephdata->comms_receive_buf);
                dev_info(&ephdata->commsdevice->dev, "Process unexpected response %d\n", ret_val);
            }
        }



        if(ret_val || ((retry > COMMS_READ_RETRY_NUM) && (false ==  type_match)))
        {
            dev_err(&ephdata->commsdevice->dev, "Failed to read expected response on second retry");
            break;
        }
        else
        {
            retry++;
        }
    }

    if ((ret_val) || (false ==  type_match))
    {
        mdelay(10);
        dev_err(&ephdata->commsdevice->dev, "Control or contfiguration write failure. Type Match: (%d)", type_match);
        return -EIO;
    }

    udelay(200);

    return ret_val;
}


int eph_write_control_config_no_response(struct eph_data *ephdata, u16 len, u8 *buf)
{
    int ret_val = 0;

    udelay(50);

    memcpy(ephdata->comms_send_crc_buf, buf, len);
    if(ephdata->ephdeviceinfo.protocol_version > CONFIG_ESWIN_TOUCH_PROTOCOL_VERSION_2)
    {
        u8 calc_crc;
        struct tlv_header* header;
        header = (struct tlv_header*)ephdata->comms_send_crc_buf;
        header->length = header->length + cpu_to_le16(1);
        calc_crc = eph_get_data_crc((u8*)ephdata->comms_send_crc_buf, len);
        memcpy(&ephdata->comms_send_crc_buf[len], &calc_crc, sizeof(u8));
        len++;
    }

    ret_val = eph_comms_write(ephdata, len, buf);

    udelay(100);

    if (ret_val)
    {
        dev_err(&ephdata->commsdevice->dev, "Control or contfiguration write failure.");
        return ret_val;
    }

    return ret_val;
}

int eph_read_control_config(struct eph_data *ephdata, struct eph_device_control_config_read_command *command_request, u8 *buf)
{
    int ret_val = 0;
    bool type_match = false;
    u8 retry = 0;
    struct tlv_header tlvheader;
    u16 len = sizeof(struct eph_device_control_config_read_command);

    memcpy(ephdata->comms_send_crc_buf, command_request, len);
    if(ephdata->ephdeviceinfo.protocol_version > CONFIG_ESWIN_TOUCH_PROTOCOL_VERSION_2)
    {
        u8 calc_crc;
        struct tlv_header* header;
        header = (struct tlv_header*)ephdata->comms_send_crc_buf;
        header->length = header->length + cpu_to_le16(1);
        calc_crc = eph_get_data_crc((u8*)ephdata->comms_send_crc_buf, (sizeof(struct eph_device_control_config_read_command)));
        memcpy(&ephdata->comms_send_crc_buf[len], &calc_crc, sizeof(u8));
        len++;
    }

    ret_val = eph_comms_write(ephdata, len, (u8*)ephdata->comms_send_crc_buf);

    udelay(200);

    if (ret_val)
    {
        return ret_val;
    }


    while (false ==  type_match)
    {
        memset(ephdata->comms_receive_buf, 0, COMMS_BUF_SIZE);

        ret_val = eph_comms_two_stage_read(ephdata, ephdata->comms_receive_buf);

        if (!ret_val)
        {
            memcpy((u8*)&tlvheader, ephdata->comms_receive_buf, sizeof(struct tlv_header));

            type_match = (TLV_CONFIG_DATA_READ ==  tlvheader.type) || (TLV_CONTROL_DATA_READ ==  tlvheader.type);

            if (type_match == false) {
                ret_val = eph_handle_report(ephdata, ephdata->comms_receive_buf);
                dev_info(&ephdata->commsdevice->dev, "Process unexpected response %d\n", ret_val);
            } else {
            }
        }

        if(ret_val || ((retry > COMMS_READ_RETRY_NUM) && (false ==  type_match)))
        {
            dev_err(&ephdata->commsdevice->dev, "Failed to read expected response on second retry");
            break;
        }
        else
        {
            retry++;
        }
    }

    if ((ret_val) || (false ==  type_match))
    {
        mdelay(10);
        dev_err(&ephdata->commsdevice->dev, "Control or contfiguration write failure. Type Match: (%d)", type_match);
        return -EIO;
    }

    return ret_val;
}


int eph_read_bootloader_information(struct eph_data *ephdata)
{
    int ret_val = 0;
    u8 retry = 0;
    u8 type_match = false;
    const struct tlv_header tlvheader = {1, 0};

    ret_val = eph_comms_write(ephdata,
                              TLV_HEADER_SIZE,
                              (u8*)&tlvheader);

    if (ret_val)
    {
        return ret_val;
    }

    udelay(100);

    while (false ==  type_match)
    {

        memset(ephdata->comms_receive_buf, 0, COMMS_BUF_SIZE);
        memset(&ephdata->ephdeviceinfo, 0 , sizeof(struct eph_device_info));

        ret_val = eph_comms_two_stage_read(ephdata, ephdata->comms_receive_buf);

        if (!ret_val)
        {
            u8 calc_crc;
            type_match = (TLV_DEVICE_INFO_DATA ==  ephdata->comms_receive_buf[0]);

            if(type_match)
            {
                memcpy((u8*)&ephdata->ephdeviceinfo, ephdata->comms_receive_buf, sizeof(struct eph_device_info));
            }

            calc_crc = eph_get_data_crc((u8*)&ephdata->ephdeviceinfo.product_id, (sizeof(struct eph_device_info) - (TLV_HEADER_SIZE + sizeof(u8))));
            if (ephdata->ephdeviceinfo.crc != calc_crc)
            {
                dev_err(&ephdata->commsdevice->dev, "CRC mismatch - expected: %d, got: %d", calc_crc, ephdata->ephdeviceinfo.crc);
                type_match= false;
            }
        }

        if(ret_val || ((retry > COMMS_READ_RETRY_NUM) && (false ==  type_match)))
        {
            dev_err(&ephdata->commsdevice->dev, "Failed to read expected response on second retry");
            break;
        }
        else
        {
            retry++;
        }
    }

    return ret_val;
}


int eph_write_engineering_data(struct eph_data *ephdata, u16 len, u8 *buf)
{
    int ret_val = 0;
    struct eph_device_eng_data_write_response response;
    bool type_match = false;
    u8 retry = 0;

    u16 payload_length = (len - sizeof(struct eph_device_eng_data_write_command));

    udelay(50);

    ret_val = eph_comms_write(ephdata, len, buf);

    udelay(100);

    if (ret_val)
    {
        dev_err(&ephdata->commsdevice->dev, "Write engineering frame failure. ");
        return ret_val;
    }

    while (false ==  type_match)
    {
        memset(ephdata->comms_receive_buf, 0, COMMS_BUF_SIZE);
        ret_val = eph_comms_two_stage_read(ephdata, ephdata->comms_receive_buf);

        if (!ret_val)
        {
            memcpy((u8*)&response, ephdata->comms_receive_buf, sizeof(struct eph_device_eng_data_write_response));

            if (le16_to_cpu(response.bytes_written) != payload_length)
            {
                dev_err(&ephdata->commsdevice->dev, "Error writing to eng data: bytes written did not match what was sent");
                dev_err(&ephdata->commsdevice->dev, "Sent the following number of bytes (%d) but (%d) bytes where written", payload_length, le16_to_cpu(response.bytes_written));
            }

            type_match = (TLV_ENG_DEBUG_DATA_WRITE ==  response.header.type);
            if (type_match == false) {
                ret_val = eph_handle_report(ephdata, ephdata->comms_receive_buf);
                dev_info(&ephdata->commsdevice->dev, "Process unexpected response %d\n", ret_val);
            }
        }

        if(ret_val || ((retry > COMMS_READ_RETRY_NUM) && (false ==  type_match)))
        {
            dev_err(&ephdata->commsdevice->dev, "Failed to read expected response on retry");
            break;
        }
        else
        {
            retry++;
        }
    }

        /* retry */
        if ((ret_val) || (false ==  type_match))
        {
            mdelay(10);
            dev_err(&ephdata->commsdevice->dev, "Write engineering failure. Type Match: (%d)", type_match);
            return -EIO;
        }

    return ret_val;
}
