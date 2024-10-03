#include <linux/init.h> /* For init/exit macros */
#include <linux/module.h> /* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <mtk_charger.h>
#include <mtk_charger_algorithm_class.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/thermal.h>
#include <linux/mmi_wireless_class.h>
#include "moto_wlc2.h"

static ssize_t wireless_fw_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int fw_version = 0x00;
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}

	if (pWlc->data.wls_fw_version != 0) {
		fw_version = pWlc->data.wls_fw_version;
	} else {
		wls_rx_get_fw_version(pWlc->wls_dev, &fw_version);
		pWlc->data.wls_fw_version = fw_version;
	}

	return sprintf(buf, "%08x\n", fw_version);
}
static DEVICE_ATTR(wireless_fw_version, 0444, wireless_fw_version_show, NULL);

static ssize_t wireless_fw_update_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int update = 0;
	int sys_mode = 0;
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}

	if (kstrtoint(buf, 0, &update) ||
		(update != NORMAL_FW_UPDATE && update != FORCE_FW_UPDATE)) {
		wlc_err("%s: invalid FW update mode %d\n", __func__, update);
		return -EINVAL;
	}

	wls_rx_get_sys_mode(pWlc->wls_dev, &sys_mode);
	if (sys_mode == SYS_MODE_RX) {
		wlc_info("wireless_fw_update wls online,forbid fw update\n");
	} else {
		//queue_delayed_work(pWlc->wls_wq,
		//		&pWlc->fw_update_work, msecs_to_jiffies(2000));
	}

	return count;
}
static DEVICE_ATTR(wireless_fw_update, 0220, NULL, wireless_fw_update_store);

static ssize_t mode_select_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	bool val = 0;
	int ret = 0;
	struct moto_wlc *pWlc = dev->driver_data;
	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	if (kstrtobool(buf, &val)) {
		pWlc->ctl.mode_select_force = false;
		wlc_info("mode_select_store force exit\n");
		return -EINVAL;
	}

	pWlc->ctl.mode_select_force = val;
	ret = wls_rx_set_mode_select(pWlc->wls_dev, val);

	wlc_info("mode_select_store %d, wls_online:%d force:%d ret:%d\n",
			val , pWlc->wls_online, pWlc->ctl.mode_select_force, ret);

	return count;
}
static DEVICE_ATTR(mode_select, 0220, NULL, mode_select_store);

static ssize_t show_rx_irect(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;
	int rx_irect = 0;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	wls_rx_get_rx_irect(pWlc->wls_dev, &rx_irect);

	return sprintf(buf, "%d\n", rx_irect);
}
static DEVICE_ATTR(get_rx_irect, 0444, show_rx_irect, NULL);

static ssize_t show_rx_vrect(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;
	int rx_vrect = 0;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	wls_rx_get_rx_vrect(pWlc->wls_dev, &rx_vrect);

	return sprintf(buf, "%d\n", rx_vrect);
}
static DEVICE_ATTR(get_rx_vrect, 0444, show_rx_vrect, NULL);

static ssize_t show_rx_vout(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;
	int rx_vout = 0;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	wls_rx_get_rx_vout(pWlc->wls_dev, &rx_vout);

	return sprintf(buf, "%d\n", rx_vout);
}
static DEVICE_ATTR(get_rx_vout, 0444, show_rx_vout, NULL);

static ssize_t wls_input_current_limit_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long r;
	unsigned long wls_input_curr_max;
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}

	r = kstrtoul(buf, 0, &wls_input_curr_max);
	if (r) {
		wlc_err("Invalid current = %lu\n", wls_input_curr_max);
		return -EINVAL;
	}

	if (wls_input_curr_max > pWlc->config.MaxI) {
		wls_input_curr_max = pWlc->config.MaxI;
	}
	if (wls_input_curr_max > 0) {
		wlc_hal_set_input_current(pWlc->alg, CHG1, wls_input_curr_max * 1000);
	}

	wlc_info("wls input_current = %lu pWlc->MaxI = %dmA\n",
				wls_input_curr_max, pWlc->config.MaxI);
	pWlc->ctl.input_current_max = wls_input_curr_max;
	return r ? r : count;
}

static ssize_t wls_input_current_limit_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;
	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}

	return sprintf(buf, "%d\n", pWlc->ctl.input_current_max);
}
static DEVICE_ATTR(wls_input_current_limit, S_IRUGO|S_IWUSR, wls_input_current_limit_show, wls_input_current_limit_store);


static ssize_t show_wlc_fan_speed(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;
	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	return sprintf(buf, " %d\n", pWlc->ctl.fan_speed);
}

static ssize_t store_wlc_fan_speed(struct device *dev,
			struct device_attribute *attr,
			const char *buf,
			size_t count)
{
	struct moto_wlc *pWlc = dev->driver_data;
	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	if (!pWlc->data.moto_stand) {
		wlc_err("[%s] not moto 50w dock %d, skip\n", __func__ , pWlc->data.mode_type);
		return count;
	}

	pWlc->ctl.fan_speed = simple_strtoul(buf, NULL, 0);

	return count;
}
static DEVICE_ATTR(wlc_fan_speed, S_IRUGO|S_IWUSR, show_wlc_fan_speed, store_wlc_fan_speed);

static ssize_t show_wlc_light_ctl(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	return sprintf(buf, " %d\n", pWlc->ctl.light_level);
}

static ssize_t store_wlc_light_ctl(struct device *dev,
			struct device_attribute *attr,
			const char *buf,
			size_t count)
{
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	if (!pWlc->data.moto_stand) {
		wlc_err("[%s] not moto dock %d, skip\n",
				__func__ , pWlc->data.mode_type);
		return count;
	}

	pWlc->ctl.light_level = simple_strtoul(buf, NULL, 0);

	return count;
}
static DEVICE_ATTR(wlc_light_ctl, S_IRUGO|S_IWUSR, show_wlc_light_ctl, store_wlc_light_ctl);

static ssize_t show_wlc_tx_power(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;
	int power = 0;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	wls_rx_get_rx_neg_power(pWlc->wls_dev, &power);

	return sprintf(buf, "%d\n", power);
}
static DEVICE_ATTR(wlc_tx_power, 0444, show_wlc_tx_power, NULL);

static ssize_t show_wlc_tx_type(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	if (pWlc->data.moto_stand)
		return sprintf(buf, "%d\n", WLC_MOTO);
	else
		return sprintf(buf, "%d\n", pWlc->data.mode_type);
}

static DEVICE_ATTR(wlc_tx_type, 0444, show_wlc_tx_type, NULL);

static ssize_t show_wlc_st_changed(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	wlc_err("%s status:%d\n", __func__, pWlc->data.wlc_status);
	return sprintf(buf, "%d\n", pWlc->data.wlc_status);
}

static DEVICE_ATTR(wlc_st_changed, S_IRUGO, show_wlc_st_changed, NULL);

static ssize_t show_wlc_tx_capability(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}

	return sprintf(buf, "%d\n", pWlc->auth.wlc_tx_capability);
}

static DEVICE_ATTR(wlc_tx_capability, 0444, show_wlc_tx_capability, NULL);

static ssize_t show_wlc_tx_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}

	return sprintf(buf, "%d\n", pWlc->auth.wlc_tx_id);
}

static DEVICE_ATTR(wlc_tx_id, 0444, show_wlc_tx_id, NULL);

static ssize_t show_wlc_tx_sn(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}

	return sprintf(buf, "%d\n", pWlc->auth.wlc_tx_sn);
}
static DEVICE_ATTR(wlc_tx_sn, 0444, show_wlc_tx_sn, NULL);


static ssize_t factory_wireless_en_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long r;
	unsigned long en;
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}

	r = kstrtoul(buf, 0, &en);
	if (r) {
		wlc_err("Invalid factory_wls_en = %lu\n", en);
		return -EINVAL;
	}
	pWlc->ctl.factory_wls_en = en;

	return r ? r : count;
}

static ssize_t factory_wireless_en_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;
	if (IS_ERR_OR_NULL(pWlc) || IS_ERR_OR_NULL(pWlc->wls_dev)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}

	return sprintf(buf, "%d\n", pWlc->ctl.factory_wls_en);
}
static DEVICE_ATTR(factory_wireless_en, S_IRUGO|S_IWUSR, factory_wireless_en_show, factory_wireless_en_store);


int wls_device_node_create(struct device *dev)
{
	if (IS_ERR_OR_NULL(dev)) {
		wlc_err("%s dev is ERR or NULL\n", __func__);
		return -ENODEV;
	}
	wlc_info("%s\n", __func__);

//-----------------------program---------------------
	device_create_file(dev, &dev_attr_wireless_fw_version);
	device_create_file(dev, &dev_attr_wireless_fw_update);

//-----------------------factory wireless test-------
	device_create_file(dev, &dev_attr_factory_wireless_en);

//-----------------------RX--------------------------
	device_create_file(dev, &dev_attr_get_rx_irect);
	device_create_file(dev, &dev_attr_get_rx_vrect);
	device_create_file(dev, &dev_attr_get_rx_vout);
	device_create_file(dev, &dev_attr_mode_select);
	device_create_file(dev, &dev_attr_wls_input_current_limit);

	device_create_file(dev, &dev_attr_wlc_fan_speed);
	device_create_file(dev, &dev_attr_wlc_light_ctl);
	device_create_file(dev, &dev_attr_wlc_tx_power);
	device_create_file(dev, &dev_attr_wlc_tx_type);
	device_create_file(dev, &dev_attr_wlc_st_changed);

//-----------------------MOTO WLC2.0-------------------
	device_create_file(dev, &dev_attr_wlc_tx_capability);
	device_create_file(dev, &dev_attr_wlc_tx_id);
	device_create_file(dev, &dev_attr_wlc_tx_sn);

	return 0;
}
