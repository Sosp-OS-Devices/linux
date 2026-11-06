// SPDX-License-Identifier: GPL-2.0
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/io.h>
#include <linux/mailbox_client.h>
#include <linux/bitmap.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>
#include <linux/workqueue.h>
#include "remoteproc_internal.h"

#define E906_STA_ADD_REG	0x204

#define MBOX_NB_VQ		2

/* Device addresses of the RISC-V SRAM windows on the E906. */
#define RSRAM_DA		0x3ffc0000
#define RSRAM1_DA		0x40000000

struct sun55i_mbox {
	struct mbox_chan *chan;
	struct mbox_client client;
	struct work_struct vq_work;
	unsigned long vq_pending;
	spinlock_t vq_lock;
	int vq_id;
};

struct sun55i_e906_rproc {
	struct device *dev;
	struct rproc *rproc;

	void __iomem *e906_cfg;
	void __iomem *r_sram;
	void __iomem *r_sram1;
	size_t r_sram_sz;
	size_t r_sram1_sz;

	struct clk *bus_clk;
	struct clk *core_clk;
	struct clk *sram_clk;

	struct reset_control *cfg_rst;
	struct reset_control *core_rst;
	struct reset_control *sram_rst;
	struct reset_control *debug_rst;

	struct sun55i_mbox mb;
	struct workqueue_struct *workqueue;
};

static struct sun55i_e906_rproc *
sun55i_rproc_priv(struct rproc *rproc)
{
	return (struct sun55i_e906_rproc *)rproc->priv;
}

static void *sun55i_e906_da_to_va(struct rproc *rproc, u64 da, size_t len,
				  bool *is_iomem)
{
	struct sun55i_e906_rproc *priv = sun55i_rproc_priv(rproc);

	/* RISC-V SRAM window 0 (R_SRAM). */
	if (da >= RSRAM_DA && da + len <= RSRAM_DA + priv->r_sram_sz) {
		if (is_iomem)
			*is_iomem = true;
		return priv->r_sram + (da - RSRAM_DA);
	}

	/* RISC-V SRAM window 1 (R_SRAM1). */
	if (da >= RSRAM1_DA && da + len <= RSRAM1_DA + priv->r_sram1_sz) {
		if (is_iomem)
			*is_iomem = true;
		return priv->r_sram1 + (da - RSRAM1_DA);
	}

	/* Shared DDR is handled by the core through the carveouts. */
	return NULL;
}

static int sun55i_mem_alloc(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	void *va;

	va = ioremap_wc(mem->dma, mem->len);
	if (!va)
		return -ENOMEM;

	mem->va = va;
	return 0;
}

static int sun55i_mem_release(struct rproc *rproc, struct rproc_mem_entry *mem)
{
	iounmap(mem->va);
	return 0;
}

static void sun55i_mb_vq_work(struct work_struct *work)
{
	struct sun55i_mbox *mb = container_of(work, struct sun55i_mbox, vq_work);
	struct rproc *rproc = dev_get_drvdata(mb->client.dev);
	unsigned long pending, flags;
	int vq_id, oflow_vq;

	spin_lock_irqsave(&mb->vq_lock, flags);
	pending = mb->vq_pending;
	mb->vq_pending = 0;
	oflow_vq = mb->vq_id;
	mb->vq_id = -1;
	spin_unlock_irqrestore(&mb->vq_lock, flags);

	for_each_set_bit(vq_id, &pending, BITS_PER_LONG)
		if (rproc_vq_interrupt(rproc, vq_id) == IRQ_NONE)
			dev_dbg(&rproc->dev, "no message in vq%d\n", vq_id);

	if (oflow_vq >= 0 && rproc_vq_interrupt(rproc, oflow_vq) == IRQ_NONE)
		dev_dbg(&rproc->dev, "no message in vq%d\n", oflow_vq);
}

static void sun55i_mb_rx_callback(struct mbox_client *cl, void *data)
{
	struct rproc *rproc = dev_get_drvdata(cl->dev);
	struct sun55i_e906_rproc *priv = sun55i_rproc_priv(rproc);
	struct sun55i_mbox *mb = container_of(cl, struct sun55i_mbox, client);
	u32 vq_id = *(u32 *)data;
	unsigned long flags;

	if (vq_id < BITS_PER_LONG) {
		spin_lock_irqsave(&mb->vq_lock, flags);
		mb->vq_pending |= BIT(vq_id);
		spin_unlock_irqrestore(&mb->vq_lock, flags);
	} else {
		spin_lock_irqsave(&mb->vq_lock, flags);
		mb->vq_id = vq_id;
		spin_unlock_irqrestore(&mb->vq_lock, flags);
	}
	queue_work(priv->workqueue, &mb->vq_work);
}

static void sun55i_mb_tx_done(struct mbox_client *cl, void *msg, int r)
{
	struct rproc *rproc = dev_get_drvdata(cl->dev);

	devm_kfree(&rproc->dev, msg);
}

static int sun55i_request_mbox(struct rproc *rproc)
{
	struct sun55i_e906_rproc *priv = sun55i_rproc_priv(rproc);
	struct device *dev = priv->dev;

	if (!of_property_present(dev->of_node, "mboxes"))
		return 0;

	priv->mb.client.rx_callback = sun55i_mb_rx_callback;
	priv->mb.client.tx_done = sun55i_mb_tx_done;
	priv->mb.client.tx_block = false;
	priv->mb.client.dev = dev;

	/* mbox_request_channel() enables the RX IRQ before it returns,
	 * so the callback state must be ready before the request. */
	INIT_WORK(&priv->mb.vq_work, sun55i_mb_vq_work);
	spin_lock_init(&priv->mb.vq_lock);
	priv->mb.vq_id = -1;

	priv->mb.chan = mbox_request_channel(&priv->mb.client, 0);
	if (IS_ERR(priv->mb.chan)) {
		int ret = PTR_ERR(priv->mb.chan);

		if (ret == -EPROBE_DEFER)
			return ret;
		priv->mb.chan = NULL;
	}
	return 0;
}

static void sun55i_free_mbox(struct rproc *rproc)
{
	struct sun55i_e906_rproc *priv = sun55i_rproc_priv(rproc);

	if (priv->mb.chan) {
		mbox_free_channel(priv->mb.chan);
		priv->mb.chan = NULL;
	}
}

static int sun55i_e906_assert(struct sun55i_e906_rproc *priv)
{
	int ret;

	ret = reset_control_assert(priv->core_rst);
	if (ret)
		return ret;
	ret = reset_control_assert(priv->cfg_rst);
	if (ret)
		return ret;
	return reset_control_assert(priv->debug_rst);
}

static int sun55i_e906_deassert(struct sun55i_e906_rproc *priv)
{
	int ret;

	ret = reset_control_deassert(priv->core_rst);
	if (ret)
		return ret;
	ret = reset_control_deassert(priv->cfg_rst);
	if (ret)
		return ret;
	return reset_control_deassert(priv->debug_rst);
}

static int sun55i_e906_enable_sram(struct sun55i_e906_rproc *priv)
{
	int ret;

	ret = reset_control_deassert(priv->sram_rst);
	if (ret)
		return ret;

	ret = clk_prepare_enable(priv->sram_clk);
	if (ret)
		return ret;

	return 0;
}

static int sun55i_e906_start(struct rproc *rproc)
{
	struct sun55i_e906_rproc *priv = sun55i_rproc_priv(rproc);
	struct device *dev = priv->dev;
	int ret;

	ret = sun55i_e906_assert(priv);
	if (ret)
		goto disable_sram;

	ret = sun55i_e906_deassert(priv);
	if (ret)
		goto disable_sram;

	ret = clk_prepare_enable(priv->bus_clk);
	if (ret) {
		dev_err(dev, "bus clk enable failed\n");
		sun55i_e906_assert(priv);
		goto disable_sram;
	}

	/* set start vector */
	writel(rproc->bootaddr, priv->e906_cfg + E906_STA_ADD_REG);

	ret = clk_prepare_enable(priv->core_clk);
	if (ret) {
		dev_err(dev, "core clk enable failed\n");
		clk_disable_unprepare(priv->bus_clk);
		sun55i_e906_assert(priv);
		goto disable_sram;
	}

	return 0;

disable_sram:
	clk_disable_unprepare(priv->sram_clk);
	return ret;
}

static int sun55i_e906_stop(struct rproc *rproc)
{
	struct sun55i_e906_rproc *priv = sun55i_rproc_priv(rproc);
	int ret;

	/* reset before turning off the clock for rv core */
	ret = reset_control_assert(priv->core_rst);
	if (ret)
		return ret;

	clk_disable_unprepare(priv->core_clk);
	clk_disable_unprepare(priv->bus_clk);

	ret = reset_control_assert(priv->cfg_rst);
	if (ret)
		return ret;

	ret = reset_control_assert(priv->debug_rst);
	if (ret)
		return ret;

	clk_disable_unprepare(priv->sram_clk);

	return 0;
}

static int sun55i_e906_attach(struct rproc *rproc)
{
	struct sun55i_e906_rproc *priv = sun55i_rproc_priv(rproc);
	int ret;

	pm_runtime_get_sync(priv->dev);

	ret = clk_prepare_enable(priv->sram_clk);
	if (ret)
		return ret;

	ret = sun55i_e906_deassert(priv);
	if (ret)
		return ret;

	ret = clk_prepare_enable(priv->bus_clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(priv->core_clk);
	if (ret)
		return ret;

	return 0;
}

static void sun55i_e906_kick(struct rproc *rproc, int vqid)
{
	struct sun55i_e906_rproc *priv = sun55i_rproc_priv(rproc);
	u32 *msg;
	int err;

	if (!priv->mb.chan)
		return;
	if (WARN_ON(vqid >= MBOX_NB_VQ))
		return;

	msg = devm_kzalloc(&rproc->dev, sizeof(*msg), GFP_KERNEL);
	if (!msg)
		return;

	*msg = vqid;
	err = mbox_send_message(priv->mb.chan, msg);
	if (err < 0) {
		dev_err(&rproc->dev, "kick mbox failed: %d\n", err);
		devm_kfree(&rproc->dev, msg);
	}
}

static int sun55i_e906_parse_fw(struct rproc *rproc, const struct firmware *fw)
{
	if (rproc_elf_load_rsc_table(rproc, fw)) {
		rproc->cached_table = NULL;
		rproc->table_ptr = NULL;
		rproc->table_sz = 0;
	}

	return 0;
}

static int sun55i_e906_load(struct rproc *rproc, const struct firmware *fw)
{
	struct sun55i_e906_rproc *priv = sun55i_rproc_priv(rproc);
	int ret;

	ret = sun55i_e906_enable_sram(priv);
	if (ret)
		return ret;

	ret = rproc_elf_load_segments(rproc, fw);
	if (ret)
		clk_disable_unprepare(priv->sram_clk);

	return ret;
}

static int sun55i_e906_prepare(struct rproc *rproc)
{
	struct sun55i_e906_rproc *priv = sun55i_rproc_priv(rproc);
	struct device *dev = priv->dev;
	struct device_node *np = dev->of_node;
	struct of_phandle_iterator it;
	struct rproc_mem_entry *mem, *tmp;
	struct reserved_mem *rmem;
	int ret;

	ret = of_phandle_iterator_init(&it, np, "memory-region", NULL, 0);
	if (ret)
		return 0;

	while (of_phandle_iterator_next(&it) == 0) {
		u64 da;

		rmem = of_reserved_mem_lookup(it.node);
		if (!rmem) {
			dev_err(dev, "unable to find memory-region %pOF\n",
				it.node);
			return -EINVAL;
		}

		/* Shared DDR keeps the same address for both ends. */
		da = rmem->base;

		mem = rproc_mem_entry_init(dev, NULL,
					   (dma_addr_t)rmem->base,
					   rmem->size, da, NULL,
					   sun55i_mem_release,
					   it.node->name);
		if (mem) {
			rproc_coredump_add_segment(rproc, da,
						   rmem->size);
			ret = sun55i_mem_alloc(rproc, mem);
			if (ret)
				goto clean_up;
		}

		if (!mem) {
			ret = -ENOMEM;
			goto clean_up;
		}

		rproc_add_carveout(rproc, mem);
	}

	return 0;

clean_up:
	list_for_each_entry_safe(mem, tmp, &rproc->carveouts, node) {
		if (mem->release)
			mem->release(rproc, mem);
		list_del(&mem->node);
		kfree(mem);
	}
	return ret;
}

static int sun55i_e906_unprepare(struct rproc *rproc)
{
	struct rproc_mem_entry *entry, *tmp;

	list_for_each_entry_safe(entry, tmp, &rproc->carveouts, node) {
		if (entry->release)
			entry->release(rproc, entry);
		list_del(&entry->node);
		kfree(entry);
	}
	return 0;
}

static struct resource_table *
sun55i_e906_get_loaded_rsc_table(struct rproc *rproc, size_t *table_sz)
{
	const struct firmware *fw;
	struct resource_table *table;
	int ret;

	ret = request_firmware(&fw, rproc->firmware, &rproc->dev);
	if (ret < 0)
		return ERR_PTR(-ENODEV);

	ret = rproc_elf_sanity_check(rproc, fw);
	if (ret) {
		release_firmware(fw);
		return ERR_PTR(ret);
	}

	table = rproc_elf_find_loaded_rsc_table(rproc, fw, NULL);
	release_firmware(fw);

	if (!table)
		return ERR_PTR(-ENODEV);

	*table_sz = rproc->table_sz;
	return table;
}

static const struct rproc_ops sun55i_e906_rproc_ops = {
	.prepare	= sun55i_e906_prepare,
	.unprepare	= sun55i_e906_unprepare,
	.start		= sun55i_e906_start,
	.stop		= sun55i_e906_stop,
	.attach		= sun55i_e906_attach,
	.da_to_va	= sun55i_e906_da_to_va,
	.kick		= sun55i_e906_kick,
	.parse_fw	= sun55i_e906_parse_fw,
	.load		= sun55i_e906_load,
	.get_loaded_rsc_table = sun55i_e906_get_loaded_rsc_table,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
	.sanity_check	= rproc_elf_sanity_check,
	.get_boot_addr	= rproc_elf_get_boot_addr,
};

static int sun55i_e906_parse_dt(struct sun55i_e906_rproc *priv)
{
	struct device *dev = priv->dev;
	struct platform_device *pdev = to_platform_device(dev);
	struct resource *res;

	priv->sram_clk = devm_clk_get(dev, "sram");
	if (IS_ERR(priv->sram_clk))
		return dev_err_probe(dev, PTR_ERR(priv->sram_clk),
				     "no sram clock\n");

	priv->core_clk = devm_clk_get(dev, "core");
	if (IS_ERR(priv->core_clk))
		return dev_err_probe(dev, PTR_ERR(priv->core_clk),
				     "no core clock\n");

	priv->bus_clk = devm_clk_get(dev, "bus");
	if (IS_ERR(priv->bus_clk))
		return dev_err_probe(dev, PTR_ERR(priv->bus_clk),
				     "no bus clock\n");

	priv->sram_rst = devm_reset_control_get_exclusive(dev, "sram");
	if (IS_ERR(priv->sram_rst))
		return dev_err_probe(dev, PTR_ERR(priv->sram_rst),
				     "no sram reset\n");

	priv->core_rst = devm_reset_control_get_exclusive(dev, "core");
	if (IS_ERR(priv->core_rst))
		return dev_err_probe(dev, PTR_ERR(priv->core_rst),
				     "no core reset\n");

	priv->cfg_rst = devm_reset_control_get_exclusive(dev, "cfg");
	if (IS_ERR(priv->cfg_rst))
		return dev_err_probe(dev, PTR_ERR(priv->cfg_rst),
				     "no cfg reset\n");

	priv->debug_rst = devm_reset_control_get_exclusive(dev, "debug");
	if (IS_ERR(priv->debug_rst))
		return dev_err_probe(dev, PTR_ERR(priv->debug_rst),
				     "no debug reset\n");

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cfg");
	if (!res) {
		dev_err(dev, "no cfg resource\n");
		return -ENXIO;
	}
	priv->e906_cfg = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->e906_cfg))
		return PTR_ERR(priv->e906_cfg);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "r_sram");
	if (!res) {
		dev_err(dev, "no r_sram resource\n");
		return -ENXIO;
	}
	priv->r_sram_sz = resource_size(res);
	priv->r_sram = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->r_sram))
		return PTR_ERR(priv->r_sram);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "r_sram1");
	if (!res) {
		dev_err(dev, "no r_sram1 resource\n");
		return -ENXIO;
	}
	priv->r_sram1_sz = resource_size(res);
	priv->r_sram1 = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->r_sram1))
		return PTR_ERR(priv->r_sram1);

	return 0;
}

static int sun55i_e906_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rproc *rproc;
	struct sun55i_e906_rproc *priv;
	const char *fw_name = NULL;
	int ret;

	of_property_read_string(dev->of_node, "firmware-name", &fw_name);

	rproc = rproc_alloc(dev, dev_name(dev),
			    &sun55i_e906_rproc_ops,
			    fw_name, sizeof(*priv));
	if (!rproc)
		return -ENOMEM;

	priv = sun55i_rproc_priv(rproc);
	priv->dev = dev;
	priv->rproc = rproc;

	ret = sun55i_e906_parse_dt(priv);
	if (ret)
		goto err_free_rproc;

	rproc->has_iommu = false;
	rproc->auto_boot = false;

	/* The RX callback runs in hard IRQ: it reads drvdata and queues
	 * work, so both must be ready before the mailbox request enables
	 * the RX IRQ. */
	platform_set_drvdata(pdev, rproc);

	priv->workqueue = alloc_workqueue(dev_name(dev), WQ_UNBOUND, 1);
	if (!priv->workqueue) {
		ret = -ENOMEM;
		goto err_free_rproc;
	}

	ret = sun55i_request_mbox(rproc);
	if (ret) {
		destroy_workqueue(priv->workqueue);
		goto err_free_rproc;
	}

	pm_runtime_enable(dev);

	ret = rproc_add(rproc);
	if (ret) {
		pm_runtime_disable(dev);
		sun55i_free_mbox(rproc);
		destroy_workqueue(priv->workqueue);
		rproc_free(rproc);
		return ret;
	}

	dev_info(dev, "E906 RISC-V remoteproc ready (fw: %s)\n",
		 fw_name ? fw_name : "none");
	return 0;

err_free_rproc:
	rproc_free(rproc);
	return ret;
}

static void sun55i_e906_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);
	struct sun55i_e906_rproc *priv = sun55i_rproc_priv(rproc);

	rproc_del(rproc);
	sun55i_free_mbox(rproc);
	destroy_workqueue(priv->workqueue);
	pm_runtime_disable(&pdev->dev);
	rproc_free(rproc);
}

static const struct of_device_id sun55i_e906_rproc_of_match[] = {
	{ .compatible = "allwinner,sun55i-a523-rproc" },
	{ .compatible = "allwinner,e906-rproc" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun55i_e906_rproc_of_match);

static struct platform_driver sun55i_e906_rproc_driver = {
	.probe	= sun55i_e906_probe,
	.remove	= sun55i_e906_remove,
	.driver	= {
		.name = "sun55i-e906-rproc",
		.of_match_table = sun55i_e906_rproc_of_match,
	},
};
module_platform_driver(sun55i_e906_rproc_driver);

MODULE_DESCRIPTION("Allwinner T527/A523 E906 RISC-V remoteproc driver");
MODULE_LICENSE("GPL");