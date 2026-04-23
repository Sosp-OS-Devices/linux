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

struct dsi_jdi_1 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct gpio_desc *reset_gpio;
};

static inline struct dsi_jdi_1 *to_dsi_jdi_1(struct drm_panel *panel)
{
	return container_of_const(panel, struct dsi_jdi_1, panel);
}

static void dsi_jdi_1_reset(struct dsi_jdi_1 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	msleep(200);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(20);
}

static int dsi_jdi_1_on(struct dsi_jdi_1 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

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
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb3, 0x14);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xce,
					 0x7d, 0x40, 0x48, 0x56, 0x67, 0x78,
					 0x88, 0x98, 0xa7, 0xb5, 0xc3, 0xd1,
					 0xde, 0xe9, 0xf2, 0xfa, 0xff, 0x04,
					 0x00);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x03);
	mipi_dsi_usleep_range(&dsi_ctx, 10000, 11000);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 16000, 17000);

	return dsi_ctx.accum_err;
}

static int dsi_jdi_1_off(struct dsi_jdi_1 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int dsi_jdi_1_prepare(struct drm_panel *panel)
{
	struct dsi_jdi_1 *ctx = to_dsi_jdi_1(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	dsi_jdi_1_reset(ctx);

	ret = dsi_jdi_1_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	return 0;
}

static int dsi_jdi_1_unprepare(struct drm_panel *panel)
{
	struct dsi_jdi_1 *ctx = to_dsi_jdi_1(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = dsi_jdi_1_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode dsi_jdi_1_mode = {
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

static int dsi_jdi_1_get_modes(struct drm_panel *panel,
			       struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &dsi_jdi_1_mode);
}

static const struct drm_panel_funcs dsi_jdi_1_panel_funcs = {
	.prepare = dsi_jdi_1_prepare,
	.unprepare = dsi_jdi_1_unprepare,
	.get_modes = dsi_jdi_1_get_modes,
};

static int dsi_jdi_1_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct dsi_jdi_1 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct dsi_jdi_1, panel,
				   &dsi_jdi_1_panel_funcs,
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
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_NO_EOT_PACKET |
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

static void dsi_jdi_1_remove(struct mipi_dsi_device *dsi)
{
	struct dsi_jdi_1 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id dsi_jdi_1_of_match[] = {
	{ .compatible = "dsi,jdi-1" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dsi_jdi_1_of_match);

static struct mipi_dsi_driver dsi_jdi_1_driver = {
	.probe = dsi_jdi_1_probe,
	.remove = dsi_jdi_1_remove,
	.driver = {
		.name = "panel-dsi-jdi-1",
		.of_match_table = dsi_jdi_1_of_match,
	},
};
module_mipi_dsi_driver(dsi_jdi_1_driver);

MODULE_AUTHOR("Gnathvm <gnathvm@github.com>");
MODULE_DESCRIPTION("DRM driver for Dual 1 video mode dsi panel");
MODULE_LICENSE("GPL");
