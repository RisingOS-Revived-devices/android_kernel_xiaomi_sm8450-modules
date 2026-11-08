// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"mi-dsi-display:[%s] " fmt, __func__

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <drm/drm_panel.h>

#include "msm_kms.h"
#include "sde_kms.h"
#include "sde_trace.h"
#include "sde_connector.h"
#include "dsi_display.h"
#include "dsi_panel.h"
#include "mi_disp_print.h"
#include "mi_sde_encoder.h"
#include "mi_dsi_display.h"
#include "mi_dsi_panel.h"
#include "mi_disp_feature.h"
#include "mi_disp_flatmode.h"

static char oled_wp_info_str[MAX_CMDLINE_PARAM_LEN] = {0};
static char sec_oled_wp_info_str[MAX_CMDLINE_PARAM_LEN] = {0};
static char cell_id_info_str[MAX_CMDLINE_PARAM_LEN] = {0};
static struct panel_manufaturer_info g_panel_manufaturer_info[MI_DISP_MAX];
static struct dsi_read_info g_dsi_read_info;

#define MAX_DEBUG_POLICY_CMDLINE_LEN 64
static char display_debug_policy[MAX_DEBUG_POLICY_CMDLINE_LEN] = {0};

char *get_display_power_mode_name(int power_mode)
{
	switch (power_mode) {
	case SDE_MODE_DPMS_ON:
		return "On";
	case SDE_MODE_DPMS_LP1:
		return "Doze";
	case SDE_MODE_DPMS_LP2:
		return "Doze_Suspend";
	case SDE_MODE_DPMS_STANDBY:
		return "Standby";
	case SDE_MODE_DPMS_SUSPEND:
		return "Suspend";
	case SDE_MODE_DPMS_OFF:
		return "Off";
	default:
		return "Unknown";
	}
}

int mi_get_disp_id(const char *display_type)
{
	if (display_type && !strncmp(display_type, "primary", 7))
		return MI_DISP_PRIMARY;
	else
		return MI_DISP_SECONDARY;
}

static struct dsi_display *mi_get_dsi_display(int disp_id)
{
	struct disp_feature *df = mi_get_disp_feature();
	struct disp_display *dd_ptr;

	if (!df)
		return NULL;

	dd_ptr = &df->d_display[disp_id];
	if (dd_ptr->display && dd_ptr->intf_type == MI_INTF_DSI)
		return (struct dsi_display *)dd_ptr->display;

	return NULL;
}

struct dsi_display *mi_get_primary_dsi_display(void)
{
	return mi_get_dsi_display(MI_DISP_PRIMARY);
}

struct dsi_display *mi_get_secondary_dsi_display(void)
{
	return mi_get_dsi_display(MI_DISP_SECONDARY);
}

struct dsi_display *mi_get_dsi_display_by_panel(struct dsi_panel *panel)
{
	struct dsi_display *display;

	if (!panel || !panel->type)
		return NULL;

	display = mi_get_dsi_display(mi_get_disp_id(panel->type));

	return display && display->panel == panel ? display : NULL;
}

int mi_dsi_display_set_disp_param(void *display,
			struct disp_feature_ctl *ctl)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	char trace_buf[64];
	int ret = 0;

	if (!dsi_display || !ctl) {
		DISP_ERROR("Invalid display or ctl ptr\n");
		return -EINVAL;
	}

	if (sde_kms_is_suspend_blocked(dsi_display->drm_dev) &&
		mi_dsi_panel_is_need_tx_cmd(ctl->feature_id)) {
		DISP_ERROR("sde_kms is suspended, skip to set disp_param\n");
		return -EBUSY;
	}

	mi_dsi_acquire_wakelock(dsi_display->panel);
	snprintf(trace_buf, sizeof(trace_buf), "set_disp_param:%s",
			get_disp_feature_id_name(ctl->feature_id));
	SDE_ATRACE_BEGIN(trace_buf);
	ret = mi_dsi_panel_set_disp_param(dsi_display->panel, ctl);
	SDE_ATRACE_END(trace_buf);
	mi_dsi_release_wakelock(dsi_display->panel);

	return ret;
}

int mi_dsi_display_get_disp_param(void *display,
			struct disp_feature_ctl *ctl)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	return mi_dsi_panel_get_disp_param(dsi_display->panel, ctl);
}

ssize_t mi_dsi_display_show_disp_param(void *display,
			char *buf, size_t size)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	return mi_dsi_panel_show_disp_param(dsi_display->panel, buf, size);
}

int mi_dsi_display_write_dsi_cmd(void *display,
			struct dsi_cmd_rw_ctl *ctl)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	int ret = 0;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	if (sde_kms_is_suspend_blocked(dsi_display->drm_dev)) {
		DISP_ERROR("sde_kms is suspended, skip to write dsi cmd\n");
		return -EBUSY;
	}

	mi_dsi_acquire_wakelock(dsi_display->panel);
	ret = mi_dsi_panel_write_dsi_cmd(dsi_display->panel, ctl);
	mi_dsi_release_wakelock(dsi_display->panel);

	return ret;
}

int mi_dsi_display_read_dsi_cmd(void *display,
			struct dsi_cmd_rw_ctl *ctl)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	int ret = 0;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	if (sde_kms_is_suspend_blocked(dsi_display->drm_dev)) {
		DISP_ERROR("sde_kms is suspended, skip to read dsi cmd\n");
		return -EBUSY;
	}

	mi_dsi_acquire_wakelock(dsi_display->panel);
	ret = dsi_display_cmd_receive(dsi_display, (const char *)ctl->tx_ptr,
			ctl->tx_len, ctl->rx_ptr, ctl->rx_len, NULL);
	mi_dsi_release_wakelock(dsi_display->panel);

	return ret;
}

int mi_dsi_display_set_mipi_rw(void *display, char *buf)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	struct dsi_cmd_rw_ctl ctl;
	int ret = 0;
	char *token, *input_copy, *input_dup = NULL;
	const char *delim = " ";
	bool is_read = false;
	char *buffer = NULL;
	char *alloc_buffer = NULL;
	u32 buf_size = 0;
	u32 tmp_data = 0;
	int recv_len = 0;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	memset(&ctl, 0, sizeof(struct dsi_cmd_rw_ctl));
	memset(&g_dsi_read_info, 0, sizeof(struct dsi_read_info));

	DISP_TIME_INFO("input buffer:{%s}\n", buf);

	input_copy = kstrdup(buf, GFP_KERNEL);
	if (!input_copy) {
		ret = -ENOMEM;
		goto exit;
	}

	input_dup = input_copy;
	/* removes leading and trailing whitespace from input_copy */
	input_copy = strim(input_copy);

	/* Split a string into token */
	token = strsep(&input_copy, delim);
	if (token) {
		ret = kstrtou32(token, 10, &tmp_data);
		if (ret) {
			DISP_ERROR("input buffer conversion failed\n");
			goto exit_free0;
		}
		is_read = !!tmp_data;
	}

	/* Removes leading whitespace from input_copy */
	if (input_copy)
		input_copy = skip_spaces(input_copy);
	else
		goto exit_free0;

	token = strsep(&input_copy, delim);
	if (token) {
		ret = kstrtou32(token, 10, &tmp_data);
		if (ret) {
			DISP_ERROR("input buffer conversion failed\n");
			goto exit_free0;
		}
		if (tmp_data > sizeof(g_dsi_read_info.rx_buf)) {
			DISP_ERROR("read size exceeding the limit %zu\n",
					sizeof(g_dsi_read_info.rx_buf));
			ret = -EINVAL;
			goto exit_free0;
		}
		ctl.rx_len = tmp_data;
		ctl.rx_ptr = g_dsi_read_info.rx_buf;
	}

	/* Removes leading whitespace from input_copy */
	if (input_copy)
		input_copy = skip_spaces(input_copy);
	else
		goto exit_free0;

	alloc_buffer = kzalloc(strlen(input_copy) + 1, GFP_KERNEL);
	if (!alloc_buffer) {
		ret = -ENOMEM;
		goto exit_free0;
	}
	buffer = alloc_buffer;

	token = strsep(&input_copy, delim);
	while (token) {
		ret = kstrtou32(token, 16, &tmp_data);
		if (ret) {
			DISP_ERROR("input buffer conversion failed\n");
			goto exit_free1;
		}
		DISP_DEBUG("buffer[%d] = 0x%02x\n", buf_size, tmp_data);
		buffer[buf_size++] = (tmp_data & 0xff);
		/* Removes leading whitespace from input_copy */
		if (input_copy) {
			input_copy = skip_spaces(input_copy);
			token = strsep(&input_copy, delim);
		} else {
			token = NULL;
		}
	}

	if (!buf_size) {
		ret = -EINVAL;
		goto exit_free1;
	}

	/* check buffer's fist num to find whether set LP/HS flag to send command: (00: LP)/(01:HS) */
	if (buffer[0] <= 0x01) {
		if (buffer[0] == 0x00)
			ctl.rx_state = MI_DSI_CMD_LP_STATE;
		else if (buffer[0] == 0x01)
			ctl.rx_state = MI_DSI_CMD_HS_STATE;
		++buffer;
		--buf_size;
	} else {
		ctl.rx_state = MI_DSI_CMD_LP_STATE;
		DISP_INFO("transfer speed not set, default use LPM. buffer[0] = 0x%02x\n",
				buffer[0]);
	}
	DISP_INFO("ctl.rx_state= 0x%02x\n", ctl.rx_state);

	ctl.tx_len = buf_size;
	ctl.tx_ptr = buffer;

	if (is_read) {
		recv_len = mi_dsi_display_read_dsi_cmd(dsi_display, &ctl);
		if (recv_len <= 0 || recv_len != ctl.rx_len) {
			DISP_ERROR("read dsi cmd transfer failed rc = %d\n",
					recv_len);
			ret = -EAGAIN;
		} else {
			g_dsi_read_info.is_read_sucess = true;
			g_dsi_read_info.rx_len = recv_len;
			ret = 0;
		}
	} else {
		ret = mi_dsi_display_write_dsi_cmd(dsi_display, &ctl);
	}

exit_free1:
	kfree(alloc_buffer);
exit_free0:
	kfree(input_dup);
exit:
	return ret;
}

ssize_t mi_dsi_display_show_mipi_rw(void *display,
			char *buf, size_t size)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	ssize_t count = 0;
	int i = 0;
	const char help_message[] = {
		"\tFirst byte means write/read: 00/01 (Decimalism)\n"
		"\tSecend byte means read back bytes size (Decimalism)\n"
		"\tThird byte means LP/HS send: 00/01 (Hexadecimal)\n"
		"\tExample:\n"
		"\t\twrite:00 00 00 39 00 00 00 00 00 03 51 07 FF\n"
		"\t\tread: 01 02 00 06 01 00 00 00 00 01 52\n"
	};

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	if (g_dsi_read_info.is_read_sucess) {
		for (i = 0; i < g_dsi_read_info.rx_len; i++) {
			if (i == g_dsi_read_info.rx_len - 1)
				count += scnprintf(buf + count, size - count,
						"0x%02X\n", g_dsi_read_info.rx_buf[i]);
			else
				count += scnprintf(buf + count, size - count,
						"0x%02X,", g_dsi_read_info.rx_buf[i]);
		}
	} else {
		count = scnprintf(buf, size, "%s\n", help_message);
	}

	return count;
}

ssize_t mi_dsi_display_read_panel_info(void *display,
			char *buf, size_t size)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	char *pname = NULL;
	int ret = 0;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	pname = mi_dsi_display_get_cmdline_panel_info(dsi_display);
	if (pname) {
		ret = scnprintf(buf, size, "panel_name=%s\n", pname);
		kfree(pname);
	} else if (dsi_display->name) {
		/* find the last occurrence of a character in a string */
		pname = strrchr(dsi_display->name, ',');
		if (pname && *pname)
			ret = scnprintf(buf, size, "panel_name=%s\n", ++pname);
		else
			ret = scnprintf(buf, size, "panel_name=%s\n",
					dsi_display->name);
	} else {
		ret = scnprintf(buf, size, "panel_name=%s\n", "null");
	}

	return ret;
}

ssize_t mi_dsi_display_read_wp_info(void *display,
			char *buf, size_t size)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	int display_id = 0;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	display_id = mi_get_disp_id(dsi_display->display_type);
	if (display_id == MI_DISP_PRIMARY)
		return scnprintf(buf, size, "%s\n", oled_wp_info_str);
	else if (display_id == MI_DISP_SECONDARY)
		return scnprintf(buf, size, "%s\n", sec_oled_wp_info_str);

	return scnprintf(buf, size, "%s\n", "Unsupported display");
}

/*
 * The manufacturer info is only read from the panels of L1 and L18 on 5.10,
 * other panels report the empty record, which is what this keeps.
 */
ssize_t mi_dsi_display_read_manufacturer_struct_by_globleparam(void *display,
			struct panel_manufaturer_info *manufaturer_info)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	struct panel_manufaturer_info *info;

	if (!dsi_display || !manufaturer_info) {
		DISP_ERROR("Invalid display/manufaturer_info ptr\n");
		return -EINVAL;
	}

	info = &g_panel_manufaturer_info[mi_get_disp_id(dsi_display->display_type)];
	if (!info->wp_info_len && !info->max_brightness_len &&
	    !info->manufacturer_time_len) {
		DISP_ERROR("read_manufacturer_info error\n");
		return -EINVAL;
	}

	memcpy(manufaturer_info, info, sizeof(*info));

	return 0;
}

ssize_t mi_dsi_display_read_manufacturer_info_by_globleparam(void *display,
			char *buf, size_t size)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	struct panel_manufaturer_info *info;
	ssize_t count = 0;

	if (!dsi_display || !buf) {
		DISP_ERROR("Invalid display/buf ptr\n");
		return -EINVAL;
	}

	info = &g_panel_manufaturer_info[mi_get_disp_id(dsi_display->display_type)];

	memset(buf, 0, size);
	count += scnprintf(buf + count, size - count, "%36s: %s\n",
			"wp_info", info->wp_info);
	count += scnprintf(buf + count, size - count, "%36s: %s\n",
			"max_brightness", info->maxbrightness);
	count += scnprintf(buf + count, size - count, "%36s: %s\n",
			"manufacturer_time", info->manufacturer_time);

	return count;
}

/* The gray scale table is only provided for L12 panels by the bootloader */
ssize_t mi_dsi_display_read_gray_scale_info(void *display,
			char *buf, size_t size)
{
	DISP_ERROR("Read gray scale failed\n");

	return -EINVAL;
}

int mi_dsi_display_get_fps(void *display, u32 *fps)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	struct dsi_display_mode *cur_mode = NULL;
	int ret = 0;

	if (!dsi_display || !dsi_display->panel) {
		DISP_ERROR("Invalid display/panel ptr\n");
		return -EINVAL;
	}

	mutex_lock(&dsi_display->display_lock);
	cur_mode = dsi_display->panel->cur_mode;
	if (cur_mode) {
		if (cur_mode->timing.h_skew)
			*fps = cur_mode->timing.h_skew;
		else
			*fps = cur_mode->timing.refresh_rate;
	} else {
		ret = -EINVAL;
	}
	mutex_unlock(&dsi_display->display_lock);

	return ret;
}

int mi_dsi_display_set_doze_brightness(void *display,
			u32 doze_brightness)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	int disp_id = MI_DISP_PRIMARY;
	int ret = 0;

	if (!dsi_display || !dsi_display->panel) {
		DISP_ERROR("invalid display/panel\n");
		return -EINVAL;
	}

	if (sde_kms_is_suspend_blocked(dsi_display->drm_dev)) {
		DISP_ERROR("sde_kms is suspended, skip to set doze brightness\n");
		return -EBUSY;
	}

	mi_dsi_acquire_wakelock(dsi_display->panel);
	mutex_lock(&dsi_display->panel->mi_cfg.doze_lock);
	SDE_ATRACE_BEGIN("set_doze_brightness");
	ret = mi_dsi_panel_set_doze_brightness(dsi_display->panel,
			doze_brightness);
	SDE_ATRACE_END("set_doze_brightness");
	mutex_unlock(&dsi_display->panel->mi_cfg.doze_lock);
	mi_dsi_release_wakelock(dsi_display->panel);

	disp_id = mi_get_disp_id(dsi_display->display_type);
	mi_disp_feature_event_notify_by_type(disp_id, MI_DISP_EVENT_DOZE,
			sizeof(doze_brightness), doze_brightness);

	return ret;
}

int mi_dsi_display_get_doze_brightness(void *display,
			u32 *doze_brightness)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	return mi_dsi_panel_get_doze_brightness(dsi_display->panel,
			doze_brightness);
}

int mi_dsi_display_get_brightness(void *display, u32 *brightness)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	return mi_dsi_panel_get_brightness(dsi_display->panel, brightness);
}

int mi_dsi_display_write_dsi_cmd_set(void *display, int type)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	int ret = 0;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	if (sde_kms_is_suspend_blocked(dsi_display->drm_dev)) {
		DISP_ERROR("sde_kms is suspended, skip to write dsi cmd_set\n");
		return -EBUSY;
	}

	mi_dsi_acquire_wakelock(dsi_display->panel);
	ret = mi_dsi_panel_write_dsi_cmd_set(dsi_display->panel, type);
	mi_dsi_release_wakelock(dsi_display->panel);

	return ret;
}

ssize_t mi_dsi_display_show_dsi_cmd_set_type(void *display,
			char *buf, size_t size)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	return mi_dsi_panel_show_dsi_cmd_set_type(dsi_display->panel, buf, size);
}

int mi_dsi_display_set_brightness_clone(void *display,
			u32 brightness_clone)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	return mi_dsi_panel_set_brightness_clone(dsi_display->panel,
			brightness_clone);
}

int mi_dsi_display_get_brightness_clone(void *display,
			u32 *brightness_clone)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	return mi_dsi_panel_get_brightness_clone(dsi_display->panel,
			brightness_clone);
}

int mi_dsi_display_get_max_brightness_clone(void *display,
			u32 *max_brightness_clone)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	return mi_dsi_panel_get_max_brightness_clone(dsi_display->panel,
			max_brightness_clone);
}

ssize_t mi_dsi_display_get_hw_vsync_info(void *display,
			char *buf, size_t size)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	return mi_sde_encoder_calc_hw_vsync_info(dsi_display, buf, size);
}

static ssize_t compose_ddic_cell_id(char *outbuf, u32 outbuf_len,
		const u8 *inbuf, u32 inbuf_len)
{
	static const char ddic_cell_id_dictionary[] = {
		'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B',
		'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',
		'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
	};
	ssize_t count = 0;
	char ch;
	int i;

	if (!outbuf || !inbuf) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	for (i = 0; i < inbuf_len; i++) {
		ch = inbuf[i] < ARRAY_SIZE(ddic_cell_id_dictionary) ?
				ddic_cell_id_dictionary[inbuf[i]] : '?';
		if (i == inbuf_len - 1)
			count += scnprintf(outbuf + count, outbuf_len - count,
					"%c\n", ch);
		else
			count += scnprintf(outbuf + count, outbuf_len - count,
					"%c", ch);
	}

	return count;
}

static ssize_t mi_dsi_display_read_ddic_cell_id(struct dsi_display *display,
		char *buf, size_t size)
{
	struct drm_panel_cell_id_config *config;
	struct dsi_panel *panel;
	int rc = 0;

	if (!display || !display->panel)
		return -EINVAL;

	panel = display->panel;
	config = &panel->mi_cfg.cell_id_config;
	if (!config->cell_id_cmd.count || !config->cell_id_cmds_rlen ||
	    !config->return_buf)
		return -EINVAL;

	if (config->pre_tx_cmd.count) {
		mutex_lock(&panel->panel_lock);
		rc = mi_dsi_panel_write_cmd_set(panel, &config->pre_tx_cmd);
		mutex_unlock(&panel->panel_lock);
		if (rc) {
			DISP_ERROR("[%s]failed send cell id pre tx cmds, rc=%d\n",
					panel->name, rc);
			return rc;
		}
	}

	memset(config->return_buf, 0x0, config->cell_id_cmds_rlen);
	rc = mi_dsi_display_cmd_read(display, config->cell_id_cmd.cmds[0],
			config->return_buf, config->cell_id_cmds_rlen);
	if (rc != config->cell_id_cmds_rlen) {
		DISP_ERROR("failed to read panel cell id, rc = %d\n", rc);
		return -EINVAL;
	}

	return compose_ddic_cell_id(buf, size, config->return_buf,
			config->cell_id_cmds_rlen);
}

ssize_t mi_dsi_display_read_cell_id(void *display,
			char *buf, size_t size)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	int ret = 0;

	if (!dsi_display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	if (!strlen(cell_id_info_str)) {
		mi_dsi_acquire_wakelock(dsi_display->panel);
		ret = mi_dsi_display_read_ddic_cell_id(dsi_display, buf, size);
		mi_dsi_release_wakelock(dsi_display->panel);
	} else {
		ret = scnprintf(buf, size, "%s\n", cell_id_info_str);
	}

	return ret;
}

void mi_dsi_display_wakeup_pending_doze_work(struct dsi_display *display)
{
	struct disp_feature *df = mi_get_disp_feature();
	struct disp_display *dd_ptr;

	if (!display || !df)
		return;

	dd_ptr = &df->d_display[mi_get_disp_id(display->display_type)];
	if (dd_ptr->display != display)
		return;

	if (atomic_read(&dd_ptr->pending_doze_cnt)) {
		DISP_INFO("%s display wake up pending doze work, pending_doze_cnt = %d\n",
			display->display_type,
			atomic_read(&dd_ptr->pending_doze_cnt));
		wake_up_interruptible_all(&dd_ptr->pending_wq);
	}
}

int mi_dsi_display_cmd_read(struct dsi_display *display,
			struct dsi_cmd_desc cmd, u8 *rx_buf, u32 rx_len)
{
	bool state = false;
	int rc = 0;

	if (!display) {
		DISP_ERROR("Invalid display ptr\n");
		return -EINVAL;
	}

	cmd.msg.flags |= MIPI_DSI_MSG_UNICAST_COMMAND;
	cmd.msg.rx_buf = rx_buf;
	cmd.msg.rx_len = rx_len;
	cmd.ctrl_flags = DSI_CTRL_CMD_READ;

	mutex_lock(&display->display_lock);
	rc = dsi_display_ctrl_get_host_init_state(display, &state);
	if (rc || !state) {
		DISP_ERROR("[DSI] Invalid host state %d rc %d\n", state, rc);
		rc = -EPERM;
		goto end;
	}

	rc = dsi_display_cmd_rx(display, &cmd);
	if (rc <= 0)
		DISP_ERROR("[DSI] Display command receive failed, rc=%d\n", rc);
end:
	mutex_unlock(&display->display_lock);
	return rc;
}

int mi_dsi_display_check_flatmode_status(void *display, bool *status)
{
	struct dsi_display *dsi_display = (struct dsi_display *)display;
	int rc = 0;

	if (!dsi_display || !status) {
		DISP_ERROR("Invalid display/status ptr\n");
		return -EINVAL;
	}

	*status = false;

	if (sde_kms_is_suspend_blocked(dsi_display->drm_dev)) {
		DISP_ERROR("sde_kms is suspended, skip to write dsi cmd\n");
		return -EBUSY;
	}

	mi_dsi_acquire_wakelock(dsi_display->panel);
	rc = mi_dsi_panel_flatmode_validate_status(dsi_display, status);
	mi_dsi_release_wakelock(dsi_display->panel);

	return rc;
}

struct drm_panel *mi_of_drm_find_panel_for_touch(const struct device_node *np)
{
	return of_drm_find_panel(np);
}
EXPORT_SYMBOL(mi_of_drm_find_panel_for_touch);

bool mi_dsi_display_ramdump_support(void)
{
	/* when debug policy is 0x0 or 0x20, full dump not supported */
	if (strcmp(display_debug_policy, "0x0") != 0 &&
	    strcmp(display_debug_policy, "0x20") != 0)
		return true;
	return false;
}

/* Called when the display is bound to the DRM device */
void mi_dsi_display_bind(struct dsi_display *display)
{
	int disp_id = mi_get_disp_id(display->display_type);
	int rc;

	rc = mi_disp_feature_attach_display(display, disp_id, MI_INTF_DSI);
	if (rc)
		DISP_ERROR("failed to attach %s display(%s intf)\n",
				get_disp_id_name(disp_id),
				get_disp_intf_type_name(MI_INTF_DSI));
}

void mi_dsi_display_unbind(struct dsi_display *display)
{
	int disp_id = mi_get_disp_id(display->display_type);
	struct disp_feature *df = mi_get_disp_feature();

	if (!df || df->d_display[disp_id].display != display)
		return;

	if (mi_disp_feature_detach_display(display, disp_id, MI_INTF_DSI))
		DISP_ERROR("failed to detach %s display(%s intf)\n",
				get_disp_id_name(disp_id),
				get_disp_intf_type_name(MI_INTF_DSI));
}

/* Called from dsi_display_mi_notify() for every state update */
void mi_dsi_display_notify(struct dsi_display *display,
			enum dsi_display_mi_state state, u32 value, bool changed)
{
	int disp_id = mi_get_disp_id(display->display_type);

	switch (state) {
	case DSI_DISPLAY_MI_POWER:
		DISP_TIME_INFO("Display (%s), Power mode (%s)\n",
				display->display_type,
				get_display_power_mode_name(value));
		mi_disp_feature_event_notify_by_type(disp_id,
				MI_DISP_EVENT_POWER, sizeof(value), value);
		/* doze brightness requests wait for the low power modes */
		mi_dsi_display_wakeup_pending_doze_work(display);
		break;
	case DSI_DISPLAY_MI_FPS:
		/* A DDIC refresh rate described as h_skew is reported instead */
		if (display->panel && display->panel->cur_mode &&
		    display->panel->cur_mode->timing.h_skew)
			value = display->panel->cur_mode->timing.h_skew;
		mi_disp_feature_event_notify_by_type(disp_id,
				MI_DISP_EVENT_FPS, sizeof(value), value);
		if (changed)
			mi_disp_feature_sysfs_notify(disp_id,
					MI_SYSFS_DYNAMIC_FPS);
		break;
	default:
		break;
	}
}

/* Called when local HBM becomes visible or is turned off */
void mi_dsi_display_notify_fod_ui(struct dsi_display *display, bool fod_ui,
			u32 lhbm_target)
{
	u32 fod_ui_ready = LOCAL_HBM_UI_NONE;

	if (fod_ui) {
		fod_ui_ready = LOCAL_HBM_UI_READY;
		if (lhbm_target == 110)
			fod_ui_ready |= FOD_LOW_BRIGHTNESS_CAPTURE;
	}

	mi_disp_feature_event_notify_by_type(mi_get_disp_id(display->display_type),
			MI_DISP_EVENT_FOD, sizeof(fod_ui_ready), fod_ui_ready);
	DISP_INFO("%s display fod_ui_ready notify=%d\n", display->display_type,
			fod_ui_ready);
}

module_param_string(oled_wp, oled_wp_info_str, MAX_CMDLINE_PARAM_LEN, 0600);
MODULE_PARM_DESC(oled_wp, "msm_drm.oled_wp=<wp info> while <wp info> is 'white point info' ");

module_param_string(sec_oled_wp, sec_oled_wp_info_str,
		MAX_CMDLINE_PARAM_LEN, 0600);
MODULE_PARM_DESC(sec_oled_wp, "msm_drm.sec_oled_wp=<wp info> while <wp info> is 'white point info' ");

module_param_string(cell_id, cell_id_info_str, MAX_CMDLINE_PARAM_LEN, 0600);
MODULE_PARM_DESC(cell_id, "msm_drm.cell_id=<cell id> while <cell id> is 'cell id info' ");

module_param_string(debugpolicy, display_debug_policy,
		MAX_DEBUG_POLICY_CMDLINE_LEN, 0600);
MODULE_PARM_DESC(debugpolicy, "msm_drm.debugpolicy=<debug policy> to indicate supporting ramdump or not ");
