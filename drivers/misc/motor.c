/************************************************************************************
** Copyright (C), 2008-2017, OPPO Mobile Comm Corp., Ltd
** VENDOR_EDIT
** File: motor.c
**
** Description:
**	Definitions for oppo_motor common software.
**
** Version: 1.0
** Date created: 2018/01/14,20:27
**
** --------------------------- Revision History: ------------------------------------
* <version>		<date>		<author>		<desc>
**************************************************************************************/
#include <linux/delay.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/serio.h>
#include <soc/oppo/boot_mode.h>
#include <soc/oppo/oppo_project.h>
#include "oplus_motor/oppo_motor.h"
#include "oplus_motor/oppo_motor_notifier.h"

/*if can not compile success, please update vendor/oppo_motor*/

void oppo_parse_motor_info(struct oppo_motor_chip * chip)
{
	if (!chip)
		return;

	chip->info.type = MOTOR_FI5;
	chip->info.motor_ic = STSPIN220;
	chip->dir_sign = NEGATIVE;
	chip->is_support_ffd = true;
	if ((is_project(OPPO_18097) || is_project(OPPO_18099)) && get_PCB_Version() < HW_VERSION__13)
		chip->have_mode = true;
	else
		chip->have_mode = false;

	if (is_project(OPPO_18097) && (get_PCB_Version() < HW_VERSION__15)) {//need to overlay the parameter
		chip->pwm_param.normal.full.l = 68;//6.8mm
		chip->pwm_param.normal.full.speed = SPEED_64KHZ;
		chip->pwm_param.normal.speed_up.l = 15;//1.5mm
		chip->pwm_param.normal.speed_up.speed = SPEED_64KHZ;
		chip->pwm_param.normal.speed_down.l = 2;//0.2mm
		chip->pwm_param.normal.speed_down.speed	= SPEED_12_8KHZ;
		chip->pwm_param.normal.delay = 13000;//13ms
		chip->pwm_param.normal.up_brake_delay = 33000;//33ms
		chip->pwm_param.normal.down_brake_delay = 33000;//33ms
		chip->stop.offset[0] = 24;
		chip->stop.offset[1] = 24;
		chip->stop.neg[0] = -3;
		chip->stop.pos[0] = 3;
		chip->stop.neg[1] = -3;
		chip->stop.pos[1] = 3;
	}
	MOTOR_LOG("boot_mode is %d \n",get_boot_mode());
	if ((MSM_BOOT_MODE__RECOVERY == get_boot_mode()) || (MSM_BOOT_MODE__FACTORY == get_boot_mode()) ||
		qpnp_is_power_off_charging() || (MSM_BOOT_MODE__SAU == get_boot_mode()))
		chip->boot_mode = MOTOR_OTHER_MODE;
	else
		chip->boot_mode = MOTOR_NORMAL_MODE;
}

EXPORT_SYMBOL(oppo_parse_motor_info);

