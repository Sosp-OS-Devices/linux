// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 FIXME
// Copyright (c) 2026 Gnathvm
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include <video/mipi_display.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct generic {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
};

static inline struct generic *to_generic(struct drm_panel *panel)
{
	return container_of_const(panel, struct generic, panel);
}

static void generic_reset(struct generic *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(200);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
}

static int generic_on(struct generic *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_ADDRESS_MODE, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	return dsi_ctx.accum_err;
}

static int generic_off(struct generic *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_tear_off_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);

	return dsi_ctx.accum_err;
}

static int generic_prepare(struct drm_panel *panel)
{
	struct generic *ctx = to_generic(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	generic_reset(ctx);

	ret = generic_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int generic_unprepare(struct drm_panel *panel)
{
	struct generic *ctx = to_generic(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = generic_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode generic_mode = {
	.clock = (768 + 26 + 26 + 26) * (1280 + 2 + 2 + 2) * 60 / 1000,
	.hdisplay = 768,
	.hsync_start = 768 + 26,
	.hsync_end = 768 + 26 + 26,
	.htotal = 768 + 26 + 26 + 26,
	.vdisplay = 1280,
	.vsync_start = 1280 + 2,
	.vsync_end = 1280 + 2 + 2,
	.vtotal = 1280 + 2 + 2 + 2,
	.width_mm = 0,
	.height_mm = 0,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int generic_get_modes(struct drm_panel *panel,
			     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &generic_mode);
}

static const struct drm_panel_funcs generic_panel_funcs = {
	.prepare = generic_prepare,
	.unprepare = generic_unprepare,
	.get_modes = generic_get_modes,
};

static int generic_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct generic *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct generic, panel,
				   &generic_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void generic_remove(struct mipi_dsi_device *dsi)
{
	struct generic *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id generic_of_match[] = {
	{ .compatible = "generic" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, generic_of_match);

static struct mipi_dsi_driver generic_driver = {
	.probe = generic_probe,
	.remove = generic_remove,
	.driver = {
		.name = "panel-generic",
		.of_match_table = generic_of_match,
	},
};
module_mipi_dsi_driver(generic_driver);

MODULE_AUTHOR("Gnathvm <gnathvm@github.com>"); 
MODULE_DESCRIPTION("DRM driver for sharp FHD video mode dsi panel");
MODULE_LICENSE("GPL");
