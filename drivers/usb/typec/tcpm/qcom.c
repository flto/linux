// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/usb/tcpm.h>

/* TYPE C registers */
#define TYPE_C_SNK_STATUS_REG			(pdphy->typec_base + 0x06)
#define DETECTED_SRC_TYPE_MASK			GENMASK(3, 0)
#define SNK_RP_STD_BIT				BIT(3)
#define SNK_RP_1P5_BIT				BIT(2)
#define SNK_RP_3P0_BIT				BIT(1)
#define SNK_RP_SHORT_BIT			BIT(0)

#define TYPE_C_SRC_STATUS_REG			(pdphy->typec_base + 0x08)
#define DETECTED_SNK_TYPE_MASK			GENMASK(4, 0)
#define SRC_HIGH_BATT_BIT			BIT(5)
#define SRC_DEBUG_ACCESS_BIT			BIT(4)
#define SRC_RD_OPEN_BIT				BIT(3)
#define SRC_RD_RA_VCONN_BIT			BIT(2)
#define SRC_RA_OPEN_BIT				BIT(1)
#define AUDIO_ACCESS_RA_RA_BIT			BIT(0)

#define TYPEC_MISC_STATUS		(pdphy->typec_base + 0xb)
#define CC_ATTACHED			BIT(0)
#define CC_ORIENTATION			BIT(1)
#define SNK_SRC_MODE			BIT(6)

#define TYPEC_MODE_CFG			(pdphy->typec_base + 0x44)
#define TYPEC_DISABLE_CMD		BIT(0)
#define EN_SNK_ONLY			BIT(1)
#define EN_SRC_ONLY			BIT(2)
#define EN_TRY_SRC			BIT(3)
#define EN_TRY_SNK			BIT(4)

#define TYPEC_VCONN_CONTROL		(pdphy->typec_base + 0x46)
#define VCONN_EN_SRC			BIT(0)
#define VCONN_EN_VAL			BIT(1)
#define VCONN_EN_ORIENTATION		BIT(2)

#define TYPE_C_CCOUT_CONTROL_REG		(pdphy->typec_base + 0x48)
#define TYPEC_CCOUT_BUFFER_EN_BIT		BIT(2)
#define TYPEC_CCOUT_VALUE_BIT			BIT(1)
#define TYPEC_CCOUT_SRC_BIT			BIT(0)

#define TYPEC_EXIT_STATE_CFG		(pdphy->typec_base + 0x50)
#define SEL_SRC_UPPER_REF		BIT(2)

#define TYPE_C_CURRSRC_CFG_REG		(pdphy->typec_base + 0x52)

#define TYPEC_INTR_EN_CFG_1		(pdphy->typec_base + 0x5e)
#define TYPEC_INTR_EN_CFG_1_MASK	GENMASK(7, 0)

#define TYPEC_INTR_EN_CFG_2		(pdphy->typec_base + 0x60)

/* PD PHY registers  */
#define USB_PDPHY_MSG_CONFIG		(pdphy->base + 0x40)

#define USB_PDPHY_EN_CONTROL		(pdphy->base + 0x46)

#define USB_PDPHY_RX_STATUS		(pdphy->base + 0x4A)
#define RX_FRAME_TYPE			(BIT(0) | BIT(1) | BIT(2))

#define USB_PDPHY_FRAME_FILTER		(pdphy->base + 0x4C)
#define FRAME_FILTER_EN_HARD_RESET	BIT(5)
#define FRAME_FILTER_EN_SOP		BIT(0)

#define USB_PDPHY_TX_SIZE		(pdphy->base + 0x42)

#define USB_PDPHY_TX_CONTROL		(pdphy->base + 0x44)
#define TX_CONTROL_RETRY_COUNT(n)	(((n) & 0x3) << 5)
#define TX_CONTROL_FRAME_TYPE(n)	(((n) & 0x7) << 2)
#define TX_CONTROL_SEND_SIGNAL		BIT(1)
#define TX_CONTROL_SEND_MSG		BIT(0)

#define USB_PDPHY_RX_SIZE		(pdphy->base + 0x48)

#define USB_PDPHY_RX_ACKNOWLEDGE	(pdphy->base + 0x4B)

#define USB_PDPHY_TX_BUFFER		(pdphy->base + 0x60)
#define USB_PDPHY_RX_BUFFER		(pdphy->base + 0x80)

enum {
	SIG_TX,
	MSG_TX,
	MSG_TX_FAILED,
	MSG_TX_DISCARDED,
	SIG_RX,
	MSG_RX,
	MSG_RX_DISCARDED,
	TYPEC_IRQ,
	NUM_IRQS = TYPEC_IRQ + 7,
};

static const char * irq_name[] = {
	"sig-tx",
	"msg-tx",
	"msg-tx-failed",
	"msg-tx-discarded",
	"sig-rx",
	"msg-rx",
	"msg-rx-discarded",
	// "typec-or-rid-detect-change", (get lot of this irq while toggling)
	"typec-vpd-detect",
	"typec-cc-state-change",
	"typec-vconn-oc",
	"typec-vbus-change",
	"typec-attach-detach",
	"typec-legacy-cable-detect",
	"typec-try-snk-src-detect",
};

struct usb_pdphy {
	struct device *dev;
	struct regmap *regmap;
	u32 base, typec_base;

	int irq[NUM_IRQS];

	struct regulator *vdd_pdphy, *vbus;
	bool vbus_status;

	struct tcpc_dev tcpc;
	struct tcpm_port *tcpm;
};

#define tcpc_to_pdphy(_tcpc_) container_of(_tcpc_, struct usb_pdphy, tcpc)

static irqreturn_t pdphy_tx_irq(int irq, void *data)
{
	struct usb_pdphy *pdphy = data;
	int ret;

	enum tcpm_transmit_status status = TCPC_TX_FAILED;

	if (irq == pdphy->irq[SIG_TX]) {
		/* TODO: hard reset signal path (TYPEC_EXIT_STATE_CFG?) */

		regmap_write(pdphy->regmap, USB_PDPHY_TX_CONTROL, 0);
		regmap_write(pdphy->regmap, USB_PDPHY_FRAME_FILTER, 0);

		regmap_write(pdphy->regmap, USB_PDPHY_EN_CONTROL, 0);

		regulator_disable(pdphy->vdd_pdphy);

		regmap_update_bits(pdphy->regmap, TYPEC_EXIT_STATE_CFG, 1, 1);
		udelay(500);
		regmap_update_bits(pdphy->regmap, TYPEC_EXIT_STATE_CFG, 1, 0);

		regulator_enable(pdphy->vdd_pdphy);
		regmap_write(pdphy->regmap, USB_PDPHY_MSG_CONFIG, PD_REV20);
		regmap_write(pdphy->regmap, USB_PDPHY_RX_ACKNOWLEDGE, 1);
		regmap_write(pdphy->regmap, USB_PDPHY_EN_CONTROL, 1);
		regmap_write(pdphy->regmap, USB_PDPHY_FRAME_FILTER,
				FRAME_FILTER_EN_SOP | FRAME_FILTER_EN_HARD_RESET);

		tcpm_pd_hard_reset(pdphy->tcpm); /* XXX might be cable reset */
		return IRQ_HANDLED;
	}

	if (irq == pdphy->irq[MSG_TX])
		status = TCPC_TX_SUCCESS;
	else if (irq == pdphy->irq[MSG_TX_DISCARDED])
		status = TCPC_TX_DISCARDED;
	else if (irq == pdphy->irq[MSG_TX_FAILED])
		status = TCPC_TX_FAILED;
	else
		WARN_ON(1);

	tcpm_pd_transmit_complete(pdphy->tcpm, status);

	return IRQ_HANDLED;
}

static irqreturn_t pdphy_rx_irq(int irq, void *data)
{
	unsigned int size, rx_status;
	int ret;
	struct usb_pdphy *pdphy = data;
	struct pd_message msg;

	/* TODO: sig-tx and msg-rx-discarded */
	if (irq == pdphy->irq[SIG_RX] || irq == pdphy->irq[MSG_RX_DISCARDED])
		return IRQ_HANDLED;

	WARN_ON(irq != pdphy->irq[MSG_RX]);

	ret = regmap_read(pdphy->regmap, USB_PDPHY_RX_SIZE, &size);
	if (ret)
		return IRQ_HANDLED;

	size += 1;

	WARN_ON(size < sizeof(u16) || size > sizeof(msg));

	ret = regmap_read(pdphy->regmap, USB_PDPHY_RX_STATUS, &rx_status);
	if (ret)
		return IRQ_HANDLED;

	/* TODO: what do to with rx_status ? */

	ret = regmap_bulk_read(pdphy->regmap, USB_PDPHY_RX_BUFFER, (u8*) &msg, size);
	if (ret)
		return IRQ_HANDLED;

	/* ack to change ownership of rx buffer back to PDPHY RX HW */
	regmap_write(pdphy->regmap, USB_PDPHY_RX_ACKNOWLEDGE, 0);

	tcpm_pd_receive(pdphy->tcpm, &msg);

	return IRQ_HANDLED;
}

static irqreturn_t typec_irq(int irq, void *data)
{
	struct usb_pdphy *pdphy = data;

	/* TODO: call these in this right interrupts instead of all interrupts */

	tcpm_cc_change(pdphy->tcpm);
	tcpm_vbus_change(pdphy->tcpm);

	return IRQ_HANDLED;
}

static int pdphy_init(struct tcpc_dev *tcpc)
{
	struct usb_pdphy *pdphy = tcpc_to_pdphy(tcpc);
	int ret;
	unsigned val;

	/* TYPE C init */

	regmap_write(pdphy->regmap, TYPEC_INTR_EN_CFG_1, 0xff);
	regmap_write(pdphy->regmap, TYPEC_INTR_EN_CFG_2, 0x7f);

	/* start in SINK only mode */
	regmap_write(pdphy->regmap, TYPEC_MODE_CFG, EN_SNK_ONLY);

	/* enable vconn control (init off) */
	regmap_update_bits(pdphy->regmap, TYPEC_VCONN_CONTROL,
			VCONN_EN_SRC | VCONN_EN_VAL, VCONN_EN_SRC);

	regmap_update_bits(pdphy->regmap, TYPEC_EXIT_STATE_CFG,
			SEL_SRC_UPPER_REF, SEL_SRC_UPPER_REF);

	/* PD PHY init */

	ret = regulator_enable(pdphy->vdd_pdphy);
	if (ret)
		return ret;

	/* PD 2.0, DR=TYPEC_DEVICE, PR=TYPEC_SINK */
	ret = regmap_write(pdphy->regmap, USB_PDPHY_MSG_CONFIG, PD_REV20);
	if (ret)
		return ret;

	/* block rx buffer */
	ret = regmap_write(pdphy->regmap, USB_PDPHY_RX_ACKNOWLEDGE, 1);
	if (ret)
		return ret;

	ret = regmap_write(pdphy->regmap, USB_PDPHY_EN_CONTROL, 1);
	if (ret)
		return ret;

	/* update frame filter */
	ret = regmap_write(pdphy->regmap, USB_PDPHY_FRAME_FILTER,
			FRAME_FILTER_EN_SOP | FRAME_FILTER_EN_HARD_RESET);
	if (ret)
		return ret;

	return 0;
}

static int pdphy_get_vbus(struct tcpc_dev *tcpc)
{
	struct usb_pdphy *pdphy = tcpc_to_pdphy(tcpc);
	unsigned int stat;

	regmap_read(pdphy->regmap, TYPEC_MISC_STATUS, &stat);

	/* TODO: fix this.
	 * note: BIT(5) seems to be "sink mode" bit,
	 * which only happens with external vbus
	 */

	return pdphy->vbus_status || !!(stat & BIT(5));
}

static int pdphy_set_vbus(struct tcpc_dev *tcpc, bool on, bool sink)
{
	struct usb_pdphy *pdphy = tcpc_to_pdphy(tcpc);
	int ret;

	if (pdphy->vbus_status == on)
		return 0;

	if (on)
		ret = regulator_enable(pdphy->vbus);
	else
		ret = regulator_disable(pdphy->vbus); /* TODO: force disable ? */

	pdphy->vbus_status = on;

	/* There isn't an interrupt for when vbus output is changed */
	tcpm_vbus_change(pdphy->tcpm);

	return ret;
}

static int pdphy_set_vconn(struct tcpc_dev *tcpc, bool on)
{
	struct usb_pdphy *pdphy = tcpc_to_pdphy(tcpc);
	unsigned int stat;

	regmap_read(pdphy->regmap, TYPEC_MISC_STATUS, &stat);

	/* note: assumes orientation doesn't change during vconn enable */

	return regmap_update_bits(pdphy->regmap, TYPEC_VCONN_CONTROL,
		VCONN_EN_ORIENTATION | VCONN_EN_VAL,
		(on ? VCONN_EN_VAL : 0) |
		/* inverse orientation for vconn */
		((stat & CC_ORIENTATION) ? 0 : VCONN_EN_ORIENTATION));
}

static int pdphy_get_cc(struct tcpc_dev *tcpc, enum typec_cc_status *cc1,
			enum typec_cc_status *cc2)
{
	struct usb_pdphy *pdphy = tcpc_to_pdphy(tcpc);
	unsigned int stat, val;

	regmap_read(pdphy->regmap, TYPEC_MISC_STATUS, &stat);

	*cc1 = TYPEC_CC_OPEN;
	*cc2 = TYPEC_CC_OPEN;

	if (!(stat & CC_ATTACHED))
		return 0;

	if (stat & SNK_SRC_MODE) {
		regmap_read(pdphy->regmap, TYPE_C_SRC_STATUS_REG, &val);
		switch (val & DETECTED_SNK_TYPE_MASK) {
		case SRC_RD_OPEN_BIT:
			val = TYPEC_CC_RD;
			break;
		case SRC_RD_RA_VCONN_BIT:
			val = TYPEC_CC_RD;
			*cc1 = TYPEC_CC_RA;
			*cc2 = TYPEC_CC_RA;
			break;
		default:
			dev_warn(pdphy->dev, "unexpected src status %.2x\n", val);
			val = TYPEC_CC_RD;
			break;
		}
	} else {
		regmap_read(pdphy->regmap, TYPE_C_SNK_STATUS_REG, &val);
		switch (val & DETECTED_SRC_TYPE_MASK) {
		case SNK_RP_STD_BIT:
			val = TYPEC_CC_RP_DEF;
			break;
		case SNK_RP_1P5_BIT:
			val = TYPEC_CC_RP_1_5;
			break;
		case SNK_RP_3P0_BIT:
			val = TYPEC_CC_RP_3_0;
			break;
		default:
			dev_warn(pdphy->dev, "unexpected snk status %.2x\n", val);
			val = TYPEC_CC_RP_DEF;
			break;
		}
		val = TYPEC_CC_RP_DEF;
	}

	if (stat & CC_ORIENTATION)
		*cc2 = val;
	else
		*cc1 = val;

	return 0;
}


static int pdphy_set_cc(struct tcpc_dev *tcpc, enum typec_cc_status cc)
{
	struct usb_pdphy *pdphy = tcpc_to_pdphy(tcpc);

	//regmap_update_bits(pdphy->regmap, TYPE_C_CCOUT_CONTROL_REG,
	//	TYPEC_CCOUT_BUFFER_EN_BIT, TYPEC_CCOUT_BUFFER_EN_BIT);

	switch (cc) {
	case TYPEC_CC_RP_DEF:
	case TYPEC_CC_RP_1_5:
	case TYPEC_CC_RP_3_0:
		regmap_write(pdphy->regmap, TYPE_C_CURRSRC_CFG_REG, cc - TYPEC_CC_RP_DEF);
		//regmap_update_bits(pdphy->regmap, TYPE_C_CCOUT_CONTROL_REG,
		//	TYPEC_CCOUT_SRC_BIT, TYPEC_CCOUT_SRC_BIT);
		regmap_write(pdphy->regmap, TYPEC_MODE_CFG, EN_SRC_ONLY);
		break;
	case TYPEC_CC_RD:
		//regmap_update_bits(pdphy->regmap, TYPE_C_CCOUT_CONTROL_REG,
		//	TYPEC_CCOUT_SRC_BIT, 0);
		regmap_write(pdphy->regmap, TYPEC_MODE_CFG, EN_SNK_ONLY);
		break;
	default:
		dev_warn(pdphy->dev, "unexpected set_cc %d\n", cc);
		break;
	}

	return 0;
}

static int pdphy_set_polarity(struct tcpc_dev *tcpc, enum typec_cc_polarity pol)
{
	//struct usb_pdphy *pdphy = tcpc_to_pdphy(tcpc);

	//regmap_update_bits(pdphy->regmap, TYPE_C_CCOUT_CONTROL_REG, TYPEC_CCOUT_VALUE_BIT,
	//	pol ? TYPEC_CCOUT_VALUE_BIT : 0);

	return 0;
}

static int pdphy_set_current_limit(struct tcpc_dev *tcpc, u32 max_ma, u32 mv)
{
	/* TODO */

	return 0;
}

static int pdphy_set_roles(struct tcpc_dev *tcpc, bool attached,
			   enum typec_role role, enum typec_data_role data)
{
	struct usb_pdphy *pdphy = tcpc_to_pdphy(tcpc);

	return regmap_update_bits(pdphy->regmap, USB_PDPHY_MSG_CONFIG,
			BIT(2) | BIT(3), role << 2 | data << 3);
}

static int pdphy_set_pd_rx(struct tcpc_dev *tcpc, bool on)
{
	struct usb_pdphy *pdphy = tcpc_to_pdphy(tcpc);

	/* use rx acknowledge to block incoming messages
	 * TODO: probably not the right thing to do..
	 */
	regmap_write(pdphy->regmap, USB_PDPHY_RX_ACKNOWLEDGE, !on);

	return 0;
}

static int pdphy_pd_transmit(struct tcpc_dev *tcpc,
			     enum tcpm_transmit_type type,
			     const struct pd_message *msg,
			     unsigned int negotiated_rev)
{
	struct usb_pdphy *pdphy = tcpc_to_pdphy(tcpc);
	unsigned int val;
	int ret;

	ret = regmap_read(pdphy->regmap, USB_PDPHY_RX_ACKNOWLEDGE, &val);
	if (ret || val) {
		dev_err(pdphy->dev, "%s: RX message pending\n", __func__);
		return -EBUSY;
	}

	if (msg) {
		u32 len = 2 + pd_header_cnt(msg->header) * 4;

		ret = regmap_bulk_write(pdphy->regmap, USB_PDPHY_TX_BUFFER, (u8*) msg, len);
		if (ret)
			return ret;

		ret = regmap_write(pdphy->regmap, USB_PDPHY_TX_SIZE, len - 1);
		if (ret)
			return ret;

		val |= TX_CONTROL_FRAME_TYPE(type) | TX_CONTROL_SEND_MSG;

		/* nRetryCount == 2 for PD 3.0, 3 for PD 2.0 */
		if (pd_header_rev(msg->header) == PD_REV30)
			val |= TX_CONTROL_RETRY_COUNT(2);
		else
			val |= TX_CONTROL_RETRY_COUNT(3);
	} else {
		val |= TX_CONTROL_SEND_SIGNAL | TX_CONTROL_RETRY_COUNT(3);
		if (type == TCPC_TX_CABLE_RESET)
			val |= TX_CONTROL_FRAME_TYPE(1);
	}

	ret = regmap_write(pdphy->regmap, USB_PDPHY_TX_CONTROL, 0);
	if (ret)
		return ret;

	usleep_range(2, 3);

	ret = regmap_write(pdphy->regmap, USB_PDPHY_TX_CONTROL, val);
	if (ret)
		return ret;

	return 0;
}

static int pdphy_start_toggling(struct tcpc_dev *tcpc,
				enum typec_port_type port_type,
				enum typec_cc_status cc)
{
	struct usb_pdphy *pdphy = tcpc_to_pdphy(tcpc);
	u8 mode = 0;

	switch (port_type) {
	case TYPEC_PORT_SRC:
		mode = EN_SRC_ONLY;
		break;
	case TYPEC_PORT_SNK:
		mode = EN_SNK_ONLY;
		break;
	case TYPEC_PORT_DRP:
		/* this bit seems to prioritize sink mode when it is possible? */
		mode = EN_TRY_SNK;
		break;
	}

	/* force it to toggle at least once */

	regmap_write(pdphy->regmap, TYPEC_MODE_CFG, BIT(0));

	usleep_range(100, 200);

	regmap_write(pdphy->regmap, TYPEC_MODE_CFG, mode);

	/* TODO: */
#if 0
	switch (cc) {
	case TYPEC_CC_RP_1_5:
		usbc_ctrl |= USBC_CONTROL1_CURSRC_UA_180;
		break;
	case TYPEC_CC_RP_3_0:
		usbc_ctrl |= USBC_CONTROL1_CURSRC_UA_330;
		break;
	default:
		usbc_ctrl |= USBC_CONTROL1_CURSRC_UA_80;
		break;
	}
#endif

	return 0;
}

static int pdphy_probe(struct platform_device *pdev)
{
	int ret, i;
	struct usb_pdphy *pdphy;

	pdphy = devm_kzalloc(&pdev->dev, sizeof(*pdphy), GFP_KERNEL);
	if (!pdphy)
		return -ENOMEM;

	pdphy->dev = &pdev->dev;

	pdphy->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!pdphy->regmap) {
		dev_err(&pdev->dev, "Couldn't get parent's regmap\n");
		return -EINVAL;
	}

	pdphy->base = 0x1700;
	pdphy->typec_base = 0x1500;

	pdphy->vdd_pdphy = devm_regulator_get(&pdev->dev, "vdd-pdphy");
	if (IS_ERR(pdphy->vdd_pdphy)) {
		dev_err(&pdev->dev, "unable to get vdd-pdphy\n");
		return PTR_ERR(pdphy->vdd_pdphy);
	}

	pdphy->vbus = devm_regulator_get(&pdev->dev, "vbus");
	if (IS_ERR(pdphy->vbus)) {
		dev_err(&pdev->dev, "unable to get vbus\n");
		return PTR_ERR(pdphy->vbus);
	}

	for (i = 0; i < ARRAY_SIZE(pdphy->irq); i++) {
		pdphy->irq[i] = of_irq_get_byname(pdev->dev.of_node, irq_name[i]);
		if (pdphy->irq[i] < 0)
			return pdphy->irq[i];
	}

	pdphy->tcpc.init = pdphy_init;
	pdphy->tcpc.get_vbus = pdphy_get_vbus;
	pdphy->tcpc.set_vbus = pdphy_set_vbus;
	pdphy->tcpc.set_cc = pdphy_set_cc;
	pdphy->tcpc.get_cc = pdphy_get_cc;
	pdphy->tcpc.set_polarity = pdphy_set_polarity;
	pdphy->tcpc.set_vconn = pdphy_set_vconn;
	pdphy->tcpc.set_current_limit = pdphy_set_current_limit;
	pdphy->tcpc.start_toggling = pdphy_start_toggling;

	pdphy->tcpc.set_pd_rx = pdphy_set_pd_rx;
	pdphy->tcpc.set_roles = pdphy_set_roles;
	pdphy->tcpc.pd_transmit = pdphy_pd_transmit;

	pdphy->tcpc.fwnode = device_get_named_child_node(pdphy->dev, "connector");
	if (IS_ERR(pdphy->tcpc.fwnode))
		return PTR_ERR(pdphy->tcpc.fwnode);

	pdphy->tcpm = tcpm_register_port(pdphy->dev, &pdphy->tcpc);
	if (IS_ERR(pdphy->tcpm)) {
		fwnode_remove_software_node(pdphy->tcpc.fwnode);
		return PTR_ERR(pdphy->tcpm);
	}

	for (i = 0; i < ARRAY_SIZE(pdphy->irq); i++) {
		irq_handler_t handler = pdphy_tx_irq;
		if (i >= SIG_RX)
			handler = pdphy_rx_irq;
		if (i >= TYPEC_IRQ)
			handler = typec_irq;

		ret = devm_request_irq(pdphy->dev, pdphy->irq[i], handler, 0,
				irq_name[i], pdphy);
		if (ret) {
			tcpm_unregister_port(pdphy->tcpm);
			fwnode_remove_software_node(pdphy->tcpc.fwnode);
			return ret;
		}
	}

	platform_set_drvdata(pdev, pdphy);
	return 0;
}

static int pdphy_remove(struct platform_device *pdev)
{
	struct usb_pdphy *pdphy = platform_get_drvdata(pdev);

	tcpm_unregister_port(pdphy->tcpm);

	return 0;
}

static const struct of_device_id pdphy_match_table[] = {
	{ .compatible = "qcom,pm8150b-tcpm" },
	{ },
};
MODULE_DEVICE_TABLE(of, pdphy_match_table);

static struct platform_driver pdphy_driver = {
	 .driver	 = {
		 .name			= "qpnp-pdphy",
		 .of_match_table	= pdphy_match_table,
	 },
	 .probe		= pdphy_probe,
	 .remove	= pdphy_remove,
};

module_platform_driver(pdphy_driver);

MODULE_DESCRIPTION("QPNP PD PHY Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qpnp-pdphy");
