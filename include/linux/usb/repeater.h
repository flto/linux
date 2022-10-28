/* SPDX-License-Identifier: GPL-2.0 */
/*
 * USB Repeater defines
 */

#ifndef __LINUX_USB_REPEATER_H
#define __LINUX_USB_REPEATER_H

struct usb_repeater;

/**
 * struct usb_repeater_ops - set of function pointers for performing repeater operations
 * @reset: operation to be performed for resetting the repeater
 * @init: operation to be performed for initializing the repeater
 * @power_on: powering on the repeater
 * @power_off: powering off the repeater
 */
struct usb_repeater_ops {
	int	(*reset)(struct usb_repeater *rptr, bool assert);
	int	(*init)(struct usb_repeater *rptr);
	int	(*power_on)(struct usb_repeater *rptr);
	int	(*power_off)(struct usb_repeater *rptr);
};

/**
 * struct usb_repeater - represents the repeater device
 * @dev: repeater device
 * @ops: function pointers for performing repeater operations
 * @name: repeater's name
 * @flags: repeater's flags
 * @entry: entry in the global USB repeaters list
 */
struct usb_repeater {
	struct device			*dev;
	const struct usb_repeater_ops	*ops;
	const char			*name;
	unsigned int			flags;

	struct list_head		entry;
};

#if IS_ENABLED(CONFIG_USB_REPEATER)
extern struct usb_repeater *devm_usb_get_repeater_by_phandle(struct device *dev,
	const char *phandle, u8 index);
extern struct usb_repeater *devm_usb_get_repeater_by_node(struct device *dev,
	struct device_node *node, struct notifier_block *nb);
extern int usb_add_repeater_dev(struct usb_repeater *rptr);
extern void usb_remove_repeater_dev(struct usb_repeater *rptr);
#else
static inline struct usb_repeater *devm_usb_get_repeater_by_phandle(struct device *dev,
	const char *phandle, u8 index)
{
	return ERR_PTR(-ENXIO);
}

static inline struct usb_repeater *devm_usb_get_repeater_by_node(struct device *dev,
	struct device_node *node, struct notifier_block *nb)
{
	return ERR_PTR(-ENXIO);
}

static inline int usb_add_repeater_dev(struct usb_repeater *rptr) { return 0; }
static inline void usb_remove_repeater_dev(struct usb_repeater *rptr) { }

#endif

static inline int usb_repeater_reset(struct usb_repeater *rptr, bool assert)
{
	if (rptr && rptr->ops && rptr->ops->reset != NULL)
		return rptr->ops->reset(rptr, assert);
	else
		return 0;
}

static inline int usb_repeater_init(struct usb_repeater *rptr)
{
	if (rptr && rptr->ops && rptr->ops->init != NULL)
		return rptr->ops->init(rptr);
	else
		return 0;
}

static inline int usb_repeater_power_on(struct usb_repeater *rptr)
{
	if (rptr && rptr->ops && rptr->ops->power_on != NULL)
		return rptr->ops->power_on(rptr);
	else
		return 0;
}

static inline int usb_repeater_power_off(struct usb_repeater *rptr)
{
	if (rptr && rptr->ops && rptr->ops->power_off != NULL)
		return rptr->ops->power_off(rptr);
	else
		return 0;
}

#endif /* __LINUX_USB_REPEATER_H */
