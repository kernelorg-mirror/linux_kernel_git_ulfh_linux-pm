// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Linaro Ltd.
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * Implements and auxiliary driver and registers a PM domain onecell provider
 * through the generic PM domain.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/auxiliary_bus.h>

#include "pm_domain_oc_test.h"

/*
 * The below is to test PM domain registration from a n auxiliary device and in
 * particular how it affect the sync_state mechanism.
 */
static int pm_domain_oc_aux_test_probe(struct auxiliary_device *auxdev,
				       const struct auxiliary_device_id *id)
{
	struct device *dev = &auxdev->dev;

	return _pm_domain_oc_test_probe(dev);
}

static void pm_domain_oc_aux_test_remove(struct auxiliary_device *auxdev)
{
	struct device *dev = &auxdev->dev;

	_pm_domain_oc_test_remove(dev);
}

static const struct auxiliary_device_id pm_domain_oc_aux_test_id_table[] = {
	{ .name = "pm_domain_oc_test.oc_aux" },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, pm_domain_oc_aux_test_id_table);

static struct auxiliary_driver pm_domain_oc_aux_test_driver = {
	.probe = pm_domain_oc_aux_test_probe,
	.remove = pm_domain_oc_aux_test_remove,
	.id_table = pm_domain_oc_aux_test_id_table,
/*
 * TBD: Test later!
 *	.driver = {
 *		.sync_state = pm_domain_oc_test_sync_state,
 *	},
 */
};
//module_auxiliary_driver(pm_domain_oc_aux_test_driver);

static int __init pm_domain_oc_aux_test_driver_init(void)
{
	int ret;

	pr_info("%s\n", __func__);

	ret = auxiliary_driver_register(&pm_domain_oc_aux_test_driver);

	pr_info("%s ret=%d\n", __func__, ret);
	return ret;
}
//subsys_initcall(pm_domain_oc_aux_test_driver_init);
late_initcall(pm_domain_oc_aux_test_driver_init);

MODULE_LICENSE("GPL v2");
