// SPDX-License-Identifier: GPL-2.0

#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/regmap.h>

/* 8 banks / channels
 * 7 ctl registers for each bank
 * BANK_SEL selects bank for ctl
 * BANK_EN bitmask of enabled banks
 * LUT_CFG0 8bits of duty cycle
 * LUT_CFG1 9th bit of duty cycle (0x80) | idx
 */

#define	PM8058_LPG_BANKS		8
#define	PM8058_PWM_CHANNELS		PM8058_LPG_BANKS	/* MAX=8 */

#define	PM8058_LPG_CTL_REGS		7

/* PMIC8058 LPG/PWM */
#define	SSBI_REG_ADDR_LPG_CTL_BASE	0x13C
#define	SSBI_REG_ADDR_LPG_CTL(n)	(SSBI_REG_ADDR_LPG_CTL_BASE + (n))
#define	SSBI_REG_ADDR_LPG_BANK_SEL	0x143
#define	SSBI_REG_ADDR_LPG_BANK_EN	0x144
#define	SSBI_REG_ADDR_LPG_LUT_CFG0	0x145
#define	SSBI_REG_ADDR_LPG_LUT_CFG1	0x146
#define	SSBI_REG_ADDR_LPG_TEST		0x147

/* Control 0 */
#define	PM8058_PWM_1KHZ_COUNT_MASK	0xF0
#define	PM8058_PWM_1KHZ_COUNT_SHIFT	4

#define	PM8058_PWM_1KHZ_COUNT_MAX	15

#define	PM8058_PWM_OUTPUT_EN		0x08
#define	PM8058_PWM_PWM_EN		0x04
#define	PM8058_PWM_RAMP_GEN_EN		0x02
#define	PM8058_PWM_RAMP_START		0x01

#define	PM8058_PWM_PWM_START		(PM8058_PWM_OUTPUT_EN \
					| PM8058_PWM_PWM_EN)
#define	PM8058_PWM_RAMP_GEN_START	(PM8058_PWM_RAMP_GEN_EN \
					| PM8058_PWM_RAMP_START)

/* Control 1 */
#define	PM8058_PWM_REVERSE_EN		0x80
#define	PM8058_PWM_BYPASS_LUT		0x40
#define	PM8058_PWM_HIGH_INDEX_MASK	0x3F

/* Control 2 */
#define	PM8058_PWM_LOOP_EN		0x80
#define	PM8058_PWM_RAMP_UP		0x40
#define	PM8058_PWM_LOW_INDEX_MASK	0x3F

/* Control 3 */
#define	PM8058_PWM_VALUE_BIT7_0		0xFF
#define	PM8058_PWM_VALUE_BIT5_0		0x3F

/* Control 4 */
#define	PM8058_PWM_VALUE_BIT8		0x80

#define	PM8058_PWM_CLK_SEL_MASK		0x60
#define	PM8058_PWM_CLK_SEL_SHIFT	5

#define	PM8058_PWM_CLK_SEL_NO		0
#define	PM8058_PWM_CLK_SEL_1KHZ		1
#define	PM8058_PWM_CLK_SEL_32KHZ	2
#define	PM8058_PWM_CLK_SEL_19P2MHZ	3

#define	PM8058_PWM_PREDIVIDE_MASK	0x18
#define	PM8058_PWM_PREDIVIDE_SHIFT	3

#define	PM8058_PWM_PREDIVIDE_2		0
#define	PM8058_PWM_PREDIVIDE_3		1
#define	PM8058_PWM_PREDIVIDE_5		2
#define	PM8058_PWM_PREDIVIDE_6		3

#define	PM8058_PWM_M_MASK	0x07
#define	PM8058_PWM_M_MIN	0
#define	PM8058_PWM_M_MAX	7

/* Control 5 */
#define	PM8058_PWM_PAUSE_COUNT_HI_MASK		0xFC
#define	PM8058_PWM_PAUSE_COUNT_HI_SHIFT		2

#define	PM8058_PWM_PAUSE_ENABLE_HIGH		0x02
#define	PM8058_PWM_SIZE_9_BIT			0x01

/* Control 6 */
#define	PM8058_PWM_PAUSE_COUNT_LO_MASK		0xFC
#define	PM8058_PWM_PAUSE_COUNT_LO_SHIFT		2

#define	PM8058_PWM_PAUSE_ENABLE_LOW		0x02
#define	PM8058_PWM_RESERVED			0x01

#define	PM8058_PWM_PAUSE_COUNT_MAX		56 /* < 2^6 = 64*/

/* LUT_CFG1 */
#define	PM8058_PWM_LUT_READ			0x40

/* TEST */
#define	PM8058_PWM_DTEST_MASK		0x38
#define	PM8058_PWM_DTEST_SHIFT		3

#define	PM8058_PWM_DTEST_BANK_MASK	0x07


struct pm8058_lpg {
	struct regmap *map;
	struct led_classdev cdev;
};

static void pm8058_lpg_set(struct led_classdev *cled,
	enum led_brightness value)
{
	struct pm8058_lpg *lpg = container_of(cled, struct pm8058_lpg, cdev);

	regmap_write(lpg->map, SSBI_REG_ADDR_LPG_CTL(3), value);
}

static enum led_brightness pm8058_lpg_get(struct led_classdev *cled)
{
	struct pm8058_lpg *lpg = container_of(cled, struct pm8058_lpg, cdev);

	return 0;
}

#if 0
static void dump_state(struct pm8058_lpg *lpg)
{
    unsigned i, j, val;

    regmap_read(lpg->map, SSBI_REG_ADDR_LPG_BANK_EN, &val);

    printk("LPG: BANK_EN=%2X\n", val);

    for (i = 0; i < 8; i++) {
		printk(" BANK %u:\n", i);
		regmap_write(lpg->map, SSBI_REG_ADDR_LPG_BANK_SEL, i);
		for (j = 0; j < 7; j++) {
			regmap_read(lpg->map, SSBI_REG_ADDR_LPG_CTL(j), &val);
			printk("    %2X\n", val);
		}
    }
}
#endif

static int pm8058_lpg_probe(struct platform_device *pdev)
{
	struct pm8058_lpg *lpg;
	struct device_node *np = pdev->dev.of_node;
	int ret, i;
	struct regmap *map;

	lpg = devm_kzalloc(&pdev->dev, sizeof(*lpg), GFP_KERNEL);
	if (!lpg)
		return -ENOMEM;

	map = dev_get_regmap(pdev->dev.parent, NULL);
	if (!map) {
		dev_err(&pdev->dev, "Parent regmap unavailable.\n");
		return -ENXIO;
	}
	lpg->map = map;

    regmap_write(lpg->map, SSBI_REG_ADDR_LPG_BANK_SEL, 0);
    regmap_write(lpg->map, SSBI_REG_ADDR_LPG_CTL(0), 0xfc);
    regmap_write(lpg->map, SSBI_REG_ADDR_LPG_CTL(1), 0x40);
    regmap_write(lpg->map, SSBI_REG_ADDR_LPG_CTL(2), 0x00);
    regmap_write(lpg->map, SSBI_REG_ADDR_LPG_CTL(3), 0x20); // brightness
    regmap_write(lpg->map, SSBI_REG_ADDR_LPG_CTL(4), 0x40);
    regmap_write(lpg->map, SSBI_REG_ADDR_LPG_CTL(5), 0x00);
    regmap_write(lpg->map, SSBI_REG_ADDR_LPG_CTL(6), 0x00);

	/* Use label else node name */
	lpg->cdev.name = of_get_property(np, "label", NULL) ? : np->name;
	lpg->cdev.default_trigger =
		of_get_property(np, "linux,default-trigger", NULL);
	lpg->cdev.brightness_set = pm8058_lpg_set;
	lpg->cdev.brightness_get = pm8058_lpg_get;
	lpg->cdev.max_brightness = 63;

	ret = devm_led_classdev_register(&pdev->dev, &lpg->cdev);
	if (ret) {
		dev_err(&pdev->dev, "unable to register led \"%s\"\n",
			lpg->cdev.name);
		return ret;
	}

	return 0;
}

static const struct of_device_id pm8058_lpg_id_table[] = {
	{ .compatible = "qcom,pm8058-lpg" },
	{ },
};
MODULE_DEVICE_TABLE(of, pm8058_lpg_id_table);

static struct platform_driver pm8058_lpg_driver = {
	.probe		= pm8058_lpg_probe,
	.driver		= {
		.name	= "pm8058-lpg",
		.of_match_table = pm8058_lpg_id_table,
	},
};
module_platform_driver(pm8058_lpg_driver);

MODULE_DESCRIPTION("PM8058 LPG driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pm8058-lpg");
