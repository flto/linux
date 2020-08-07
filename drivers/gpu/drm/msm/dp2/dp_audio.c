// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 */

#include "dp_display.h"

#define DP_DEBUG printk

#include <sound/hdmi-codec.h>

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

static void set_header(struct msm_dp *dp, u32 reg, u8 hb1, u8 hb2, u8 hb3)
{
	u32 tmp;

	tmp = dp_read(dp, reg);
	tmp &= 0xffff;
	tmp |= hb1 << 16 | dp_header_get_parity(hb1) << 24;
	dp_write(dp, reg, tmp);

	if (reg == REG_MMSS_DP_AUDIO_ISRC_0)
		tmp = dp_read(dp, reg + 4) & 0xffff0000;
	else
		tmp = hb3 << 16 | dp_header_get_parity(hb3) << 24;
	tmp |= hb2 << 0 | dp_header_get_parity(hb2) << 8;
	dp_write(dp, reg + 4, tmp);
}

void dp_audio_setup(struct msm_dp *dp)
{
	static const struct {
		u8 vendor[8];
		u8 product[16];
		u8 device_type;
		u8 pad[3];
		u32 zero;
	} sdp_infoframe = {
		"Qualcomm",
		"Snapdragon",
		HDMI_SPD_SDI_UNKNOWN,
	};
	u32 val, i;

	val = dp_read(dp, REG_MMSS_DP_SDP_CFG);
	/* AUDIO_TIMESTAMP_SDP_EN */
	val |= BIT(1);
	/* AUDIO_STREAM_SDP_EN */
	val |= BIT(2);
	/* AUDIO_COPY_MANAGEMENT_SDP_EN */
	val |= BIT(5);
	/* AUDIO_ISRC_SDP_EN  */
	val |= BIT(6);
	/* GENERIC1_SDP for SPD Infoframe */
	val |= BIT(18);
	/* AUDIO_INFOFRAME_SDP_EN  */
	val |= BIT(20);
	dp_write(dp, REG_MMSS_DP_SDP_CFG, val);

	val = dp_read(dp, REG_MMSS_DP_SDP_CFG2);
	/* IFRM_REGSRC -> Do not use reg values */
	val &= ~BIT(0);
	/* AUDIO_STREAM_HB3_REGSRC-> Do not use reg values */
	val &= ~BIT(1);
	/* 28 data bytes for SPD Infoframe with GENERIC1 set */
	val |= BIT(17);
	dp_write(dp, REG_MMSS_DP_SDP_CFG2, val);

	dp_write(dp, REG_MMSS_DP_SDP_CFG3 + 0, 0x01);
	dp_write(dp, REG_MMSS_DP_SDP_CFG3 + 0, 0x00);

	set_header(dp, REG_MMSS_DP_GENERIC1_0, HDMI_INFOFRAME_TYPE_SPD, 0x1b, (0x0 | (0x12 << 2)));
	for (i = 0; i < 8; i++)
		dp_write(dp, REG_MMSS_DP_GENERIC1_2 + i * 4, ((const u32*) &sdp_infoframe)[i]);

	set_header(dp, REG_MMSS_DP_AUDIO_STREAM_0, DP_SDP_AUDIO_STREAM, 0x00, 1); /* 2 channels */
	set_header(dp, REG_MMSS_DP_AUDIO_TIMESTAMP_0, DP_SDP_AUDIO_TIMESTAMP, 0x17, 0x0 | (0x11 << 2));
	set_header(dp, REG_MMSS_DP_AUDIO_INFOFRAME_0, HDMI_INFOFRAME_TYPE_AUDIO, 0x1b, 0x0 | (0x11 << 2));
	set_header(dp, REG_MMSS_DP_AUDIO_COPYMANAGEMENT_0, DP_SDP_AUDIO_COPYMANAGEMENT, 0x0f, 0);
	set_header(dp, REG_MMSS_DP_AUDIO_ISRC_0, DP_SDP_ISRC, 0x0f, 0);

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
	val |= BIT(0);
	dp_write(dp, REG_MMSS_DP_AUDIO_CFG, val);
}

static int msm_hdmi_audio_hw_params(struct device *dev, void *data,
				    struct hdmi_codec_daifmt *daifmt,
				    struct hdmi_codec_params *params)
{
	return 0;
}

static void msm_hdmi_audio_shutdown(struct device *dev, void *data)
{
#if 0
	struct msm_dp *dp = data;
	u32 val;

	val = dp_read(dp, REG_MMSS_DP_AUDIO_CFG);
	val &= ~BIT(0);
	dp_write(dp, REG_MMSS_DP_AUDIO_CFG, val);
#endif
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
