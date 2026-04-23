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

struct jdi_dualmipi1 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
};

static inline struct jdi_dualmipi1 *to_jdi_dualmipi1(struct drm_panel *panel)
{
	return container_of_const(panel, struct jdi_dualmipi1, panel);
}

static void jdi_dualmipi1_reset(struct jdi_dualmipi1 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(200);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(200);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
}

static int jdi_dualmipi1_on(struct jdi_dualmipi1 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x03);
	mipi_dsi_dcs_nop_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_set_pixel_format_multi(&dsi_ctx, 0x77);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0x0000, 0x04ff);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x059f);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_set_tear_scanline_multi(&dsi_ctx, 0x0000);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x00ff);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x24);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 16000, 17000);

	return dsi_ctx.accum_err;
}

static int jdi_dualmipi1_off(struct jdi_dualmipi1 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int jdi_dualmipi1_prepare(struct drm_panel *panel)
{
	struct jdi_dualmipi1 *ctx = to_jdi_dualmipi1(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	jdi_dualmipi1_reset(ctx);

	ret = jdi_dualmipi1_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int jdi_dualmipi1_unprepare(struct drm_panel *panel)
{
	struct jdi_dualmipi1 *ctx = to_jdi_dualmipi1(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = jdi_dualmipi1_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode jdi_dualmipi1_mode = {
	.clock = (1280 + 120 + 16 + 44) * (1440 + 8 + 4 + 4) * 60 / 1000,
	.hdisplay = 1280,
	.hsync_start = 1280 + 120,
	.hsync_end = 1280 + 120 + 16,
	.htotal = 1280 + 120 + 16 + 44,
	.vdisplay = 1440,
	.vsync_start = 1440 + 8,
	.vsync_end = 1440 + 8 + 4,
	.vtotal = 1440 + 8 + 4 + 4,
	.width_mm = 0,
	.height_mm = 0,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int jdi_dualmipi1_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &jdi_dualmipi1_mode);
}

static const struct drm_panel_funcs jdi_dualmipi1_panel_funcs = {
	.prepare = jdi_dualmipi1_prepare,
	.unprepare = jdi_dualmipi1_unprepare,
	.get_modes = jdi_dualmipi1_get_modes,
};

static int jdi_dualmipi1_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct jdi_dualmipi1 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct jdi_dualmipi1, panel,
				   &jdi_dualmipi1_panel_funcs,
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

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void jdi_dualmipi1_remove(struct mipi_dsi_device *dsi)
{
	struct jdi_dualmipi1 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id jdi_dualmipi1_of_match[] = {
	{ .compatible = "jdi,dualmipi1" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jdi_dualmipi1_of_match);

static struct mipi_dsi_driver jdi_dualmipi1_driver = {
	.probe = jdi_dualmipi1_probe,
	.remove = jdi_dualmipi1_remove,
	.driver = {
		.name = "panel-jdi-dualmipi1",
		.of_match_table = jdi_dualmipi1_of_match,
	},
};
module_mipi_dsi_driver(jdi_dualmipi1_driver);

MODULE_AUTHOR("Gnathvm <gnathvm@github.com>"); 
MODULE_DESCRIPTION("DRM driver for Dual 1 cmd mode dsi panel");
MODULE_LICENSE("GPL");
