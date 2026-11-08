// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"mi-disp-lhbm:[%s:%d] " fmt, __func__, __LINE__

#include <linux/module.h>

#include "dsi_display.h"
#include "dsi_panel.h"
#include "mi_disp_feature.h"
#include "mi_disp_print.h"
#include "mi_disp_lhbm.h"
#include "mi_dsi_display.h"

/*
 * MI_DISP_IOCTL_SET_LOCAL_HBM requests go to the local HBM worker of the
 * display (dsi_display_mi), the same one behind the local_hbm sysfs node. It
 * waits for the panel to be on, applies the latest request only, and reports
 * the visible state as MI_DISP_EVENT_FOD (LOCAL_HBM_UI_READY), which the 5.10
 * disp_lhbm_fod thread did.
 */

static struct dsi_display *mi_disp_lhbm_display(int disp_id)
{
	if (!is_support_disp_id(disp_id))
		return NULL;

	return disp_id == MI_DISP_PRIMARY ? mi_get_primary_dsi_display() :
			mi_get_secondary_dsi_display();
}

bool is_local_hbm(int disp_id)
{
	struct dsi_display *display = mi_disp_lhbm_display(disp_id);

	return display && display->panel && display->panel->mi.lhbm;
}

bool mi_disp_lhbm_fod_enabled(struct dsi_panel *panel)
{
	return panel ? panel->mi.lhbm : false;
}

int mi_disp_set_local_hbm(int disp_id, int local_hbm_value)
{
	struct dsi_display *display = mi_disp_lhbm_display(disp_id);
	u32 nits;

	if (!is_local_hbm(disp_id)) {
		DISP_DEBUG("%s panel is not local hbm\n", get_disp_id_name(disp_id));
		return 0;
	}

	switch (local_hbm_value) {
	case LHBM_TARGET_BRIGHTNESS_WHITE_1000NIT:
		nits = 1000;
		break;
	case LHBM_TARGET_BRIGHTNESS_WHITE_110NIT:
		nits = 110;
		break;
	case LHBM_TARGET_BRIGHTNESS_GREEN_500NIT:
		nits = DSI_PANEL_MI_LHBM_GREEN_500NIT;
		break;
	case LHBM_TARGET_BRIGHTNESS_OFF_FINGER_UP:
	case LHBM_TARGET_BRIGHTNESS_OFF_AUTH_STOP:
		nits = 0;
		break;
	default:
		DISP_ERROR("invalid local hbm value %d\n", local_hbm_value);
		return -EINVAL;
	}

	DISP_INFO("%s display %s\n", get_disp_id_name(disp_id),
			get_lhbm_value_name(local_hbm_value));

	return dsi_display_mi_set_lhbm_target(display, nits);
}

int mi_disp_lhbm_fod_set_finger_event(int disp_id, u32 fod_status, bool from_touch)
{
	/* FodEngine controls local HBM through mi_disp_ioctl_set_local_hbm */
	return 0;
}
EXPORT_SYMBOL_GPL(mi_disp_lhbm_fod_set_finger_event);
