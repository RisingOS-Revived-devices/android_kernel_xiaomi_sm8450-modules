/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"mi-disp-parse:[%s:%d] " fmt, __func__, __LINE__

#include <linux/slab.h>
#include <linux/string.h>

#include "mi_disp_print.h"
#include "mi_disp_parser.h"
#include "mi_panel_id.h"

#define DEFAULT_MAX_BRIGHTNESS_CLONE 4095

/*
 * Only the mi,* properties the 6.6 mi_disp layer uses are parsed here. The
 * panel features themselves (HBM, doze, local HBM, ESD, VDC, gamma ...) are
 * parsed by dsi_panel_mi_parse().
 */

static void mi_dsi_panel_parse_dc_config(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = &panel->utils;
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;
	const char *string;

	mi_cfg->dc_feature_enable = utils->read_bool(utils->data,
			"mi,dc-feature-enabled");
	if (!mi_cfg->dc_feature_enable)
		return;

	if (utils->read_string(utils->data, "mi,dc-feature-type", &string)) {
		DISP_ERROR("mi,dc-feature-type not defined!\n");
		mi_cfg->dc_feature_enable = false;
		return;
	}

	if (!strcmp(string, "lut_compatible_backlight")) {
		mi_cfg->dc_type = TYPE_LUT_COMPATIBLE_BL;
	} else if (!strcmp(string, "crc_skip_backlight")) {
		mi_cfg->dc_type = TYPE_CRC_SKIP_BL;
	} else {
		DISP_ERROR("No valid mi,dc-feature-type string\n");
		mi_cfg->dc_feature_enable = false;
		return;
	}

	if (utils->read_u32(utils->data, "mi,mdss-dsi-panel-dc-threshold",
			&mi_cfg->dc_threshold))
		mi_cfg->dc_threshold = 440;

	DISP_INFO("dc type %s, threshold %d\n", string, mi_cfg->dc_threshold);
}

static void mi_dsi_panel_parse_backlight_config(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = &panel->utils;
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;

	if (utils->read_u32(utils->data, "mi,max-brightness-clone",
			&mi_cfg->max_brightness_clone))
		mi_cfg->max_brightness_clone = DEFAULT_MAX_BRIGHTNESS_CLONE;

	if (utils->read_u32(utils->data, "mi,normal-max-brightness-clone",
			&mi_cfg->normal_max_brightness_clone))
		mi_cfg->normal_max_brightness_clone = DEFAULT_MAX_BRIGHTNESS_CLONE;

	mi_cfg->thermal_dimming_enabled = utils->read_bool(utils->data,
			"mi,thermal-dimming-flag");

	DISP_INFO("max brightness clone %d, normal %d, thermal dimming %d\n",
			mi_cfg->max_brightness_clone,
			mi_cfg->normal_max_brightness_clone,
			mi_cfg->thermal_dimming_enabled);
}

static int mi_dsi_panel_parse_cell_id_read_config(struct dsi_panel *panel)
{
	struct drm_panel_cell_id_config *config = &panel->mi_cfg.cell_id_config;
	struct dsi_parser_utils *utils = &panel->utils;

	dsi_panel_parse_cmd_sets_sub(&config->cell_id_cmd,
			DSI_CMD_SET_MI_PANEL_CELL_ID_READ, utils);
	if (!config->cell_id_cmd.count)
		return -EINVAL;

	dsi_panel_parse_cmd_sets_sub(&config->pre_tx_cmd,
			DSI_CMD_SET_MI_PANEL_CELL_ID_READ_PRE_TX, utils);

	if (utils->read_u32(utils->data, "mi,mdss-dsi-panel-cell-id-read-length",
			&config->cell_id_cmds_rlen) || !config->cell_id_cmds_rlen) {
		config->cell_id_cmds_rlen = 0;
		return -EINVAL;
	}

	if (utils->read_u32(utils->data, "mi,mdss-dsi-panel-cell-id-read-index",
			&config->cell_id_read_info_index))
		config->cell_id_read_info_index = 0;

	config->return_buf = kcalloc(config->cell_id_cmds_rlen,
			sizeof(unsigned char), GFP_KERNEL);
	if (!config->return_buf) {
		config->cell_id_cmds_rlen = 0;
		return -ENOMEM;
	}

	return 0;
}

int mi_dsi_panel_parse_config(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = &panel->utils;
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;

	if (utils->read_u64(utils->data, "mi,panel-id", &mi_cfg->mi_panel_id))
		mi_cfg->mi_panel_id = 0;
	else
		DISP_INFO("mi,panel-id is 0x%llx (%s)\n", mi_cfg->mi_panel_id,
				mi_get_panel_id_name(mi_cfg->mi_panel_id));

	mi_cfg->flatmode_check_enabled = utils->read_bool(utils->data,
			"mi,flatmode-status-check-enabled");
	mi_cfg->flat_sync_te = utils->read_bool(utils->data,
			"mi,flat-need-sync-te");
	mi_cfg->ddic_round_corner_enabled = utils->read_bool(utils->data,
			"mi,ddic-round-corner-enabled");
	mi_cfg->need_fod_animal_in_normal = utils->read_bool(utils->data,
			"mi,need-fod-animal-in-normal-enabled");

	if (mi_dsi_panel_parse_cell_id_read_config(panel))
		DISP_DEBUG("[%s] no panel cell id read config\n", panel->name);

	mi_dsi_panel_parse_dc_config(panel);
	mi_dsi_panel_parse_backlight_config(panel);

	return 0;
}

static bool mi_disp_parse_flatmode_status_len(struct dsi_parser_utils *utils,
	const char *prop_key, u32 **target, u32 cmd_cnt)
{
	int tmp;

	if (!utils->find_property(utils->data, prop_key, &tmp))
		return false;

	tmp /= sizeof(u32);
	if (tmp != cmd_cnt) {
		DISP_ERROR("request property(%d) do not match cmd count(%d)\n",
				tmp, cmd_cnt);
		return false;
	}

	*target = kcalloc(tmp, sizeof(u32), GFP_KERNEL);
	if (!*target)
		return false;

	if (utils->read_u32_array(utils->data, prop_key, *target, tmp)) {
		DISP_ERROR("cannot get values from dts\n");
		kfree(*target);
		*target = NULL;
		return false;
	}

	return true;
}

int mi_dsi_panel_parse_flatmode_configs(struct dsi_panel *panel)
{
	struct mi_panel_flatmode_config *config =
			&panel->mi_cfg.flatmode_check_config;
	struct dsi_parser_utils *utils = &panel->utils;
	struct property *data;
	u32 i, status_len;
	int tmp = 0;
	int rc = 0;

	dsi_panel_parse_cmd_sets_sub(&config->status_cmd,
			DSI_CMD_SET_MI_FLATMODE_STATUS, utils);
	if (!config->status_cmd.count) {
		DISP_ERROR("flat mode status command parsing failed\n");
		rc = -EINVAL;
		goto error;
	}

	if (!mi_disp_parse_flatmode_status_len(utils,
			"mi,mdss-dsi-panel-flatmode-status-read-length",
			&config->status_cmds_rlen, config->status_cmd.count)) {
		DISP_ERROR("Invalid flat mode status read length\n");
		rc = -EINVAL;
		goto error1;
	}

	status_len = 0;
	for (i = 0; i < config->status_cmd.count; ++i)
		status_len += config->status_cmds_rlen[i];
	if (!status_len) {
		rc = -EINVAL;
		goto error2;
	}

	data = utils->find_property(utils->data,
			"mi,mdss-dsi-panel-flatmode-on-status-value", &tmp);
	tmp /= sizeof(u32);
	if (IS_ERR_OR_NULL(data) || tmp != status_len) {
		DISP_ERROR("error parse panel-status-value\n");
		rc = -EINVAL;
		goto error2;
	}

	config->status_value = kcalloc(status_len, sizeof(u32), GFP_KERNEL);
	if (!config->status_value) {
		rc = -ENOMEM;
		goto error2;
	}

	config->return_buf = kcalloc(status_len, sizeof(unsigned char),
			GFP_KERNEL);
	if (!config->return_buf) {
		rc = -ENOMEM;
		goto error3;
	}

	config->status_buf = kzalloc(256, GFP_KERNEL);
	if (!config->status_buf) {
		rc = -ENOMEM;
		goto error4;
	}

	rc = utils->read_u32_array(utils->data,
			"mi,mdss-dsi-panel-flatmode-on-status-value",
			config->status_value, status_len);
	if (rc) {
		DISP_ERROR("error reading flat mode status values\n");
		goto error5;
	}

	return 0;

error5:
	kfree(config->status_buf);
	config->status_buf = NULL;
error4:
	kfree(config->return_buf);
	config->return_buf = NULL;
error3:
	kfree(config->status_value);
	config->status_value = NULL;
error2:
	kfree(config->status_cmds_rlen);
	config->status_cmds_rlen = NULL;
error1:
	dsi_panel_destroy_cmd_packets(&config->status_cmd);
	dsi_panel_dealloc_cmd_packets(&config->status_cmd);
error:
	panel->mi_cfg.flatmode_check_enabled = false;
	return rc;
}
