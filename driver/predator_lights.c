// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Case and fan lighting for Acer Predator Orion desktops (written for the
 * Orion 3000, PO3-655).
 *
 * The lights hang off a controller that only the board firmware talks to.
 * The firmware exposes it through the AcerGamingFunction WMI class; this
 * driver calls the four lighting methods of that class and nothing else:
 *
 *   5  SetGamingLedBehavior   effect, speed
 *   6  GetGamingLedBehavior
 *   7  SetGamingRgbSetting    colour, brightness
 *   8  GetGamingRgbSetting
 *
 * Payload layouts follow the protocol notes of the orion-unchained project
 * (https://github.com/s0t0h/orion-unchained, docs/PROTOCOL.md).
 *
 * Each lighting area the firmware reports gets a sysfs file "areaN" on the
 * WMI device. Reading shows the current state, writing
 *
 *   <effect> [rrggbb [brightness 0-100 [speed 0-9]]]
 *
 * changes it. Area 0 addresses all areas at once.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/wmi.h>

#define GAMING_GUID		"7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56"

#define METHOD_SET_BEHAVIOR	5
#define METHOD_GET_BEHAVIOR	6
#define METHOD_SET_RGB		7
#define METHOD_GET_RGB		8

#define SETTLE_MS		20
#define MODE_OFF		0xfe
#define AREA_COUNT		6	/* 0 = all, 1-5 = single areas */

static bool long_payload;
module_param(long_payload, bool, 0644);
MODULE_PARM_DESC(long_payload, "Send the 16-byte SetGamingLedBehavior payload of newer firmware");

struct lights_mode {
	const char *name;
	u8 id;
	bool colour;	/* effect uses the colour */
};

static const struct lights_mode lights_modes[] = {
	{ "off",	MODE_OFF, false },
	{ "static",	0x00, true  },
	{ "breathing",	0x01, true  },
	{ "heartbeat",	0x02, true  },
	{ "twinkling",	0x03, true  },
	{ "rainbow",	0x06, false },
	{ "wave",	0x09, true  },
	{ "risen",	0x0a, false },
	{ "stack",	0x0b, false },
	{ "extend",	0x0c, false },
	{ "meteorite",	0x0d, false },
	{ "magic",	0x0e, false },
	{ "snake",	0x0f, true  },
};

struct lights {
	struct wmi_device *wdev;
	struct mutex lock;	/* one firmware call at a time */
	unsigned long areas;
};

struct area_attribute {
	struct device_attribute dev_attr;
	unsigned int area;
};

#define to_area(attr) container_of(attr, struct area_attribute, dev_attr)->area

static int lights_call(struct lights *priv, u32 method, const void *in, size_t in_len,
		       u8 reply[8])
{
	struct wmi_buffer input = { .length = in_len, .data = (void *)in };
	struct wmi_buffer output = { };
	int ret;

	memset(reply, 0, 8);
	ret = wmidev_invoke_method(priv->wdev, 0, method, &input, &output, sizeof(u32));
	if (!ret) {
		memcpy(reply, output.data, min_t(size_t, output.length, 8));
		kfree(output.data);
	}
	msleep(SETTLE_MS);

	return ret;
}

/* Replies start with a status byte: 0 = ok, anything else = no such area. */
static int lights_read(struct lights *priv, unsigned int area, u8 led[8], u8 rgb[8])
{
	__le64 led_in = cpu_to_le64(BIT(area));
	__le32 rgb_in = cpu_to_le32(BIT(area));
	int ret;

	ret = lights_call(priv, METHOD_GET_BEHAVIOR, &led_in, sizeof(led_in), led);
	if (ret)
		return ret;
	if (led[0])
		return -ENODEV;

	return lights_call(priv, METHOD_GET_RGB, &rgb_in, sizeof(rgb_in), rgb);
}

static int lights_write(struct lights *priv, unsigned int area, const struct lights_mode *mode,
			u32 colour, u8 brightness, u8 speed)
{
	struct device *dev = &priv->wdev->dev;
	u8 led[16] = { };
	u8 rgb[8] = { };
	u8 reply[8];
	size_t led_len = long_payload ? 16 : 8;
	int ret;

	/*
	 * The PO3-655 firmware shows the colour at full brightness in mode 0xFE,
	 * so off is sent as static black, the one thing that turns the LEDs dark.
	 */
	if (mode->id == MODE_OFF)
		colour = 0;

	led[0] = BIT(area);
	led[2] = 1;
	led[3] = mode->id == MODE_OFF ? 0x00 : mode->id;
	led[4] = long_payload ? 0x0f : 0x07;
	led[5] = speed;

	rgb[0] = BIT(area);
	rgb[2] = colour >> 16;
	rgb[3] = colour >> 8;
	rgb[4] = colour;
	rgb[5] = mode->id == MODE_OFF ? 0 : brightness;
	rgb[6] = mode->colour || mode->id == MODE_OFF ? 0x03 : 0x02;

	ret = lights_call(priv, METHOD_SET_BEHAVIOR, led, led_len, reply);
	dev_dbg(dev, "SetGamingLedBehavior %*ph -> %d, reply %8ph\n", (int)led_len, led, ret, reply);
	if (ret)
		return ret;

	ret = lights_call(priv, METHOD_SET_RGB, rgb, sizeof(rgb), reply);
	dev_dbg(dev, "SetGamingRgbSetting %8ph -> %d, reply %8ph\n", rgb, ret, reply);

	return ret;
}

static ssize_t area_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lights *priv = dev_get_drvdata(dev);
	const char *name = "unknown";
	u8 led[8], rgb[8], id;
	unsigned int i;
	int ret;

	mutex_lock(&priv->lock);
	ret = lights_read(priv, to_area(attr), led, rgb);
	mutex_unlock(&priv->lock);
	if (ret)
		return ret;

	id = led[1] ? led[2] : MODE_OFF;
	if (id == 0x00 && !rgb[1] && !rgb[2] && !rgb[3])
		id = MODE_OFF;	/* static black is how off is sent */
	for (i = 0; i < ARRAY_SIZE(lights_modes); i++) {
		if (lights_modes[i].id == id)
			name = lights_modes[i].name;
	}

	return sysfs_emit(buf, "%s %02x%02x%02x %u %u\nraw %8ph / %8ph\n", name,
			  rgb[1], rgb[2], rgb[3], rgb[4], led[3], led, rgb);
}

static ssize_t area_store(struct device *dev, struct device_attribute *attr, const char *buf,
			  size_t count)
{
	struct lights *priv = dev_get_drvdata(dev);
	const struct lights_mode *mode = NULL;
	unsigned int colour = 0xffffff, brightness = 100, speed = 5, i;
	char name[16];
	int ret;

	if (sscanf(buf, "%15s %x %u %u", name, &colour, &brightness, &speed) < 1)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(lights_modes); i++) {
		if (!strcmp(name, lights_modes[i].name))
			mode = &lights_modes[i];
	}
	if (!mode || colour > 0xffffff || brightness > 100 || speed > 9)
		return -EINVAL;

	mutex_lock(&priv->lock);
	ret = lights_write(priv, to_area(attr), mode, colour, brightness, speed);
	mutex_unlock(&priv->lock);

	return ret ? ret : count;
}

#define AREA_ATTR(_n)								\
	static struct area_attribute area##_n##_attr = {			\
		.dev_attr = __ATTR(area##_n, 0664, area_show, area_store),	\
		.area = _n,							\
	}

AREA_ATTR(0);
AREA_ATTR(1);
AREA_ATTR(2);
AREA_ATTR(3);
AREA_ATTR(4);
AREA_ATTR(5);

static struct attribute *lights_attrs[] = {
	&area0_attr.dev_attr.attr,
	&area1_attr.dev_attr.attr,
	&area2_attr.dev_attr.attr,
	&area3_attr.dev_attr.attr,
	&area4_attr.dev_attr.attr,
	&area5_attr.dev_attr.attr,
	NULL
};

static umode_t lights_is_visible(struct kobject *kobj, struct attribute *attr, int n)
{
	struct lights *priv = dev_get_drvdata(kobj_to_dev(kobj));

	return test_bit(n, &priv->areas) ? attr->mode : 0;
}

static const struct attribute_group lights_group = {
	.attrs = lights_attrs,
	.is_visible = lights_is_visible,
};

static int lights_probe(struct wmi_device *wdev, const void *context)
{
	struct device *dev = &wdev->dev;
	struct lights *priv;
	u8 led[8], rgb[8];
	unsigned int i;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->wdev = wdev;
	mutex_init(&priv->lock);
	dev_set_drvdata(dev, priv);

	/* Loading only reads: ask the firmware which areas exist. */
	for (i = 0; i < AREA_COUNT; i++) {
		ret = lights_read(priv, i, led, rgb);
		if (ret == -ENODEV) {
			dev_info(dev, "area %u: not present (%8ph)\n", i, led);
			continue;
		}
		if (ret)
			return dev_err_probe(dev, ret, "reading area %u failed\n", i);
		dev_info(dev, "area %u: behavior %8ph rgb %8ph\n", i, led, rgb);
		__set_bit(i, &priv->areas);
	}
	if (!priv->areas)
		return dev_err_probe(dev, -ENODEV, "firmware reports no lighting areas\n");

	return devm_device_add_group(dev, &lights_group);
}

static const struct wmi_device_id lights_id_table[] = {
	{ GAMING_GUID, NULL },
	{ }
};
MODULE_DEVICE_TABLE(wmi, lights_id_table);

static struct wmi_driver lights_driver = {
	.driver = {
		.name = "predator_lights",
	},
	.id_table = lights_id_table,
	.probe = lights_probe,
};
module_wmi_driver(lights_driver);

MODULE_DESCRIPTION("Acer Predator Orion desktop case and fan lighting");
MODULE_LICENSE("GPL");
