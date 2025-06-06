/************************************************************************************
** File:  \\192.168.144.3\Linux_Share\12015\ics2\development\mediatek\custom\oppo77_12015\kernel\battery\battery
** VENDOR_EDIT
** Copyright (C), 2008-2012, OPLUS Mobile Comm Corp., Ltd
**
** Description:
**      for dc-dc sn111008 charg
**
** Version: 1.0
** Date created: 21:03:46,05/04/2012
** Author: Fanhong.Kong@ProDrv.CHG
**
** --------------------------- Revision History: ------------------------------------------------------------
* <version>       <date>        <author>                                <desc>
* Revision 1.0    2015-06-22    Fanhong.Kong@ProDrv.CHG                 Created for new architecture
************************************************************************************************************/

#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#include <linux/xlog.h>
//#include <upmu_common.h>
#include <linux/gpio.h>
//#include <linux/irqchip/mtk-eic.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <mt-plat/mtk_boot_common.h>
#else
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <soc/oppo/boot_mode.h>
#endif

#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include "../oplus_adapter.h"

#include "oplus_vooc_fw.h"

int g_hw_version = 0;
void oplus_vooc_data_irq_init(struct oplus_vooc_chip *chip);

void init_hw_version(void)
{
}

extern	int oplus_vooc_mcu_hwid_check(struct oplus_vooc_chip *chip);
int get_vooc_mcu_type(struct oplus_vooc_chip *chip)
{
	if(chip == NULL){
		chg_err("oplus_vooc_chip is not ready, enable stm8s\n");
		return OPLUS_VOOC_MCU_HWID_STM8S;
	}
	return oplus_vooc_mcu_hwid_check(chip);
}

static int opchg_bq27541_gpio_pinctrl_init(struct oplus_vooc_chip *chip)
{
	chip->vooc_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->vooc_gpio.pinctrl)) {
		chg_err(": %d Getting pinctrl handle failed\n", __LINE__);
		return -EINVAL;
	}
	/* set switch1 is active and switch2 is active*/
	if (1) {
		chip->vooc_gpio.gpio_switch1_act_switch2_act =
			pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_act_switch3_act");
		if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_act_switch2_act)) {
			chg_err(": %d Failed to get the active state pinctrl handle\n", __LINE__);
			return -EINVAL;
		}
		/* set switch1 is sleep and switch2 is sleep*/
		chip->vooc_gpio.gpio_switch1_sleep_switch2_sleep =
			pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_sleep_switch3_sleep");
		if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_sleep_switch2_sleep)) {
			chg_err(": %d Failed to get the suspend state pinctrl handle\n", __LINE__);
			return -EINVAL;
		}

		chip->vooc_gpio.gpio_switch1_ctr1_act =
			pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "charging_switch1_ctr1_active");
		if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_ctr1_act)) {
			chg_err(": %d Failed to get the charging_switch1_ctr1_active handle\n", __LINE__);
			//return -EINVAL;
		}

		chip->vooc_gpio.gpio_switch1_ctr1_sleep =
			pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "charging_switch1_ctr1_sleep");
		if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_ctr1_sleep)) {
			chg_err(": %d Failed to get the charging_switch1_ctr1_sleep handle\n", __LINE__);
			//return -EINVAL;
		}
	} else {
		chip->vooc_gpio.gpio_switch1_act_switch2_act =
			pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_act_switch2_act");
		if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_act_switch2_act)) {
			chg_err(": %d Failed to get the active state pinctrl handle\n", __LINE__);
			return -EINVAL;
		}
		/* set switch1 is sleep and switch2 is sleep*/
		chip->vooc_gpio.gpio_switch1_sleep_switch2_sleep =
			pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_sleep_switch2_sleep");
		if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_sleep_switch2_sleep)) {
			chg_err(": %d Failed to get the suspend state pinctrl handle\n", __LINE__);
			return -EINVAL;
		}
	}
	/* set switch1 is active and switch2 is sleep*/
	chip->vooc_gpio.gpio_switch1_act_switch2_sleep =
		pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_act_switch2_sleep");
	if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_act_switch2_sleep)) {
		chg_err(": %d Failed to get the state 2 pinctrl handle\n", __LINE__);
		return -EINVAL;
	}
	/* set switch1 is sleep and switch2 is active*/
	chip->vooc_gpio.gpio_switch1_sleep_switch2_act =
		pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "switch1_sleep_switch2_act");
	if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_switch1_sleep_switch2_act)) {
			chg_err(": %d Failed to get the state 3 pinctrl handle\n", __LINE__);
		return -EINVAL;
	}
	/* set clock is active*/
	chip->vooc_gpio.gpio_clock_active =
		pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "clock_active");
	if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_clock_active)) {
			chg_err(": %d Failed to get the state 3 pinctrl handle\n", __LINE__);
		return -EINVAL;
	}
	/* set clock is sleep*/
	chip->vooc_gpio.gpio_clock_sleep =
		pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "clock_sleep");
	if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_clock_sleep)) {
		chg_err(": %d Failed to get the state 3 pinctrl handle\n", __LINE__);
		return -EINVAL;
	}
	/* set clock is active*/
	chip->vooc_gpio.gpio_data_active =
		pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "data_active");
	if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_data_active)) {
		chg_err(": %d Failed to get the state 3 pinctrl handle\n", __LINE__);
		return -EINVAL;
	}
	/* set clock is sleep*/
	chip->vooc_gpio.gpio_data_sleep =
		pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "data_sleep");
	if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_data_sleep)) {
		chg_err(": %d Failed to get the state 3 pinctrl handle\n", __LINE__);
		return -EINVAL;
	}
	/* set reset is atcive*/
	chip->vooc_gpio.gpio_reset_active =
		pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "reset_active");
	if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_reset_active)) {
		chg_err(": %d Failed to get the state 3 pinctrl handle\n", __LINE__);
		return -EINVAL;
	}
	/* set reset is sleep*/
	chip->vooc_gpio.gpio_reset_sleep =
		pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "reset_sleep");
	if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_reset_sleep)) {
		chg_err(": %d Failed to get the state 3 pinctrl handle\n", __LINE__);
		return -EINVAL;
	}
	return 0;
}

void oplus_vooc_fw_type_dt(struct oplus_vooc_chip *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc;

	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return ;
	}

	chip->batt_type_4400mv = of_property_read_bool(node, "qcom,oplus_batt_4400mv");
	chip->support_vooc_by_normal_charger_path = of_property_read_bool(node,
		"qcom,support_vooc_by_normal_charger_path");

	rc = of_property_read_u32(node, "qcom,vooc-fw-type", &chip->vooc_fw_type);
	if (rc) {
		chip->vooc_fw_type = VOOC_FW_TYPE_INVALID;
	}

	chg_debug("oplus_vooc_fw_type_dt batt_type_4400 is %d,vooc_fw_type = 0x%x\n",
		chip->batt_type_4400mv, chip->vooc_fw_type);

	chip->vooc_fw_update_newmethod = of_property_read_bool(node,
		"qcom,vooc_fw_update_newmethod");
	chg_debug(" vooc_fw_upate:%d\n", chip->vooc_fw_update_newmethod);

	rc = of_property_read_u32(node, "qcom,vooc-low-temp",
		&chip->vooc_low_temp);
	if (rc) {
		chip->vooc_low_temp = 165;
	} else {
		chg_debug("qcom,vooc-low-temp is %d\n", chip->vooc_low_temp);
	}

	rc = of_property_read_u32(node, "qcom,vooc-little-cool-temp",
			&chip->vooc_little_cool_temp);
	if (rc) {
		chip->vooc_little_cool_temp = 160;
	} else {
		chg_debug("qcom,vooc-little-cool-temp is %d\n", chip->vooc_little_cool_temp);
	}

	rc = of_property_read_u32(node, "qcom,vooc-cool-temp",
			&chip->vooc_cool_temp);
	if (rc) {
		chip->vooc_cool_temp = 120;
	} else {
		chg_debug("qcom,vooc-cool-temp is %d\n", chip->vooc_cool_temp);
	}

	rc = of_property_read_u32(node, "qcom,vooc-little-cool-to-normal-temp",
			&chip->vooc_little_cool_to_normal_temp);
	if (rc) {
		chip->vooc_little_cool_to_normal_temp = 180;
	} else {
		chg_debug("qcom,vooc-little-cool-to-normal-temp is %d\n", chip->vooc_little_cool_to_normal_temp);
	}

	rc = of_property_read_u32(node, "qcom,vooc-normal-to-little-cool-current",
			&chip->vooc_normal_to_little_cool_current);
	if (rc) {
		chip->vooc_normal_to_little_cool_current = 6;
	} else {
		chg_debug("qcom,vooc-normal-to-little-cool-current is %d\n", chip->vooc_normal_to_little_cool_current);
	}

	rc = of_property_read_u32(node, "qcom,vooc-high-temp", &chip->vooc_high_temp);
	if (rc) {
		chip->vooc_high_temp = 430;
	} else {
		chg_debug("qcom,vooc-high-temp is %d\n", chip->vooc_high_temp);
	}

	rc = of_property_read_u32(node, "qcom,vooc-low-soc", &chip->vooc_low_soc);
	if (rc) {
		chip->vooc_low_soc = 1;
	} else {
		chg_debug("qcom,vooc-low-soc is %d\n", chip->vooc_low_soc);
	}

	rc = of_property_read_u32(node, "qcom,vooc-high-soc", &chip->vooc_high_soc);
	if (rc) {
		chip->vooc_high_soc = 85;
	} else {
		chg_debug("qcom,vooc-high-soc is %d\n", chip->vooc_high_soc);
	}
	chip->vooc_multistep_adjust_current_support = of_property_read_bool(node,
		"qcom,vooc_multistep_adjust_current_support");
	chg_debug("qcom,vooc_multistep_adjust_current_supportis %d\n",
		chip->vooc_multistep_adjust_current_support);

	rc = of_property_read_u32(node, "qcom,vooc_multistep_initial_batt_temp",
		&chip->vooc_multistep_initial_batt_temp);
	if (rc) {
		chip->vooc_multistep_initial_batt_temp = 305;
	} else {
		chg_debug("qcom,vooc_multistep_initial_batt_temp is %d\n",
			chip->vooc_multistep_initial_batt_temp);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy_normal_current",
		&chip->vooc_strategy_normal_current);
	if (rc) {
		chip->vooc_strategy_normal_current = 0x03;
	} else {
		chg_debug("qcom,vooc_strategy_normal_current is %d\n",
			chip->vooc_strategy_normal_current);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_batt_low_temp1",
		&chip->vooc_strategy1_batt_low_temp1);
	if (rc) {
		chip->vooc_strategy1_batt_low_temp1  = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy1_batt_low_temp1 is %d\n",
			chip->vooc_strategy1_batt_low_temp1);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_batt_low_temp2",
		&chip->vooc_strategy1_batt_low_temp2);
	if (rc) {
		chip->vooc_strategy1_batt_low_temp2 = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy1_batt_low_temp2 is %d\n",
			chip->vooc_strategy1_batt_low_temp2);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_batt_low_temp0",
		&chip->vooc_strategy1_batt_low_temp0);
	if (rc) {
		chip->vooc_strategy1_batt_low_temp0 = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy1_batt_low_temp0 is %d\n",
			chip->vooc_strategy1_batt_low_temp0);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_batt_high_temp0",
		&chip->vooc_strategy1_batt_high_temp0);
	if (rc) {
		chip->vooc_strategy1_batt_high_temp0 = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy1_batt_high_temp0 is %d\n",
			chip->vooc_strategy1_batt_high_temp0);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_batt_high_temp1",
		&chip->vooc_strategy1_batt_high_temp1);
	if (rc) {
		chip->vooc_strategy1_batt_high_temp1 = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy1_batt_high_temp1 is %d\n",
			chip->vooc_strategy1_batt_high_temp1);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_batt_high_temp2",
		&chip->vooc_strategy1_batt_high_temp2);
	if (rc) {
		chip->vooc_strategy1_batt_high_temp2 = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy1_batt_high_temp2 is %d\n",
			chip->vooc_strategy1_batt_high_temp2);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_high_current0",
		&chip->vooc_strategy1_high_current0);
	if (rc) {
		chip->vooc_strategy1_high_current0  = chip->vooc_strategy_normal_current;
	} else {
		chg_debug("qcom,vooc_strategy1_high_current0 is %d\n",
			chip->vooc_strategy1_high_current0);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_high_current1",
		&chip->vooc_strategy1_high_current1);
	if (rc) {
		chip->vooc_strategy1_high_current1  = chip->vooc_strategy_normal_current;
	} else {
		chg_debug("qcom,vooc_strategy1_high_current1 is %d\n",
			chip->vooc_strategy1_high_current1);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_high_current2",
		&chip->vooc_strategy1_high_current2);
	if (rc) {
		chip->vooc_strategy1_high_current2  = chip->vooc_strategy_normal_current;
	} else {
		chg_debug("qcom,vooc_strategy1_high_current2 is %d\n",
			chip->vooc_strategy1_high_current2);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_low_current2",
		&chip->vooc_strategy1_low_current2);
	if (rc) {
		chip->vooc_strategy1_low_current2  = chip->vooc_strategy_normal_current;
	} else {
		chg_debug("qcom,vooc_strategy1_low_current2 is %d\n",
			chip->vooc_strategy1_low_current2);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_low_current1",
		&chip->vooc_strategy1_low_current1);
	if (rc) {
		chip->vooc_strategy1_low_current1  = chip->vooc_strategy_normal_current;
	} else {
		chg_debug("qcom,vooc_strategy1_low_current1 is %d\n",
			chip->vooc_strategy1_low_current1);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy1_low_current0",
		&chip->vooc_strategy1_low_current0);
	if (rc) {
		chip->vooc_strategy1_low_current0  = chip->vooc_strategy_normal_current;
	} else {
		chg_debug("qcom,vooc_strategy1_low_current0 is %d\n",
			chip->vooc_strategy1_low_current0);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy2_batt_up_temp1",
		&chip->vooc_strategy2_batt_up_temp1);
	if (rc) {
		chip->vooc_strategy2_batt_up_temp1  = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy2_batt_up_temp1 is %d\n",
			chip->vooc_strategy2_batt_up_temp1);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy2_batt_up_down_temp2",
		&chip->vooc_strategy2_batt_up_down_temp2);
	if (rc) {
		chip->vooc_strategy2_batt_up_down_temp2  = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy2_batt_up_down_temp2 is %d\n",
			chip->vooc_strategy2_batt_up_down_temp2);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy2_batt_up_temp3",
		&chip->vooc_strategy2_batt_up_temp3);
	if (rc) {
		chip->vooc_strategy2_batt_up_temp3  = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy2_batt_up_temp3 is %d\n",
			chip->vooc_strategy2_batt_up_temp3);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy2_batt_up_down_temp4",
		&chip->vooc_strategy2_batt_up_down_temp4);
	if (rc) {
		chip->vooc_strategy2_batt_up_down_temp4  = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy2_batt_up_down_temp4 is %d\n",
			chip->vooc_strategy2_batt_up_down_temp4);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy2_batt_up_temp5",
		&chip->vooc_strategy2_batt_up_temp5);
	if (rc) {
		chip->vooc_strategy2_batt_up_temp5  = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy2_batt_up_temp5 is %d\n",
			chip->vooc_strategy2_batt_up_temp5);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy2_batt_up_temp6",
		&chip->vooc_strategy2_batt_up_temp6);
	if (rc) {
		chip->vooc_strategy2_batt_up_temp6  = chip->vooc_multistep_initial_batt_temp;
	} else {
		chg_debug("qcom,vooc_strategy2_batt_up_temp6 is %d\n",
			chip->vooc_strategy2_batt_up_temp6);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy2_high0_current",
		&chip->vooc_strategy2_high0_current);
	if (rc) {
		chip->vooc_strategy2_high0_current	= chip->vooc_strategy_normal_current;
	} else {
		chg_debug("qcom,vooc_strategy2_high0_current is %d\n",
			chip->vooc_strategy2_high0_current);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy2_high1_current",
		&chip->vooc_strategy2_high1_current);
	if (rc) {
		chip->vooc_strategy2_high1_current	= chip->vooc_strategy_normal_current;
	} else {
		chg_debug("qcom,vooc_strategy2_high1_current is %d\n",
			chip->vooc_strategy2_high1_current);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy2_high2_current",
		&chip->vooc_strategy2_high2_current);
	if (rc) {
		chip->vooc_strategy2_high2_current	= chip->vooc_strategy_normal_current;
	} else {
		chg_debug("qcom,vooc_strategy2_high2_current is %d\n",
			chip->vooc_strategy2_high2_current);
	}

	rc = of_property_read_u32(node, "qcom,vooc_strategy2_high3_current",
		&chip->vooc_strategy2_high3_current);
	if (rc) {
		chip->vooc_strategy2_high3_current	= chip->vooc_strategy_normal_current;
	} else {
		chg_debug("qcom,vooc_strategy2_high3_current is %d\n",
			chip->vooc_strategy2_high3_current);
	}

	rc = of_property_read_u32(node, "qcom,vooc_batt_over_high_temp",
		&chip->vooc_batt_over_high_temp);
	if (rc) {
		chip->vooc_batt_over_high_temp = -EINVAL;
	} else {
		chg_debug("qcom,vooc_batt_over_high_temp is %d\n",
			chip->vooc_batt_over_high_temp);
	}

	rc = of_property_read_u32(node, "qcom,vooc_batt_over_low_temp",
		&chip->vooc_batt_over_low_temp);
	if (rc) {
		chip->vooc_batt_over_low_temp = -EINVAL;
	} else {
		chg_debug("qcom,vooc_batt_over_low_temp is %d\n",
			chip->vooc_batt_over_low_temp);
	}

	rc = of_property_read_u32(node, "qcom,vooc_over_high_or_low_current",
		&chip->vooc_over_high_or_low_current);
	if (rc) {
		chip->vooc_over_high_or_low_current = -EINVAL;
	} else {
		chg_debug("qcom,vooc_over_high_or_low_current is %d\n",
			chip->vooc_over_high_or_low_current);
	}
}

/*This is only for P60 P(17197 P)*/
#ifdef CONFIG_OPLUS_CHARGER_MTK6771
extern int main_hwid5_val;
#endif

int oplus_vooc_mcu_hwid_check(struct oplus_vooc_chip *chip)
{
	int rc;
	static int mcu_hwid_type = -1;
	struct device_node *node = NULL;

/*This is only for P60 P(17197 P)*/
#ifdef CONFIG_OPLUS_CHARGER_MTK6771
	return main_hwid5_val;
#endif

	if(mcu_hwid_type != -1) {
		chg_debug("mcu_hwid_type[%d]\n", mcu_hwid_type);
		return mcu_hwid_type;
	}

	if(chip == NULL){
		chg_err("oplus_vooc_chip is not ready, enable stm8s\n");
			mcu_hwid_type = OPLUS_VOOC_MCU_HWID_STM8S;
		return OPLUS_VOOC_MCU_HWID_STM8S;
	}

	node = chip->dev->of_node;
	/* Parsing gpio swutch1*/
	chip->vooc_gpio.vooc_mcu_id_gpio = of_get_named_gpio(node,
		"qcom,vooc_mcu_id-gpio", 0);
	if (chip->vooc_gpio.vooc_mcu_id_gpio < 0) {
		chg_err("chip->vooc_gpio.vooc_mcu_id_gpio not specified, enable stm8s\n");
			mcu_hwid_type = OPLUS_VOOC_MCU_HWID_STM8S;
		return OPLUS_VOOC_MCU_HWID_STM8S;
	} else {
		if (gpio_is_valid(chip->vooc_gpio.vooc_mcu_id_gpio)) {
			rc = gpio_request(chip->vooc_gpio.vooc_mcu_id_gpio, "vooc-mcu-id-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n",
					chip->vooc_gpio.vooc_mcu_id_gpio);
			}
		}
		chg_err("chip->vooc_gpio.vooc_mcu_id_gpio =%d\n",
			chip->vooc_gpio.vooc_mcu_id_gpio);
	}

	chip->vooc_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->vooc_gpio.pinctrl)) {
		chg_err(": %d Getting pinctrl handle failed, enable stm8s\n", __LINE__);
			mcu_hwid_type = OPLUS_VOOC_MCU_HWID_STM8S;
		return OPLUS_VOOC_MCU_HWID_STM8S;
	}

	chip->vooc_gpio.gpio_vooc_mcu_id_default =
		pinctrl_lookup_state(chip->vooc_gpio.pinctrl, "vooc_mcu_id_default");
	if (IS_ERR_OR_NULL(chip->vooc_gpio.gpio_vooc_mcu_id_default)) {
		chg_err(": %d Failed to get the gpio_vooc_mcu_id_default\
			pinctrl handle, enable stm8s\n", __LINE__);
		mcu_hwid_type = OPLUS_VOOC_MCU_HWID_STM8S;
		return OPLUS_VOOC_MCU_HWID_STM8S;
	}
	pinctrl_select_state(chip->vooc_gpio.pinctrl,
		chip->vooc_gpio.gpio_vooc_mcu_id_default);

	usleep_range(10000, 10000);
	if (gpio_is_valid(chip->vooc_gpio.vooc_mcu_id_gpio)){
		if(gpio_get_value(chip->vooc_gpio.vooc_mcu_id_gpio) == 0){
			chg_debug("it is  n76e\n");
			mcu_hwid_type = OPLUS_VOOC_MCU_HWID_N76E;
			return OPLUS_VOOC_MCU_HWID_N76E;
		}
	}

	chg_debug("it is  stm8s\n");
		mcu_hwid_type = OPLUS_VOOC_MCU_HWID_STM8S;
	return OPLUS_VOOC_MCU_HWID_STM8S;
}

int oplus_vooc_gpio_dt_init(struct oplus_vooc_chip *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;

	/* Parsing gpio swutch1*/
	chip->vooc_gpio.switch1_gpio = of_get_named_gpio(node,
		"qcom,charging_switch1-gpio", 0);
	if (chip->vooc_gpio.switch1_gpio < 0) {
		chg_err("chip->vooc_gpio.switch1_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->vooc_gpio.switch1_gpio)) {
			rc = gpio_request(chip->vooc_gpio.switch1_gpio,
				"charging-switch1-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n",
					chip->vooc_gpio.switch1_gpio);
			}
		}
		chg_err("chip->vooc_gpio.switch1_gpio =%d\n",
			chip->vooc_gpio.switch1_gpio);
	}

	chip->vooc_gpio.switch1_ctr1_gpio = of_get_named_gpio(node, "qcom,charging_switch1_ctr1-gpio", 0);
	if (chip->vooc_gpio.switch1_ctr1_gpio < 0) {
		chg_err("chip->vooc_gpio.switch1_ctr1_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->vooc_gpio.switch1_ctr1_gpio)) {
			rc = gpio_request(chip->vooc_gpio.switch1_ctr1_gpio, "charging-switch1-ctr1-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n", chip->vooc_gpio.switch1_ctr1_gpio);
			} else {
				gpio_direction_output(chip->vooc_gpio.switch1_ctr1_gpio, 0);
			}
		}
		chg_err("chip->vooc_gpio.switch1_ctr1_gpio =%d\n", chip->vooc_gpio.switch1_ctr1_gpio);
	}
	/* Parsing gpio swutch2*/
	/*if(get_PCB_Version()== 0)*/
	if (1) {
		chip->vooc_gpio.switch2_gpio = of_get_named_gpio(node,
			"qcom,charging_switch3-gpio", 0);
		if (chip->vooc_gpio.switch2_gpio < 0) {
			chg_err("chip->vooc_gpio.switch2_gpio not specified\n");
		} else {
			if (gpio_is_valid(chip->vooc_gpio.switch2_gpio)) {
				rc = gpio_request(chip->vooc_gpio.switch2_gpio,
					"charging-switch3-gpio");
				if (rc) {
					chg_err("unable to request gpio [%d]\n",
						chip->vooc_gpio.switch2_gpio);
				}
			}
			chg_err("chip->vooc_gpio.switch2_gpio =%d\n",
				chip->vooc_gpio.switch2_gpio);
		}
	} else {
		chip->vooc_gpio.switch2_gpio = of_get_named_gpio(node,
			"qcom,charging_switch2-gpio", 0);
		if (chip->vooc_gpio.switch2_gpio < 0) {
			chg_err("chip->vooc_gpio.switch2_gpio not specified\n");
		} else {
			if (gpio_is_valid(chip->vooc_gpio.switch2_gpio)) {
				rc = gpio_request(chip->vooc_gpio.switch2_gpio,
					"charging-switch2-gpio");
				if (rc) {
					chg_err("unable to request gpio [%d]\n",
						chip->vooc_gpio.switch2_gpio);
				}
			}
			chg_err("chip->vooc_gpio.switch2_gpio =%d\n",
				chip->vooc_gpio.switch2_gpio);
		}
	}
	/* Parsing gpio reset*/
	chip->vooc_gpio.reset_gpio = of_get_named_gpio(node,
		"qcom,charging_reset-gpio", 0);
	if (chip->vooc_gpio.reset_gpio < 0) {
		chg_err("chip->vooc_gpio.reset_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->vooc_gpio.reset_gpio)) {
			rc = gpio_request(chip->vooc_gpio.reset_gpio,
				"charging-reset-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n",
					chip->vooc_gpio.reset_gpio);
			}
		}
		chg_err("chip->vooc_gpio.reset_gpio =%d\n",
			chip->vooc_gpio.reset_gpio);
	}
	/* Parsing gpio clock*/
	chip->vooc_gpio.clock_gpio = of_get_named_gpio(node, "qcom,charging_clock-gpio", 0);
	if (chip->vooc_gpio.clock_gpio < 0) {
		chg_err("chip->vooc_gpio.reset_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->vooc_gpio.clock_gpio)) {
			rc = gpio_request(chip->vooc_gpio.clock_gpio, "charging-clock-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d], rc = %d\n",
					chip->vooc_gpio.clock_gpio, rc);
			}
		}
		chg_err("chip->vooc_gpio.clock_gpio =%d\n", chip->vooc_gpio.clock_gpio);
	}
	/* Parsing gpio data*/
	chip->vooc_gpio.data_gpio = of_get_named_gpio(node, "qcom,charging_data-gpio", 0);
	if (chip->vooc_gpio.data_gpio < 0) {
		chg_err("chip->vooc_gpio.data_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->vooc_gpio.data_gpio)) {
			rc = gpio_request(chip->vooc_gpio.data_gpio, "charging-data-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n", chip->vooc_gpio.data_gpio);
			}
		}
		chg_err("chip->vooc_gpio.data_gpio =%d\n", chip->vooc_gpio.data_gpio);
	}
	oplus_vooc_data_irq_init(chip);
	rc = opchg_bq27541_gpio_pinctrl_init(chip);
	chg_debug(" switch1_gpio = %d,switch2_gpio = %d, reset_gpio = %d,\
			clock_gpio = %d, data_gpio = %d, data_irq = %d\n",
			chip->vooc_gpio.switch1_gpio, chip->vooc_gpio.switch2_gpio,
			chip->vooc_gpio.reset_gpio, chip->vooc_gpio.clock_gpio,
			chip->vooc_gpio.data_gpio, chip->vooc_gpio.data_irq);
	return rc;
}

void opchg_set_clock_active(struct oplus_vooc_chip *chip)
{
	if (chip->mcu_boot_by_gpio) {
		chg_debug(" mcu_boot_by_gpio,return\n");
		return;
	}

	mutex_lock(&chip->pinctrl_mutex);
	pinctrl_select_state(chip->vooc_gpio.pinctrl, chip->vooc_gpio.gpio_clock_sleep); /* PULL_down */
	gpio_direction_output(chip->vooc_gpio.clock_gpio, 0);		/* out 0 */
	mutex_unlock(&chip->pinctrl_mutex);
}

void opchg_set_clock_sleep(struct oplus_vooc_chip *chip)
{
	if (chip->mcu_boot_by_gpio) {
		chg_debug(" mcu_boot_by_gpio,return\n");
		return;
	}

	mutex_lock(&chip->pinctrl_mutex);
	pinctrl_select_state(chip->vooc_gpio.pinctrl, chip->vooc_gpio.gpio_clock_active);/* PULL_up */
	gpio_direction_output(chip->vooc_gpio.clock_gpio, 1);	/* out 1 */
	mutex_unlock(&chip->pinctrl_mutex);
}

void opchg_set_data_active(struct oplus_vooc_chip *chip)
{
	mutex_lock(&chip->pinctrl_mutex);
	gpio_direction_input(chip->vooc_gpio.data_gpio);	/* in */
	pinctrl_select_state(chip->vooc_gpio.pinctrl, chip->vooc_gpio.gpio_data_active); /* no_PULL */
	mutex_unlock(&chip->pinctrl_mutex);
}

void opchg_set_data_sleep(struct oplus_vooc_chip *chip)
{
	mutex_lock(&chip->pinctrl_mutex);
	pinctrl_select_state(chip->vooc_gpio.pinctrl, chip->vooc_gpio.gpio_data_sleep);/* PULL_down */
	gpio_direction_output(chip->vooc_gpio.data_gpio, 0);	/* out 1 */
	mutex_unlock(&chip->pinctrl_mutex);
}

void opchg_set_reset_sleep(struct oplus_vooc_chip *chip)
{
	if (chip->adapter_update_real == ADAPTER_FW_NEED_UPDATE || chip->btb_temp_over || chip->mcu_update_ing) {
		chg_debug(" adapter_fw_need_update:%d,btb_temp_over:%d,mcu_update_ing:%d,return\n",
			chip->adapter_update_real, chip->btb_temp_over, chip->mcu_update_ing);
		return;
	}
	mutex_lock(&chip->pinctrl_mutex);
	gpio_direction_output(chip->vooc_gpio.reset_gpio, 1); /* out 1 */
#ifdef CONFIG_OPLUS_CHARGER_MTK
	pinctrl_select_state(chip->vooc_gpio.pinctrl, chip->vooc_gpio.gpio_reset_sleep); /* PULL_down */
#else
	pinctrl_select_state(chip->vooc_gpio.pinctrl, chip->vooc_gpio.gpio_reset_active); /* PULL_up */
#endif
	gpio_set_value(chip->vooc_gpio.reset_gpio, 0);
	usleep_range(5000, 5000);
	mutex_unlock(&chip->pinctrl_mutex);
	chg_debug("%s\n", __func__);
}

void opchg_set_reset_active(struct oplus_vooc_chip *chip)
{
	int active_level = 0;
	int sleep_level = 1;

	if (chip->adapter_update_real == ADAPTER_FW_NEED_UPDATE
			|| chip->btb_temp_over || chip->mcu_update_ing) {
		chg_debug(" adapter_fw_need_update:%d,btb_temp_over:%d,mcu_update_ing:%d,return\n",
			chip->adapter_update_real, chip->btb_temp_over, chip->mcu_update_ing);
		return;
	}
	if (chip->vooc_gpio.switch1_ctr1_gpio > 0) {//rk826
		active_level = 1;
		sleep_level = 0;
	}
	mutex_lock(&chip->pinctrl_mutex);
	gpio_direction_output(chip->vooc_gpio.reset_gpio, active_level);		/* out 1 */

#ifdef CONFIG_OPLUS_CHARGER_MTK
	pinctrl_select_state(chip->vooc_gpio.pinctrl,
		chip->vooc_gpio.gpio_reset_sleep);	/* PULL_down */
#else
	pinctrl_select_state(chip->vooc_gpio.pinctrl,
		chip->vooc_gpio.gpio_reset_active);	/* PULL_up */
#endif

	gpio_set_value(chip->vooc_gpio.reset_gpio, active_level);
	usleep_range(5000, 5000);
	gpio_set_value(chip->vooc_gpio.reset_gpio, sleep_level);
	usleep_range(10000, 10000);
	gpio_set_value(chip->vooc_gpio.reset_gpio, active_level);
	usleep_range(5000, 5000);
	mutex_unlock(&chip->pinctrl_mutex);
	chg_debug("%s\n", __func__);
}

int oplus_vooc_get_reset_gpio_val(struct oplus_vooc_chip *chip)
{
	return gpio_get_value(chip->vooc_gpio.reset_gpio);
}

bool oplus_is_power_off_charging(struct oplus_vooc_chip *chip)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT) {
		return true;
	} else {
		return false;
	}
#else
	return qpnp_is_power_off_charging();
#endif
}

bool oplus_is_charger_reboot(struct oplus_vooc_chip *chip)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	int charger_type;

	charger_type = oplus_chg_get_chg_type();
	if (charger_type == 5) {
		chg_debug("dont need check fw_update\n");
		return true;
	} else {
		return false;
	}
#else
	return qpnp_is_charger_reboot();
#endif
}

static void delay_reset_mcu_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_vooc_chip *chip = container_of(dwork,
		struct oplus_vooc_chip, delay_reset_mcu_work);
	opchg_set_clock_sleep(chip);
	opchg_set_reset_active(chip);
}

void oplus_vooc_delay_reset_mcu_init(struct oplus_vooc_chip *chip)
{
	INIT_DELAYED_WORK(&chip->delay_reset_mcu_work, delay_reset_mcu_work_func);
}

static void oplus_vooc_delay_reset_mcu(struct oplus_vooc_chip *chip)
{
	schedule_delayed_work(&chip->delay_reset_mcu_work,
		round_jiffies_relative(msecs_to_jiffies(1500)));
}

static bool is_allow_fast_chg_real(struct oplus_vooc_chip *chip)
{
	int temp = 0;
	int cap = 0;
	int chg_type = 0;

	temp = oplus_chg_get_chg_temperature();
	cap = oplus_chg_get_ui_soc();
	chg_type = oplus_chg_get_chg_type();

	if (chg_type != POWER_SUPPLY_TYPE_USB_DCP) {
		return false;
	}
	if (temp < chip->vooc_low_temp) {
		return false;
	}
	if (temp > chip->vooc_high_temp) {
		return false;
	}
	if (cap < chip->vooc_low_soc) {
		return false;
	}
	if (cap > chip->vooc_high_soc) {
		return false;
	}
	if (oplus_vooc_get_fastchg_to_normal() == true) {
		chg_debug(" oplus_vooc_get_fastchg_to_normal is true\n");
		return false;
	}
	return true;
}

static bool is_allow_fast_chg_dummy(struct oplus_vooc_chip *chip)
{
	int chg_type = 0;
	bool allow_real = false;

	chg_type = oplus_chg_get_chg_type();
	if (chg_type != POWER_SUPPLY_TYPE_USB_DCP) {
		return false;
	}
	if (oplus_vooc_get_fastchg_to_normal() == true) {
		chg_debug(" fast_switch_to_noraml is true\n");
		return false;
	}
	allow_real = is_allow_fast_chg_real(chip);
	if (oplus_vooc_get_fastchg_dummy_started() == true && (!allow_real)) {
		chg_debug(" dummy_started true, allow_real false\n");
		return false;
	}
	oplus_vooc_set_fastchg_allow(allow_real);
	return true;
}

void switch_fast_chg(struct oplus_vooc_chip *chip)
{
	bool allow_real = false;

	if (chip->dpdm_switch_mode == VOOC_CHARGER_MODE
		&& gpio_get_value(chip->vooc_gpio.switch1_gpio) == 1)
		{
		if (oplus_vooc_get_fastchg_started() == false) {
			allow_real = is_allow_fast_chg_real(chip);
			oplus_vooc_set_fastchg_allow(allow_real);
		}
		return;
	}

	if (is_allow_fast_chg_dummy(chip) == true) {
		if (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_UPDATE_FAIL) {
			opchg_set_switch_mode(chip, VOOC_CHARGER_MODE);
			oplus_vooc_delay_reset_mcu(chip);
		} else {
			if (oplus_vooc_get_fastchg_allow() == false
					&& oplus_vooc_get_fastchg_to_warm() == true) {
				chg_err(" fastchg_allow false, to_warm true, don't switch to vooc mode\n");
			} else {
				opchg_set_switch_mode(chip, VOOC_CHARGER_MODE);
				opchg_set_clock_sleep(chip);
				opchg_set_reset_active(chip);
			}
		}
	}
	chg_err(" end, allow_fast_chg:%d\n", oplus_vooc_get_fastchg_allow());
}

int oplus_vooc_get_ap_clk_gpio_val(struct oplus_vooc_chip *chip)
{
	return gpio_get_value(chip->vooc_gpio.clock_gpio);
}

int opchg_get_gpio_ap_data(struct oplus_vooc_chip *chip)
{
	return gpio_get_value(chip->vooc_gpio.data_gpio);
}

int opchg_get_clk_gpio_num(struct oplus_vooc_chip *chip)
{
	return chip->vooc_gpio.clock_gpio;
}

int opchg_get_data_gpio_num(struct oplus_vooc_chip *chip)
{
	return chip->vooc_gpio.data_gpio;
}

int opchg_read_ap_data(struct oplus_vooc_chip *chip)
{
	int bit = 0;
	opchg_set_clock_active(chip);
	usleep_range(1000, 1000);
	opchg_set_clock_sleep(chip);
	usleep_range(19000, 19000);
	bit = gpio_get_value(chip->vooc_gpio.data_gpio);
	return bit;
}

void opchg_reply_mcu_data(struct oplus_vooc_chip *chip, int ret_info, int device_type)
{
	int i = 0;
	for (i = 0; i < 3; i++) {
		if (i == 0) {		/*tell mcu1503 device_type*/
			gpio_set_value(chip->vooc_gpio.data_gpio, ret_info >> 1);
		} else if (i == 1) {
			gpio_set_value(chip->vooc_gpio.data_gpio, ret_info & 0x1);
		} else {
			gpio_set_value(chip->vooc_gpio.data_gpio, device_type);
			chg_err("device_type = %d\n", device_type);
		}
		opchg_set_clock_active(chip);
		usleep_range(1000, 1000);
		opchg_set_clock_sleep(chip);
		usleep_range(19000, 19000);
	}
}


void opchg_reply_mcu_data_4bits
		(struct oplus_vooc_chip *chip, int ret_info, int device_type)
{
	int i = 0;
	for (i = 0; i < 4; i++) {
		if (i == 0) {					/*tell mcu1503 device_type*/
			gpio_set_value(chip->vooc_gpio.data_gpio, ret_info >> 2);
			chg_err("first_send_bit = %d\n", ret_info >> 2);
		} else if (i == 1) {
			gpio_set_value(chip->vooc_gpio.data_gpio, (ret_info >> 1) & 0x1);
			chg_err("second_send_bit = %d\n", (ret_info >> 1) & 0x1);
		} else if (i == 2) {
			gpio_set_value(chip->vooc_gpio.data_gpio, ret_info & 0x1);
			chg_err("third_send_bit = %d\n", ret_info & 0x1);
		}else {
			gpio_set_value(chip->vooc_gpio.data_gpio, device_type);
			chg_err("device_type = %d\n", device_type);
		}
		opchg_set_clock_active(chip);
		usleep_range(1000, 1000);
		opchg_set_clock_sleep(chip);
		usleep_range(19000, 19000);
	}
}


void opchg_set_switch_fast_charger(struct oplus_vooc_chip *chip)
{
	gpio_direction_output(chip->vooc_gpio.switch1_gpio, 1);	/* out 1*/
	if (chip->vooc_gpio.switch1_ctr1_gpio > 0) {//asic rk826
		gpio_direction_output(chip->vooc_gpio.switch1_ctr1_gpio, 0);        /* out 0*/
	}
}

void opchg_set_vooc_chargerid_switch_val(struct oplus_vooc_chip *chip, int value)
{
	int level = 0;

	if (value == 1)
		level = 1;
	else if (value == 0)
		level = 0;
	else
		return;

	if (chip->vooc_gpio.switch1_ctr1_gpio > 0) {//asic rk826/op10
		if (level == 1) {
			gpio_direction_output(chip->vooc_gpio.reset_gpio, 1);
			gpio_set_value(chip->vooc_gpio.reset_gpio, 1);
		}
		gpio_direction_output(chip->vooc_gpio.switch1_ctr1_gpio, level);
		chg_err("switch1_gpio[%d], switch1_ctr1_gpio[%d]\n",
			gpio_get_value(chip->vooc_gpio.switch1_gpio),
			gpio_get_value(chip->vooc_gpio.switch1_ctr1_gpio));
		if (level == 0) {
			gpio_direction_output(chip->vooc_gpio.reset_gpio, 0);
			gpio_set_value(chip->vooc_gpio.reset_gpio, 0);
		}
	}
}

void opchg_set_switch_normal_charger(struct oplus_vooc_chip *chip)
{
	if (chip->vooc_gpio.switch1_gpio > 0) {
		gpio_direction_output(chip->vooc_gpio.switch1_gpio, 0);	/* in 0*/
	}
	if (chip->vooc_gpio.switch1_ctr1_gpio > 0) {//asic rk826
		gpio_direction_output(chip->vooc_gpio.switch1_ctr1_gpio, 0);/* out 0*/
		gpio_set_value(chip->vooc_gpio.reset_gpio, 0);
	}
}

void opchg_set_switch_earphone(struct oplus_vooc_chip *chip)
{
	return;
}

void opchg_set_switch_mode(struct oplus_vooc_chip *chip, int mode)
{
	if (chip->adapter_update_real == ADAPTER_FW_NEED_UPDATE || chip->btb_temp_over) {
		chg_err("adapter_fw_need_update: %d, btb_temp_over: %d\n",
			chip->adapter_update_real, chip->btb_temp_over);
		return;
	}
	if (mode == VOOC_CHARGER_MODE && chip->mcu_update_ing) {
		chg_err(" mcu_update_ing, don't switch to vooc mode\n");
		return;
	}
	switch (mode) {
	case VOOC_CHARGER_MODE:	       /*11*/
		opchg_set_switch_fast_charger(chip);
		chg_err(" vooc mode, switch1_gpio:%d\n", gpio_get_value(chip->vooc_gpio.switch1_gpio));
		break;
	case HEADPHONE_MODE:		  /*10*/
		opchg_set_switch_earphone(chip);
		chg_err(" headphone mode, switch1_gpio:%d\n", gpio_get_value(chip->vooc_gpio.switch1_gpio));
		break;
	case NORMAL_CHARGER_MODE:	    /*01*/
	default:
		opchg_set_switch_normal_charger(chip);
		chg_err(" normal mode, switch1_gpio:%d\n", gpio_get_value(chip->vooc_gpio.switch1_gpio));
		break;
	}
	chip->dpdm_switch_mode = mode;
}

int oplus_vooc_get_switch_gpio_val(struct oplus_vooc_chip *chip)
{
	return gpio_get_value(chip->vooc_gpio.switch1_gpio);
}

void reset_fastchg_after_usbout(struct oplus_vooc_chip *chip)
{
	if (oplus_vooc_get_fastchg_started() == false) {
		chg_err(" switch off fastchg\n");
		opchg_set_switch_mode(chip, NORMAL_CHARGER_MODE);
		oplus_vooc_set_fastchg_type_unknow();
	}
	oplus_vooc_set_fastchg_to_normal_false();
	oplus_vooc_set_fastchg_to_warm_false();
	oplus_vooc_set_fastchg_low_temp_full_false();
	oplus_vooc_set_fastchg_dummy_started_false();
}

static irqreturn_t irq_rx_handler(int irq, void *dev_id)
{
	oplus_vooc_shedule_fastchg_work();
	return IRQ_HANDLED;
}

void oplus_vooc_data_irq_init(struct oplus_vooc_chip *chip)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	struct device_node *node = NULL;
	struct device_node *node_new = NULL;
	u32 intr[2] = {0, 0};

	node = of_find_compatible_node(NULL, NULL, "mediatek, VOOC_AP_DATA-eint");
	node_new = of_find_compatible_node(NULL, NULL, "mediatek, VOOC_EINT_NEW_FUNCTION");
	if (node) {
		if (node_new) {
			chip->vooc_gpio.data_irq = gpio_to_irq(chip->vooc_gpio.data_gpio);
			chg_err("vooc_gpio.data_irq:%d\n", chip->vooc_gpio.data_irq);
		} else {
			of_property_read_u32_array(node , "interrupts", intr, ARRAY_SIZE(intr));
			chg_debug(" intr[0]  = %d, intr[1]  = %d\r\n", intr[0], intr[1]);
			chip->vooc_gpio.data_irq = irq_of_parse_and_map(node, 0);
		}
	} else {
		chg_err(" node not exist!\r\n");
		chip->vooc_gpio.data_irq = CUST_EINT_MCU_AP_DATA;
	}
#else
	chip->vooc_gpio.data_irq = gpio_to_irq(chip->vooc_gpio.data_gpio);
#endif
}

void oplus_vooc_eint_register(struct oplus_vooc_chip *chip)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	static int register_status = 0;
	int ret = 0;
	struct device_node *node = NULL;
	node = of_find_compatible_node(NULL, NULL, "mediatek, VOOC_EINT_NEW_FUNCTION");
	if (node) {
		opchg_set_data_active(chip);
		ret = request_irq(chip->vooc_gpio.data_irq, (irq_handler_t)irq_rx_handler,
				IRQF_TRIGGER_RISING, "VOOC_AP_DATA-eint", chip);
		if (ret < 0) {
			chg_err("ret = %d, oplus_vooc_eint_register failed to request_irq \n", ret);
		}
	} else {
		if (!register_status) {
			opchg_set_data_active(chip);
			ret = request_irq(chip->vooc_gpio.data_irq, (irq_handler_t)irq_rx_handler,
					IRQF_TRIGGER_RISING, "VOOC_AP_DATA-eint",  NULL);
			if (ret) {
				chg_err("ret = %d, oplus_vooc_eint_register failed to request_irq \n", ret);
			}
			register_status = 1;
		} else {
			chg_debug("enable_irq!\r\n");
			enable_irq(chip->vooc_gpio.data_irq);
		}
	}
#else
	int retval = 0;
	opchg_set_data_active(chip);
	retval = request_irq(chip->vooc_gpio.data_irq, irq_rx_handler, IRQF_TRIGGER_RISING, "mcu_data", chip);
	if (retval < 0) {
		chg_err("request ap rx irq failed.\n");
	}
#endif
}

void oplus_vooc_eint_unregister(struct oplus_vooc_chip *chip)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	struct device_node *node = NULL;
	node = of_find_compatible_node(NULL, NULL, "mediatek, VOOC_EINT_NEW_FUNCTION");
	chg_debug("disable_irq_mtk!\r\n");
	if (node) {
		free_irq(chip->vooc_gpio.data_irq, chip);
	} else {
		disable_irq(chip->vooc_gpio.data_irq);
	}
#else
	free_irq(chip->vooc_gpio.data_irq, chip);
#endif
}

