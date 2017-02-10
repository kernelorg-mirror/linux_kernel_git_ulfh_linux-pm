// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Linaro Ltd.
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * Implements PM domain test using the generic PM domain.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pm_domain.h>

static int pd_power_off(struct generic_pm_domain *pd)
{
	dev_info(&pd->dev, "%s state=%u\n", __func__, pd->state_idx);
	return 0;
}

static int pd_power_on(struct generic_pm_domain *pd)
{
	dev_info(&pd->dev, "%s\n", __func__);
	return 0;
}

static int pd_set_performance_state(struct generic_pm_domain *pd,
				    unsigned int state)
{
	dev_info(&pd->dev, "%s: state=%u\n", __func__, state);
	return 0;
}

static void _pd_remove(struct device_node *np)
{
	struct generic_pm_domain *pd;

	of_genpd_del_provider(np);

	pd = of_genpd_remove_last(np);
	if (IS_ERR(pd))
		pr_err("%s: FAILED to remove: %pOF\n", __func__, np);
}

static void pd_remove_topology(struct device_node *np)
{
	struct of_phandle_args child, parent;

	if (of_parse_phandle_with_args(np, "power-domains",
				       "#power-domain-cells", 0, &parent))
		return;

	child.np = np;
	child.args_count = 0;

	of_genpd_remove_subdomain(&parent, &child);
	of_node_put(parent.np);
}

static void pd_remove(struct device_node *np)
{
	struct device_node *node;

	/* Check for single provider node first. */
	if (of_find_property(np, "#power-domain-cells", NULL)) {
		pd_remove_topology(np);
		_pd_remove(np);
		return;
	}

	/* TBD: Respect topology by removing subdomains first */
	for_each_child_of_node(np, node) {
		if (!of_find_property(node, "#power-domain-cells", NULL))
			continue;

		pd_remove_topology(node);
		_pd_remove(node);
	}
}

static int _pd_init_topology(struct device_node *np)
{
	struct of_phandle_args child, parent;
	int ret;

	if (of_parse_phandle_with_args(np, "power-domains",
				       "#power-domain-cells", 0, &parent))
		return 0;

	child.np = np;
	child.args_count = 0;

	ret = of_genpd_add_subdomain(&parent, &child);
	of_node_put(parent.np);

	return ret;
}

static int pd_init_topology(struct device_node *np)
{
	struct device_node *node;
	int ret;

	for_each_child_of_node(np, node) {
		ret = _pd_init_topology(node);
		if (ret) {
			of_node_put(node);
			return ret;
		}
	}

	return 0;
}

static void pd_free_states(struct genpd_power_state *states,
			   unsigned int state_count)
{
	kfree(states);
}

static int pd_init(struct device *dev, struct device_node *np)
{
	struct generic_pm_domain *pd;
	struct dev_power_governor *pd_gov = NULL;
	struct genpd_power_state *states = NULL;
	int state_count = 0, ret;
	bool boot_on, cpu_pd;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	/* DT property for CPU PM domain. */
	cpu_pd = of_property_present(np, "cpu_pm_domain");
	if (cpu_pd)
		pd->flags |= GENPD_FLAG_IRQ_SAFE | GENPD_FLAG_CPU_DOMAIN;

	ret = of_genpd_parse_idle_states(np, &states, &state_count);
	if (ret)
		dev_err(dev, "%s failed parsing idle states err=%d\n",
			__func__, ret);
	if (states) {
		pd->free_states = pd_free_states;
		pd->states = states;
		pd->state_count = state_count;

		if (cpu_pd)
			pd_gov = &pm_domain_cpu_gov;
		else
			pd_gov = &simple_qos_governor;
	}

	pd->name = devm_kasprintf(dev, GFP_KERNEL, "%pOF", np);
	pd->name = kbasename(pd->name);
	pd->power_off = pd_power_off;
	pd->power_on = pd_power_on;

	if (strcmp(dev_name(dev), "domain_perf") == 0) {
		dev_info(dev, "set perf-cb for %s\n", pd->name);
		pd->set_performance_state = pd_set_performance_state;
	}

	if (strcmp(dev_name(dev), "domain_perf_single") == 0) {
		dev_info(dev, "set perf-cb for %s\n", pd->name);
		pd->set_performance_state = pd_set_performance_state;
	}

	if (strcmp(dev_name(dev), "domain_irq_safe") == 0) {
		dev_info(dev, "Set GENPD_FLAG_IRQ_SAFE for %s\n", pd->name);
		pd->flags |= GENPD_FLAG_IRQ_SAFE;
	}

	/* DT property to see if boot-on|off. */
	boot_on = of_property_present(np, "boot-on");

	ret = pm_genpd_init(pd, pd_gov, !boot_on);
	if (ret) {
		kfree(pd->states);
		return ret;
	}

	ret = of_genpd_add_provider_simple(np, pd);
	if (ret)
		pm_genpd_remove(pd);

	return ret;
}

static int pd_init_single(struct device *dev)
{
	struct device_node *node = dev->of_node;
	int ret;

	ret = pd_init(dev, node);
	if (ret)
		goto err;

	ret = _pd_init_topology(node);
	if (ret)
		goto remove_pd;

	dev_info(dev, "%s: Single PM domain initialized\n", __func__);
	return 0;

remove_pd:
	_pd_remove(node);
err:
	dev_info(dev, "%s: returned %d\n", __func__, ret);
	return ret;
}

static int pm_domain_test_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *node;
	int ret, pd_count = 0;

	dev_info(&pdev->dev, "%s\n", __func__);

	/* Check for a single provider in the top-node first. */
	if (of_find_property(np, "#power-domain-cells", NULL))
		return pd_init_single(&pdev->dev);

	/*
	 * Parse child nodes for the "#power-domain-cells" property and
	 * initialize a provider when found.
	 */
	for_each_child_of_node(np, node) {
		if (!of_find_property(node, "#power-domain-cells", NULL))
			continue;

		ret = pd_init(&pdev->dev, node);
		if (ret)
			goto put_node;

		pd_count++;
	}

	if (!pd_count)
		goto out;

	/* Create parent/child-domains based on the topology in DT. */
	ret = pd_init_topology(np);
	if (ret)
		goto remove_pd;
out:
	dev_info(&pdev->dev, "%s: %d PM domains initialized\n", __func__,
		pd_count);
	return 0;

put_node:
	of_node_put(node);
remove_pd:
	if (pd_count)
		pd_remove(np);
	dev_info(&pdev->dev, "%s: returned %d\n", __func__, ret);
	return ret;
}

static void pm_domain_test_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s\n", __func__);
	pd_remove(pdev->dev.of_node);
}

static void pm_domain_test_sync_state(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
}

static const struct of_device_id pm_domain_test_ids[] = {
	{ .compatible = "test,pm-domain-test", },
	{},
};
MODULE_DEVICE_TABLE(of, pm_domain_test_ids);

static const struct of_device_id pm_domain_early_test_ids[] = {
	{ .compatible = "test,pm-domain-early-test", },
	{},
};
MODULE_DEVICE_TABLE(of, pm_domain_early_test_ids);

static struct platform_driver pm_domain_test_driver = {
	.probe = pm_domain_test_probe,
	.remove = pm_domain_test_remove,
	.driver = {
		.name = "pm-domain-test-drv",
		.of_match_table = pm_domain_test_ids,
		.sync_state = pm_domain_test_sync_state,
	},
};

static struct platform_driver pm_domain_early_test_driver = {
	.probe = pm_domain_test_probe,
	.remove = pm_domain_test_remove,
	.driver = {
		.name = "pm-domain-early-test-drv",
		.of_match_table = pm_domain_early_test_ids,
		.sync_state = pm_domain_test_sync_state,
	},
};

/*
 * Note, registering a platform driver doesn't work in an early_initcall since
 * the platform bus have not been registered at that point:
 * "Driver 'pm-domain-test-early-drv' was unable to register with
 * bus_type 'platform' because the bus was not initialized."
 */
static int __init pm_domain_test_drv_init(void)
{
	int ret;

	ret = platform_driver_register(&pm_domain_test_driver);

	pr_info("%s ret=%d\n", __func__, ret);
	return ret;
}
device_initcall(pm_domain_test_drv_init);

static int __init pm_domain_early_test_drv_init(void)
{
	int ret;

	ret = platform_driver_register(&pm_domain_early_test_driver);

	pr_info("%s ret=%d\n", __func__, ret);
	return ret;
}
postcore_initcall(pm_domain_early_test_drv_init);
//module_platform_driver(pm_domain_test_driver);
MODULE_LICENSE("GPL v2");
