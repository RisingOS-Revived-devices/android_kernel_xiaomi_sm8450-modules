/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#ifndef _MI_DISP_LHBM_H_
#define _MI_DISP_LHBM_H_

#include <linux/types.h>

struct dsi_panel;

bool is_local_hbm(int disp_id);
bool mi_disp_lhbm_fod_enabled(struct dsi_panel *panel);
int mi_disp_set_local_hbm(int disp_id, int local_hbm_value);
int mi_disp_lhbm_fod_set_finger_event(int disp_id, u32 fod_status, bool from_touch);

#endif /* _MI_DISP_LHBM_H_ */
