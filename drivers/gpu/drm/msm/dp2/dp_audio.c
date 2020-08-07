// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 */

#include "dp_display.h"

#define DP_DEBUG printk

#include <sound/hdmi-codec.h>

#define HEADER_BYTE_2_BIT	 0
#define PARITY_BYTE_2_BIT	 8
#define HEADER_BYTE_1_BIT	16
#define PARITY_BYTE_1_BIT	24
#define HEADER_BYTE_3_BIT	16
#define PARITY_BYTE_3_BIT	24

enum dp_catalog_audio_sdp_type {
	DP_AUDIO_SDP_STREAM,
	DP_AUDIO_SDP_TIMESTAMP,
	DP_AUDIO_SDP_INFOFRAME,
	DP_AUDIO_SDP_COPYMANAGEMENT,
	DP_AUDIO_SDP_ISRC,
	DP_AUDIO_SDP_MAX,
};

enum dp_catalog_audio_header_type {
	DP_AUDIO_SDP_HEADER_1,
	DP_AUDIO_SDP_HEADER_2,
	DP_AUDIO_SDP_HEADER_3,
	DP_AUDIO_SDP_HEADER_MAX,
};

static inline u8 dp_ecc_get_g0_value(u8 data)
{
	u8 c[4];
	u8 g[4];
	u8 ret_data = 0;
	u8 i;

	for (i = 0; i < 4; i++)
		c[i] = (data >> i) & 0x01;

	g[0] = c[3];
	g[1] = c[0] ^ c[3];
	g[2] = c[1];
	g[3] = c[2];

	for (i = 0; i < 4; i++)
		ret_data = ((g[i] & 0x01) << i) | ret_data;

	return ret_data;
}

static inline u8 dp_ecc_get_g1_value(u8 data)
{
	u8 c[4];
	u8 g[4];
	u8 ret_data = 0;
	u8 i;

	for (i = 0; i < 4; i++)
		c[i] = (data >> i) & 0x01;

	g[0] = c[0] ^ c[3];
	g[1] = c[0] ^ c[1] ^ c[3];
	g[2] = c[1] ^ c[2];
	g[3] = c[2] ^ c[3];

	for (i = 0; i < 4; i++)
		ret_data = ((g[i] & 0x01) << i) | ret_data;

	return ret_data;
}

static inline u8 dp_header_get_parity(u32 data)
{
	u8 x0 = 0;
	u8 x1 = 0;
	u8 ci = 0;
	u8 iData = 0;
	u8 i = 0;
	u8 parity_byte;
	u8 num_byte = (data > 0xFF) ? 8 : 2;

	for (i = 0; i < num_byte; i++) {
		iData = (data >> i*4) & 0xF;

		ci = iData ^ x1;
		x1 = x0 ^ dp_ecc_get_g1_value(ci);
		x0 = dp_ecc_get_g0_value(ci);
	}

	parity_byte = x1 | (x0 << 4);

	return parity_byte;
}

static const u32 sdp_map[][DP_AUDIO_SDP_HEADER_MAX] = {
	{
		REG_MMSS_DP_AUDIO_STREAM_0,
		REG_MMSS_DP_AUDIO_STREAM_1,
		REG_MMSS_DP_AUDIO_STREAM_1,
	},
	{
		REG_MMSS_DP_AUDIO_TIMESTAMP_0,
		REG_MMSS_DP_AUDIO_TIMESTAMP_1,
		REG_MMSS_DP_AUDIO_TIMESTAMP_1,
	},
	{
		REG_MMSS_DP_AUDIO_INFOFRAME_0,
		REG_MMSS_DP_AUDIO_INFOFRAME_1,
		REG_MMSS_DP_AUDIO_INFOFRAME_1,
	},
	{
		REG_MMSS_DP_AUDIO_COPYMANAGEMENT_0,
		REG_MMSS_DP_AUDIO_COPYMANAGEMENT_1,
		REG_MMSS_DP_AUDIO_COPYMANAGEMENT_1,
	},
	{
		REG_MMSS_DP_AUDIO_ISRC_0,
		REG_MMSS_DP_AUDIO_ISRC_1,
		REG_MMSS_DP_AUDIO_ISRC_1,
	},
};

static u32 dp_audio_get_header(struct msm_dp *dp,
		enum dp_catalog_audio_sdp_type sdp,
		enum dp_catalog_audio_header_type header)
{
	return dp_read(dp, sdp_map[sdp][header]);
}

static void dp_audio_set_header(struct msm_dp *dp,
		u32 data,
		enum dp_catalog_audio_sdp_type sdp,
		enum dp_catalog_audio_header_type header)
{
	dp_write(dp, sdp_map[sdp][header], data);
}

static void dp_audio_stream_sdp(struct msm_dp *dp, unsigned channels)
{
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_1);
	value &= 0x0000ffff;

	new_value = 0x02;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	DP_DEBUG("Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_2);
	value &= 0xffff0000;
	new_value = 0x0;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	DP_DEBUG("Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);

	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_3);
	value &= 0x0000ffff;

	new_value = channels - 1;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	DP_DEBUG("Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
		value, parity_byte);

	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_STREAM, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_timestamp_sdp(struct msm_dp *dp)
{
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_1);
	value &= 0x0000ffff;

	new_value = 0x1;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	DP_DEBUG("Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
		value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_2);
	value &= 0xffff0000;

	new_value = 0x17;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	DP_DEBUG("Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_3);
	value &= 0x0000ffff;

	new_value = (0x0 | (0x11 << 2));
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	DP_DEBUG("Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_TIMESTAMP, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_infoframe_sdp(struct msm_dp *dp)
{
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_1);
	value &= 0x0000ffff;

	new_value = 0x84;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	DP_DEBUG("Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_2);
	value &= 0xffff0000;

	new_value = 0x1b;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	DP_DEBUG("Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_3);
	value &= 0x0000ffff;

	new_value = (0x0 | (0x11 << 2));
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	DP_DEBUG("Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
			new_value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_INFOFRAME, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_copy_management_sdp(struct msm_dp *dp)
{
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_1);
	value &= 0x0000ffff;

	new_value = 0x05;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	DP_DEBUG("Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_2);
	value &= 0xffff0000;

	new_value = 0x0F;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	DP_DEBUG("Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_2);

	/* Config header and parity byte 3 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_3);
	value &= 0x0000ffff;

	new_value = 0x0;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	DP_DEBUG("Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_COPYMANAGEMENT, DP_AUDIO_SDP_HEADER_3);
}

static void dp_audio_isrc_sdp(struct msm_dp *dp)
{
	u32 value, new_value;
	u8 parity_byte;

	/* Config header and parity byte 1 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_1);
	value &= 0x0000ffff;

	new_value = 0x06;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	DP_DEBUG("Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_1);

	/* Config header and parity byte 2 */
	value = dp_audio_get_header(dp,
			DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_2);
	value &= 0xffff0000;

	new_value = 0x0F;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	DP_DEBUG("Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
			value, parity_byte);
	dp_audio_set_header(dp, value,
		DP_AUDIO_SDP_ISRC, DP_AUDIO_SDP_HEADER_2);
}

static int msm_hdmi_audio_hw_params(struct device *dev, void *data,
				    struct hdmi_codec_daifmt *daifmt,
				    struct hdmi_codec_params *params)
{
	struct msm_dp *dp = data;
	u32 val;

	printk("msm_hdmi_audio_hw_params\n");

	dp_catalog_panel_config_spd(dp);

	val = dp_read(dp, REG_MMSS_DP_SDP_CFG);
	/* AUDIO_TIMESTAMP_SDP_EN */
	val |= BIT(1);
	/* AUDIO_STREAM_SDP_EN */
	val |= BIT(2);
	/* AUDIO_COPY_MANAGEMENT_SDP_EN */
	val |= BIT(5);
	/* AUDIO_ISRC_SDP_EN  */
	val |= BIT(6);
	/* AUDIO_INFOFRAME_SDP_EN  */
	val |= BIT(20);
	dp_write(dp, REG_MMSS_DP_SDP_CFG, val);

	val = dp_read(dp, REG_MMSS_DP_SDP_CFG2);
	/* IFRM_REGSRC -> Do not use reg values */
	val &= ~BIT(0);
	/* AUDIO_STREAM_HB3_REGSRC-> Do not use reg values */
	val &= ~BIT(1);
	dp_write(dp, REG_MMSS_DP_SDP_CFG2, val);

	dp_audio_stream_sdp(dp, params->cea.channels);
	dp_audio_timestamp_sdp(dp);
	dp_audio_infoframe_sdp(dp);
	dp_audio_copy_management_sdp(dp);
	dp_audio_isrc_sdp(dp);

	switch (dp->phy_opts.dp.link_rate) {
	case 1620:
		val = 0;
		break;
	case 2700:
		val = 1;
		break;
	case 5400:
		val = 2;
		break;
	case 8100:
		val = 3;
		break;
	}
	dp_write(dp, REG_MMSS_DP_AUDIO_ACR_CTRL, val << 4 | BIT(31) | BIT(8) | BIT(14));

	val = dp_read(dp, REG_MMSS_DP_AUDIO_CFG);
	if (1) // (enable)
		val |= BIT(0);
	else
		val &= ~BIT(0);
	dp_write(dp, REG_MMSS_DP_AUDIO_CFG, val);

	return 0;
}

static void msm_hdmi_audio_shutdown(struct device *dev, void *data)
{
	printk("msm_hdmi_audio_shutdown\n");
}

static const struct hdmi_codec_ops msm_hdmi_audio_codec_ops = {
	.hw_params = msm_hdmi_audio_hw_params,
	.audio_shutdown = msm_hdmi_audio_shutdown,
	//.digital_mute = cdn_dp_audio_digital_mute,
	//.get_eld = dp_audio_get_edid_blk,
};

void dp_audio_init(struct msm_dp *dp)
{
	struct hdmi_codec_pdata codec_data = {
		.ops = &msm_hdmi_audio_codec_ops,
		.max_i2s_channels = 8,
		.i2s = 1,
		// .spdif = 1,
		.data = dp,
	};

	dp->audio_pdev = platform_device_register_data(&dp->pdev->dev,
		HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_AUTO, &codec_data, sizeof(codec_data));
}

static void dp_catalog_config_spd_header(struct msm_dp *dp)
{
	u32 value, new_value, offset = 0;
	u8 parity_byte;

	//if (panel->stream_id == DP_STREAM_1)
	//	offset = MMSS_DP1_GENERIC0_0 - REG_MMSS_DP_GENERIC0_0;

	/* Config header and parity byte 1 */
	value = dp_read(dp, REG_MMSS_DP_GENERIC1_0 + offset);

	new_value = 0x83;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_1_BIT)
			| (parity_byte << PARITY_BYTE_1_BIT));
	//DP_DEBUG("Header Byte 1: value = 0x%x, parity_byte = 0x%x\n",
	//		value, parity_byte);
	dp_write(dp, REG_MMSS_DP_GENERIC1_0 + offset, value);

	/* Config header and parity byte 2 */
	value = dp_read(dp, REG_MMSS_DP_GENERIC1_1 + offset);

	new_value = 0x1b;
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_2_BIT)
			| (parity_byte << PARITY_BYTE_2_BIT));
	//DP_DEBUG("Header Byte 2: value = 0x%x, parity_byte = 0x%x\n",
	//		value, parity_byte);
	dp_write(dp, REG_MMSS_DP_GENERIC1_1 + offset, value);

	/* Config header and parity byte 3 */
	value = dp_read(dp, REG_MMSS_DP_GENERIC1_1 + offset);

	new_value = (0x0 | (0x12 << 2));
	parity_byte = dp_header_get_parity(new_value);
	value |= ((new_value << HEADER_BYTE_3_BIT)
			| (parity_byte << PARITY_BYTE_3_BIT));
	//DP_DEBUG("Header Byte 3: value = 0x%x, parity_byte = 0x%x\n",
	//		new_value, parity_byte);
	dp_write(dp, REG_MMSS_DP_GENERIC1_1 + offset, value);
}

/* OEM NAME */
static const u8 vendor_name[8] = {81, 117, 97, 108, 99, 111, 109, 109};

/* MODEL NAME */
static const u8 product_desc[16] = {83, 110, 97, 112, 100, 114, 97, 103,
	111, 110, 0, 0, 0, 0, 0, 0};

void dp_catalog_panel_config_spd(struct msm_dp *dp)
{
	u32 spd_cfg = 0, spd_cfg2 = 0;
	u8 *vendor = NULL, *product = NULL;
	u32 offset = 0;
	u32 sdp_cfg_off = 0;
	u32 sdp_cfg2_off = 0;

	/*
	 * Source Device Information
	 * 00h unknown
	 * 01h Digital STB
	 * 02h DVD
	 * 03h D-VHS
	 * 04h HDD Video
	 * 05h DVC
	 * 06h DSC
	 * 07h Video CD
	 * 08h Game
	 * 09h PC general
	 * 0ah Bluray-Disc
	 * 0bh Super Audio CD
	 * 0ch HD DVD
	 * 0dh PMP
	 * 0eh-ffh reserved
	 */
	u32 device_type = 0;

	//if (panel->stream_id == DP_STREAM_1)
	//	offset = MMSS_DP1_GENERIC0_0 - REG_MMSS_DP_GENERIC0_0;

	dp_catalog_config_spd_header(dp);

	vendor = vendor_name;
	product = product_desc;

	dp_write(dp, REG_MMSS_DP_GENERIC1_2 + offset,
			((vendor[0] & 0x7f) |
			((vendor[1] & 0x7f) << 8) |
			((vendor[2] & 0x7f) << 16) |
			((vendor[3] & 0x7f) << 24)));
	dp_write(dp, REG_MMSS_DP_GENERIC1_3 + offset,
			((vendor[4] & 0x7f) |
			((vendor[5] & 0x7f) << 8) |
			((vendor[6] & 0x7f) << 16) |
			((vendor[7] & 0x7f) << 24)));
	dp_write(dp, REG_MMSS_DP_GENERIC1_4 + offset,
			((product[0] & 0x7f) |
			((product[1] & 0x7f) << 8) |
			((product[2] & 0x7f) << 16) |
			((product[3] & 0x7f) << 24)));
	dp_write(dp, REG_MMSS_DP_GENERIC1_5 + offset,
			((product[4] & 0x7f) |
			((product[5] & 0x7f) << 8) |
			((product[6] & 0x7f) << 16) |
			((product[7] & 0x7f) << 24)));
	dp_write(dp, REG_MMSS_DP_GENERIC1_6 + offset,
			((product[8] & 0x7f) |
			((product[9] & 0x7f) << 8) |
			((product[10] & 0x7f) << 16) |
			((product[11] & 0x7f) << 24)));
	dp_write(dp, REG_MMSS_DP_GENERIC1_7 + offset,
			((product[12] & 0x7f) |
			((product[13] & 0x7f) << 8) |
			((product[14] & 0x7f) << 16) |
			((product[15] & 0x7f) << 24)));
	dp_write(dp, REG_MMSS_DP_GENERIC1_8 + offset, device_type);
	dp_write(dp, REG_MMSS_DP_GENERIC1_9 + offset, 0x00);

	/*if (panel->stream_id == DP_STREAM_1) {
		sdp_cfg_off = MMSS_DP1_SDP_CFG - REG_MMSS_DP_SDP_CFG;
		sdp_cfg2_off = MMSS_DP1_SDP_CFG2 - REG_MMSS_DP_SDP_CFG2;
	}*/

	spd_cfg = dp_read(dp, REG_MMSS_DP_SDP_CFG + sdp_cfg_off);
	/* GENERIC1_SDP for SPD Infoframe */
	spd_cfg |= BIT(18);
	dp_write(dp, REG_MMSS_DP_SDP_CFG + sdp_cfg_off, spd_cfg);

	spd_cfg2 = dp_read(dp, REG_MMSS_DP_SDP_CFG2 + sdp_cfg2_off);
	/* 28 data bytes for SPD Infoframe with GENERIC1 set */
	spd_cfg2 |= BIT(17);
	dp_write(dp, REG_MMSS_DP_SDP_CFG2 + sdp_cfg2_off, spd_cfg2);

	//dp_catalog_panel_sdp_update(panel);

	// sdp_cfg3_off = MMSS_DP1_SDP_CFG3 - REG_MMSS_DP_SDP_CFG3;
	dp_write(dp, REG_MMSS_DP_SDP_CFG3 + 0, 0x01);
	dp_write(dp, REG_MMSS_DP_SDP_CFG3 + 0, 0x00);
}
