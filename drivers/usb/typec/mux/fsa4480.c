// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Fairchild FSA4480
 *
 * Copyright (C) 2018-2020 The Linux Foundation
 */

#include <linux/module.h>
#include <linux/usb/pd.h>
#include <linux/usb/typec_mux.h>
#include <linux/usb/typec_dp.h>
#include <linux/regmap.h>
#include <linux/i2c.h>

#define FSA4480_SWITCH_SETTINGS 0x04
#define FSA4480_SWITCH_CONTROL  0x05
#define FSA4480_SWITCH_STATUS1  0x07
#define FSA4480_SLOW_L          0x08
#define FSA4480_SLOW_R          0x09
#define FSA4480_SLOW_MIC        0x0A
#define FSA4480_SLOW_SENSE      0x0B
#define FSA4480_SLOW_GND        0x0C
#define FSA4480_DELAY_L_R       0x0D
#define FSA4480_DELAY_L_MIC     0x0E
#define FSA4480_DELAY_L_SENSE   0x0F
#define FSA4480_DELAY_L_AGND    0x10
#define FSA4480_RESET           0x1E

struct fsa4480 {
	struct regmap *regmap;
	struct typec_mux *typec_mux;
	u8 switch_control, switch_settings;
};

static int
fsa_typec_mux_set(struct typec_mux *mux, struct typec_mux_state *state)
{
	struct fsa4480 *fsa = typec_mux_get_drvdata(mux);
	u8 switch_control, switch_settings;

	if (!state->alt)
		return 0;

	if (state->alt->svid == USB_TYPEC_DP_SID && state->alt->active) {
		if (typec_altmode_get_orientation(state->alt) == TYPEC_ORIENTATION_REVERSE)
			switch_control = 0x78;
		else
			switch_control = 0x18;
		switch_settings = 0xf8;
	} else {
		switch_control = 0x18;
		switch_settings = 0x98;
	}

	/* XXX: writing settings resets USB2 connection, so avoid it if possible? */
	if (switch_control == fsa->switch_control &&
	    switch_settings == fsa->switch_settings)
		return 0;

	fsa->switch_control = switch_control;
	fsa->switch_settings = switch_settings;

	regmap_write(fsa->regmap, FSA4480_SWITCH_SETTINGS, 0x80);
	regmap_write(fsa->regmap, FSA4480_SWITCH_CONTROL, switch_control);
	/* FSA4480 chip hardware requirement */
	usleep_range(50, 55);
	regmap_write(fsa->regmap, FSA4480_SWITCH_SETTINGS, 0xf8);
	return 0;
}

static const struct regmap_config fsa4480_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = FSA4480_RESET,
};

static int
fsa4480_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct typec_mux_desc mux_desc = { };
	struct device *dev = &client->dev;
	struct fsa4480 *fsa;

	fsa = devm_kzalloc(dev, sizeof(*fsa), GFP_KERNEL);
	if (!fsa)
		return -ENOMEM;

	fsa->regmap = devm_regmap_init_i2c(client, &fsa4480_regmap_config);
	if (IS_ERR(fsa->regmap))
		return PTR_ERR(fsa->regmap);

	mux_desc.fwnode = dev->fwnode;
	mux_desc.drvdata = fsa;
	mux_desc.set = fsa_typec_mux_set;
	fsa->typec_mux = typec_mux_register(dev, &mux_desc);
	if (IS_ERR(fsa->typec_mux))
		return PTR_ERR(fsa->typec_mux);

	i2c_set_clientdata(client, fsa);

	/* set default settings */
	regmap_write(fsa->regmap, FSA4480_SLOW_L, 0x00);
	regmap_write(fsa->regmap, FSA4480_SLOW_R, 0x00);
	regmap_write(fsa->regmap, FSA4480_SLOW_MIC, 0x00);
	regmap_write(fsa->regmap, FSA4480_SLOW_SENSE, 0x00);
	regmap_write(fsa->regmap, FSA4480_SLOW_GND, 0x00);
	regmap_write(fsa->regmap, FSA4480_DELAY_L_R, 0x00);
	regmap_write(fsa->regmap, FSA4480_DELAY_L_MIC, 0x00);
	regmap_write(fsa->regmap, FSA4480_DELAY_L_SENSE, 0x00);
	regmap_write(fsa->regmap, FSA4480_DELAY_L_AGND, 0x09);
	regmap_write(fsa->regmap, FSA4480_SWITCH_SETTINGS, 0x98);

	return 0;
}

static int fsa4480_remove(struct i2c_client *i2c)
{
	struct fsa4480 *fsa = i2c_get_clientdata(i2c);

	typec_mux_unregister(fsa->typec_mux);

	return 0;
}

static const struct of_device_id fsa4480_dt_match[] = {
	{ .compatible = "fairchild,fsa4480" },
	{ }
};

static struct i2c_driver fsa4480_driver = {
	.driver = {
		.name = "fsa4480",
		.of_match_table = fsa4480_dt_match,
	},
	.probe = fsa4480_probe,
	.remove = fsa4480_remove,
};

module_i2c_driver(fsa4480_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("FSA4480 driver");
