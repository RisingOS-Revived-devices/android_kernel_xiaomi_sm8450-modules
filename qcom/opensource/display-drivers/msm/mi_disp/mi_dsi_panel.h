/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#ifndef _MI_DSI_PANEL_H_
#define _MI_DSI_PANEL_H_

#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/device.h> /* pm_wakeup.h is only included through device.h */
#include <linux/types.h>
#include <drm/mi_disp.h>

#include "dsi_defs.h"
#include "mi_disp_feature.h"

struct dsi_panel;

enum dc_feature_type {
	TYPE_NONE = 0,
	TYPE_LUT_COMPATIBLE_BL,
	TYPE_CRC_SKIP_BL
};

struct mi_panel_flatmode_config {
	struct dsi_panel_cmd_set status_cmd;
	u32 *status_cmds_rlen;
	u32 *status_value;
	u8 *return_buf;
	u8 *status_buf;
};

struct drm_panel_cell_id_config {
	struct dsi_panel_cmd_set pre_tx_cmd;
	struct dsi_panel_cmd_set cell_id_cmd;
	u32 cell_id_read_info_index;
	u32 cell_id_cmds_rlen;
	u8 *return_buf;
};

/**
 * struct mi_dsi_panel_cfg - mi_disp state of a panel
 *
 * The panel is driven by dsi_panel_mi (struct dsi_panel_mi); this only keeps
 * what the 5.10 mi_disp interface reports or parses on top of it.
 */
struct mi_dsi_panel_cfg {
	u64 mi_panel_id;

	/* values last set through disp_param, read back by GET_FEATURE */
	int feature_val[DISP_FEATURE_MAX];

	struct mutex doze_lock;
	struct wakeup_source *disp_wakelock;
	u32 doze_brightness;

	/* brightness clone, set by user space */
	u32 user_brightness_clone;
	atomic_t real_brightness_clone;
	u32 max_brightness_clone;
	u32 normal_max_brightness_clone;
	bool thermal_dimming_enabled;

	bool need_fod_animal_in_normal;
	bool in_fod_calibration;
	u32 fod_calibration_bl;

	bool dc_feature_enable;
	enum dc_feature_type dc_type;
	u32 dc_threshold;
	u32 real_dc_state;

	bool flat_sync_te;
	int cur_flat_state;

	bool ddic_round_corner_enabled;

	bool flatmode_check_enabled;
	struct mi_panel_flatmode_config flatmode_check_config;

	struct drm_panel_cell_id_config cell_id_config;

	u32 last_dbv;
};

extern const char *cmd_set_prop_map[DSI_CMD_SET_MAX];

int mi_dsi_panel_init(struct dsi_panel *panel);
void mi_dsi_panel_deinit(struct dsi_panel *panel);
void mi_dsi_panel_enable(struct dsi_panel *panel);
void mi_dsi_panel_disable(struct dsi_panel *panel);

int mi_dsi_acquire_wakelock(struct dsi_panel *panel);
int mi_dsi_release_wakelock(struct dsi_panel *panel);

bool is_aod_and_panel_initialized(struct dsi_panel *panel);

bool mi_dsi_panel_skip_backlight(struct dsi_panel *panel, u32 bl_lvl);
void mi_dsi_panel_notify_dbv(struct dsi_panel *panel, u32 dbv);
void mi_dsi_panel_notify_hbm(struct dsi_panel *panel, bool hbm);

bool mi_dsi_panel_is_need_tx_cmd(u32 feature_id);

int mi_dsi_panel_set_disp_param(struct dsi_panel *panel,
			struct disp_feature_ctl *ctl);

int mi_dsi_panel_get_disp_param(struct dsi_panel *panel,
			struct disp_feature_ctl *ctl);

ssize_t mi_dsi_panel_show_disp_param(struct dsi_panel *panel,
			char *buf, size_t size);

int mi_dsi_panel_set_doze_brightness(struct dsi_panel *panel,
			u32 doze_brightness);

int mi_dsi_panel_get_doze_brightness(struct dsi_panel *panel,
			u32 *doze_brightness);

int mi_dsi_panel_get_brightness(struct dsi_panel *panel,
			u32 *brightness);

int mi_dsi_panel_write_cmd_set(struct dsi_panel *panel,
			struct dsi_panel_cmd_set *cmd_sets);

int mi_dsi_panel_write_dsi_cmd(struct dsi_panel *panel,
			struct dsi_cmd_rw_ctl *ctl);

int mi_dsi_panel_write_dsi_cmd_set(struct dsi_panel *panel, int type);

ssize_t mi_dsi_panel_show_dsi_cmd_set_type(struct dsi_panel *panel,
			char *buf, size_t size);

int mi_dsi_panel_set_brightness_clone(struct dsi_panel *panel,
			u32 brightness_clone);

int mi_dsi_panel_get_brightness_clone(struct dsi_panel *panel,
			u32 *brightness_clone);

int mi_dsi_panel_get_max_brightness_clone(struct dsi_panel *panel,
			u32 *max_brightness_clone);

#endif /* _MI_DSI_PANEL_H_ */
