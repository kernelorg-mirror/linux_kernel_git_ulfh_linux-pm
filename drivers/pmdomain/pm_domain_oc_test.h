/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2025 Linaro Ltd
 *
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 */

#ifndef _PM_DOMAIN_ONECELL_TEST_H
#define _PM_DOMAIN_ONECELL_TEST_H

int _pm_domain_oc_test_probe(struct device *dev);
void _pm_domain_oc_test_remove(struct device *dev);

#endif
