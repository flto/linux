// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 */

#include <linux/interconnect-provider.h>
#include <linux/list_sort.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <soc/qcom/rpmh.h>
#include <soc/qcom/tcs.h>

#include "bcm-voter.h"
#include "icc-rpmh.h"

static LIST_HEAD(bcm_voters);

struct bcm_voter {
	struct device *dev;
	struct device_node *np;
	struct mutex lock;
	struct list_head commit_list;
	struct list_head voter_node;
};

static int cmp_vcd(void *priv, struct list_head *a, struct list_head *b)
{
	const struct qcom_icc_bcm *bcm_a =
			list_entry(a, struct qcom_icc_bcm, list);
	const struct qcom_icc_bcm *bcm_b =
			list_entry(b, struct qcom_icc_bcm, list);

	if (bcm_a->aux_data.vcd < bcm_b->aux_data.vcd)
		return -1;
	else if (bcm_a->aux_data.vcd == bcm_b->aux_data.vcd)
		return 0;
	else
		return 1;
}

static void bcm_aggregate(struct qcom_icc_bcm *bcm)
{
	size_t i;
	u64 agg_avg = 0;
	u64 agg_peak = 0;
	u64 temp;

	for (i = 0; i < bcm->num_nodes; i++) {
		temp = bcm->nodes[i]->sum_avg * bcm->aux_data.width;
		do_div(temp, bcm->nodes[i]->buswidth * bcm->nodes[i]->channels);
		agg_avg = max(agg_avg, temp);

		temp = bcm->nodes[i]->max_peak * bcm->aux_data.width;
		do_div(temp, bcm->nodes[i]->buswidth);
		agg_peak = max(agg_peak, temp);
	}

	temp = agg_avg * 1000ULL;
	do_div(temp, bcm->aux_data.unit);
	bcm->vote_x = temp;

	temp = agg_peak * 1000ULL;
	do_div(temp, bcm->aux_data.unit);
	bcm->vote_y = temp;

	if (bcm->keepalive && bcm->vote_x == 0 && bcm->vote_y == 0) {
		bcm->vote_x = 1;
		bcm->vote_y = 1;
	}
}

static void tcs_cmd_gen(struct tcs_cmd *cmd, u64 vote_x, u64 vote_y,
			u32 addr, bool commit)
{
	bool valid = true;

	if (!cmd)
		return;

	if (vote_x == 0 && vote_y == 0)
		valid = false;

	if (vote_x > BCM_TCS_CMD_VOTE_MASK)
		vote_x = BCM_TCS_CMD_VOTE_MASK;

	if (vote_y > BCM_TCS_CMD_VOTE_MASK)
		vote_y = BCM_TCS_CMD_VOTE_MASK;

	cmd->addr = addr;
	cmd->data = BCM_TCS_CMD(commit, valid, vote_x, vote_y);

	/*
	 * Set the wait for completion flag on command that need to be completed
	 * before the next command.
	 */
	if (commit)
		cmd->wait = true;
}

static void tcs_list_gen(struct list_head *bcm_list,
			 struct tcs_cmd tcs_list[MAX_VCD],
			 int n[MAX_VCD])
{
	struct qcom_icc_bcm *bcm;
	bool commit;
	size_t idx = 0, batch = 0, cur_vcd_size = 0;

	memset(n, 0, sizeof(int) * MAX_VCD);

	list_for_each_entry(bcm, bcm_list, list) {
		commit = false;
		cur_vcd_size++;
		if ((list_is_last(&bcm->list, bcm_list)) ||
			bcm->aux_data.vcd !=
			list_next_entry(bcm, list)->aux_data.vcd) {
			commit = true;
			cur_vcd_size = 0;
		}
		tcs_cmd_gen(&tcs_list[idx], bcm->vote_x, bcm->vote_y,
			    bcm->addr, commit);
		idx++;
		n[batch]++;
		/*
		 * Batch the BCMs in such a way that we do not split them in
		 * multiple payloads when they are under the same VCD. This is
		 * to ensure that every BCM is committed since we only set the
		 * commit bit on the last BCM request of every VCD.
		 */
		if (n[batch] >= MAX_RPMH_PAYLOAD) {
			if (!commit) {
				n[batch] -= cur_vcd_size;
				n[batch + 1] = cur_vcd_size;
			}
			batch++;
		}
	}
}

struct bcm_voter *of_bcm_voter_get(struct device *dev, const char *name)
{
	struct bcm_voter *voter = ERR_PTR(-EPROBE_DEFER);
	struct bcm_voter *temp;
	struct device_node *np, *node;
	int idx = 0;

	if (!dev || !dev->of_node)
		return ERR_PTR(-ENODEV);

	np = dev->of_node;

	if (name) {
		idx = of_property_match_string(np, "bcm-voter-names", name);
		if (idx < 0)
			return ERR_PTR(idx);
	}

	node = of_parse_phandle(np, "qcom,bcm-voter", idx);

	list_for_each_entry(temp, &bcm_voters, voter_node) {
		if (temp->np == node)
			voter = temp;
			break;
	}

	return voter;
}

int qcom_icc_bcm_voter_add(struct bcm_voter *voter, struct qcom_icc_bcm *bcm)
{
	if (!voter)
		return 0;

	mutex_lock(&voter->lock);
	list_add_tail(&bcm->list, &voter->commit_list);
	mutex_unlock(&voter->lock);

	return 0;
}

int qcom_icc_bcm_voter_commit(struct bcm_voter *voter)
{
	struct qcom_icc_bcm *bcm;
	int commit_idx[MAX_VCD];
	struct tcs_cmd cmds[MAX_BCMS];
	int ret = 0;

	if (!voter)
		return 0;

	mutex_lock(&voter->lock);
	list_for_each_entry(bcm, &voter->commit_list, list)
		bcm_aggregate(bcm);

	/*
	 * Pre sort the BCMs based on VCD for ease of generating a command list
	 * that groups the BCMs with the same VCD together. VCDs are numbered
	 * with lowest being the most expensive time wise, ensuring that
	 * those commands are being sent the earliest in the queue. This needs
	 * to be sorted every commit since we can't guarantee the order in which
	 * the BCMs are added to the list.
	 */
	list_sort(NULL, &voter->commit_list, cmp_vcd);

	/*
	 * based on VCD.
	 */
	tcs_list_gen(&voter->commit_list, cmds, commit_idx);

	if (!commit_idx[0])
		goto out;

	ret = rpmh_invalidate(voter->dev);
	if (ret) {
		pr_err("Error invalidating RPMH client (%d)\n", ret);
		goto out;
	}

	ret = rpmh_write_batch(voter->dev, RPMH_ACTIVE_ONLY_STATE,
			       cmds, commit_idx);
	if (ret) {
		pr_err("Error sending AMC RPMH requests (%d)\n", ret);
		goto out;
	}

out:
	INIT_LIST_HEAD(&voter->commit_list);
	mutex_unlock(&voter->lock);
	return ret;
}

static int qcom_icc_bcm_voter_probe(struct platform_device *pdev)
{
	struct bcm_voter *voter;

	voter = devm_kzalloc(&pdev->dev, sizeof(*voter), GFP_KERNEL);
	if (!voter)
		return -ENOMEM;

	voter->dev = &pdev->dev;
	voter->np = pdev->dev.of_node;
	mutex_init(&voter->lock);
	INIT_LIST_HEAD(&voter->commit_list);
	list_add_tail(&voter->voter_node, &bcm_voters);

	return 0;
}

static const struct of_device_id bcm_voter_of_match[] = {
	{ .compatible = "qcom,sdm845-bcm-voter" },
	{ .compatible = "qcom,sc7180-bcm-voter" },
	{ },
};

static struct platform_driver qcom_icc_bcm_voter_driver = {
	.probe = qcom_icc_bcm_voter_probe,
	.driver = {
		.name = "sdm845_bcm_voter",
		.of_match_table = bcm_voter_of_match,
	},
};
module_platform_driver(qcom_icc_bcm_voter_driver);
MODULE_AUTHOR("David Dai <daidavid1@codeaurora.org>");
MODULE_DESCRIPTION("QTI BCM Voter interconnect driver");
MODULE_LICENSE("GPL v2");
