/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#ifndef _MI_SDE_CONNECTOR_H_
#define _MI_SDE_CONNECTOR_H_

#include <linux/types.h>
#include <drm/drm_connector.h>

#include "mi_disp_config.h"

void mi_sde_connector_backlight_notify(struct drm_connector *connector,
		int brightness);

int mi_sde_connector_flat_fence(struct drm_connector *connector);

#if MI_DISP_DEBUGFS_ENABLE
int mi_sde_connector_debugfs_esd_sw_trigger(void *display);
#else
static inline int mi_sde_connector_debugfs_esd_sw_trigger(void *display) { return 0; }
#endif

#endif /* _MI_SDE_CONNECTOR_H_ */
