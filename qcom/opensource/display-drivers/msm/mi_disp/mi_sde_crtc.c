// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "mi_sde_crtc:[%s:%d] " fmt, __func__, __LINE__

#include "msm_drv.h"
#include "mi_sde_crtc.h"

/*
 * The CRTC keeps the mi_fod_sync_info property of 5.10. As on 5.10 (see
 * "display-drivers: Disable xiaomi layer handling"), the FOD/AOD layer flags
 * written to it are not acted on, AOSP never sets them.
 */
void mi_sde_crtc_install_properties(struct msm_property_info *property_info)
{
	msm_property_install_range(property_info, "mi_fod_sync_info",
		0x0, 0, U32_MAX, 0, CRTC_PROP_MI_FOD_SYNC_INFO);
}
