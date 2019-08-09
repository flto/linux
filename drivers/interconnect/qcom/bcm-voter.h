/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#ifndef __QCOM_ICC_BCM_VOTER_H__
#define __QCOM_ICC_BCM_VOTER_H__

#include <soc/qcom/cmd-db.h>
#include <soc/qcom/rpmh.h>
#include <soc/qcom/tcs.h>

#include "icc-rpmh.h"

#define BCM_TCS_CMD_COMMIT_SHFT		30
#define BCM_TCS_CMD_COMMIT_MASK		0x40000000
#define BCM_TCS_CMD_VALID_SHFT		29
#define BCM_TCS_CMD_VALID_MASK		0x20000000
#define BCM_TCS_CMD_VOTE_X_SHFT		14
#define BCM_TCS_CMD_VOTE_MASK		0x3fff
#define BCM_TCS_CMD_VOTE_Y_SHFT		0
#define BCM_TCS_CMD_VOTE_Y_MASK		0xfffc000

#define BCM_TCS_CMD(commit, valid, vote_x, vote_y)		\
	(((commit) << BCM_TCS_CMD_COMMIT_SHFT) |		\
	((valid) << BCM_TCS_CMD_VALID_SHFT) |			\
	((cpu_to_le32(vote_x) &					\
	BCM_TCS_CMD_VOTE_MASK) << BCM_TCS_CMD_VOTE_X_SHFT) |	\
	((cpu_to_le32(vote_y) &					\
	BCM_TCS_CMD_VOTE_MASK) << BCM_TCS_CMD_VOTE_Y_SHFT))

#define DEFINE_QBCM(_name, _bcmname, _keepalive, _numnodes, ...)	\
		static struct qcom_icc_bcm _name = {			\
		.name = _bcmname,					\
		.keepalive = _keepalive,				\
		.num_nodes = _numnodes,					\
		.nodes = { __VA_ARGS__ },				\
	}

struct bcm_voter *of_bcm_voter_get(struct device *dev, const char *name);
int qcom_icc_bcm_voter_add(struct bcm_voter *voter, struct qcom_icc_bcm *bcm);
int qcom_icc_bcm_voter_commit(struct bcm_voter *voter);

#endif
