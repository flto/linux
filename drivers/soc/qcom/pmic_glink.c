// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2011-2018, The Linux Foundation. All rights reserved.
// Copyright (c) 2018, Linaro Limited

#include <linux/completion.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/sort.h>
#include <linux/of_platform.h>
#include <linux/rpmsg.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/usb/role.h>

struct pmic_glink {
	struct fwnode_handle *fwnode;
	struct usb_role_switch *role_sw;
	struct work_struct usb_work;
	u8 orientation, orientation_prev;
	u8 dp_data, dp_data_prev;
};

#define NOTIFY_PAYLOAD_SIZE	16
#define USBC_WRITE_BUFFER_SIZE	8

#define MSG_OWNER_USBC_PAN	32780
#define MSG_TYPE_REQ_RESP	1
#define USBC_CMD_WRITE_REQ	0x15
#define USBC_NOTIFY_IND		0x16
struct pmic_glink_hdr {
	u32 owner;
	u32 type;
	u32 opcode;
};

struct usbc_write_buffer_req_msg {
	struct pmic_glink_hdr	hdr;
	u32 cmd;
	u8 port_index;
	u8 reserved1[3];
	u32 reserved;
};

enum altmode_send_msg_type {
	ALTMODE_PAN_EN = 0x10,
	ALTMODE_PAN_ACK,
};

struct altmode_pan_ack_msg {
	u32 cmd_type;
	u8 port_index;
};

int dp_typec_mux_set(bool orientation, bool dp_enabled, bool dp_connected, bool hpd_irq);
int fsa_typec_mux_set(bool orientation);

static void usb_work(struct work_struct *work)
{
	struct pmic_glink *pmic = container_of(work, struct pmic_glink, usb_work);

	if (pmic->orientation != pmic->orientation_prev) {
		fsa_typec_mux_set(pmic->orientation);
		//typec_set_orientation(pmic->typec_port, pmic->orientation);
	}

	if (pmic->dp_data != pmic->dp_data_prev) {
		dp_typec_mux_set(pmic->orientation == 2,
				 (pmic->dp_data & 0x3f) != 0,
				 pmic->dp_data & BIT(6),
				 pmic->dp_data & BIT(7));
	}

	pmic->dp_data_prev = pmic->dp_data;
	pmic->orientation_prev = pmic->orientation;
}

static int pmic_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct device *dev = &rpdev->dev;
	struct pmic_glink *data;
	int ret;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	dev_set_drvdata(dev, data);

	data->fwnode = device_get_named_child_node(dev, "connector");
	if (IS_ERR(data->fwnode))
		return PTR_ERR(data->fwnode);

	data->role_sw = fwnode_usb_role_switch_get(data->fwnode);
	if (IS_ERR(data->role_sw))
		return PTR_ERR(data->role_sw);

	struct usbc_write_buffer_req_msg req = {
		.hdr = {
			.owner = MSG_OWNER_USBC_PAN,
			.type = MSG_TYPE_REQ_RESP,
			.opcode = USBC_CMD_WRITE_REQ,
		},
		.cmd = ALTMODE_PAN_EN,
	};

	//printk("pmic probe\n");

	INIT_WORK(&data->usb_work, usb_work);

	// XXX
	usb_role_switch_set_role(data->role_sw, USB_ROLE_HOST); // USB_ROLE_DEVICE

	ret = rpmsg_send(rpdev->ept, (void *)&req, sizeof(req));
	//printk("rpmsg_send ret=%d\n", ret);
	return 0;
	//return of_platform_populate(rdev->of_node, NULL, NULL, rdev);

out_role_sw_put:
	usb_role_switch_put(data->role_sw);
	return ret;
}

static void pmic_rpmsg_remove(struct rpmsg_device *rpdev)
{
}

struct usbc_notify_ind_msg {
	struct pmic_glink_hdr	hdr;
	u8			payload[NOTIFY_PAYLOAD_SIZE];
	u32			reserved;
};

static int pmic_rpmsg_callback(struct rpmsg_device *rpdev, void *data,
				  int len, void *priv, u32 addr)
{
	struct pmic_glink *pmic = dev_get_drvdata(&rpdev->dev);
	struct usbc_notify_ind_msg *msg = data;

	//printk("hdr: owner=%.8x type=%.8x opcode=%.8x (len=%d)\n", msg->hdr.owner, msg->hdr.type, msg->hdr.opcode, len);

	switch (msg->hdr.opcode & 0xff) {
	case USBC_CMD_WRITE_REQ:
		break;
	case USBC_NOTIFY_IND: {
		//printk("port_index=%2x orientation=%2x dp_data=%2x\n",
		//       msg->payload[0], msg->payload[1], msg->payload[8]);

		pmic->orientation = msg->payload[1];
		if ((msg->hdr.opcode >> 16) == 0xff01)
			pmic->dp_data = msg->payload[8];

		schedule_work(&pmic->usb_work);

		struct usbc_write_buffer_req_msg req = {
			.hdr = {
				.owner = MSG_OWNER_USBC_PAN,
				.type = MSG_TYPE_REQ_RESP,
				.opcode = USBC_CMD_WRITE_REQ,
			},
			.cmd = ALTMODE_PAN_ACK,
			.port_index = msg->payload[0]
		};
		rpmsg_send(rpdev->ept, (void *)&req, sizeof(req));
	} break;
	default:
		break;
	}

	//printk("pmic_rpmsg_callback: %d\n", len);
	return 0;
}

static const struct of_device_id fastrpc_rpmsg_of_match[] = {
	{ .compatible = "qcom,pmic-glink" },
	{ },
};
MODULE_DEVICE_TABLE(of, fastrpc_rpmsg_of_match);

static struct rpmsg_driver fastrpc_driver = {
	.probe = pmic_rpmsg_probe,
	.remove = pmic_rpmsg_remove,
	.callback = pmic_rpmsg_callback,
	.drv = {
		.name = "qcom,pmic-glink",
		.of_match_table = fastrpc_rpmsg_of_match,
	},
};

static int fastrpc_init(void)
{
	return register_rpmsg_driver(&fastrpc_driver);
}
module_init(fastrpc_init);

static void fastrpc_exit(void)
{
	unregister_rpmsg_driver(&fastrpc_driver);
}
module_exit(fastrpc_exit);

MODULE_LICENSE("GPL v2");
