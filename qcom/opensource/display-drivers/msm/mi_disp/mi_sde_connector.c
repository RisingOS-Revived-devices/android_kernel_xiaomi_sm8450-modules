// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "mi_sde_connector:[%s:%d] " fmt, __func__, __LINE__

#include <drm/sde_drm.h>
#include "msm_drv.h"
#include "sde_connector.h"
#include "sde_encoder.h"
#include "sde_trace.h"
#include "dsi_display.h"
#include "dsi_panel.h"

#include "mi_disp_print.h"
#include "mi_disp_feature.h"
#include "mi_dsi_panel.h"
#include "mi_dsi_display.h"
#include "mi_sde_connector.h"

/*
 * The panel error interrupt (mi,esd-err-irq-gpio) is handled by
 * sde_connector itself on 6.6, see _sde_connector_register_esd_irq().
 */

void mi_sde_connector_backlight_notify(struct drm_connector *connector,
		int brightness)
{
	struct sde_connector *c_conn = to_sde_connector(connector);
	struct dsi_display *display;

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI)
		return;

	display = (struct dsi_display *)c_conn->display;
	if (!display)
		return;

	if (mi_get_backlight_log_mask() & BACKLIGHT_LOG_ENABLE)
		DISP_INFO("brightness = %d\n", brightness);

	mi_disp_feature_event_notify_by_type(mi_get_disp_id(display->display_type),
			MI_DISP_EVENT_BACKLIGHT, sizeof(brightness), brightness);
}

#if MI_DISP_DEBUGFS_ENABLE
int mi_sde_connector_debugfs_esd_sw_trigger(void *display)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	struct drm_connector *connector = NULL;
	struct sde_connector *c_conn = NULL;
	struct drm_event event;
	struct dsi_panel *panel;
	int power_mode;

	if (!dsi_display || !dsi_display->panel || !dsi_display->drm_conn) {
		DISP_ERROR("invalid display/panel/drm_conn ptr\n");
		return -EINVAL;
	}

	panel = dsi_display->panel;

	connector = dsi_display->drm_conn;
	c_conn = to_sde_connector(connector);
	if (!c_conn) {
		DISP_ERROR("invalid sde_connector ptr\n");
		return -EINVAL;
	}

	if (!dsi_panel_initialized(dsi_display->panel)) {
		DISP_ERROR("Panel not initialized\n");
		return -EINVAL;
	}

	if (atomic_read(&(dsi_display->panel->esd_recovery_pending))) {
		DISP_INFO("[esd-test]ESD recovery already pending\n");
		return 0;
	}

	if (c_conn->panel_dead) {
		DISP_INFO("panel_dead is true, return!\n");
		return 0;
	}

	dsi_panel_acquire_panel_lock(panel);
	atomic_set(&dsi_display->panel->esd_recovery_pending, 1);
	dsi_panel_release_panel_lock(panel);

	c_conn->panel_dead = true;
	DISP_ERROR("[esd-test]esd irq check failed report PANEL_DEAD conn_id: %d enc_id: %d\n",
			c_conn->base.base.id, c_conn->encoder->base.id);

	power_mode = dsi_display->panel->power_mode;
	DISP_INFO("[esd-test]%s display enabled (%d), power_mode (%s)\n",
			dsi_display->display_type, dsi_display->enabled,
			get_display_power_mode_name(power_mode));
	if (dsi_display->enabled) {
		if (power_mode == SDE_MODE_DPMS_ON ||
		    power_mode == SDE_MODE_DPMS_LP1)
			sde_encoder_display_failure_notification(c_conn->encoder,
					false);
	}

	event.type = DRM_EVENT_PANEL_DEAD;
	event.length = sizeof(bool);
	msm_mode_object_event_notify(&c_conn->base.base,
			c_conn->base.dev, &event, (u8 *)&c_conn->panel_dead);

	return 0;
}
#endif

/*
 * Panels with mi,flat-need-sync-te get the flat mode commands right before a
 * frame, split around a vblank.
 */
int mi_sde_connector_flat_fence(struct drm_connector *connector)
{
	struct sde_connector *c_conn;
	struct dsi_display *dsi_display;
	struct mi_dsi_panel_cfg *mi_cfg;
	int flat_mode_val = FEATURE_OFF;

	if (!connector) {
		DISP_ERROR("invalid connector ptr\n");
		return -EINVAL;
	}

	c_conn = to_sde_connector(connector);

	if (c_conn->connector_type != DRM_MODE_CONNECTOR_DSI)
		return 0;

	dsi_display = (struct dsi_display *) c_conn->display;
	if (!dsi_display || !dsi_display->panel) {
		DISP_ERROR("invalid display/panel ptr\n");
		return -EINVAL;
	}

	if (mi_get_disp_id(dsi_display->display_type) != MI_DISP_PRIMARY)
		return -EINVAL;

	mi_cfg = &dsi_display->panel->mi_cfg;
	if (!mi_cfg->flat_sync_te)
		return 0;

	dsi_panel_acquire_panel_lock(dsi_display->panel);
	flat_mode_val = mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE];
	if (dsi_display->panel->panel_initialized &&
	    flat_mode_val != mi_cfg->cur_flat_state) {
		if (flat_mode_val == FEATURE_ON) {
			dsi_panel_tx_cmd_set(dsi_display->panel,
					DSI_CMD_SET_MI_FLAT_MODE_ON);
			sde_encoder_wait_for_event(c_conn->encoder,
					MSM_ENC_VBLANK);
			dsi_panel_tx_cmd_set(dsi_display->panel,
					DSI_CMD_SET_MI_FLAT_MODE_SEC_ON);
			DISP_INFO("DSI_CMD_SET_MI_FLAT_MODE_ON sync with te");
		} else {
			dsi_panel_tx_cmd_set(dsi_display->panel,
					DSI_CMD_SET_MI_FLAT_MODE_OFF);
			sde_encoder_wait_for_event(c_conn->encoder,
					MSM_ENC_VBLANK);
			dsi_panel_tx_cmd_set(dsi_display->panel,
					DSI_CMD_SET_MI_FLAT_MODE_SEC_OFF);
			DISP_INFO("DSI_CMD_SET_MI_FLAT_MODE_OFF sync with te");
		}
		mi_disp_feature_event_notify_by_type(
				mi_get_disp_id(dsi_display->display_type),
				MI_DISP_EVENT_FLAT_MODE, sizeof(int), flat_mode_val);
		mi_cfg->cur_flat_state = flat_mode_val;
	}
	dsi_panel_release_panel_lock(dsi_display->panel);

	return 0;
}
