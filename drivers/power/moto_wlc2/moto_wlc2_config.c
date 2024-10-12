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


struct tags_bootmode {
	uint32_t size;
	uint32_t tag;
	uint32_t bootmode;
	uint32_t boottype;
};

int wls_get_secure_hardware(struct moto_wlc *wlc, struct device *dev)
{
	struct device_node *np = of_find_node_by_path("/chosen");
	const char *mmi_bootconfig = NULL;

	if (!IS_ERR_OR_NULL(np)) {
		if (!of_property_read_string(np, "mmi,bootconfig", &mmi_bootconfig)) {
			pr_debug("%s mmi_bootconfig=%s\n", __func__, mmi_bootconfig);
			if (strstr(mmi_bootconfig, "secure_hardware=1"))
				wlc->config.secure_hardware = true;
			pr_info("%s secure_hardware=%d\n", __func__, wlc->config.secure_hardware);
		} else {
			pr_err("%s mmi,bootconfig read failed\n", __func__);
		}
		of_node_put(np);
	} else {
		pr_err("%s chosen is error or null\n", __func__);
	}

	return wlc->config.secure_hardware;
}

int wls_get_bootmode(struct moto_wlc *wlc, struct device *dev)
{
	struct device_node *node = dev->of_node;
	struct device_node *boot_node = NULL;
	struct tags_bootmode *tag = NULL;

	boot_node = of_parse_phandle(node, "bootmode", 0);
	if (IS_ERR_OR_NULL(boot_node))
		pr_info("%s: failed to get boot mode phandle\n", __func__);
	else {
		tag = (struct tags_bootmode *)of_get_property(boot_node, "atag,boot", NULL);
		if (!tag)
			pr_info("%s: failed to get atag,boot\n", __func__);
		else {
			pr_info("%s: size:0x%x tag:0x%x bootmode:0x%x\n",
					__func__, tag->size, tag->tag, tag->bootmode);
			wlc->config.bootmode = tag->bootmode;
		}
	}

	return wlc->config.bootmode;
}

int wls_get_sys_config(struct moto_wlc *wlc, struct device *dev)
{
	struct device_node *node = dev->of_node;

	wlc->config.enable_stop_epp = 0x00;
	of_property_read_u32(node, "enable-stop-epp", &wlc->config.enable_stop_epp);
	pr_info("[%s] enable-stop-epp %d\n", __func__, wlc->config.enable_stop_epp);

	wlc->config.enable_bat_full_stop_epp = 0x00;
	of_property_read_u32(node, "enable-bat-full-stop-epp", &wlc->config.enable_bat_full_stop_epp);
	pr_info("[%s] enable-bat-full-stop-epp %d\n", __func__, wlc->config.enable_bat_full_stop_epp);

	wlc->config.fw_update_soc_limit = 10;
	of_property_read_u32(node, "fw-update-soc-limit", &wlc->config.fw_update_soc_limit);
	pr_info("[%s] fw-update-soc-limit %d\n", __func__, wlc->config.fw_update_soc_limit);

	return 0;
}

int wls_get_bpp_config(struct moto_wlc *wlc, struct device *dev)
{
	struct device_node *node = dev->of_node;

	wlc->config.bpp_icl_min_uA = 300000; //300mA
	wlc->config.bpp_icl_max_uA = 1000000;//1000mA
	wlc->config.bpp_icl_step_uA = 100000;//100mA
	wlc->config.bpp_step_delay_ms = 100; //100ms

	of_property_read_u32(node, "bpp-icl-min-uA", &wlc->config.bpp_icl_min_uA);
	of_property_read_u32(node, "bpp-icl-max-uA", &wlc->config.bpp_icl_max_uA);
	of_property_read_u32(node, "bpp-icl-step-uA", &wlc->config.bpp_icl_step_uA);
	of_property_read_u32(node, "bpp-step-delay-ms", &wlc->config.bpp_step_delay_ms);

	pr_info("[%s] bpp icl_min:%duA max:%duA step:%duA delay:%dms\n", __func__,
			wlc->config.bpp_icl_min_uA, wlc->config.bpp_icl_max_uA,
			wlc->config.bpp_icl_step_uA, wlc->config.bpp_step_delay_ms);

	return 0;
}

int wls_get_epp_config(struct moto_wlc *wlc, struct device *dev)
{
	struct device_node *node = dev->of_node;

	pr_info("%s\n", __func__);
	wlc->config.epp_icl_min_uA = 300000; //300mA
	wlc->config.epp_icl_max_uA = 1250000;//1250mA
	wlc->config.epp_icl_step_uA = 100000;//100mA
	wlc->config.epp_step_delay_ms = 100; //100ms

	of_property_read_u32(node, "epp-icl-min-uA", &wlc->config.epp_icl_min_uA);
	of_property_read_u32(node, "epp-icl-max-uA", &wlc->config.epp_icl_max_uA);
	of_property_read_u32(node, "epp-icl-step-uA", &wlc->config.epp_icl_step_uA);
	of_property_read_u32(node, "epp-step-delay-ms", &wlc->config.epp_step_delay_ms);

	pr_info("[%s] epp icl_min:%duA max:%duA step:%duA delay:%dms\n", __func__,
			wlc->config.epp_icl_min_uA, wlc->config.epp_icl_max_uA,
			wlc->config.epp_icl_step_uA, wlc->config.epp_step_delay_ms);

	return 0;
}

int wls_get_offset_detect_config(struct moto_wlc *wlc, struct device *dev)
{
	struct device_node *node = dev->of_node;

	wlc->config.enable_rx_offset_detect = 0x00;
	of_property_read_u32(node, "enable-rx-offset-detect", &wlc->config.enable_rx_offset_detect);
	pr_info("[%s] enable-rx-offset-detect %d\n", __func__, wlc->config.enable_rx_offset_detect);

	wlc->config.rod_stop_battery_soc = 90;
	of_property_read_u32(node, "rod-stop-battery-soc", &wlc->config.rod_stop_battery_soc);
	pr_info("[%s] rod-stop-battery-soc %d\n", __func__, wlc->config.rod_stop_battery_soc);

	return 0;
}

int wls_get_tx_mode_config(struct moto_wlc *wlc, struct device *dev)
{
	struct device_node *node = dev->of_node;

	wlc->config.wls_boost_support = of_property_read_bool(node, "wireless-boost-support");
	pr_info("[%s] wireless-boost-support %d\n", __func__, wlc->config.wls_boost_support);

	wlc->config.wls_tx_support = of_property_read_bool(node, "wireless-tx-support");
	pr_info("[%s] wireless-tx-support %d\n", __func__, wlc->config.wls_tx_support);

	if (!wlc->config.wls_tx_support) {
		return 0;
	}

	wlc->config.config_otg_support = of_property_read_bool(node, "config-otg-support");
	pr_info("[%s] config-otg-support %d\n", __func__, wlc->config.config_otg_support);

	if (wlc->config.config_otg_support) {
		wlc->config.config_otg_vout = 5000000; //default 5V
		wlc->config.config_otg_iout = 1000000; //default 1A
		of_property_read_u32(node, "config-otg-vout", &wlc->config.config_otg_vout);
		pr_info("[%s] config-otg-vout %d\n", __func__, wlc->config.config_otg_vout);
		of_property_read_u32(node, "config-otg-iout", &wlc->config.config_otg_iout);
		pr_info("[%s] config-otg-iout %d\n", __func__, wlc->config.config_otg_iout);
	}

	return 0;
}


int wls_get_auto_config(struct moto_wlc *wlc, struct device *dev)
{
	struct device_node *node = dev->of_node;

	wlc->config.enable_wls_auto_switch = 0x00;
	of_property_read_u32(node, "enable-wls-auto-switch", &wlc->config.enable_wls_auto_switch);
	pr_info("[%s] enable-wls-auto-switch %d\n", __func__, wlc->config.enable_wls_auto_switch);

	if (wlc->config.enable_wls_auto_switch) {
		wlc->config.wls_auto_switch_overtemp = 430;
		of_property_read_u32(node, "wls-auto-switch-overtemp", &wlc->config.wls_auto_switch_overtemp);
		pr_info("[%s] wls-auto-switch-overtemp %d\n", __func__, wlc->config.wls_auto_switch_overtemp);
	}

	wlc->config.enable_wls_auto_stop = 0x00;
	of_property_read_u32(node, "enable-wls-auto-stop", &wlc->config.enable_wls_auto_stop);
	pr_info("[%s] enable-wls-auto-stop %d\n", __func__, wlc->config.enable_wls_auto_stop);

	if (wlc->config.enable_wls_auto_stop) {
		wlc->config.wls_auto_stop_overtemp = 450;
		of_property_read_u32(node, "wls-auto-stop-overtemp", &wlc->config.wls_auto_stop_overtemp);
		pr_info("[%s] wls-auto-stop-overtemp %d\n", __func__, wlc->config.wls_auto_stop_overtemp);

		wlc->config.wls_auto_stop_undertemp = 400;
		of_property_read_u32(node, "wls-auto-stop-undertemp", &wlc->config.wls_auto_stop_undertemp);
		pr_info("[%s] wls-auto-stop-undertemp %d\n", __func__, wlc->config.wls_auto_stop_undertemp);
	}

	return 0;
}

int wls_config_parse_dts(struct moto_wlc *wlc, struct device *dev)
{
	struct device_node *node = dev->of_node;

	if (IS_ERR_OR_NULL(wlc)) {
		pr_err("%s: wlc is ERR or NULL \n" , __func__);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(node)) {
		pr_err("%s: devices tree node missing \n" , __func__);
		return -EINVAL;
	}

	wls_get_secure_hardware(wlc, dev);

	wls_get_bootmode(wlc, dev);

	wls_get_sys_config(wlc, dev);

	wls_get_bpp_config(wlc, dev);

	wls_get_epp_config(wlc, dev);

	wls_get_tx_mode_config(wlc, dev);

	wls_get_auto_config(wlc, dev);

	wls_get_offset_detect_config(wlc, dev);

	return 0;
}