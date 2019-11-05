// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2017-2018 The Linux Foundation. All rights reserved. */

#include <linux/completion.h>
#include <linux/circ_buf.h>
#include <linux/list.h>

#include "a6xx_gmu.h"
#include "a6xx_gmu.xml.h"

#define HFI_MSG_ID(val) [val] = #val

static const char * const a6xx_hfi_msg_id[] = {
	HFI_MSG_ID(HFI_H2F_MSG_INIT),
	HFI_MSG_ID(HFI_H2F_MSG_FW_VERSION),
	HFI_MSG_ID(HFI_H2F_MSG_BW_TABLE),
	HFI_MSG_ID(HFI_H2F_MSG_PERF_TABLE),
	HFI_MSG_ID(HFI_H2F_MSG_TEST),
	HFI_MSG_ID(HFI_H2F_MSG_FEATURE_CTRL),
	HFI_MSG_ID(HFI_H2F_MSG_CORE_FW_START),
};

static int a6xx_hfi_queue_read(struct a6xx_hfi_queue *queue, u32 *data,
		u32 dwords)
{
	struct a6xx_hfi_queue_header *header = queue->header;
	u32 i, hdr, index = header->read_index;

	if (header->read_index == header->write_index) {
		header->rx_request = 1;
		return 0;
	}

	hdr = queue->data[index];

	/*
	 * If we are to assume that the GMU firmware is in fact a rational actor
	 * and is programmed to not send us a larger response than we expect
	 * then we can also assume that if the header size is unexpectedly large
	 * that it is due to memory corruption and/or hardware failure. In this
	 * case the only reasonable course of action is to BUG() to help harden
	 * the failure.
	 */

	BUG_ON(HFI_HEADER_SIZE(hdr) > dwords);

	for (i = 0; i < HFI_HEADER_SIZE(hdr); i++) {
		data[i] = queue->data[index];
		index = (index + 1) % header->size;
	}
	for (; index & 3; )
		index = (index + 1) % header->size;

	header->read_index = index;
	return HFI_HEADER_SIZE(hdr);
}

static int a6xx_hfi_queue_write(struct a6xx_gmu *gmu,
	struct a6xx_hfi_queue *queue, u32 *data, u32 dwords)
{
	struct a6xx_hfi_queue_header *header = queue->header;
	u32 i, space, index = header->write_index;

	spin_lock(&queue->lock);

	space = CIRC_SPACE(header->write_index, header->read_index,
		header->size);
	if (space < dwords) {
		header->dropped++;
		spin_unlock(&queue->lock);
		return -ENOSPC;
	}

	for (i = 0; i < dwords; i++) {
		queue->data[index] = data[i];
		index = (index + 1) % header->size;
	}
	for (; index & 3; ) {
		queue->data[index] = 0xFAFAFAFA;
		index = (index + 1) % header->size;
	}

	header->write_index = index;
	spin_unlock(&queue->lock);

	gmu_write(gmu, REG_A6XX_GMU_HOST2GMU_INTR_SET, 0x01);
	return 0;
}

static int a6xx_hfi_wait_for_ack(struct a6xx_gmu *gmu, u32 id, u32 seqnum,
		u32 *payload, u32 payload_size)
{
	struct a6xx_hfi_queue *queue = &gmu->queues[HFI_RESPONSE_QUEUE];
	u32 val;
	int ret;

	/* Wait for a response */
	ret = gmu_poll_timeout(gmu, REG_A6XX_GMU_GMU2HOST_INTR_INFO, val,
		val & A6XX_GMU_GMU2HOST_INTR_INFO_MSGQ, 100, 5000);

	if (ret) {
		DRM_DEV_ERROR(gmu->dev,
			"Message %s id %d timed out waiting for response\n",
			a6xx_hfi_msg_id[id], seqnum);
		return -ETIMEDOUT;
	}

	/* Clear the interrupt */
	gmu_write(gmu, REG_A6XX_GMU_GMU2HOST_INTR_CLR,
		A6XX_GMU_GMU2HOST_INTR_INFO_MSGQ);

	for (;;) {
		struct a6xx_hfi_msg_response resp;

		/* Get the next packet */
		ret = a6xx_hfi_queue_read(queue, (u32 *) &resp,
			sizeof(resp) >> 2);

		/* If the queue is empty our response never made it */
		if (!ret) {
			DRM_DEV_ERROR(gmu->dev,
				"The HFI response queue is unexpectedly empty\n");

			return -ENOENT;
		}

		if (HFI_HEADER_ID(resp.header) == HFI_F2H_MSG_ERROR) {
			struct a6xx_hfi_msg_error *error =
				(struct a6xx_hfi_msg_error *) &resp;

			DRM_DEV_ERROR(gmu->dev, "GMU firmware error %d\n",
				error->code);
			continue;
		}

		if (seqnum != HFI_HEADER_SEQNUM(resp.ret_header)) {
			DRM_DEV_ERROR(gmu->dev,
				"Unexpected message id %d on the response queue\n",
				HFI_HEADER_SEQNUM(resp.ret_header));
			continue;
		}

		if (resp.error) {
			DRM_DEV_ERROR(gmu->dev,
				"Message %s id %d returned error %d\n",
				a6xx_hfi_msg_id[id], seqnum, resp.error);
			return -EINVAL;
		}

		/* All is well, copy over the buffer */
		if (payload && payload_size)
			memcpy(payload, resp.payload,
				min_t(u32, payload_size, sizeof(resp.payload)));

		return 0;
	}
}

static int a6xx_hfi_send_msg(struct a6xx_gmu *gmu, int id,
		void *data, u32 size, u32 *payload, u32 payload_size)
{
	struct a6xx_hfi_queue *queue = &gmu->queues[HFI_COMMAND_QUEUE];
	int ret, dwords = size >> 2;
	u32 seqnum;

	seqnum = atomic_inc_return(&queue->seqnum) % 0xfff;

	/* First dword of the message is the message header - fill it in */
	*((u32 *) data) = (seqnum << 20) | (HFI_MSG_CMD << 16) |
		(dwords << 8) | id;

	ret = a6xx_hfi_queue_write(gmu, queue, data, dwords);
	if (ret) {
		DRM_DEV_ERROR(gmu->dev, "Unable to send message %s id %d\n",
			a6xx_hfi_msg_id[id], seqnum);
		return ret;
	}

	return a6xx_hfi_wait_for_ack(gmu, id, seqnum, payload, payload_size);
}

static int a6xx_hfi_send_gmu_init(struct a6xx_gmu *gmu, int boot_state)
{
	struct a6xx_hfi_msg_gmu_init_cmd msg = { 0 };

	msg.dbg_buffer_addr = (u32) gmu->debug->iova;
	msg.dbg_buffer_size = (u32) gmu->debug->size;
	msg.boot_state = boot_state;

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_INIT, &msg, sizeof(msg),
		NULL, 0);
}

static int a6xx_hfi_get_fw_version(struct a6xx_gmu *gmu, u32 *version)
{
	struct a6xx_hfi_msg_fw_version msg = { 0 };

	/* Currently supporting version 2.0*/
	msg.supported_version = (2 << 28) | (0 << 16);

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_FW_VERSION, &msg, sizeof(msg),
		version, sizeof(*version));
}

static int a6xx_hfi_send_perf_table(struct a6xx_gmu *gmu)
{
	struct a6xx_hfi_msg_perf_table msg = { 0 };
	int i;

	msg.num_gpu_levels = gmu->nr_gpu_freqs;
	msg.num_gmu_levels = gmu->nr_gmu_freqs;

	for (i = 0; i < gmu->nr_gpu_freqs; i++) {
		msg.gx_votes[i].vote = gmu->gx_arc_votes[i];
		msg.gx_votes[i].acd = 0xffffffff;
		msg.gx_votes[i].freq = gmu->gpu_freqs[i] / 1000;
	}

	for (i = 0; i < gmu->nr_gmu_freqs; i++) {
		msg.cx_votes[i].vote = gmu->cx_arc_votes[i];
		msg.cx_votes[i].freq = gmu->gmu_freqs[i] / 1000;
	}

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_PERF_TABLE, &msg, sizeof(msg),
		NULL, 0);
}

static int a6xx_hfi_send_bw_table(struct a6xx_gmu *gmu)
{
	struct a6xx_hfi_msg_bw_table msg = {
		.ddr_cmds_data = {
			{0x40000000, 0x40000000, 0x40000000},
			{0x6000060f, 0x6000032d, 0x60000008},
			{0x6000091b, 0x600004c6, 0x60000008},
			{0x60000db6, 0x60000730, 0x60000008},
			{0x600010a3, 0x600008b9, 0x60000008},
			{0x600014b9, 0x60000add, 0x60000008},
			{0x60001760, 0x60000c41, 0x60000008},
			{0x60001ef7, 0x6000103c, 0x60000008},
			{0x60002936, 0x6000152b, 0x60000008},
			{0x60002f5e, 0x600018d6, 0x60000008},
			{0x600036f6, 0x60001cd0, 0x60000008},
			{0x60003fbe, 0x6000216b, 0x60000008},
		},
	};

	/*
	 * The sdm845 GMU doesn't do bus frequency scaling on its own but it
	 * does need at least one entry in the list because it might be accessed
	 * when the GMU is shutting down. Send a single "off" entry.
	 */

	msg.bw_level_num = 12;

	msg.ddr_cmds_num = 3;
	msg.ddr_wait_bitmask = 1;

	msg.ddr_cmds_addrs[0] = 0x50000;
	msg.ddr_cmds_addrs[1] = 0x5003c;
	msg.ddr_cmds_addrs[2] = 0x5000c;
	/*
	 * These are the CX (CNOC) votes.  This is used but the values for the
	 * sdm845 GMU are known and fixed so we can hard code them.
	 */

	msg.cnoc_cmds_num = 3;
	msg.cnoc_wait_bitmask = 1;

	msg.cnoc_cmds_addrs[0] = 0x50034;
	msg.cnoc_cmds_addrs[1] = 0x5007c;
	msg.cnoc_cmds_addrs[2] = 0x5004c;

	msg.cnoc_cmds_data[0][0] =  0x40000000;
	msg.cnoc_cmds_data[0][1] =  0x00000000;
	msg.cnoc_cmds_data[0][2] =  0x40000000;

	msg.cnoc_cmds_data[1][0] =  0x60000001;
	msg.cnoc_cmds_data[1][1] =  0x20000001;
	msg.cnoc_cmds_data[1][2] =  0x60000001;

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_BW_TABLE, &msg, sizeof(msg),
		NULL, 0);
}

static int a6xx_hfi_send_test(struct a6xx_gmu *gmu)
{
	struct a6xx_hfi_msg_test msg = { 0 };

	return a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_TEST, &msg, sizeof(msg),
		NULL, 0);
}

int a6xx_hfi_send_prep_slumber(struct a6xx_gmu *gmu)
{
	struct a6xx_hfi_prep_slumber_cmd msg = { 0 };

	// TODO
	msg.bw = 11;
	msg.freq = gmu->nr_gpu_freqs-1;

	return a6xx_hfi_send_msg(gmu, HDI_H2F_MSG_PREPARE_SLUMBER, &msg,
		sizeof(msg), NULL, 0);
}

int a6xx_hfi_start(struct a6xx_gmu *gmu, int boot_state)
{
	int ret;
	u32 version = 0;

	//ret = a6xx_hfi_send_gmu_init(gmu, boot_state);
	//if (ret)
	//	return ret;

	ret = a6xx_hfi_get_fw_version(gmu, &version);
	if (ret)
		return ret;

	/*
	 * We have to get exchange version numbers per the sequence but at this
	 * point th kernel driver doesn't need to know the exact version of
	 * the GMU firmware
	 */

	ret = a6xx_hfi_send_perf_table(gmu);
	if (ret)
		return ret;

	ret = a6xx_hfi_send_bw_table(gmu);
	if (ret)
		return ret;

	if (1)
	{

	struct a6xx_hfi_msg_feature_ctrl msg = { 0 };
	struct a6xx_hfi_msg_core_fw_start msg2 = { 0 };

	/* Currently supporting version 2.0*/
#define HFI_FEATURE_ECP		1
	msg.feature = HFI_FEATURE_ECP;

	ret =  a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_FEATURE_CTRL, &msg, sizeof(msg), NULL, 0);
	if (ret)
		return ret;

	ret =  a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_CORE_FW_START, &msg2, sizeof(msg2), NULL, 0);
	if (ret)
		return ret;
	}

	{
	struct a6xx_hfi_gx_bw_perf_vote_cmd msg = { 0 };

	msg.ack_type = 1; /* blocking */
	msg.freq = gmu->nr_gpu_freqs-1;
	msg.bw = 11;

	ret =  a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_GX_BW_PERF_VOTE, &msg, sizeof(msg), NULL, 0);
	if (ret)
		return ret;
	}

	{
	struct a6xx_hfi_msg_start msg = { 0 };

	ret =  a6xx_hfi_send_msg(gmu, HFI_H2F_MSG_START, &msg, sizeof(msg), NULL, 0);
	if (ret)
		return ret;
	}

#if 0
	/*
	 * Let the GMU know that there won't be any more HFI messages until next
	 * boot
	 */
	a6xx_hfi_send_test(gmu);
#endif

	return 0;
}

void a6xx_hfi_stop(struct a6xx_gmu *gmu)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gmu->queues); i++) {
		struct a6xx_hfi_queue *queue = &gmu->queues[i];

		if (!queue->header)
			continue;

		if (queue->header->read_index != queue->header->write_index)
			DRM_DEV_ERROR(gmu->dev, "HFI queue %d is not empty\n", i);

		queue->header->read_index = 0;
		queue->header->write_index = 0;
	}
}

static void a6xx_hfi_queue_init(struct a6xx_hfi_queue *queue,
		struct a6xx_hfi_queue_header *header, void *virt, u64 iova,
		u32 id)
{
	spin_lock_init(&queue->lock);
	queue->header = header;
	queue->data = virt;
	atomic_set(&queue->seqnum, 0);

	/* Set up the shared memory header */
	header->iova = iova;
	header->type =  10 << 8 | id;
	header->status = 1;
	header->size = SZ_4K >> 2;
	header->msg_size = 0;
	header->dropped = 0;
	header->rx_watermark = 1;
	header->tx_watermark = 1;
	header->rx_request = 1;
	header->tx_request = 0;
	header->read_index = 0;
	header->write_index = 0;
}

void a6xx_hfi_init(struct a6xx_gmu *gmu)
{
	struct a6xx_gmu_bo *hfi = gmu->hfi;
	struct a6xx_hfi_queue_table_header *table = hfi->virt;
	struct a6xx_hfi_queue_header *headers = hfi->virt + sizeof(*table);
	u64 offset;
	int table_size;

	/*
	 * The table size is the size of the table header plus all of the queue
	 * headers
	 */
	table_size = sizeof(*table);
	table_size += (ARRAY_SIZE(gmu->queues) *
		sizeof(struct a6xx_hfi_queue_header));

	table->version = 0;
	table->size = table_size;
	/* First queue header is located immediately after the table header */
	table->qhdr0_offset = sizeof(*table) >> 2;
	table->qhdr_size = sizeof(struct a6xx_hfi_queue_header) >> 2;
	table->num_queues = ARRAY_SIZE(gmu->queues);
	table->active_queues = ARRAY_SIZE(gmu->queues);

	/* Command queue */
	offset = SZ_4K;
	a6xx_hfi_queue_init(&gmu->queues[0], &headers[0], hfi->virt + offset,
		hfi->iova + offset, 0);

	/* GMU response queue */
	offset += SZ_4K;
	a6xx_hfi_queue_init(&gmu->queues[1], &headers[1], hfi->virt + offset,
		hfi->iova + offset, 1);

	/* DBG queue */
	offset += SZ_4K;
	a6xx_hfi_queue_init(&gmu->queues[2], &headers[2], hfi->virt + offset,
		hfi->iova + offset, 2);

	/* DSP queue */
	offset += SZ_4K;
	a6xx_hfi_queue_init(&gmu->queues[3], &headers[3], hfi->virt + offset,
		hfi->iova + offset, 3);
	headers[3].status = 0;
}
