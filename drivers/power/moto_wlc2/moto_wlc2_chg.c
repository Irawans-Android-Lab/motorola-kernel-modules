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

int wls_chg_event_handler(struct wireless_device* wls_dev, struct wls_event_msg *msg)
{
	struct moto_wlc *wlc = NULL;

	wlc = (struct moto_wlc *)wls_dev->driver_data;

	if (IS_ERR_OR_NULL(wlc))
		return -1;

	if (msg == NULL)
		return -1;

	pr_info("%s event:%d len=%d\n", __func__ , msg->event, msg->len);
	switch(msg->event) {
		case WLS_EVENT_TX_DETECTED:
			break;
		case WLS_EVENT_RX_POWER_ON:
			break;
		case WLS_EVENT_RX_NEGO_POWER_READY:
			break;
		case WLS_EVENT_RX_LDO_ON:
			break;
		case WLS_EVENT_HS_OK:
			wlc->data.moto_stand = true;
			break;
		case WLS_EVENT_HS_FAIL:
			wlc->data.moto_stand = false;
			break;
		case WLS_EVENT_RX_FSK_PKT:
			break;
		default:
			break;
	}
	power_supply_changed(wlc->wls_psy);

	return 0;
}


struct wireless_device * wls_get_wireless_device(struct moto_wlc *wlc)
{
	//pr_info("%s\n", __func__);

	if (IS_ERR_OR_NULL(wlc))
		return NULL;

	if (!IS_ERR_OR_NULL(wlc->wls_dev))
		return wlc->wls_dev;
	else {
		wlc->wls_dev = get_wireless_by_name("moto_wlc2");
		if (!IS_ERR_OR_NULL(wlc->wls_dev)) {
			wlc->callback_ops.event_handler = wls_chg_event_handler;
			wireless_dev_set_drvdata(wlc->wls_dev, (void *)wlc);
			wireless_dev_set_callback(wlc->wls_dev, (void *)&wlc->callback_ops);
			wls_rx_set_irq_enable(wlc->wls_dev, true);
			pr_info("%s wlc->wls_dev=%p\n", __func__, wlc->wls_dev);
			return wlc->wls_dev;
		} else {
			pr_info("%s get wls dev failed\n", __func__);
		}
	}

	return NULL;
}

enum power_supply_property wls_chg_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_POWER_NOW,
};

int wls_chg_get_property(struct power_supply *psy,
			enum power_supply_property psp,
			union power_supply_propval *val)
{
	struct moto_wlc *wlc = power_supply_get_drvdata(psy);
	int ret = 0;
	int sys_mode = 0;

	if (IS_ERR_OR_NULL(wlc)) {
		return -EINVAL;
	}
	wls_get_wireless_device(wlc);
	if (IS_ERR_OR_NULL(wlc->wls_dev)) {
		val->intval = -1;
		return -EINVAL;
	}
	switch(psp){
		case POWER_SUPPLY_PROP_PRESENT:
		case POWER_SUPPLY_PROP_ONLINE:
			wls_rx_get_sys_mode(wlc->wls_dev, &sys_mode);
			pr_info("%s online:%d\n", __func__, sys_mode == SYS_MODE_RX);
			if (sys_mode == SYS_MODE_RX)
				val->intval = 1;
			else {
				val->intval = 0;
			}
			break;

		case POWER_SUPPLY_PROP_TYPE:
			val->intval = wlc->wls_psd.type;
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_MAX:
			val->intval = 12000000;//uV
			break;

		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			wls_rx_get_rx_vout(wlc->wls_dev, &val->intval);
			if (val->intval > 0) {
				val->intval = val->intval * 1000;
				pr_info("%s rx_vout: %d\n", __func__, val->intval);
			} else
				val->intval = -1;
			break;

		case POWER_SUPPLY_PROP_CURRENT_MAX:
			val->intval = 1250000;//uA
			break;

		case POWER_SUPPLY_PROP_CURRENT_NOW:
			wls_rx_get_rx_iout(wlc->wls_dev, &val->intval);
			if (val->intval > 0) {
				val->intval = val->intval * 1000;
				pr_info("%s rx_iout: %d\n", __func__, val->intval);
			} else
				val->intval = -1;
			break;

		case POWER_SUPPLY_PROP_POWER_NOW:
			wls_rx_get_rx_neg_power(wlc->wls_dev, &val->intval);
			if (val->intval > 0) {
				pr_info("%s power_now:%d\n", __func__, val->intval);
			} else
				val->intval = -1;
			break;

		default:
			ret = -EINVAL;
			break;
	}
	wlc_info("[%s] psp = %d val = %d.\n", __func__, psp,val->intval);
	return ret;
}

void wls_charger_external_power_changed(struct power_supply *psy)
{
}

static char *wls_psy_supplied_to[] = {
	"battery",
	"mtk-master-charger",
};

int wls_chg_register_psy(struct moto_wlc *wlc)
{
	struct power_supply_config wls_psy_cfg = {};

	wlc->wls_psd.name = "wireless";
	wlc->wls_psd.type = POWER_SUPPLY_TYPE_WIRELESS;
	wlc->wls_psd.properties = wls_chg_props;
	wlc->wls_psd.num_properties = ARRAY_SIZE(wls_chg_props);
	wlc->wls_psd.get_property = wls_chg_get_property;
	wlc->wls_psd.external_power_changed = wls_charger_external_power_changed;

	wls_psy_cfg.drv_data = wlc;
	wls_psy_cfg.of_node = wlc->pdev->dev.of_node;
	wls_psy_cfg.supplied_to = wls_psy_supplied_to;
	wls_psy_cfg.num_supplicants = ARRAY_SIZE(wls_psy_supplied_to);
	wlc->wls_psy = power_supply_register(&wlc->pdev->dev,
								&wlc->wls_psd,
								&wls_psy_cfg);
	if (IS_ERR(wlc->wls_psy)) {
		return PTR_ERR(wlc->wls_psy);
	}

	return 0;
}
