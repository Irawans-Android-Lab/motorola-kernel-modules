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

static int factory_test_wls_en(void *input, bool en);

void wls_device_pm_set_awake(struct moto_wlc *wlc, bool awake)
{
	if (IS_ERR_OR_NULL(wlc->fw_update_wake_lock))
		return;

	pr_info("%s %d\n", __func__, awake);
	if (!wlc->fw_update_wake_lock->active && awake) {
		__pm_stay_awake(wlc->fw_update_wake_lock);
	} else if(wlc->fw_update_wake_lock->active && !awake) {
		__pm_relax(wlc->fw_update_wake_lock);
	}
}

bool wls_device_fw_set_boost(struct moto_wlc *wlc, bool en)
{
	int ret = 0;
	int vbus = 0;
	struct charger_device *chg_psy = NULL;

	wls_chg_mmi_mux_chan_set(MMI_MUX_CHANNEL_WLC_FW_UPDATE, en);

	if (!wlc->config.wls_boost_support) {
		chg_psy = get_charger_by_name("primary_chg");
		if (IS_ERR_OR_NULL(chg_psy)) {
			wlc_err("%s Couldn't get chg_psy\n", __func__);
			return false;
		}
		ret = charger_dev_enable_otg(chg_psy, en);
		if(ret < 0){
			wlc_err("%s set otg fail\n", __func__);
			return false;
		}
		msleep(100);
		vbus = wlc_hal_get_vbus(wlc->alg) / 1000;
		wlc_info("%s vbus:%dmV\n", __func__, vbus);
		if (en && vbus < VBUS_VALID_MV) {
			wlc_err("%s enable otg fail\n", __func__);
			return false;
		} else if(!en && vbus >= VBUS_VALID_MV) {
			wlc_err("%s disable otg fail\n", __func__);
			return false;
		}
	}

	return true;
}

void wls_device_fw_update_work(struct work_struct *work)
{
	struct moto_wlc *wlc =
		container_of((struct delayed_work*)work, struct moto_wlc, fw_update_work);
	union power_supply_propval prop = {0x00};
	int chip_id = 0x00;
	int rt = 0;
	int vbus = 0;
	int soc = 0;
	bool boost_flag = false;

	if (IS_ERR_OR_NULL(wlc)) {
		wlc_err("can't get wlc\n");
		return;
	}

	if (IS_ERR_OR_NULL(wlc->wls_dev)) {
		wlc_err("can't get wls_dev\n");
		return;
	}

	if (wlc_hal_get_bat_property(wlc->alg, POWER_SUPPLY_PROP_CAPACITY, &prop)) {
		wlc_err("can't get battery capacity\n");
		return;
	}

	soc = prop.intval;
	vbus = wlc_hal_get_vbus(wlc->alg) / 1000;

	if ((vbus > VBUS_VALID_MV/2) && wlc->config.secure_hardware) {
		wlc_info("%s Skip FW update when secure phone has charger plug-in\n", __func__);
		return;
	} else if ((vbus < VBUS_VALID_MV/2) &&
			soc < wlc->config.fw_update_soc_limit &&
			!wlc->ctl.fw_update_force) {
		wlc_info("%s Wireless fw update failed. Battery SOC should be at least %d%%\n",
				__func__, wlc->config.fw_update_soc_limit);
		return;
	}

	//read chip
	rt = wls_rx_get_chip_id(wlc->wls_dev, &chip_id);
	wlc_info("%s chip_id=%d, rt=%d\n", __func__, chip_id, rt);

	if (rt) //if iic err, need boost
		boost_flag = true;

	wlc->ctl.fw_uploading = true;
	wls_device_pm_set_awake(wlc, true);
	if (boost_flag) {
		wls_device_fw_set_boost(wlc, true);
	}

	rt = wls_rx_get_chip_id(wlc->wls_dev, &chip_id);// recheck iic
	wlc_info("%s chip_id=%d rt=%d\n", __func__, chip_id, rt);
	if (rt) {
		wlc_err("%s Error: boost failed or usb plug-out, rt=%d\n", __func__, rt);
		goto fw_update_exit;
	}
	wls_rx_set_fw_update(wlc->wls_dev, wlc->ctl.fw_update_force);

fw_update_exit:
	if (boost_flag) {
		wls_device_fw_set_boost(wlc, false);
	}

	wlc->ctl.fw_uploading = false;
	wls_device_pm_set_awake(wlc, false);
}

int wls_device_uisoc_change(struct moto_wlc *wlc, int uisoc)
{
	if (uisoc == 100 || wlc->data.uisoc == 100) {
		queue_delayed_work(wlc->wls_wq, &wlc->light_fan_work, msecs_to_jiffies(0));
	}

	return 0;
}

int wls_device_update_light_fan(struct moto_wlc *wlc)
{
	int status = 0;
	uint8_t data[4] = {0x38, 0x05, 0x40, 0x28};
	int retry = 2;

	if (100 == wlc_hal_get_uisoc(wlc->alg)) {
		if (wlc->ctl.light_level == 0)
			data[2] = MMI_DOCK_LIGHT_OFF;
		else
			data[2] = MMI_DOCK_LIGHT_ON;
		data[3] = MMI_DOCK_FAN_SPEED_OFF;
	} else {
		if (wlc->ctl.fan_speed == 0)
			data[3] = MMI_DOCK_FAN_SPEED_LOW;
		else
			data[3] = MMI_DOCK_FAN_SPEED_HIGH;

		if (wlc->ctl.light_level == 0)
			data[2] = MMI_DOCK_LIGHT_OFF;
		else
			data[2] = MMI_DOCK_LIGHT_DEFAULT;
	}

	do {
		status = wls_rx_send_ask_packet(wlc->wls_dev, data, sizeof(data));
		wlc_info("%s: QI set fan/light, FAN_SPEED %d, LIGHT %d",
					__func__, wlc->ctl.fan_speed, wlc->ctl.light_level);
		wlc_info("%s: QI set fan/light, ight 0x%x, fan 0x%x", __func__, data[2], data[3]);
		msleep(200);
		retry--;
	} while (retry);

	return status;
}

int wls_device_notify_tx_chgfull(struct moto_wlc *wlc)
{
	int status = 0;
	uint8_t data[2] = {0x5, 0x64};
	int retry = 3;

	do {
		status = wls_rx_send_ask_packet(wlc->wls_dev, data, sizeof(data));
		wlc_info("%s: QI notify TX battery full , head 0x%x, cmd 0x%x, status %d\n",
				__func__, data[0], data[1], status);
		msleep(200);
		retry--;
	} while(retry);

	return status;
}

void wls_device_light_fan_work(struct work_struct *work)
{
	struct moto_wlc *wlc =
		container_of((struct delayed_work*)work, struct moto_wlc, light_fan_work);

	pr_info("%s hs_st:%d auth_done:%d\n", __func__, wlc->auth.hs_st, wlc->auth.auth_done);
	if (wlc->auth.hs_st == AUTH_HS_OK) {
		wls_device_update_light_fan(wlc);
	} else if (100 == wlc_hal_get_uisoc(wlc->alg)) {
		wls_device_notify_tx_chgfull(wlc);
	}
}

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
	} else if (!pWlc->ctl.fw_uploading) {
		pWlc->ctl.fw_update_force = (update == FORCE_FW_UPDATE ? true : false);
		wlc_info("%s pWlc->ctl.fw_update_force=%d\n", __func__, pWlc->ctl.fw_update_force);
		queue_delayed_work(pWlc->wls_wq,
				&pWlc->fw_update_work, msecs_to_jiffies(2000));
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
	wlc_info("[%s] fan_speed %d\n", __func__ , pWlc->ctl.fan_speed);
	queue_delayed_work(pWlc->wls_wq, &pWlc->light_fan_work, msecs_to_jiffies(0));

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
	wlc_info("[%s] light_level %d\n", __func__ , pWlc->ctl.light_level);
	queue_delayed_work(pWlc->wls_wq, &pWlc->light_fan_work, msecs_to_jiffies(0));

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

	factory_test_wls_en(pWlc, en);

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

static ssize_t inhibit_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	unsigned long r;
	unsigned long en;
	struct moto_wlc *pWlc = dev->driver_data;

	if (IS_ERR_OR_NULL(pWlc)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}

	r = kstrtoul(buf, 0, &en);
	if (r) {
		wlc_err("Invalid inhibit = %lu\n", en);
		return -EINVAL;
	}

	if (gpio_is_valid(pWlc->wls_control_en)) {
		gpio_set_value(pWlc->wls_control_en, en);
		r = gpio_get_value(pWlc->wls_control_en);
	}

	return r ? r : count;
}

static ssize_t inhibit_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct moto_wlc *pWlc = dev->driver_data;
	int rt = -EINVAL;

	if (IS_ERR_OR_NULL(pWlc)) {
		wlc_err("%s: chip not valid\n", __func__);
		return -ENODEV;
	}
	if (gpio_is_valid(pWlc->wls_control_en)) {
		rt = gpio_get_value(pWlc->wls_control_en);
	}

	return sprintf(buf, "%d\n", rt);
}
static DEVICE_ATTR(inhibit, S_IRUGO|S_IWUSR, inhibit_show, inhibit_store);

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
	device_create_file(dev, &dev_attr_inhibit);

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

static int wireless_get_chip_id(void *input)
{
	struct moto_wlc *wlc = NULL;
	int rt = 0;
	int retry = 0;

	wlc_info("%s input=%p\n", __func__, input);
	wlc = (struct moto_wlc *) input;
	if (IS_ERR_OR_NULL(wlc)) {
		wlc_err("%s wlc is err or null\n", __func__);
		return -1;
	}

	if (wlc->data.chip_id == wlc->config.chip_id) {
		return wlc->config.chip_id;
	}

	rt = wls_rx_get_chip_id(wlc->wls_dev, &wlc->data.chip_id);
	wlc_info("%s chip_id=%d rt=%d\n", __func__, wlc->data.chip_id, rt);
	if (!rt) { //IIC ok
		return wlc->data.chip_id;
	}

	if ((wlc_hal_get_vbus(wlc->alg) / 1000) < (VBUS_VALID_MV / 2)) {
		wls_device_fw_set_boost(wlc, true);
		while (retry < 3 && (wlc->data.chip_id != wlc->config.chip_id)) {
			msleep(50);
			rt = wls_rx_get_chip_id(wlc->wls_dev, &wlc->data.chip_id);
			retry ++;
		}
		wls_device_fw_set_boost(wlc, false);
	}

	wlc_info("%s chip_id=0x%X, retry=%d\n", __func__, wlc->data.chip_id, retry);

	return wlc->data.chip_id;
}

static int factory_test_wls_en(void *input, bool en)
{
	struct moto_wlc *wlc = NULL;
	int ret = 0;
	int wait = 0;

	wlc = (struct moto_wlc *) input;
	if (IS_ERR_OR_NULL(wlc)) {
		wlc_err("%s wlc is err or null\n", __func__);
		return -1;
	}

	if (en) {
		if (wlc->ctl.factory_wls_en == false) {
			wlc->ctl.factory_wls_en = true;
			ret = wls_chg_mmi_mux_chan_set(MMI_MUX_CHANNEL_WLC_FACTORY_TEST, true);
		}
	} else {
		if (wlc->ctl.factory_wls_en == true) {
			if (gpio_is_valid(wlc->wls_control_en)) { //inhibit gpio can disable wireless charging
				ret = wls_chg_mmi_mux_chan_set(MMI_MUX_CHANNEL_WLC_FACTORY_TEST, false);
			} else {
				wait = 50;
				while (wait > 0 && wlc->auth.hs_st == AUTH_HS_UNKONWN) {
					msleep(100);
					wait --;
				}
				ret |= wls_rx_set_mode_select(wlc->wls_dev, false);
				wait = 50;
				while (wait > 0 && (wlc_hal_get_vbus(wlc->alg) > 5500000)) {
					msleep(10);
					wait --;
				}
				ret |= wls_chg_mmi_mux_chan_set(MMI_MUX_CHANNEL_WLC_FACTORY_TEST, false);
				ret |= wls_rx_set_mode_select(wlc->wls_dev, true);
				wlc->ctl.factory_wls_en = false;
			}
		}
	}

	wlc_info("wls: factory wls_en %d, ret=%d\n", en, ret);
	return ret;
}


int wls_device_tcmd_register(struct moto_wlc *wlc)
{
	int ret = 0;

	pr_info("%s\n", __func__);
	wlc->wls_tcmd_client.data = wlc;
	wlc->wls_tcmd_client.client_id = MOTO_CHG_TCMD_CLIENT_WLS;

	wlc->wls_tcmd_client.get_chip_id = wireless_get_chip_id;
	wlc->wls_tcmd_client.wls_en = factory_test_wls_en;

	ret = moto_chg_tcmd_register(&wlc->wls_tcmd_client);

	return ret;
}
