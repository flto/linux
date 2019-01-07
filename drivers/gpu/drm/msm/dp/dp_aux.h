/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2019, The Linux Foundation. All rights reserved.
 */

#ifndef _DP_AUX_H_
#define _DP_AUX_H_

#include "dp_catalog.h"
#include <drm/drm_dp_helper.h>

enum dp_aux_error {
	DP_AUX_ERR_NONE	= 0,
	DP_AUX_ERR_ADDR	= -1,
	DP_AUX_ERR_TOUT	= -2,
	DP_AUX_ERR_NACK	= -3,
	DP_AUX_ERR_DEFER	= -4,
	DP_AUX_ERR_NACK_DEFER	= -5,
};

struct dp_aux {
	struct drm_dp_aux *drm_aux;
	struct dp_aux_cfg *cfg;
};

int dp_aux_register(struct dp_aux *aux);
void dp_aux_unregister(struct dp_aux *aux);
void dp_aux_isr(struct dp_aux *aux);
void dp_aux_init(struct dp_aux *aux, struct dp_aux_cfg *aux_cfg);
void dp_aux_deinit(struct dp_aux *aux);
void dp_aux_reconfig(struct dp_aux *aux);

struct dp_aux *dp_aux_get(struct device *dev, struct dp_catalog_aux *catalog,
			  struct dp_aux_cfg *aux_cfg);
void dp_aux_put(struct dp_aux *aux);

#endif /*__DP_AUX_H_*/
