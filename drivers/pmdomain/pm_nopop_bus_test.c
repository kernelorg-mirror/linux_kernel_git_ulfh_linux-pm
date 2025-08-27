// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Linaro Ltd.
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * Implements device node popluation to test consumers getting attached to PM
 * PM domains in regards to the sync_state mechanism.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pm_domain.h>

static void __maybe_unused pm_test_nopop_plat_populate(struct device_node *np)
{
	int err;

	/*
	 * of_platform_populate() calls of_device_alloc() that creates
	 * a device and assigns its fwnode via device_set_node().
	 * This means the device are taken into account for sync_state.
	 */

	err = of_platform_populate(np, NULL, NULL, NULL);
	if (err)
		pr_info("%s: OF populate failed %d\n", __func__, err);
}

static const struct bus_type pm_test_nopop_bus_type = {
        .name           = "pm_test_nopop_bus",
};

static int pm_test_nopop_probe(struct device *dev)
{
	u32 probe_delay_ms = 0;
	int ret = 0;

	dev_info(dev, "%s\n", __func__);

	/* DT property to see if we should delay probe. */
	of_property_read_u32(dev->of_node, "probe-delay-ms", &probe_delay_ms);
	if (probe_delay_ms) {
		dev_info(dev, "%s Delaying probe %ums\n",  __func__, probe_delay_ms);
		msleep(probe_delay_ms);
	}

	/* DT property to see if we should defer probe. */
	if (of_property_present(dev->of_node, "probe-defer")) {
		dev_info(dev, "%s Enforce a probe defer\n",  __func__);
		ret = -EPROBE_DEFER;
		goto out;
	}

out:
	dev_info(dev, "%s ret=%d\n", __func__, ret);
	return ret;
}

static struct device_driver pm_test_nopop_drv = {
	.name = "pm_test_nopop_drv",
	.bus = &pm_test_nopop_bus_type,
	.probe = pm_test_nopop_probe,
	.probe_type = PROBE_PREFER_ASYNCHRONOUS,
};

static void pm_test_nopop_release(struct device *dev) { }

static void __maybe_unused pm_test_nopop_bus_populate(struct device_node *np)
{
	struct device *dev;
	int ret;

	ret = bus_register(&pm_test_nopop_bus_type);
	if (ret) {
		pr_info("%s: failed (err %d) to register bus\n", __func__, ret);
		return;
	}

	ret = driver_register(&pm_test_nopop_drv);
	if (ret) {
		pr_info("%s: failed (err %d) to register drv\n", __func__, ret);
		return;
	}

	for_each_child_of_node_scoped(np, node) {
		struct fwnode_handle *fwnode;
		const char *name;

		dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	        if (!dev)
			return;

		name = kasprintf(GFP_KERNEL, "%pOF", node);
		name = kbasename(name);
		dev_set_name(dev, "%s", name);
		dev->release = pm_test_nopop_release;
		dev->bus = &pm_test_nopop_bus_type;

		/*
		 * Alone and with the driver being probed later, we need to set
		 * the fwnode for the device, otherwise the device will not be
		 * taken into account in the ->sync_state() condition for the
		 * supplier (the genpd OF provider). In other words, just
		 * setting the OF node as below, is not suffient for
		 * ->sync_state() to work:
		 *
		 * dev->of_node = of_node_get(node);
		 */
		fwnode = of_fwnode_handle(node);
		device_set_node(dev, fwnode);

		pr_info("%s: adding dev %s\n", __func__, name);

		ret = device_register(dev);
		if (ret)
			pr_info("%s: failed (err %d) to add dev %pOF\n",
				__func__, ret, node);
	}
}

static int __init pm_test_nopop_init(void)
{
	struct device_node *np;

	pr_info("%s\n", __func__);

	np = of_find_node_by_path("/pm_tests_nopop");
	if (!np)
		return 0;

	/*
	 * If no population of the DT node is node, the node isn't taken into
	 * account at all, which means the ->sync_state() callback of the
	 * supplieris gets invoked.
	 * If population is done, a compatible driver must be probed to allow
	 * the ->sync_state() callback to be invoked.
	 */

#if 0
	pm_test_nopop_plat_populate(np);
#else
	pm_test_nopop_bus_populate(np);
#endif

	pr_info("%s /pm_tests_nopop node populated\n", __func__);
	return 0;
}
late_initcall(pm_test_nopop_init);

MODULE_LICENSE("GPL v2");
