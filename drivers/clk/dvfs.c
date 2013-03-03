/*
 * Copyright (C) 2011-2012 Linaro Ltd <mturquette@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Helper functions for dynamic voltage & frequency transitions using
 * the OPP library.
 */

#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/opp.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/module.h>

/*
 * XXX clk, regulator & tolerance should be stored in the OPP table?
 */
struct dvfs_info {
	struct device *dev;
	struct clk *clk;
	struct regulator *reg;
	int tol;
	struct notifier_block nb;
};

#define to_dvfs_info(_nb) container_of(_nb, struct dvfs_info, nb)

static int dvfs_clk_notifier_handler(struct notifier_block *nb,
		unsigned long flags, void *data)
{
	struct clk_notifier_data *cnd = data;
	struct dvfs_info *di = to_dvfs_info(nb);
	int ret, volt_new, volt_old;
	struct opp *opp;

	volt_old = regulator_get_voltage(di->reg);
	rcu_read_lock();
	opp = opp_find_freq_floor(di->dev, &cnd->new_rate);
	volt_new = opp_get_voltage(opp);
	rcu_read_unlock();

	/* scaling up?  scale voltage before frequency */
	if (flags & PRE_RATE_CHANGE && cnd->new_rate > cnd->old_rate) {
		dev_dbg(di->dev, "%s: %d mV --> %d mV\n",
				__func__, volt_old, volt_new);

		ret = regulator_set_voltage_tol(di->reg, volt_new, di->tol);

		if (ret) {
			dev_warn(di->dev, "%s: unable to scale voltage up.\n",
				 __func__);
			return notifier_from_errno(ret);
		}
	}

	/* scaling down?  scale voltage after frequency */
	if (flags & POST_RATE_CHANGE && cnd->new_rate < cnd->old_rate) {
		dev_dbg(di->dev, "%s: %d mV --> %d mV\n",
				__func__, volt_old, volt_new);

		ret = regulator_set_voltage_tol(di->reg, volt_new, di->tol);

		if (ret) {
			dev_warn(di->dev, "%s: unable to scale voltage down.\n",
				 __func__);
			return notifier_from_errno(ret);
		}
	}

	return NOTIFY_OK;
}

struct dvfs_info *dvfs_clk_notifier_register(struct dvfs_info_init *dii)
{
	struct dvfs_info *di;
	int ret = 0;

	if (!dii)
		return ERR_PTR(-EINVAL);

	di = kzalloc(sizeof(struct dvfs_info), GFP_KERNEL);
	if (!di)
		return ERR_PTR(-ENOMEM);

	di->dev = dii->dev;
	di->clk = clk_get(di->dev, dii->con_id);
	if (IS_ERR(di->clk)) {
		ret = -ENOMEM;
		goto err;
	}

	di->reg = regulator_get(di->dev, dii->reg_id);
	if (IS_ERR(di->reg)) {
		ret = -ENOMEM;
		goto err;
	}

	di->tol = dii->tol;
	di->nb.notifier_call = dvfs_clk_notifier_handler;

	ret = clk_notifier_register(di->clk, &di->nb);

	if (ret)
		goto err;

	return di;

err:
	kfree(di);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_GPL(dvfs_clk_notifier_register);

void dvfs_clk_notifier_unregister(struct dvfs_info *di)
{
	clk_notifier_unregister(di->clk, &di->nb);
	clk_put(di->clk);
	regulator_put(di->reg);
	kfree(di);
}
EXPORT_SYMBOL_GPL(dvfs_clk_notifier_unregister);
