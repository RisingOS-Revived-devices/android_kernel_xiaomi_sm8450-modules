// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"mi-dsi-panel:[%s:%d] " fmt, __func__, __LINE__

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/device.h> /* pm_wakeup.h is only included through device.h */
#include <video/mipi_display.h>

#include "dsi_panel.h"
#include "dsi_display.h"
#include "mi_dsi_panel.h"
#include "mi_disp_feature.h"
#include "mi_disp_print.h"
#include "mi_disp_parser.h"
#include "mi_dsi_display.h"
#include "mi_panel_id.h"

/*
 * mi_disp interface of the panel. Everything that drives the panel goes
 * through dsi_panel_mi, which is also used by the 6.6 sysfs nodes of the
 * display device, so the panel is never driven twice.
 */

static u64 g_panel_id[MI_DISP_MAX];

enum mi_project_panel_id mi_get_panel_id_by_dsi_panel(struct dsi_panel *panel)
{
	if (!panel)
		return PANEL_ID_INVALID;

	return mi_get_panel_id(panel->mi_cfg.mi_panel_id);
}

enum mi_project_panel_id mi_get_panel_id_by_disp_id(int disp_id)
{
	if (!is_support_disp_id(disp_id))
		return PANEL_ID_INVALID;

	return mi_get_panel_id(g_panel_id[disp_id]);
}

int mi_dsi_panel_init(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg;

	if (!panel)
		return -EINVAL;

	mi_cfg = &panel->mi_cfg;
	mutex_init(&mi_cfg->doze_lock);
	atomic_set(&mi_cfg->real_brightness_clone, 0);

	mi_cfg->disp_wakelock = wakeup_source_register(NULL, "disp_wakelock");
	if (!mi_cfg->disp_wakelock)
		DISP_ERROR("doze_wakelock wake_source register failed\n");

	mi_dsi_panel_parse_config(panel);
	if (panel->type)
		g_panel_id[mi_get_disp_id(panel->type)] = mi_cfg->mi_panel_id;

	return 0;
}

void mi_dsi_panel_deinit(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;
	struct drm_panel_cell_id_config *cell_id = &mi_cfg->cell_id_config;

	if (mi_cfg->disp_wakelock) {
		wakeup_source_unregister(mi_cfg->disp_wakelock);
		mi_cfg->disp_wakelock = NULL;
	}

	dsi_panel_destroy_cmd_packets(&cell_id->cell_id_cmd);
	dsi_panel_dealloc_cmd_packets(&cell_id->cell_id_cmd);
	dsi_panel_destroy_cmd_packets(&cell_id->pre_tx_cmd);
	dsi_panel_dealloc_cmd_packets(&cell_id->pre_tx_cmd);
	kfree(cell_id->return_buf);
	cell_id->return_buf = NULL;
}

int mi_dsi_acquire_wakelock(struct dsi_panel *panel)
{
	if (!panel)
		return -EINVAL;

	if (panel->mi_cfg.disp_wakelock)
		__pm_stay_awake(panel->mi_cfg.disp_wakelock);

	return 0;
}

int mi_dsi_release_wakelock(struct dsi_panel *panel)
{
	if (!panel)
		return -EINVAL;

	if (panel->mi_cfg.disp_wakelock)
		__pm_relax(panel->mi_cfg.disp_wakelock);

	return 0;
}

bool is_aod_and_panel_initialized(struct dsi_panel *panel)
{
	return (panel->power_mode == SDE_MODE_DPMS_LP1 ||
		panel->power_mode == SDE_MODE_DPMS_LP2) &&
		dsi_panel_initialized(panel);
}

static int mi_dsi_panel_set_dc_mode_locked(struct dsi_panel *panel, bool enable)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;
	int rc;

	if (!mi_cfg->dc_feature_enable) {
		DISP_INFO("DC mode: TODO\n");
		return 0;
	}

	rc = dsi_panel_tx_cmd_set(panel, enable ? DSI_CMD_SET_MI_DC_ON :
			DSI_CMD_SET_MI_DC_OFF);
	mi_cfg->real_dc_state = enable ? FEATURE_ON : FEATURE_OFF;
	if (rc)
		DISP_ERROR("failed to set DC mode: %d\n", enable);
	else
		DISP_INFO("DC mode: %s\n", enable ? "On" : "Off");

	return rc;
}

/* Called with panel_lock held after the panel on commands */
void mi_dsi_panel_enable(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;

	/* DC is a panel state, restore it after the panel was off */
	if (mi_cfg->dc_feature_enable &&
	    mi_cfg->feature_val[DISP_FEATURE_DC] == FEATURE_ON)
		mi_dsi_panel_set_dc_mode_locked(panel, true);

	mi_cfg->in_fod_calibration = false;
	mi_cfg->cur_flat_state = FEATURE_OFF;
}

/* Called with panel_lock held after the panel was turned off */
void mi_dsi_panel_disable(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;

	mi_cfg->feature_val[DISP_FEATURE_HBM] = FEATURE_OFF;
	mi_cfg->feature_val[DISP_FEATURE_HBM_FOD] = FEATURE_OFF;
	mi_cfg->feature_val[DISP_FEATURE_LOCAL_HBM] = LOCAL_HBM_OFF_TO_NORMAL;
	mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE] = FEATURE_OFF;
	mi_cfg->feature_val[DISP_FEATURE_FP_STATUS] = FINGERPRINT_NONE;
	mi_cfg->in_fod_calibration = false;
	mi_cfg->doze_brightness = DOZE_TO_NORMAL;
	mi_cfg->real_dc_state = FEATURE_OFF;
	mi_cfg->last_dbv = 0;
	panel->mi.doze_request = DSI_PANEL_MI_DOZE_OFF;
}

/* Called with panel_lock held from dsi_panel_mi_set_backlight() */
bool mi_dsi_panel_skip_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;

	/* CRC based DC keeps low backlight levels at the DC threshold */
	if (bl_lvl && mi_cfg->dc_feature_enable &&
	    mi_cfg->dc_type == TYPE_CRC_SKIP_BL &&
	    mi_cfg->feature_val[DISP_FEATURE_DC] == FEATURE_ON &&
	    mi_cfg->feature_val[DISP_FEATURE_HBM] == FEATURE_OFF &&
	    bl_lvl < mi_cfg->dc_threshold) {
		DISP_INFO("[%s] skip set backlight %d due to DC on\n",
				panel->type, bl_lvl);
		return true;
	}

	return false;
}

/*
 * Called with panel_lock held after register 0x51 was written. In HBM mode the
 * DBV is reported above the normal range, as 5.10 did for HBM panels.
 */
void mi_dsi_panel_notify_dbv(struct dsi_panel *panel, u32 dbv)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;
	static int use_count = 20;
	u32 value = dbv;

	if (!panel->type)
		return;

	if (panel->mi.hbm_active)
		value += panel->bl_config.bl_max_level / 2;

	if (use_count-- > 0 && mi_cfg->last_dbv != value)
		DISP_TIME_INFO("[%s] set 51 backlight %d\n", panel->type, dbv);
	if (!mi_cfg->last_dbv != !value)
		use_count = 20;

	if (mi_get_backlight_log_mask() & BACKLIGHT_LOG_ENABLE) {
		DISP_INFO("[%s] [0x51 backlight debug] dbv = %d, hbm = %d\n",
				panel->type, dbv, panel->mi.hbm_active);
		if (mi_get_backlight_log_mask() & BACKLIGHT_LOG_DUMP_STACK)
			dump_stack();
	}

	if (!value && !mi_cfg->last_dbv)
		return;

	mi_cfg->last_dbv = value;
	mi_disp_feature_event_notify_by_type(mi_get_disp_id(panel->type),
			MI_DISP_EVENT_51_BRIGHTNESS, sizeof(value), value);
}

/* Called with panel_lock held when the panel enters or leaves HBM mode */
void mi_dsi_panel_notify_hbm(struct dsi_panel *panel, bool hbm)
{
	int value = hbm ? FEATURE_ON : FEATURE_OFF;

	if (!panel->type)
		return;

	mi_disp_feature_event_notify_by_type(mi_get_disp_id(panel->type),
			MI_DISP_EVENT_HBM, sizeof(value), value);
}

bool mi_dsi_panel_is_need_tx_cmd(u32 feature_id)
{
	switch (feature_id) {
	case DISP_FEATURE_SENSOR_LUX:
	case DISP_FEATURE_LOW_BRIGHTNESS_FOD:
	case DISP_FEATURE_FP_STATUS:
	case DISP_FEATURE_FOLD_STATUS:
	case DISP_FEATURE_POWERSTATUS:
	case DISP_FEATURE_SYSTEM_BUILD_VERSION:
		return false;
	default:
		return true;
	}
}

static int mi_dsi_panel_set_round_corner_locked(struct dsi_panel *panel,
			bool enable)
{
	int rc;

	if (!panel->mi_cfg.ddic_round_corner_enabled) {
		DISP_INFO("[%s] ddic round corner feature not enabled\n",
				panel->type);
		return 0;
	}

	rc = dsi_panel_tx_cmd_set(panel, enable ?
			DSI_CMD_SET_MI_ROUND_CORNER_ON :
			DSI_CMD_SET_MI_ROUND_CORNER_OFF);
	if (rc)
		DISP_ERROR("[%s] failed to send ROUND_CORNER(%s) cmds, rc=%d\n",
				panel->type, enable ? "On" : "Off", rc);

	return rc;
}

static int mi_dsi_panel_send_flat_mode_locked(struct dsi_panel *panel,
			bool enable)
{
	int rc;

	if (enable) {
		DISP_INFO("flat mode on\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_SEC_ON);
	} else {
		DISP_INFO("flat mode off\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_SEC_OFF);
	}

	return rc;
}

/* LOCAL_HBM values of disp_param as local HBM levels of dsi_panel_mi */
static u32 mi_dsi_panel_lhbm_level(int local_hbm_state)
{
	switch (local_hbm_state) {
	case LOCAL_HBM_NORMAL_WHITE_1000NIT:
	case LOCAL_HBM_HLPM_WHITE_1000NIT:
		return 1000;
	case LOCAL_HBM_NORMAL_WHITE_750NIT:
		return 750;
	case LOCAL_HBM_NORMAL_WHITE_500NIT:
		return 500;
	case LOCAL_HBM_NORMAL_WHITE_110NIT:
	case LOCAL_HBM_HLPM_WHITE_110NIT:
		return 110;
	case LOCAL_HBM_NORMAL_GREEN_500NIT:
		return DSI_PANEL_MI_LHBM_GREEN_500NIT;
	default:
		/* All OFF_TO_* states, the doze state picks the target mode */
		return 0;
	}
}

static enum dsi_panel_mi_doze mi_dsi_panel_doze_request(u32 doze_brightness)
{
	switch (doze_brightness) {
	case DOZE_BRIGHTNESS_HBM:
		return DSI_PANEL_MI_DOZE_HBM;
	case DOZE_BRIGHTNESS_LBM:
		return DSI_PANEL_MI_DOZE_LBM;
	default:
		return DSI_PANEL_MI_DOZE_OFF;
	}
}

int mi_dsi_panel_set_disp_param(struct dsi_panel *panel,
			struct disp_feature_ctl *ctl)
{
	struct mi_dsi_panel_cfg *mi_cfg;
	struct dsi_display *display;
	int rc = 0;

	if (!panel || !ctl) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	DISP_TIME_INFO("[%s] feature: %s, value: %d\n", panel->type,
			get_disp_feature_id_name(ctl->feature_id), ctl->feature_val);

	mi_cfg = &panel->mi_cfg;

	if (!panel->panel_initialized &&
		mi_dsi_panel_is_need_tx_cmd(ctl->feature_id)) {
		if (ctl->feature_id == DISP_FEATURE_DC)
			mi_cfg->feature_val[DISP_FEATURE_DC] = ctl->feature_val;
		DISP_WARN("[%s] panel not initialized!\n", panel->type);
		rc = -ENODEV;
		goto exit;
	}

	switch (ctl->feature_id) {
	case DISP_FEATURE_DIMMING:
		if (panel->power_mode != SDE_MODE_DPMS_ON) {
			DISP_INFO("skip dimming due to power_mode is not ON\n");
			break;
		}
		/* HBM and local HBM commands handle dimming themselves */
		if (panel->mi.hbm_active || panel->mi.lhbm_nits ||
		    panel->mi.fod_hbm) {
			DISP_INFO("skip dimming %s\n",
					ctl->feature_val ? "on" : "off");
			break;
		}
		rc = dsi_panel_tx_cmd_set(panel, ctl->feature_val == FEATURE_ON ?
				DSI_CMD_SET_MI_DIMMINGON :
				DSI_CMD_SET_MI_DIMMINGOFF);
		mi_cfg->feature_val[DISP_FEATURE_DIMMING] = ctl->feature_val;
		break;
	case DISP_FEATURE_HBM:
		mi_cfg->feature_val[DISP_FEATURE_HBM] = ctl->feature_val;
		rc = dsi_panel_mi_set_hbm(panel, ctl->feature_val == FEATURE_ON);
		mi_dsi_panel_notify_hbm(panel, ctl->feature_val == FEATURE_ON);
		break;
	case DISP_FEATURE_HBM_FOD:
		rc = dsi_panel_mi_set_fod_hbm(panel,
				ctl->feature_val == FEATURE_ON);
		mi_cfg->feature_val[DISP_FEATURE_HBM_FOD] = ctl->feature_val;
		break;
	case DISP_FEATURE_DOZE_BRIGHTNESS:
		mi_cfg->doze_brightness = ctl->feature_val;
		rc = dsi_panel_mi_set_doze_request(panel,
				mi_dsi_panel_doze_request(ctl->feature_val));
		mi_cfg->feature_val[DISP_FEATURE_DOZE_BRIGHTNESS] = ctl->feature_val;
		break;
	case DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS:
		if (ctl->feature_val == -1) {
			DISP_INFO("FOD calibration brightness restore last_bl_level=%d\n",
					mi_cfg->fod_calibration_bl);
			if (mi_cfg->in_fod_calibration)
				dsi_panel_set_backlight(panel,
						mi_cfg->fod_calibration_bl);
			mi_cfg->in_fod_calibration = false;
		} else if (ctl->feature_val >= 0 &&
				(u32)ctl->feature_val <= panel->bl_config.bl_max_level) {
			if (!mi_cfg->in_fod_calibration)
				mi_cfg->fod_calibration_bl = panel->mi.last_bl_lvl;
			mi_cfg->in_fod_calibration = true;
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
			dsi_panel_set_backlight(panel, ctl->feature_val);
		} else {
			mi_cfg->in_fod_calibration = false;
			DISP_ERROR("FOD calibration invalid brightness level:%d\n",
					ctl->feature_val);
		}
		mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS] =
				ctl->feature_val;
		break;
	case DISP_FEATURE_FOD_CALIBRATION_HBM:
		mi_cfg->in_fod_calibration = ctl->feature_val != -1;
		rc = dsi_panel_mi_set_fod_hbm(panel, ctl->feature_val != -1);
		mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_HBM] =
				ctl->feature_val;
		break;
	case DISP_FEATURE_FLAT_MODE:
		if (!mi_cfg->flat_sync_te) {
			rc = mi_dsi_panel_send_flat_mode_locked(panel,
					ctl->feature_val == FEATURE_ON);
			mi_cfg->cur_flat_state = ctl->feature_val;
			mi_disp_feature_event_notify_by_type(
					mi_get_disp_id(panel->type),
					MI_DISP_EVENT_FLAT_MODE,
					sizeof(ctl->feature_val), ctl->feature_val);
		}
		/* else sent with the next frame by mi_sde_connector_flat_fence */
		mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE] = ctl->feature_val;
		break;
	case DISP_FEATURE_CRC:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_DC:
		DISP_INFO("DC mode state:%d\n", ctl->feature_val);
		if (mi_cfg->dc_feature_enable)
			rc = mi_dsi_panel_set_dc_mode_locked(panel,
					ctl->feature_val == FEATURE_ON);
		mi_cfg->feature_val[DISP_FEATURE_DC] = ctl->feature_val;
		mi_disp_feature_event_notify_by_type(mi_get_disp_id(panel->type),
				MI_DISP_EVENT_DC, sizeof(ctl->feature_val),
				ctl->feature_val);
		break;
	case DISP_FEATURE_LOCAL_HBM:
		DISP_TIME_INFO("[%s] Local HBM: %s\n", panel->type,
				get_local_hbm_state_name(ctl->feature_val));
		display = mi_get_dsi_display_by_panel(panel);
		rc = display ? dsi_display_mi_set_lhbm_target(display,
				mi_dsi_panel_lhbm_level(ctl->feature_val)) : -ENODEV;
		mi_cfg->feature_val[DISP_FEATURE_LOCAL_HBM] = ctl->feature_val;
		break;
	case DISP_FEATURE_SENSOR_LUX:
		DISP_DEBUG("DISP_FEATURE_SENSOR_LUX=%d\n", ctl->feature_val);
		mi_cfg->feature_val[DISP_FEATURE_SENSOR_LUX] = ctl->feature_val;
		break;
	case DISP_FEATURE_FP_STATUS:
		mi_cfg->feature_val[DISP_FEATURE_FP_STATUS] = ctl->feature_val;
		/* Go back to doze if the fingerprint left it */
		if ((ctl->feature_val == ENROLL_STOP ||
		     ctl->feature_val == AUTH_STOP ||
		     ctl->feature_val == HEART_RATE_STOP) &&
		    is_aod_and_panel_initialized(panel))
			dsi_panel_mi_aod_to_normal(panel, false);
		break;
	case DISP_FEATURE_FOLD_STATUS:
		DISP_INFO("DISP_FEATURE_FOLD_STATUS=%d\n", ctl->feature_val);
		mi_cfg->feature_val[DISP_FEATURE_FOLD_STATUS] = ctl->feature_val;
		break;
	case DISP_FEATURE_NATURE_FLAT_MODE:
	case DISP_FEATURE_SPR_RENDER:
	case DISP_FEATURE_COLOR_INVERT:
	case DISP_FEATURE_DC_BACKLIGHT:
	case DISP_FEATURE_GIR:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_AOD_TO_NORMAL:
		if (!mi_cfg->need_fod_animal_in_normal ||
		    panel->power_mode == SDE_MODE_DPMS_ON) {
			rc = 0;
		} else if (ctl->feature_val != FEATURE_ON &&
			   mi_cfg->feature_val[DISP_FEATURE_FP_STATUS] == AUTH_STOP) {
			/* Unlocking, stay in the normal mode */
			rc = 0;
		} else {
			rc = dsi_panel_mi_aod_to_normal(panel,
					ctl->feature_val == FEATURE_ON);
		}
		mi_cfg->feature_val[DISP_FEATURE_AOD_TO_NORMAL] = ctl->feature_val;
		break;
	case DISP_FEATURE_DBI:
		/* Gray by temperature, only N16 panels have the 3D LUT commands */
		DISP_INFO("DBI:%d\n", ctl->feature_val);
		break;
	case DISP_FEATURE_DDIC_ROUND_CORNER:
		DISP_INFO("DDIC round corner state:%d\n", ctl->feature_val);
		rc = mi_dsi_panel_set_round_corner_locked(panel,
				ctl->feature_val == FEATURE_ON);
		mi_cfg->feature_val[DISP_FEATURE_DDIC_ROUND_CORNER] =
				ctl->feature_val;
		break;
	case DISP_FEATURE_HBM_BACKLIGHT:
		DISP_INFO("hbm backlight:%d\n", ctl->feature_val);
		if (ctl->feature_val < 0 ||
		    (u32)ctl->feature_val > panel->bl_config.bl_max_level) {
			rc = -EINVAL;
			break;
		}
		panel->mi.last_bl_lvl = ctl->feature_val;
		if (panel->power_mode == SDE_MODE_DPMS_ON)
			dsi_panel_set_backlight(panel, ctl->feature_val);
		break;
	case DISP_FEATURE_CABC:
		DISP_INFO("feature :CABC ,cabc val = %d\n", ctl->feature_val);
		if (ctl->feature_val == LCD_CABC_OFF)
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABC_OFF);
		else if (ctl->feature_val == LCD_CABC_UI_ON)
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_CABC_UI_ON);
		else if (ctl->feature_val == LCD_CABC_MOVIE_ON)
			rc = dsi_panel_tx_cmd_set(panel,
					DSI_CMD_SET_MI_CABC_MOVIE_ON);
		else if (ctl->feature_val == LCD_CABC_STILL_ON)
			rc = dsi_panel_tx_cmd_set(panel,
					DSI_CMD_SET_MI_CABC_STILL_ON);
		break;
	case DISP_FEATURE_POWERSTATUS:
		DISP_DEBUG("power get:%d, last power mode = %d\n",
				ctl->feature_val, panel->power_mode);
		break;
	case DISP_FEATURE_SYSTEM_BUILD_VERSION:
		DISP_INFO("system build version:%.*s\n", (int)ctl->tx_len,
				ctl->tx_ptr ? (char *)ctl->tx_ptr : "");
		break;
	default:
		DISP_ERROR("invalid feature argument: %d\n", ctl->feature_id);
		break;
	}
exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int mi_dsi_panel_get_disp_param(struct dsi_panel *panel,
			struct disp_feature_ctl *ctl)
{
	if (!panel || !ctl) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (!is_support_disp_feature_id(ctl->feature_id)) {
		DISP_ERROR("unsupported disp feature id\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	ctl->feature_val = panel->mi_cfg.feature_val[ctl->feature_id];
	DISP_INFO("%s: %d\n", get_disp_feature_id_name(ctl->feature_id),
			ctl->feature_val);
	mutex_unlock(&panel->panel_lock);

	return 0;
}

ssize_t mi_dsi_panel_show_disp_param(struct dsi_panel *panel,
			char *buf, size_t size)
{
	struct mi_dsi_panel_cfg *mi_cfg;
	ssize_t count = 0;
	int i = 0;

	if (!panel || !buf || !size) {
		DISP_ERROR("invalid params\n");
		return -EAGAIN;
	}

	count = scnprintf(buf, size, "%40s: feature vaule\n",
			"feature name[feature id]");

	mutex_lock(&panel->panel_lock);
	mi_cfg = &panel->mi_cfg;
	for (i = DISP_FEATURE_DIMMING; i < DISP_FEATURE_MAX; i++) {
		count += scnprintf(buf + count, size - count, "%36s[%02d]: %d\n",
				get_disp_feature_id_name(i), i,
				mi_cfg->feature_val[i]);
	}
	mutex_unlock(&panel->panel_lock);

	return count;
}

int mi_dsi_panel_set_doze_brightness(struct dsi_panel *panel,
			u32 doze_brightness)
{
	int rc;

	mutex_lock(&panel->panel_lock);

	if (!panel->panel_initialized) {
		DISP_ERROR("panel is not initialized!\n");
		mutex_unlock(&panel->panel_lock);
		return -EINVAL;
	}

	panel->mi_cfg.doze_brightness = doze_brightness;
	rc = dsi_panel_mi_set_doze_request(panel,
			mi_dsi_panel_doze_request(doze_brightness));
	if (rc)
		DISP_ERROR("[%s] failed to set doze brightness %s, rc=%d\n",
				panel->type,
				get_doze_brightness_name(doze_brightness), rc);
	else
		DISP_TIME_INFO("[%s] set doze brightness to %s\n", panel->type,
				get_doze_brightness_name(doze_brightness));

	mutex_unlock(&panel->panel_lock);

	return rc;
}

int mi_dsi_panel_get_doze_brightness(struct dsi_panel *panel,
			u32 *doze_brightness)
{
	if (!panel)
		return -EINVAL;

	mutex_lock(&panel->panel_lock);
	*doze_brightness = panel->mi_cfg.doze_brightness;
	mutex_unlock(&panel->panel_lock);

	return 0;
}

int mi_dsi_panel_get_brightness(struct dsi_panel *panel,
			u32 *brightness)
{
	if (!panel)
		return -EINVAL;

	mutex_lock(&panel->panel_lock);
	*brightness = panel->mi.last_bl_lvl;
	mutex_unlock(&panel->panel_lock);

	return 0;
}

/* Called with panel_lock held */
int mi_dsi_panel_write_cmd_set(struct dsi_panel *panel,
			struct dsi_panel_cmd_set *cmd_sets)
{
	struct dsi_cmd_desc *cmds;
	ssize_t len;
	int rc = 0, i;

	if (!panel || !cmd_sets)
		return -EINVAL;

	cmds = cmd_sets->cmds;
	for (i = 0; i < cmd_sets->count; i++, cmds++) {
		cmds->ctrl_flags = 0;
		if (cmd_sets->state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		len = dsi_host_transfer_sub(panel->host, cmds);
		if (len < 0) {
			rc = len;
			DISP_ERROR("failed to set cmds, rc=%d\n", rc);
			break;
		}

		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms * 1000,
					(cmds->post_wait_ms * 1000) + 10);
	}

	return rc;
}

int mi_dsi_panel_write_dsi_cmd(struct dsi_panel *panel,
			struct dsi_cmd_rw_ctl *ctl)
{
	struct dsi_panel_cmd_set cmd_sets = {0};
	u32 packet_count = 0;
	int rc = 0;

	if (!panel || !ctl)
		return -EINVAL;

	mutex_lock(&panel->panel_lock);

	if (!panel->panel_initialized) {
		DISP_ERROR("Panel not initialized!\n");
		rc = -EAGAIN;
		goto exit_unlock;
	}

	if (!ctl->tx_len || !ctl->tx_ptr) {
		DISP_ERROR("[%s] invalid params\n", panel->type);
		rc = -EINVAL;
		goto exit_unlock;
	}

	rc = dsi_panel_get_cmd_pkt_count((const char *)ctl->tx_ptr,
			ctl->tx_len, &packet_count);
	if (rc) {
		DISP_ERROR("[%s] write dsi commands failed, rc=%d\n",
				panel->type, rc);
		goto exit_unlock;
	}

	rc = dsi_panel_alloc_cmd_packets(&cmd_sets, packet_count);
	if (rc) {
		DISP_ERROR("[%s] failed to allocate cmd packets, rc=%d\n",
				panel->type, rc);
		goto exit_unlock;
	}

	rc = dsi_panel_create_cmd_packets((const char *)ctl->tx_ptr,
			ctl->tx_len, packet_count, cmd_sets.cmds);
	if (rc) {
		DISP_ERROR("[%s] failed to create cmd packets, rc=%d\n",
				panel->type, rc);
		goto exit_free1;
	}

	if (ctl->tx_state == MI_DSI_CMD_LP_STATE) {
		cmd_sets.state = DSI_CMD_SET_STATE_LP;
	} else if (ctl->tx_state == MI_DSI_CMD_HS_STATE) {
		cmd_sets.state = DSI_CMD_SET_STATE_HS;
	} else {
		DISP_ERROR("[%s] command state unrecognized-%d\n",
				panel->type, ctl->tx_state);
		rc = -EINVAL;
		goto exit_free2;
	}

	rc = mi_dsi_panel_write_cmd_set(panel, &cmd_sets);
	if (rc)
		DISP_ERROR("[%s] failed to send cmds, rc=%d\n", panel->type, rc);

exit_free2:
	dsi_panel_destroy_cmd_packets(&cmd_sets);
exit_free1:
	dsi_panel_dealloc_cmd_packets(&cmd_sets);
exit_unlock:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int mi_dsi_panel_write_dsi_cmd_set(struct dsi_panel *panel, int type)
{
	struct dsi_panel_cmd_set *set;
	int rc;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info ||
	    type < 0 || type >= DSI_CMD_SET_MAX) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	set = &panel->cur_mode->priv_info->cmd_sets[type];
	if (!set->count) {
		DISP_ERROR("[%s] No commands to be sent\n", cmd_set_prop_map[type]);
		rc = -EAGAIN;
		goto exit;
	}

	DISP_INFO("set cmds [%s], count (%d), state(%s)\n",
			cmd_set_prop_map[type], set->count,
			set->state == DSI_CMD_SET_STATE_LP ?
			"dsi_lp_mode" : "dsi_hs_mode");

	rc = dsi_panel_tx_cmd_set(panel, type);
exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

ssize_t mi_dsi_panel_show_dsi_cmd_set_type(struct dsi_panel *panel,
			char *buf, size_t size)
{
	ssize_t count = 0;
	int type;

	if (!panel || !buf)
		return -EINVAL;

	count = scnprintf(buf, size, "%s: dsi cmd_set name\n", "id");
	for (type = DSI_CMD_SET_PRE_ON; type < DSI_CMD_SET_MAX; type++)
		count += scnprintf(buf + count, size - count, "%02d: %s\n",
				type, cmd_set_prop_map[type]);

	return count;
}

int mi_dsi_panel_set_brightness_clone(struct dsi_panel *panel,
			u32 brightness_clone)
{
	struct mi_dsi_panel_cfg *mi_cfg;
	static int log_count = 20;

	if (!panel)
		return -EINVAL;

	mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;
	mi_cfg->user_brightness_clone = brightness_clone;

	/* The thermal limit is applied by the brightness0-clone cooling device */
	if (brightness_clone > mi_cfg->max_brightness_clone)
		brightness_clone = mi_cfg->max_brightness_clone;

	if (!brightness_clone)
		log_count = 20;

	if (log_count-- > 0)
		DISP_TIME_INFO("[%s] set brightness clone to %d\n",
				panel->type, brightness_clone);

	atomic_set(&mi_cfg->real_brightness_clone, brightness_clone);

	mi_disp_feature_event_notify_by_type(mi_get_disp_id(panel->type),
			MI_DISP_EVENT_BRIGHTNESS_CLONE,
			sizeof(brightness_clone), brightness_clone);

	mutex_unlock(&panel->panel_lock);

	return 0;
}

int mi_dsi_panel_get_brightness_clone(struct dsi_panel *panel,
			u32 *brightness_clone)
{
	if (!panel)
		return -EINVAL;

	*brightness_clone = atomic_read(&panel->mi_cfg.real_brightness_clone);

	return 0;
}

int mi_dsi_panel_get_max_brightness_clone(struct dsi_panel *panel,
			u32 *max_brightness_clone)
{
	if (!panel)
		return -EINVAL;

	*max_brightness_clone = panel->mi_cfg.max_brightness_clone;

	return 0;
}
