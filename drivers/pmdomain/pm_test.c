// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/printk.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/pm.h>
#include <linux/pm_domain.h>
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>

struct pm_test_dev {
	struct device *dev;
	/* add more if needed */
	struct dev_pm_domain_list *pds;
	struct dev_pm_domain_list *pds0;
	struct dev_pm_domain_list *pds1;
	struct dentry *debugfs_root;
	u32 perf_state;
	struct dev_pm_opp *opp;
};

static int pm_test_perf_get(void *data, u64 *val)
{
	struct pm_test_dev *rdev = data;

	*val = rdev->perf_state;

	return 0;
}

static int pm_test_perf_set(void *data, u64 val)
{
	struct pm_test_dev *rdev = data;
	int ret;

	ret = dev_pm_domain_set_performance_state(rdev->dev, val);
	if (ret)
		return ret;

	rdev->perf_state = val;
	return 0;
}

static int pm_test_rate_opp_get(void *data, u64 *val)
{
	struct pm_test_dev *rdev = data;
	struct dev_pm_opp *opp = rdev->opp;

	if (opp)
		*val = dev_pm_opp_get_freq_indexed(opp, 0);
	else
		*val = 0;

	return 0;
}

static int pm_test_rate_opp_set(void *data, u64 val)
{
	struct pm_test_dev *rdev = data;
	struct dev_pm_opp *opp = NULL;
	int ret;

	if (val) {
		opp = dev_pm_opp_find_freq_exact(rdev->dev, val, true);
		if (IS_ERR(opp))
			return -EINVAL;
	}

	ret = dev_pm_opp_set_opp(rdev->dev, opp);
	if (ret) {
		dev_pm_opp_put(opp);
		return ret;
	}

	if (rdev->opp)
		dev_pm_opp_put(rdev->opp);

	rdev->opp = opp;
	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(pm_test_perf_ops, pm_test_perf_get,
			 pm_test_perf_set, "%llu\n");
DEFINE_DEBUGFS_ATTRIBUTE(pm_test_rate_opp_ops, pm_test_rate_opp_get,
			 pm_test_rate_opp_set, "%llu\n");

static int pm_test_probe(struct platform_device *pdev)
{
	struct pm_test_dev *rdev;
	struct dev_pm_opp *opp;
	u32 probe_delay_ms = 0;
	int ret;

	dev_info(&pdev->dev, "%s\n", __func__);

	rdev = devm_kzalloc(&pdev->dev,	sizeof(*rdev), GFP_KERNEL);
	if (!rdev)
		return -ENOMEM;

	/* SCMI perf */
	if (strcmp(dev_name(&pdev->dev), "pm_test3") == 0) {
		opp = dev_pm_opp_find_level_exact(&pdev->dev, 450000);
		if (IS_ERR(opp)) {
			dev_info(&pdev->dev, "OPP not found\n");
		} else {
			dev_pm_opp_set_opp(&pdev->dev, opp);
			rdev->opp = opp;
		}
	}

	/* SCMI perf */
	if (strcmp(dev_name(&pdev->dev), "pm_test4") == 0) {
		opp = dev_pm_opp_find_level_exact(&pdev->dev, 600000);
		if (IS_ERR(opp)) {
			dev_info(&pdev->dev, "OPP not found\n");
		} else {
			dev_pm_opp_set_opp(&pdev->dev, opp);
			rdev->opp = opp;
		}
	}

	/* Single perf domain with one required-opps. */
	if (strcmp(dev_name(&pdev->dev), "pm_test5") == 0) {
		/*
		 * Calling devm_pm_opp_of_add_table() twice works fine. Cleanup?
		 * _read_opp_key() fails if the primary opp-node lacks any of
		 * opp-level, opp-hz, opp-peak-kBps/opp-avg-kBps.
		 * Get lazy link WARN when opp-level is both in primary node and
		 * in the required-opps node. Needs to be fixed!
		 */
		ret = devm_pm_opp_of_add_table(&pdev->dev);
		if (ret)
			dev_info(&pdev->dev, "Unable to add OPP table %d\n", ret);

		/*
		 * Works when level is located in the required-opps and no level
		 * in the primary node. If no required-opps but level in primary
		 * node, then the level will set be, which may not be supported
		 * by the genpd. The latter is still useful when the genpd has
		 * created the OPPs dynamically via GENPD_FLAG_OPP_TABLE_FW.
		 */
		//opp = dev_pm_opp_find_level_exact(&pdev->dev, 100);

		/*
		 * Works when opp-hz is in the primary opp-node and level in the
		 * required-opps for the single PM domain case.
		 */
		opp = dev_pm_opp_find_freq_exact(&pdev->dev, 133, true);
		if (IS_ERR(opp)) {
			dev_info(&pdev->dev, "OPP not found\n");
		} else {
			dev_pm_opp_set_opp(&pdev->dev, opp);
			rdev->opp = opp;
		}

		/*
		 * Thought: If there is no rate, level or bandwidth specified
		 * for an OPP, but only a required-opps, we can't set the OPP.
		 * Perhaps we allow a "label" to be used?
		 */
	}

	/* Multi perf domain - multi required-opps. */
	if (strcmp(dev_name(&pdev->dev), "pm_test6") == 0) {
		struct dev_pm_domain_attach_data attach_data = {
			.pd_flags = PD_FLAG_DEV_LINK_ON | PD_FLAG_REQUIRED_OPP,
		};

		ret = dev_pm_domain_attach_list(&pdev->dev, &attach_data,
						&rdev->pds);
		dev_info(&pdev->dev, "Attached to multi PM domains ret=%d\n", ret);
		if (ret < 0)
			return ret;

		ret = devm_pm_opp_of_add_table(&pdev->dev);
		if (ret)
			dev_info(&pdev->dev, "Unable to add OPP table %d\n", ret);

	}

	/* Single perf domain, opp-table with level/hz - no required-opps. */
	if (strcmp(dev_name(&pdev->dev), "pm_test7") == 0) {

		ret = devm_pm_opp_of_add_table(&pdev->dev);
		if (ret)
			dev_info(&pdev->dev, "Unable to add OPP table %d\n", ret);

		opp = dev_pm_opp_find_level_exact(&pdev->dev, 125);
		if (IS_ERR(opp)) {
			dev_info(&pdev->dev, "OPP not found\n");
		} else {
			dev_pm_opp_set_opp(&pdev->dev, opp);
			rdev->opp = opp;
		}

		/*
		 * If the level doesn't match an OPP corresponding to the
		 * perf-domain's opp-table, the level is requested anyways and
		 * hence could then potentially fail?
		 */
	}

	/* Single PM domain with parent perf domain. */
	if (strcmp(dev_name(&pdev->dev), "pm_test8") == 0) {

		ret = devm_pm_opp_of_add_table(&pdev->dev);
		if (ret)
			dev_info(&pdev->dev, "Unable to add OPP table %d\n", ret);

		opp = dev_pm_opp_find_freq_exact(&pdev->dev, 266, true);
		if (IS_ERR(opp)) {
			dev_info(&pdev->dev, "OPP not found\n");
		} else {
			dev_pm_opp_set_opp(&pdev->dev, opp);
			rdev->opp = opp;
		}
	}

	/* Multi PM domain in DT - no perf/OPP. */
	if ((strcmp(dev_name(&pdev->dev), "pm_test9") == 0) ||
	    (strcmp(dev_name(&pdev->dev), "pm_test14") == 0) ||
	    (strcmp(dev_name(&pdev->dev), "pm_test15") == 0)) {
		ret = dev_pm_domain_attach_list(&pdev->dev, NULL,
						&rdev->pds);
		dev_info(&pdev->dev, "Attached to multi PM domains ret=%d\n", ret);
		if (ret < 0)
			return ret;

		ret = devm_pm_opp_of_add_table(&pdev->dev);
		if (ret)
			dev_info(&pdev->dev, "Unable to add OPP table %d\n", ret);
	}

	/* Multi power domain, with common perf parent - multi required-opps. */
	if (strcmp(dev_name(&pdev->dev), "pm_test10") == 0) {
		struct dev_pm_domain_attach_data attach_data = {
			.pd_names = (const char *[]) { "perf4", "perf5" },
			.num_pd_names = 2,
			.pd_flags = PD_FLAG_DEV_LINK_ON | PD_FLAG_REQUIRED_OPP,
		};

		ret = dev_pm_domain_attach_list(&pdev->dev, &attach_data,
						&rdev->pds);
		dev_info(&pdev->dev, "Attached to multi PM domains ret=%d\n", ret);
		if (ret < 0)
			return ret;

		ret = devm_pm_opp_of_add_table(&pdev->dev);
		if (ret)
			dev_info(&pdev->dev, "Unable to add OPP table %d\n", ret);
	}

	if (strcmp(dev_name(&pdev->dev), "pm_test11") == 0) {
		struct dev_pm_domain_attach_data attach_data0 = {
			.pd_names = (const char *[]) { "perf0" },
			.num_pd_names = 1,
			.pd_flags = PD_FLAG_DEV_LINK_ON,
		};
		struct dev_pm_domain_attach_data attach_data1 = {
			.pd_names = (const char *[]) { "perf1" },
			.num_pd_names = 1,
			.pd_flags = PD_FLAG_DEV_LINK_ON | PD_FLAG_REQUIRED_OPP,
		};

		ret = dev_pm_domain_attach_list(&pdev->dev, &attach_data0,
						&rdev->pds0);
		dev_info(&pdev->dev, "Attached to multi PM domains ret=%d\n", ret);
		if (ret < 0)
			return ret;

		ret = dev_pm_domain_attach_list(&pdev->dev, &attach_data1,
						&rdev->pds1);
		dev_info(&pdev->dev, "Attached to multi PM domains ret=%d\n", ret);
		if (ret < 0)
			return ret;

		ret = devm_pm_opp_of_add_table(&pdev->dev);
		if (ret)
			dev_info(&pdev->dev, "Unable to add OPP table %d\n", ret);
	}

	/* Debugfs for performance states. */
	rdev->debugfs_root = debugfs_create_dir(dev_name(&pdev->dev), NULL);
	debugfs_create_file_unsafe("perf_state", S_IRUSR | S_IWUSR,
				   rdev->debugfs_root, rdev,
				   &pm_test_perf_ops);

	debugfs_create_file_unsafe("rate_opp", S_IRUSR | S_IWUSR,
				   rdev->debugfs_root, rdev,
				   &pm_test_rate_opp_ops);

	/* DT property to see if we should delay probe. */
	device_property_read_u32(&pdev->dev, "probe-delay-ms", &probe_delay_ms);

	rdev->dev = &pdev->dev;
	platform_set_drvdata(pdev, rdev);

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 100);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put(&pdev->dev);

	if (probe_delay_ms) {
		dev_info(&pdev->dev, "%s Delaying probe %ums\n",
			 __func__, probe_delay_ms);
		msleep(probe_delay_ms);
	}

	dev_info(&pdev->dev, "%s - DONE\n", __func__);
	return 0;
}

static void pm_test_remove(struct platform_device *pdev)
{
	struct pm_test_dev *rdev = platform_get_drvdata(pdev);

	pm_runtime_get_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	if (rdev->opp)
		dev_pm_opp_put(rdev->opp);

	dev_info(&pdev->dev, "%s 1\n", __func__);
	dev_pm_domain_detach_list(rdev->pds);
	dev_info(&pdev->dev, "%s 2\n", __func__);
	dev_pm_domain_detach_list(rdev->pds0);
	dev_info(&pdev->dev, "%s 3\n", __func__);
	dev_pm_domain_detach_list(rdev->pds1);
	dev_info(&pdev->dev, "%s 4\n", __func__);
	debugfs_remove_recursive(rdev->debugfs_root);
}

#ifdef CONFIG_PM_SLEEP
static int pm_test_suspend(struct device *dev)
{
	int ret;

	dev_info(dev, "%s\n", __func__);
	ret = pm_runtime_force_suspend(dev);
	dev_info(dev, "%s ret=%d\n", __func__, ret);

	return ret;
}

static int pm_test_resume(struct device *dev)
{
	int ret;

	dev_info(dev, "%s\n", __func__);
	ret = pm_runtime_force_resume(dev);
	dev_info(dev, "%s ret=%d\n", __func__, ret);

	return ret;
}
#endif

#ifdef CONFIG_PM
static int pm_test_runtime_suspend(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
	return 0;
}

static int pm_test_runtime_resume(struct device *dev)
{
	dev_info(dev, "%s\n", __func__);
	return 0;
}
#endif

static const struct of_device_id pm_test_ids[] = {
	{
		.compatible = "test,pm-test",
	},
	{},
};
MODULE_DEVICE_TABLE(of, pm_test_ids);

static const struct dev_pm_ops pm_test_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_test_suspend, pm_test_resume)
	SET_RUNTIME_PM_OPS(pm_test_runtime_suspend,
			pm_test_runtime_resume,
			NULL)
};

static struct platform_driver pm_test_driver = {
	.probe = pm_test_probe,
	.remove = pm_test_remove,
	.driver = {
		.name = "pm-test-drv",
		.pm = &pm_test_dev_pm_ops,
		.of_match_table = pm_test_ids,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

module_platform_driver(pm_test_driver);
MODULE_LICENSE("GPL v2");
