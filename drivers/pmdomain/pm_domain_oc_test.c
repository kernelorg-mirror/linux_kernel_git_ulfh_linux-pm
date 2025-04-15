// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Linaro Ltd.
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * Implements PM domain onecell test using the generic PM domain.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pm_domain.h>
#include <linux/auxiliary_bus.h>

#include "pm_domain_oc_test.h"

struct pm_domain_oc {
	struct generic_pm_domain genpd;
	u32 domain_id;
};

static inline struct pm_domain_oc *genpd_to_oc(struct generic_pm_domain *pd)
{
	return container_of(pd, struct pm_domain_oc, genpd);
}

static int pd_oc_power_off(struct generic_pm_domain *pd)
{
	dev_info(&pd->dev, "%s domain_id=%u\n",
		 __func__, genpd_to_oc(pd)->domain_id);
	return 0;
}

static int pd_oc_power_on(struct generic_pm_domain *pd)
{
	dev_info(&pd->dev, "%s domain_id=%u\n",
		 __func__, genpd_to_oc(pd)->domain_id);
	return 0;
}

static int pd_init(struct device *dev, struct device_node *np)
{
	struct genpd_onecell_data *pd_data;
	struct generic_pm_domain **domains;
	struct pm_domain_oc *pd;
	u32 num_domains = 0;
	int i, ret;

	/* A DT property tells us how many domains to initialize. */
	of_property_read_u32(np, "num-power-domains", &num_domains);
	if (!num_domains)
		return -EINVAL;

	pd = devm_kcalloc(dev, num_domains, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	pd_data = devm_kzalloc(dev, sizeof(*pd_data), GFP_KERNEL);
	if (!pd_data)
		return -ENOMEM;

	domains = devm_kcalloc(dev, num_domains, sizeof(*domains), GFP_KERNEL);
	if (!domains)
		return -ENOMEM;

	for (i = 0; i < num_domains; i++, pd++) {
		pd->domain_id = i;
		pd->genpd.name = devm_kasprintf(dev, GFP_KERNEL, "%pOF", np);
		pd->genpd.name = kbasename(pd->genpd.name);
		pd->genpd.flags = GENPD_FLAG_DEV_NAME_FW;
		pd->genpd.power_off = pd_oc_power_off;
		pd->genpd.power_on = pd_oc_power_on;

		ret = pm_genpd_init(&pd->genpd, NULL, false);
		if (ret)
			goto err;

		domains[i] = &pd->genpd;
	}

	pd_data->domains = domains;
	pd_data->num_domains = num_domains;

	ret = of_genpd_add_provider_onecell(np, pd_data);
	if (ret)
		goto err;

	dev_info(dev, "%s: %d PM domains initialized\n", __func__, num_domains);
	return 0;

err:
	for (i--; i >= 0; i--)
		pm_genpd_remove(domains[i]);
	dev_info(dev, "%s: ret=%d\n", __func__, ret);
	return ret;
}

static void pd_remove(struct device_node *np)
{
	unsigned num_domains = 0;

	of_genpd_del_provider(np);

	while (!IS_ERR(of_genpd_remove_last(np))) {
		num_domains++;
	}

	pr_info("%s: %pOF num_domains=%u\n", __func__, np, num_domains);
}

static void pd_remove_children(struct device_node *np)
{
	struct device_node *node;

	for_each_child_of_node(np, node) {
		if (!of_find_property(node, "#power-domain-cells", NULL))
			continue;
		pd_remove(node);
	}
}

static int pm_domain_oc_test_add_aux_dev(struct device *dev)
{
	struct auxiliary_device *aux_dev;
	int ret = 0;

	dev_info(dev, "%s\n", __func__);

	aux_dev = devm_auxiliary_device_create(dev, "oc_aux", NULL);
	if (!aux_dev)
		ret = -ENOMEM;

	dev_info(dev, "%s: ret=%d\n", __func__, ret);
	return ret;
}

static void pm_domain_oc_test_remove_aux_dev(struct device *dev)
{
	dev_info(dev, "%s: Not implemented - if needed at all?\n", __func__);
}

int _pm_domain_oc_test_probe(struct device *dev)
{
	struct device_node *np = dev->of_node, *node;
	unsigned int num_providers = 0;
	int ret;

	dev_info(dev, "%s\n", __func__);

	/* Check for single provider node first. */
	if (of_find_property(np, "#power-domain-cells", NULL)) {

		ret = pd_init(dev, np);
		if (ret)
			return ret;

		/*
		 * If there are child-nodes with compatible-strings,
		 * of_platform_populate() creates devices for them. Without this
		 * fw_devlink takes the child nodes (and its consumers) into
		 * account before invoking the ->sync_state() for us. With this,
		 * the children will need their own ->sync_state(), as they will
		 * be handled independently from the parent.
		 */
		ret = of_platform_populate(np, NULL, NULL, dev);
		if (ret)
			dev_info(dev, "%s: OF populate failed %d\n",
				 __func__, ret);

		num_providers = 1;
		goto out;
	}

	/*
	 * Parse child nodes for the "#power-domain-cells" property and
	 * initialize a genpd/genpd-of-provider pair when it's found.
	 */
	for_each_child_of_node(np, node) {
		if (!of_find_property(node, "#power-domain-cells", NULL))
			continue;

		ret = pd_init(dev, node);
		if (ret)
			goto put_node;

		num_providers++;
	}

out:
	dev_info(dev, "%s: %d PM domain providers initialized\n",
		 __func__, num_providers);
	return 0;

put_node:
	of_node_put(node);
	if (num_providers)
		pd_remove_children(np);
	dev_info(dev, "%s: ret=%d\n", __func__, ret);
	return ret;
}

static int pm_domain_oc_test_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (of_property_present(dev->of_node, "pm-domain-auxiliary"))
		return pm_domain_oc_test_add_aux_dev(dev);

	return _pm_domain_oc_test_probe(dev);
}

void _pm_domain_oc_test_remove(struct device *dev)
{
	struct device_node *np = dev->of_node;

	dev_info(dev, "%s\n", __func__);

	if (of_find_property(np, "#power-domain-cells", NULL)) {
		pd_remove(np);
		return;
	}

	pd_remove_children(np);
}

static void pm_domain_oc_test_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	if (of_property_present(dev->of_node, "pm-domain-auxiliary"))
		return pm_domain_oc_test_remove_aux_dev(dev);

	_pm_domain_oc_test_remove(dev);
}

static const struct of_device_id pm_domain_oc_test_ids[] = {
	{ .compatible = "test,pm-domain-oc-test", },
	{},
};
MODULE_DEVICE_TABLE(of, pm_domain_oc_test_ids);

static struct platform_driver pm_domain_oc_test_driver = {
	.probe = pm_domain_oc_test_probe,
	.remove = pm_domain_oc_test_remove,
	.driver = {
		.name = "pm-domain-onecell-test-drv",
		.of_match_table = pm_domain_oc_test_ids,
	},
};
module_platform_driver(pm_domain_oc_test_driver);

/*
 * This code tests registration of PM domains at early initcalls, before the
 * corresponding compatible device-node have been populated probed by a
 * platform-driver. We look only for the first "test,pm-domain-oc-test-early"
 * compatible node and register a onecell PM domain provider based on it.
 * This is similar to what some Renesas platforms do.
 */
static struct device_node *early_pd_node;
static struct genpd_onecell_data *early_pd_data;

static int __init pm_domain_oc_test_early_init_pd(void)
{
	struct generic_pm_domain **domains;
	struct pm_domain_oc *pd;
	u32 num_domains = 0;
	int i, ret;

	pr_info("%s\n", __func__);

	early_pd_node = of_find_compatible_node(NULL, NULL, "test,pm-domain-oc-early-test");
	if (!early_pd_node)
		return -ENODEV;

	/* A DT property tells us how many domains to initialize. */
	of_property_read_u32(early_pd_node, "num-power-domains", &num_domains);
	if (!num_domains) {
		ret = -EINVAL;
		goto put_node;
	}

	pd = kcalloc(num_domains, sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		ret = -ENOMEM;
		goto put_node;
	}

	early_pd_data = kzalloc(sizeof(*early_pd_data), GFP_KERNEL);
	if (!early_pd_data) {
		ret = -ENOMEM;
		goto free_pd;
	}

	domains = kcalloc(num_domains, sizeof(*domains), GFP_KERNEL);
	if (!domains) {
		ret = -ENOMEM;
		goto free_early;
	}

	for (i = 0; i < num_domains; i++, pd++) {
		pd->domain_id = i;
		pd->genpd.name = kasprintf(GFP_KERNEL, "%pOF", early_pd_node);
		pd->genpd.name = kbasename(pd->genpd.name);
		pd->genpd.flags = GENPD_FLAG_DEV_NAME_FW;
		pd->genpd.power_off = pd_oc_power_off;
		pd->genpd.power_on = pd_oc_power_on;

		ret = pm_genpd_init(&pd->genpd, NULL, false);
		if (ret)
			goto free_genpd;

		domains[i] = &pd->genpd;
	}

	early_pd_data->domains = domains;
	early_pd_data->num_domains = num_domains;

	pr_info("%s: %d Early PM domains initialized\n", __func__, num_domains);
	return 0;

free_genpd:
	for (i--; i >= 0; i--) {
		pm_genpd_remove(domains[i]);
		kfree(domains[i]->name);
	}
	kfree(domains);
free_early:
	kfree(early_pd_data);
	early_pd_data = NULL;
free_pd:
	kfree(pd);
put_node:
	of_node_put(early_pd_node);
	early_pd_node = NULL;
	pr_info("%s: ret=%d\n", __func__, ret);
	return ret;
}
early_initcall(pm_domain_oc_test_early_init_pd);

static int __init pm_domain_oc_test_early_init_of(void)
{
	int ret = -EINVAL;

	pr_info("%s\n", __func__);

	if (early_pd_data)
		ret = of_genpd_add_provider_onecell(early_pd_node,
						    early_pd_data);
	pr_info("%s ret=%d\n", __func__, ret);
	return ret;
}
postcore_initcall(pm_domain_oc_test_early_init_of);

MODULE_LICENSE("GPL v2");
