// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2018 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "sun8i_dw_hdmi.h"

/*
 * Address can be actually any value. Here is set to same value as
 * it is set in BSP driver.
 */
#define I2C_ADDR	0x69

static const struct dw_hdmi_mpll_config sun50i_h6_mpll_cfg[] = {
	{
		30666000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40f3, 0x0000 },
		},
	},  {
		36800000, {
			{ 0x00b3, 0x0000 },
			{ 0x2153, 0x0000 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		46000000, {
			{ 0x00b3, 0x0000 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		61333000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x40a2, 0x0001 },
		},
	},  {
		73600000, {
			{ 0x0072, 0x0001 },
			{ 0x2142, 0x0001 },
			{ 0x4061, 0x0002 },
		},
	},  {
		92000000, {
			{ 0x0072, 0x0001 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		122666000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4061, 0x0002 },
		},
	},  {
		147200000, {
			{ 0x0051, 0x0002 },
			{ 0x2145, 0x0002 },
			{ 0x4064, 0x0003 },
		},
	},  {
		184000000, {
			{ 0x0051, 0x0002 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		226666000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		272000000, {
			{ 0x0040, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		340000000, {
			{ 0x0040, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		594000000, {
			{ 0x1a7c, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	}, {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_curr_ctrl sun50i_h6_cur_ctr[] = {
	/* pixelclk    bpp8    bpp10   bpp12 */
	{ 27000000,  { 0x0012, 0x0000, 0x0000 }, },
	{ 74250000,  { 0x0013, 0x001a, 0x001b }, },
	{ 148500000, { 0x0019, 0x0033, 0x0034 }, },
	{ 297000000, { 0x0019, 0x001b, 0x001b }, },
	{ 594000000, { 0x0010, 0x001b, 0x001b }, },
	{ ~0UL,      { 0x0000, 0x0000, 0x0000 }, }
};

static const struct dw_hdmi_phy_config sun50i_h6_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 27000000,  0x8009, 0x0007, 0x02b0 },
	{ 74250000,  0x8009, 0x0006, 0x022d },
	{ 148500000, 0x8029, 0x0006, 0x0270 },
	{ 297000000, 0x8039, 0x0005, 0x01ab },
	{ 594000000, 0x8029, 0x0000, 0x008a },
	{ ~0UL,	     0x0000, 0x0000, 0x0000}
};

static const struct dw_hdmi_mpll_config sun50i_h616_mpll_cfg[] = {
	{
		27000000, {
			{ 0x00b3, 0x0003 },
			{ 0x2153, 0x0003 },
			{ 0x40f3, 0x0003 },
		},
	},  {
		74250000, {
			{ 0x0072, 0x0003 },
			{ 0x2145, 0x0003 },
			{ 0x4061, 0x0003 },
		},
	},  {
		148500000, {
			{ 0x0051, 0x0003 },
			{ 0x214c, 0x0003 },
			{ 0x4064, 0x0003 },
		},
	},  {
		297000000, {
			{ 0x0040, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	},  {
		594000000, {
			{ 0x1a40, 0x0003 },
			{ 0x3b4c, 0x0003 },
			{ 0x5a64, 0x0003 },
		},
	}, {
		~0UL, {
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
			{ 0x0000, 0x0000 },
		},
	}
};

static const struct dw_hdmi_curr_ctrl sun50i_h616_cur_ctr[] = {
	/* pixelclk    bpp8    bpp10   bpp12 */
	{ 27000000,  { 0x0012, 0x0000, 0x0000 }, },
	{ 74250000,  { 0x0013, 0x0013, 0x0013 }, },
	{ 148500000, { 0x0019, 0x0019, 0x0019 }, },
	{ 297000000, { 0x0019, 0x001b, 0x0019 }, },
	{ 594000000, { 0x0010, 0x0010, 0x0010 }, },
	{ ~0UL,      { 0x0000, 0x0000, 0x0000 }, }
};

static const struct dw_hdmi_phy_config sun50i_h616_phy_config[] = {
	/*pixelclk   symbol   term   vlev*/
	{ 27000000,  0x8009, 0x0007, 0x02b0},
	{ 74250000,  0x8019, 0x0004, 0x0290},
	{ 148500000, 0x8019, 0x0004, 0x0290},
	{ 297000000, 0x8039, 0x0004, 0x022b},
	{ 594000000, 0x8029, 0x0000, 0x008a},
	{ ~0UL,	     0x0000, 0x0000, 0x0000}
};

static void sun8i_hdmi_phy_set_polarity(struct sun8i_hdmi_phy *phy,
					const struct drm_display_mode *mode)
{
	u32 val = 0;

	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		val |= SUN8I_HDMI_PHY_DBG_CTRL_POL_NHSYNC;

	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		val |= SUN8I_HDMI_PHY_DBG_CTRL_POL_NVSYNC;

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_DBG_CTRL_REG,
			   SUN8I_HDMI_PHY_DBG_CTRL_POL_MASK, val);
};

static int sun8i_a83t_hdmi_phy_config(struct dw_hdmi *hdmi, void *data,
				      const struct drm_display_info *display,
				      const struct drm_display_mode *mode)
{
	unsigned int clk_rate = mode->crtc_clock * 1000;
	struct sun8i_hdmi_phy *phy = data;

	sun8i_hdmi_phy_set_polarity(phy, mode);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN);

	/* power down */
	dw_hdmi_phy_gen2_txpwron(hdmi, 0);
	dw_hdmi_phy_gen2_pddq(hdmi, 1);

	dw_hdmi_phy_gen2_reset(hdmi);

	dw_hdmi_phy_gen2_pddq(hdmi, 0);

	dw_hdmi_phy_i2c_set_addr(hdmi, I2C_ADDR);

	/*
	 * Values are taken from BSP HDMI driver. Although AW didn't
	 * release any documentation, explanation of this values can
	 * be found in i.MX 6Dual/6Quad Reference Manual.
	 */
	if (clk_rate <= 27000000) {
		dw_hdmi_phy_i2c_write(hdmi, 0x01e0, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x08da, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0007, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x0318, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x8009, 0x09);
	} else if (clk_rate <= 74250000) {
		dw_hdmi_phy_i2c_write(hdmi, 0x0540, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x0005, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0007, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x02b5, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x8009, 0x09);
	} else if (clk_rate <= 148500000) {
		dw_hdmi_phy_i2c_write(hdmi, 0x04a0, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x000a, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0002, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x0021, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x8029, 0x09);
	} else {
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x06);
		dw_hdmi_phy_i2c_write(hdmi, 0x000f, 0x15);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x10);
		dw_hdmi_phy_i2c_write(hdmi, 0x0002, 0x19);
		dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x0e);
		dw_hdmi_phy_i2c_write(hdmi, 0x802b, 0x09);
	}

	dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x1e);
	dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x13);
	dw_hdmi_phy_i2c_write(hdmi, 0x0000, 0x17);

	dw_hdmi_phy_gen2_txpwron(hdmi, 1);

	return 0;
}

static void sun8i_a83t_hdmi_phy_disable(struct dw_hdmi *hdmi, void *data)
{
	struct sun8i_hdmi_phy *phy = data;

	dw_hdmi_phy_gen2_txpwron(hdmi, 0);
	dw_hdmi_phy_gen2_pddq(hdmi, 1);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN, 0);
}

static const struct dw_hdmi_phy_ops sun8i_a83t_hdmi_phy_ops = {
	.init		= sun8i_a83t_hdmi_phy_config,
	.disable	= sun8i_a83t_hdmi_phy_disable,
	.read_hpd	= dw_hdmi_phy_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_phy_setup_hpd,
};

static int sun8i_h3_hdmi_phy_config(struct dw_hdmi *hdmi, void *data,
				    const struct drm_display_info *display,
				    const struct drm_display_mode *mode)
{
	unsigned int clk_rate = mode->crtc_clock * 1000;
	struct sun8i_hdmi_phy *phy = data;
	u32 pll_cfg1_init;
	u32 pll_cfg2_init;
	u32 ana_cfg1_end;
	u32 ana_cfg2_init;
	u32 ana_cfg3_init;
	u32 b_offset = 0;
	u32 val;

	if (phy->variant->has_phy_clk)
		clk_set_rate(phy->clk_phy, clk_rate);

	sun8i_hdmi_phy_set_polarity(phy, mode);

	/* bandwidth / frequency independent settings */

	pll_cfg1_init = SUN8I_HDMI_PHY_PLL_CFG1_LDO2_EN |
			SUN8I_HDMI_PHY_PLL_CFG1_LDO1_EN |
			SUN8I_HDMI_PHY_PLL_CFG1_LDO_VSET(7) |
			SUN8I_HDMI_PHY_PLL_CFG1_UNKNOWN(1) |
			SUN8I_HDMI_PHY_PLL_CFG1_PLLDBEN |
			SUN8I_HDMI_PHY_PLL_CFG1_CS |
			SUN8I_HDMI_PHY_PLL_CFG1_CP_S(2) |
			SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(63) |
			SUN8I_HDMI_PHY_PLL_CFG1_BWS;

	pll_cfg2_init = SUN8I_HDMI_PHY_PLL_CFG2_SV_H |
			SUN8I_HDMI_PHY_PLL_CFG2_VCOGAIN_EN |
			SUN8I_HDMI_PHY_PLL_CFG2_SDIV2;

	ana_cfg1_end = SUN8I_HDMI_PHY_ANA_CFG1_REG_SVBH(1) |
		       SUN8I_HDMI_PHY_ANA_CFG1_AMP_OPT |
		       SUN8I_HDMI_PHY_ANA_CFG1_EMP_OPT |
		       SUN8I_HDMI_PHY_ANA_CFG1_AMPCK_OPT |
		       SUN8I_HDMI_PHY_ANA_CFG1_EMPCK_OPT |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENRCAL |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENCALOG |
		       SUN8I_HDMI_PHY_ANA_CFG1_REG_SCKTMDS |
		       SUN8I_HDMI_PHY_ANA_CFG1_TMDSCLK_EN |
		       SUN8I_HDMI_PHY_ANA_CFG1_TXEN_MASK |
		       SUN8I_HDMI_PHY_ANA_CFG1_TXEN_ALL |
		       SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDSCLK |
		       SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS2 |
		       SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS1 |
		       SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS0 |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS2 |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS1 |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS0 |
		       SUN8I_HDMI_PHY_ANA_CFG1_CKEN |
		       SUN8I_HDMI_PHY_ANA_CFG1_LDOEN |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENVBS |
		       SUN8I_HDMI_PHY_ANA_CFG1_ENBI;

	ana_cfg2_init = SUN8I_HDMI_PHY_ANA_CFG2_M_EN |
			SUN8I_HDMI_PHY_ANA_CFG2_REG_DENCK |
			SUN8I_HDMI_PHY_ANA_CFG2_REG_DEN |
			SUN8I_HDMI_PHY_ANA_CFG2_REG_CKSS(1) |
			SUN8I_HDMI_PHY_ANA_CFG2_REG_CSMPS(1);

	ana_cfg3_init = SUN8I_HDMI_PHY_ANA_CFG3_REG_WIRE(0x3e0) |
			SUN8I_HDMI_PHY_ANA_CFG3_SDAEN |
			SUN8I_HDMI_PHY_ANA_CFG3_SCLEN;

	/* bandwidth / frequency dependent settings */
	if (clk_rate <= 27000000) {
		pll_cfg1_init |= SUN8I_HDMI_PHY_PLL_CFG1_HV_IS_33 |
				 SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(32);
		pll_cfg2_init |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(4) |
				 SUN8I_HDMI_PHY_PLL_CFG2_S(4);
		ana_cfg1_end |= SUN8I_HDMI_PHY_ANA_CFG1_REG_CALSW;
		ana_cfg2_init |= SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(4) |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_RESDI(phy->rcal);
		ana_cfg3_init |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(3) |
				 SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(5);
	} else if (clk_rate <= 74250000) {
		pll_cfg1_init |= SUN8I_HDMI_PHY_PLL_CFG1_HV_IS_33 |
				 SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(32);
		pll_cfg2_init |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(4) |
				 SUN8I_HDMI_PHY_PLL_CFG2_S(5);
		ana_cfg1_end |= SUN8I_HDMI_PHY_ANA_CFG1_REG_CALSW;
		ana_cfg2_init |= SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(4) |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_RESDI(phy->rcal);
		ana_cfg3_init |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(5) |
				 SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(7);
	} else if (clk_rate <= 148500000) {
		pll_cfg1_init |= SUN8I_HDMI_PHY_PLL_CFG1_HV_IS_33 |
				 SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(32);
		pll_cfg2_init |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(4) |
				 SUN8I_HDMI_PHY_PLL_CFG2_S(6);
		ana_cfg2_init |= SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSWCK |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSW |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(2);
		ana_cfg3_init |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(7) |
				 SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(9);
	} else {
		b_offset = 2;
		pll_cfg1_init |= SUN8I_HDMI_PHY_PLL_CFG1_CNT_INT(63);
		pll_cfg2_init |= SUN8I_HDMI_PHY_PLL_CFG2_VCO_S(6) |
				 SUN8I_HDMI_PHY_PLL_CFG2_S(7);
		ana_cfg2_init |= SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSWCK |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_BIGSW |
				 SUN8I_HDMI_PHY_ANA_CFG2_REG_SLV(4);
		ana_cfg3_init |= SUN8I_HDMI_PHY_ANA_CFG3_REG_AMPCK(9) |
				 SUN8I_HDMI_PHY_ANA_CFG3_REG_AMP(13) |
				 SUN8I_HDMI_PHY_ANA_CFG3_REG_EMP(3);
	}

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_TXEN_MASK, 0);

	/*
	 * NOTE: We have to be careful not to overwrite PHY parent
	 * clock selection bit and clock divider.
	 */
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG,
			   (u32)~SUN8I_HDMI_PHY_PLL_CFG1_CKIN_SEL_MSK,
			   pll_cfg1_init);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG2_REG,
			   (u32)~SUN8I_HDMI_PHY_PLL_CFG2_PREDIV_MSK,
			   pll_cfg2_init);
	usleep_range(10000, 15000);
	regmap_write(phy->regs, SUN8I_HDMI_PHY_PLL_CFG3_REG,
		     SUN8I_HDMI_PHY_PLL_CFG3_SOUT_DIV2);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG,
			   SUN8I_HDMI_PHY_PLL_CFG1_PLLEN,
			   SUN8I_HDMI_PHY_PLL_CFG1_PLLEN);
	msleep(100);

	/* get B value */
	regmap_read(phy->regs, SUN8I_HDMI_PHY_ANA_STS_REG, &val);
	val = (val & SUN8I_HDMI_PHY_ANA_STS_B_OUT_MSK) >>
		SUN8I_HDMI_PHY_ANA_STS_B_OUT_SHIFT;
	val = min(val + b_offset, (u32)0x3f);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG,
			   SUN8I_HDMI_PHY_PLL_CFG1_REG_OD1 |
			   SUN8I_HDMI_PHY_PLL_CFG1_REG_OD,
			   SUN8I_HDMI_PHY_PLL_CFG1_REG_OD1 |
			   SUN8I_HDMI_PHY_PLL_CFG1_REG_OD);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG,
			   SUN8I_HDMI_PHY_PLL_CFG1_B_IN_MSK,
			   val << SUN8I_HDMI_PHY_PLL_CFG1_B_IN_SHIFT);
	msleep(100);
	regmap_write(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG, ana_cfg1_end);
	regmap_write(phy->regs, SUN8I_HDMI_PHY_ANA_CFG2_REG, ana_cfg2_init);
	regmap_write(phy->regs, SUN8I_HDMI_PHY_ANA_CFG3_REG, ana_cfg3_init);

	return 0;
}

static void sun8i_h3_hdmi_phy_disable(struct dw_hdmi *hdmi, void *data)
{
	struct sun8i_hdmi_phy *phy = data;

	regmap_write(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
		     SUN8I_HDMI_PHY_ANA_CFG1_LDOEN |
		     SUN8I_HDMI_PHY_ANA_CFG1_ENVBS |
		     SUN8I_HDMI_PHY_ANA_CFG1_ENBI);
	regmap_write(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG, 0);
}

static const struct dw_hdmi_phy_ops sun8i_h3_hdmi_phy_ops = {
	.init		= sun8i_h3_hdmi_phy_config,
	.disable	= sun8i_h3_hdmi_phy_disable,
	.read_hpd	= dw_hdmi_phy_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_phy_setup_hpd,
};

static void sun8i_hdmi_phy_unlock(struct sun8i_hdmi_phy *phy)
{
	/* enable read access to HDMI controller */
	regmap_write(phy->regs, SUN8I_HDMI_PHY_READ_EN_REG,
		     SUN8I_HDMI_PHY_READ_EN_MAGIC);

	/* unscramble register offsets */
	regmap_write(phy->regs, SUN8I_HDMI_PHY_UNSCRAMBLE_REG,
		     SUN8I_HDMI_PHY_UNSCRAMBLE_MAGIC);
}

static void sun50i_hdmi_phy_init_h6(struct sun8i_hdmi_phy *phy)
{
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN,
			   SUN8I_HDMI_PHY_REXT_CTRL_REXT_EN);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_REXT_CTRL_REG,
			   0xffff0000, 0x80c00000);
}

static void sun8i_hdmi_phy_init_a83t(struct sun8i_hdmi_phy *phy)
{
	sun8i_hdmi_phy_unlock(phy);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_DBG_CTRL_REG,
			   SUN8I_HDMI_PHY_DBG_CTRL_PX_LOCK,
			   SUN8I_HDMI_PHY_DBG_CTRL_PX_LOCK);

	/*
	 * Set PHY I2C address. It must match to the address set by
	 * dw_hdmi_phy_set_slave_addr().
	 */
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_DBG_CTRL_REG,
			   SUN8I_HDMI_PHY_DBG_CTRL_ADDR_MASK,
			   SUN8I_HDMI_PHY_DBG_CTRL_ADDR(I2C_ADDR));
}

static void sun8i_hdmi_phy_init_h3(struct sun8i_hdmi_phy *phy)
{
	unsigned int val;

	sun8i_hdmi_phy_unlock(phy);

	regmap_write(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG, 0);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENBI,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENBI);
	udelay(5);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_TMDSCLK_EN,
			   SUN8I_HDMI_PHY_ANA_CFG1_TMDSCLK_EN);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENVBS,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENVBS);
	usleep_range(10, 20);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_LDOEN,
			   SUN8I_HDMI_PHY_ANA_CFG1_LDOEN);
	udelay(5);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_CKEN,
			   SUN8I_HDMI_PHY_ANA_CFG1_CKEN);
	usleep_range(40, 100);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENRCAL,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENRCAL);
	usleep_range(100, 200);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENCALOG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENCALOG);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS0 |
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS1 |
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS2,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS0 |
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS1 |
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDS2);

	/* wait for calibration to finish */
	regmap_read_poll_timeout(phy->regs, SUN8I_HDMI_PHY_ANA_STS_REG, val,
				 (val & SUN8I_HDMI_PHY_ANA_STS_RCALEND2D),
				 100, 2000);

	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDSCLK,
			   SUN8I_HDMI_PHY_ANA_CFG1_ENP2S_TMDSCLK);
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG1_REG,
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS0 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS1 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS2 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDSCLK,
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS0 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS1 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDS2 |
			   SUN8I_HDMI_PHY_ANA_CFG1_BIASEN_TMDSCLK);

	/* enable DDC communication */
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_ANA_CFG3_REG,
			   SUN8I_HDMI_PHY_ANA_CFG3_SCLEN |
			   SUN8I_HDMI_PHY_ANA_CFG3_SDAEN,
			   SUN8I_HDMI_PHY_ANA_CFG3_SCLEN |
			   SUN8I_HDMI_PHY_ANA_CFG3_SDAEN);

	/* reset PHY PLL clock parent */
	regmap_update_bits(phy->regs, SUN8I_HDMI_PHY_PLL_CFG1_REG,
			   SUN8I_HDMI_PHY_PLL_CFG1_CKIN_SEL_MSK, 0);

	/* set HW control of CEC pins */
	regmap_write(phy->regs, SUN8I_HDMI_PHY_CEC_REG, 0);

	/* read calibration data */
	regmap_read(phy->regs, SUN8I_HDMI_PHY_ANA_STS_REG, &val);
	phy->rcal = (val & SUN8I_HDMI_PHY_ANA_STS_RCAL_MASK) >> 2;
}

int sun8i_hdmi_phy_init(struct sun8i_hdmi_phy *phy)
{
	int ret;

	ret = reset_control_deassert(phy->rst_phy);
	if (ret) {
		dev_err(phy->dev, "Cannot deassert phy reset control: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(phy->clk_bus);
	if (ret) {
		dev_err(phy->dev, "Cannot enable bus clock: %d\n", ret);
		goto err_assert_rst_phy;
	}

	ret = clk_prepare_enable(phy->clk_mod);
	if (ret) {
		dev_err(phy->dev, "Cannot enable mod clock: %d\n", ret);
		goto err_disable_clk_bus;
	}

	if (phy->variant->has_phy_clk) {
		ret = sun8i_phy_clk_create(phy, phy->dev,
					   phy->variant->has_second_pll);
		if (ret) {
			dev_err(phy->dev, "Couldn't create the PHY clock\n");
			goto err_disable_clk_mod;
		}

		clk_prepare_enable(phy->clk_phy);
	}

	/*
	 * Variants using custom dw_hdmi_phy_ops (e.g. the A523 Inno PHY) do all
	 * their bring-up in phy_ops->init and have no separate phy_init hook.
	 */
	if (phy->variant->phy_init)
		phy->variant->phy_init(phy);

	return 0;

err_disable_clk_mod:
	clk_disable_unprepare(phy->clk_mod);
err_disable_clk_bus:
	clk_disable_unprepare(phy->clk_bus);
err_assert_rst_phy:
	reset_control_assert(phy->rst_phy);

	return ret;
}

void sun8i_hdmi_phy_deinit(struct sun8i_hdmi_phy *phy)
{
	clk_disable_unprepare(phy->clk_mod);
	clk_disable_unprepare(phy->clk_bus);
	clk_disable_unprepare(phy->clk_phy);

	reset_control_assert(phy->rst_phy);
}

void sun8i_hdmi_phy_set_ops(struct sun8i_hdmi_phy *phy,
			    struct dw_hdmi_plat_data *plat_data)
{
	const struct sun8i_hdmi_phy_variant *variant = phy->variant;

	if (variant->phy_ops) {
		plat_data->phy_ops = variant->phy_ops;
		plat_data->phy_name = "sun8i_dw_hdmi_phy";
		plat_data->phy_data = phy;
	} else {
		plat_data->mpll_cfg = variant->mpll_cfg;
		plat_data->cur_ctr = variant->cur_ctr;
		plat_data->phy_config = variant->phy_cfg;
	}
}

static const struct regmap_config sun8i_hdmi_phy_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= SUN8I_HDMI_PHY_CEC_REG,
	.name		= "phy"
};

/*
 * ----------------------------------------------------------------------------
 * A523/H728 (sun55iw3) Innosilicon HDMI PHY.
 *
 * Ported from the Allwinner BSP (bsp/.../lowlevel_hdmi20/phy_inno.c). The DW-HDMI
 * controller reports DW_HDMI_PHY_VENDOR_PHY for this SoC, so it needs custom
 * dw_hdmi_phy_ops rather than the DWC mpll_cfg path the H6/H616 use.
 *
 * The PHY registers (at phy base 0x5530000) are byte-wide; consecutive bytes
 * pack into the 32-bit words this driver's mmio regmap addresses. A field in
 * byte N at bit b therefore lives at 32-bit position N*8 + b.
 * ----------------------------------------------------------------------------
 */
#define INNO_CTL0			0x00
#define INNO_CTL0_DATA_SYNC		BIT(16)	/* ctl0_2.data_sy_ctl */
#define INNO_CTL0_RST_DI		BIT(6)	/* 0 = reset, 1 = normal */
#define INNO_CTL0_RST_AN		BIT(7)
#define INNO_PLL0			0xa0
#define INNO_PLL0_PREPLL_POW		BIT(0)	/* 0 = on, 1 = off */
#define INNO_PLL0_PIX_CLK_BP		GENMASK(2, 2)	/* via clk_div2 byte2 */
#define INNO_PLL0_PREPLL_DIV		GENMASK(13, 8)
#define INNO_PLL0_PREPLL_FBDIV1		GENMASK(19, 16)
#define INNO_PLL0_PREPLL_FRA_CTL	GENMASK(21, 20)
#define INNO_PLL0_PREPLL_FBDIV0		GENMASK(31, 24)
#define INNO_PLL1			0xa4
#define INNO_PLL1_LINKTMDSCLK_DIV	GENMASK(5, 4)
#define INNO_PLL1_LINKCLK_DIV		GENMASK(3, 2)
#define INNO_PLL1_TMDSCLK_DIV		GENMASK(1, 0)
#define INNO_PLL1_MAINCLK_DIV		GENMASK(14, 13)
#define INNO_PLL1_AUXCLK_DIV		GENMASK(12, 8)
#define INNO_PLL1_RECLK_DIV		GENMASK(22, 21)
#define INNO_PLL1_PIXCLK_DIV		GENMASK(20, 16)
#define INNO_PLL2			0xa8
#define INNO_PLL2_PREPLL_LOCK		BIT(8)	/* pll2_1.prepll_lock_state */
#define INNO_PLL2_POSTPLL_POW		BIT(16)	/* 0 = on, 1 = off */
#define INNO_PLL2_POSTPLL_POSTDIV_EN	GENMASK(19, 18)
#define INNO_PLL2_POSTPLL_PRED_DIV	GENMASK(28, 24)
#define INNO_PLL3			0xac
#define INNO_PLL3_POSTPLL_FBDIV0	GENMASK(7, 0)
#define INNO_PLL3_POSTPLL_POSTDIV	GENMASK(10, 8)
#define INNO_PLL3_POSTPLL_FBDIV1	BIT(12)
#define INNO_PLL3_POSTPLL_LOCK		BIT(24)	/* pll3_3.postpll_lock_state */
#define INNO_DR0			0xb0
#define INNO_DR0_REFRES			BIT(1)	/* 1 = on-chip resistor */
#define INNO_DR0_BIAS_EN		BIT(2)
#define INNO_DR0_CH_DR_EN		GENMASK(19, 16)	/* ch0/1/2 + clk */
#define INNO_DR0_CLK_PRE_EMPL		GENMASK(26, 24)
#define INNO_DR0_CLK_POST_EMPL		GENMASK(31, 28)
#define INNO_DR1			0xb4
#define INNO_DR1_CH_LDO_EN		GENMASK(3, 0)	/* ch0/1/2 + clk */
#define INNO_DR1_CLK_VLEVEL		GENMASK(12, 8)
#define INNO_DR1_CH2_VLEVEL		GENMASK(20, 16)
#define INNO_DR1_CH1_VLEVEL		GENMASK(28, 24)
#define INNO_DR2			0xb8
#define INNO_DR2_CH0_VLEVEL		GENMASK(4, 0)
#define INNO_DR2_CH_LDO_CUR		GENMASK(10, 8)	/* ch0/1/2 */
#define INNO_DR2_CH2_PRE_EMPL		GENMASK(26, 24)
#define INNO_DR2_CH2_POST_EMPL		GENMASK(31, 28)
#define INNO_DR3			0xbc
#define INNO_DR3_CH1_PRE_EMPL		GENMASK(2, 0)
#define INNO_DR3_CH1_POST_EMPL		GENMASK(7, 4)
#define INNO_DR3_CH0_PRE_EMPL		GENMASK(10, 8)
#define INNO_DR3_CH0_POST_EMPL		GENMASK(15, 12)
#define INNO_DR3_CH_SERI_EN		GENMASK(22, 20)	/* ch0/1/2 */
#define INNO_DR3_CH2_CUR_BIAS		GENMASK(27, 24)
#define INNO_DR3_CLK_CUR_BIAS		GENMASK(31, 28)
#define INNO_DR4			0xc0
#define INNO_DR4_CH0_CUR_BIAS		GENMASK(3, 0)
#define INNO_DR4_CH1_CUR_BIAS		GENMASK(7, 4)
#define INNO_DR5			0xc4
#define INNO_DR5_TERRESCAL_CLKDIV1	GENMASK(14, 8)
#define INNO_DR5_TERRESCAL_BP		BIT(15)
#define INNO_DR5_TERRESCAL_CLKDIV0	GENMASK(23, 16)
#define INNO_DR5_TERRES_VAL		GENMASK(26, 25)
#define INNO_DR5_CH_TERRESCAL		GENMASK(31, 28)	/* ch0/1/2 + clk */
#define INNO_RXSEN			0xcc
#define INNO_RXSEN_CH_RXSENSE_EN	GENMASK(3, 0)	/* ch0/1/2 + clk */
#define INNO_RXSEN_CH_DE_STA		GENMASK(15, 8)	/* ch0/1/2/clk de_sta */
#define INNO_PLL_FRA			0xd0
#define INNO_PLL_FRA_DIV2		GENMASK(15, 8)
#define INNO_PLL_FRA_DIV1		GENMASK(23, 16)
#define INNO_PLL_FRA_DIV0		GENMASK(31, 24)

/* per-byte extractor matching the vendor's dw_to_byte() */
#define INNO_B(x, n)			(((x) >> (8 * (n))) & 0xff)

struct inno_phy_mpll {
	u32 tmds_clk;		/* kHz */
	u32 prepll_div;		/* fbdiv1<<16 | fbdiv0<<8 | prediv */
	u32 prepll_clk_div;	/* tmdsdiv<<16 | linkdiv<<8 | linktmdsdiv */
	u32 prepll_clk_div1;	/* auxdiv<<8 | maindiv */
	u32 prepll_clk_div2;	/* pixbp<<16 | pixdiv<<8 | recdiv */
	u32 prepll_fra;		/* fractl<<24 | fdiv0<<16 | fdiv1<<8 | fdiv2 */
	u32 postpll;		/* postdiven<<24 | fbdiv0<<16 | fbdiv1<<8 | preddiv */
	u8  postpll_postdiv;
};

struct inno_phy_elec {
	u32 min_clk, max_clk;	/* kHz */
	u32 cur_bias, vlevel, pre_empl, post_empl;
};

/* tables verbatim from vendor phy_inno.c (phy_mpll[] / phy_elec_default[]) */
static const struct inno_phy_mpll sun55i_a523_inno_mpll[] = {
	{ 25200,  0x00002a01, 0x00010103, 0x00000103, 0x00000403, 0x03000000, 0x03280000, 0x03 },
	{ 27000,  0x00003601, 0x00020202, 0x00000603, 0x00000403, 0x03000000, 0x03280000, 0x03 },
	{ 33750,  0x00003601, 0x00020202, 0x00000603, 0x00000403, 0x03000000, 0x03280000, 0x03 },
	{ 36000,  0x00002401, 0x00010102, 0x00000101, 0x00000403, 0x03000000, 0x03280001, 0x03 },
	{ 40000,  0x00002801, 0x00010102, 0x00000101, 0x00000403, 0x03000000, 0x03140001, 0x01 },
	{ 65000,  0x00004101, 0x00010102, 0x00000101, 0x00000403, 0x03000000, 0x03140001, 0x01 },
	{ 71000,  0x00004701, 0x00010102, 0x00000101, 0x00000403, 0x03000000, 0x03140001, 0x01 },
	{ 74250,  0x00006301, 0x00020201, 0x00000102, 0x00000403, 0x03000000, 0x03140001, 0x01 },
	{ 108000, 0x00002401, 0x00010100, 0x00000100, 0x00000202, 0x03000000, 0x030a0001, 0x00 },
	{ 148500, 0x00006301, 0x00010101, 0x00000102, 0x00000202, 0x03000000, 0x030a0001, 0x00 },
	{ 154000, 0x00004d01, 0x00000002, 0x00000101, 0x00000202, 0x03000000, 0x030a0001, 0x00 },
	{ 185625, 0x00006301, 0x00010101, 0x00000102, 0x00000202, 0x03000000, 0x030a0001, 0x00 },
	{ 234000, 0x00007501, 0x00000002, 0x00000303, 0x00000202, 0x03000000, 0x030a0000, 0x00 },
	{ 297000, 0x00006301, 0x00000001, 0x00000102, 0x00000101, 0x03000000, 0x03140002, 0x00 },
	{ 371250, 0x00007b01, 0x00000201, 0x00000103, 0x00000101, 0x000000c0, 0x000a0002, 0x00 },
	{ 594000, 0x00006301, 0x00000200, 0x00000100, 0x00000101, 0x03000000, 0x00140004, 0x00 },
};

static const struct inno_phy_elec sun55i_a523_inno_elec[] = {
	{  25000, 165000, 0x00020202, 0x1c1c1c1c, 0x00000000, 0x00000000 },
	{ 165000, 340000, 0x02060708, 0x1c1c1c1c, 0x00000000, 0x03030300 },
	{ 340000, 600000, 0x020f0f0f, 0x1c1c1c1c, 0x00000000, 0x03030300 },
};

/*
 * The Inno PHY register file is byte-addressed: every logical 32-bit register
 * is four independently-decoded byte lanes at consecutive addresses (e.g. PLL0
 * occupies 0xa0..0xa3), and the block only answers byte accesses. The regmap is
 * therefore val_bits=8/reg_stride=1. Each field defined here lives entirely
 * within one byte lane, so these helpers translate the logical 32-bit mask into
 * the target byte address + an 8-bit masked update.
 */
static inline void inno_fld(struct sun8i_hdmi_phy *phy, u32 reg, u32 mask, u32 val)
{
	u32 lane = (ffs(mask) - 1) >> 3;
	u32 bmask = (mask >> (lane * 8)) & 0xff;
	u32 bshift = (ffs(mask) - 1) & 7;

	regmap_update_bits(phy->regs, reg + lane, bmask, (val << bshift) & bmask);
}

static void sun55i_a523_inno_set(struct sun8i_hdmi_phy *phy, u32 reg, u32 mask)
{
	u32 lane = (ffs(mask) - 1) >> 3;
	u32 bmask = (mask >> (lane * 8)) & 0xff;

	regmap_update_bits(phy->regs, reg + lane, bmask, bmask);
}

static void sun55i_a523_inno_clr(struct sun8i_hdmi_phy *phy, u32 reg, u32 mask)
{
	u32 lane = (ffs(mask) - 1) >> 3;
	u32 bmask = (mask >> (lane * 8)) & 0xff;

	regmap_update_bits(phy->regs, reg + lane, bmask, 0);
}

/* Read a logical 32-bit register by combining its four byte lanes. */
static u32 inno_rd32(struct sun8i_hdmi_phy *phy, u32 reg)
{
	unsigned int b0 = 0, b1 = 0, b2 = 0, b3 = 0;

	regmap_read(phy->regs, reg + 0, &b0);
	regmap_read(phy->regs, reg + 1, &b1);
	regmap_read(phy->regs, reg + 2, &b2);
	regmap_read(phy->regs, reg + 3, &b3);
	return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static const struct inno_phy_mpll *
sun55i_a523_inno_get_mpll(unsigned int tmds_khz)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sun55i_a523_inno_mpll); i++)
		if (sun55i_a523_inno_mpll[i].tmds_clk == tmds_khz)
			return &sun55i_a523_inno_mpll[i];
	/* fall back to the nearest lower entry */
	for (i = ARRAY_SIZE(sun55i_a523_inno_mpll) - 1; i >= 0; i--)
		if (sun55i_a523_inno_mpll[i].tmds_clk <= tmds_khz)
			return &sun55i_a523_inno_mpll[i];
	return NULL;
}

static void sun55i_a523_inno_turn_off(struct sun8i_hdmi_phy *phy)
{
	sun55i_a523_inno_clr(phy, INNO_DR0, INNO_DR0_CH_DR_EN);	/* driver */
	sun55i_a523_inno_clr(phy, INNO_DR3, INNO_DR3_CH_SERI_EN); /* serializer */
	sun55i_a523_inno_clr(phy, INNO_DR1, INNO_DR1_CH_LDO_EN);	/* LDO */
	sun55i_a523_inno_set(phy, INNO_PLL2, INNO_PLL2_POSTPLL_POW); /* off=1 */
	sun55i_a523_inno_set(phy, INNO_PLL0, INNO_PLL0_PREPLL_POW);
	sun55i_a523_inno_clr(phy, INNO_RXSEN, INNO_RXSEN_CH_RXSENSE_EN);
	sun55i_a523_inno_clr(phy, INNO_DR0, INNO_DR0_BIAS_EN);
}

static bool sun55i_a523_inno_pll_locked(struct sun8i_hdmi_phy *phy)
{
	u32 a, b;

	a = inno_rd32(phy, INNO_PLL2);
	b = inno_rd32(phy, INNO_PLL3);
	return (a & INNO_PLL2_PREPLL_LOCK) && (b & INNO_PLL3_POSTPLL_LOCK);
}

static bool sun55i_a523_inno_rxsense_locked(struct sun8i_hdmi_phy *phy)
{
	u32 v;

	/* each channel's 2-bit de_sta (bits 8..15) must be non-zero */
	v = inno_rd32(phy, INNO_RXSEN);
	return ((v >> 8) & 0x3) && ((v >> 10) & 0x3) &&
	       ((v >> 12) & 0x3) && ((v >> 14) & 0x3);
}

static void sun55i_a523_inno_dump(struct sun8i_hdmi_phy *phy, const char *when)
{
	u32 ctl0, pll0, pll1, pll2, pll3, fra, dr0, rxs;

	ctl0 = inno_rd32(phy, INNO_CTL0);
	pll0 = inno_rd32(phy, INNO_PLL0);
	pll1 = inno_rd32(phy, INNO_PLL1);
	pll2 = inno_rd32(phy, INNO_PLL2);
	pll3 = inno_rd32(phy, INNO_PLL3);
	fra = inno_rd32(phy, INNO_PLL_FRA);
	dr0 = inno_rd32(phy, INNO_DR0);
	rxs = inno_rd32(phy, INNO_RXSEN);
	dev_dbg(phy->dev,
		 "inno %s: ctl0=%08x pll0=%08x pll1=%08x pll2=%08x pll3=%08x fra=%08x dr0=%08x rxsen=%08x\n",
		 when, ctl0, pll0, pll1, pll2, pll3, fra, dr0, rxs);
}

static void sun55i_a523_inno_mpll_config(struct sun8i_hdmi_phy *phy,
					 const struct inno_phy_mpll *c)
{
	inno_fld(phy, INNO_PLL0, INNO_PLL0_PREPLL_DIV,	  INNO_B(c->prepll_div, 0));
	inno_fld(phy, INNO_PLL0, INNO_PLL0_PREPLL_FBDIV0, INNO_B(c->prepll_div, 1));
	inno_fld(phy, INNO_PLL0, INNO_PLL0_PREPLL_FBDIV1, INNO_B(c->prepll_div, 2));

	inno_fld(phy, INNO_PLL1, INNO_PLL1_LINKTMDSCLK_DIV, INNO_B(c->prepll_clk_div, 0));
	inno_fld(phy, INNO_PLL1, INNO_PLL1_LINKCLK_DIV,	    INNO_B(c->prepll_clk_div, 1));
	inno_fld(phy, INNO_PLL1, INNO_PLL1_TMDSCLK_DIV,	    INNO_B(c->prepll_clk_div, 2));

	inno_fld(phy, INNO_PLL1, INNO_PLL1_MAINCLK_DIV,	INNO_B(c->prepll_clk_div1, 0));
	inno_fld(phy, INNO_PLL1, INNO_PLL1_AUXCLK_DIV,	INNO_B(c->prepll_clk_div1, 1));

	inno_fld(phy, INNO_PLL1, INNO_PLL1_RECLK_DIV,	INNO_B(c->prepll_clk_div2, 0));
	inno_fld(phy, INNO_PLL1, INNO_PLL1_PIXCLK_DIV,	INNO_B(c->prepll_clk_div2, 1));
	inno_fld(phy, INNO_PLL0, INNO_PLL0_PIX_CLK_BP,	INNO_B(c->prepll_clk_div2, 2));

	inno_fld(phy, INNO_PLL_FRA, INNO_PLL_FRA_DIV2,	INNO_B(c->prepll_fra, 0));
	inno_fld(phy, INNO_PLL_FRA, INNO_PLL_FRA_DIV1,	INNO_B(c->prepll_fra, 1));
	inno_fld(phy, INNO_PLL_FRA, INNO_PLL_FRA_DIV0,	INNO_B(c->prepll_fra, 2));
	inno_fld(phy, INNO_PLL0, INNO_PLL0_PREPLL_FRA_CTL, INNO_B(c->prepll_fra, 3));

	inno_fld(phy, INNO_PLL2, INNO_PLL2_POSTPLL_PRED_DIV,   INNO_B(c->postpll, 0));
	inno_fld(phy, INNO_PLL3, INNO_PLL3_POSTPLL_FBDIV1,     INNO_B(c->postpll, 1));
	inno_fld(phy, INNO_PLL3, INNO_PLL3_POSTPLL_FBDIV0,     INNO_B(c->postpll, 2));
	inno_fld(phy, INNO_PLL2, INNO_PLL2_POSTPLL_POSTDIV_EN, INNO_B(c->postpll, 3));

	inno_fld(phy, INNO_PLL3, INNO_PLL3_POSTPLL_POSTDIV, c->postpll_postdiv);

	/* turn prePLL + postPLL on (pow bits are active-low: 0 = on) */
	sun55i_a523_inno_clr(phy, INNO_PLL2, INNO_PLL2_POSTPLL_POW);
	sun55i_a523_inno_clr(phy, INNO_PLL0, INNO_PLL0_PREPLL_POW);
}

static const struct inno_phy_elec *
sun55i_a523_inno_get_elec(unsigned int tmds_khz)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(sun55i_a523_inno_elec); i++)
		if (tmds_khz >= sun55i_a523_inno_elec[i].min_clk &&
		    tmds_khz <= sun55i_a523_inno_elec[i].max_clk)
			return &sun55i_a523_inno_elec[i];
	return &sun55i_a523_inno_elec[0];
}

static void sun55i_a523_inno_config_drive(struct sun8i_hdmi_phy *phy,
					  unsigned int tmds_khz)
{
	const struct inno_phy_elec *e = sun55i_a523_inno_get_elec(tmds_khz);

	/* cur_bias: bytes ch0,ch1,ch2,clk */
	inno_fld(phy, INNO_DR4, INNO_DR4_CH0_CUR_BIAS, INNO_B(e->cur_bias, 0));
	inno_fld(phy, INNO_DR4, INNO_DR4_CH1_CUR_BIAS, INNO_B(e->cur_bias, 1));
	inno_fld(phy, INNO_DR3, INNO_DR3_CH2_CUR_BIAS, INNO_B(e->cur_bias, 2));
	inno_fld(phy, INNO_DR3, INNO_DR3_CLK_CUR_BIAS, INNO_B(e->cur_bias, 3));
	/* vlevel: bytes clk,ch2,ch1,ch0 */
	inno_fld(phy, INNO_DR1, INNO_DR1_CLK_VLEVEL, INNO_B(e->vlevel, 0));
	inno_fld(phy, INNO_DR1, INNO_DR1_CH2_VLEVEL, INNO_B(e->vlevel, 1));
	inno_fld(phy, INNO_DR1, INNO_DR1_CH1_VLEVEL, INNO_B(e->vlevel, 2));
	inno_fld(phy, INNO_DR2, INNO_DR2_CH0_VLEVEL, INNO_B(e->vlevel, 3));
	/* pre-emphasis: bytes clk,ch2,ch1,ch0 */
	inno_fld(phy, INNO_DR0, INNO_DR0_CLK_PRE_EMPL, INNO_B(e->pre_empl, 0));
	inno_fld(phy, INNO_DR2, INNO_DR2_CH2_PRE_EMPL, INNO_B(e->pre_empl, 1));
	inno_fld(phy, INNO_DR3, INNO_DR3_CH1_PRE_EMPL, INNO_B(e->pre_empl, 2));
	inno_fld(phy, INNO_DR3, INNO_DR3_CH0_PRE_EMPL, INNO_B(e->pre_empl, 3));
	/* post-emphasis: bytes clk,ch2,ch1,ch0 */
	inno_fld(phy, INNO_DR0, INNO_DR0_CLK_POST_EMPL, INNO_B(e->post_empl, 0));
	inno_fld(phy, INNO_DR2, INNO_DR2_CH2_POST_EMPL, INNO_B(e->post_empl, 1));
	inno_fld(phy, INNO_DR3, INNO_DR3_CH1_POST_EMPL, INNO_B(e->post_empl, 2));
	inno_fld(phy, INNO_DR3, INNO_DR3_CH0_POST_EMPL, INNO_B(e->post_empl, 3));
}

static int sun55i_a523_hdmi_phy_init(struct dw_hdmi *hdmi, void *data,
				     const struct drm_display_info *display,
				     const struct drm_display_mode *mode)
{
	struct sun8i_hdmi_phy *phy = data;
	unsigned int tmds_khz = mode->crtc_clock;
	const struct inno_phy_mpll *mpll;
	u32 v;

	mpll = sun55i_a523_inno_get_mpll(tmds_khz);
	if (!mpll) {
		dev_err(phy->dev, "no Inno PHY MPLL entry for %u kHz\n", tmds_khz);
		return -EINVAL;
	}

	/*
	 * Release the controller-side PHY reset and LEAVE it deasserted.
	 * The Inno PHY's HDMI_MC_PHYRSTZ is active-low like a Gen1 PHY: it must
	 * end at PHYRSTZ=1 or the PHY register block is dark (all reads/writes
	 * drop -> 0). dw_hdmi_phy_gen2_reset() ends at 0 and would keep it held
	 * in reset; gen1_reset() writes 0 then 1, matching the vendor's
	 * dw_mc_sw_reset(PHY, 0) -> dw_mc_sw_reset(PHY, 1) sequence.
	 */
	dw_hdmi_phy_gen2_txpwron(hdmi, 0);
	dw_hdmi_phy_gen2_pddq(hdmi, 1);
	dw_hdmi_phy_gen1_reset(hdmi);

	/* analog + digital reset (active-low pulse) */
	sun55i_a523_inno_clr(phy, INNO_CTL0, INNO_CTL0_RST_AN);
	mdelay(10);
	sun55i_a523_inno_set(phy, INNO_CTL0, INNO_CTL0_RST_AN);
	sun55i_a523_inno_clr(phy, INNO_CTL0, INNO_CTL0_RST_DI);
	mdelay(10);
	sun55i_a523_inno_set(phy, INNO_CTL0, INNO_CTL0_RST_DI);

	sun55i_a523_inno_turn_off(phy);
	mdelay(1);

	sun55i_a523_inno_set(phy, INNO_DR0, INNO_DR0_BIAS_EN);
	sun55i_a523_inno_clr(phy, INNO_DR0, INNO_DR0_REFRES);	/* off-chip */
	/* vendor pokes *(u32*)(base + 0x8004) = 0x100; little-endian byte lanes */
	regmap_write(phy->regs, 0x8004, 0x00);
	regmap_write(phy->regs, 0x8005, 0x01);
	regmap_write(phy->regs, 0x8006, 0x00);
	regmap_write(phy->regs, 0x8007, 0x00);
	sun55i_a523_inno_set(phy, INNO_RXSEN, INNO_RXSEN_CH_RXSENSE_EN);
	mdelay(1);

	if (read_poll_timeout(sun55i_a523_inno_rxsense_locked, v, v, 1000, 10000,
			      false, phy))
		dev_warn(phy->dev, "Inno PHY rxsense lock timeout\n");

	sun55i_a523_inno_dump(phy, "post-rxsense");
	dev_dbg(phy->dev, "inno: tmds=%ukHz mpll prepll_div=%08x clkdiv=%08x postpll=%08x\n",
		 tmds_khz, mpll->prepll_div, mpll->prepll_clk_div, mpll->postpll);

	sun55i_a523_inno_mpll_config(phy, mpll);
	sun55i_a523_inno_dump(phy, "post-mpll");

	if (read_poll_timeout(sun55i_a523_inno_pll_locked, v, v, 1000, 10000,
			      false, phy)) {
		sun55i_a523_inno_dump(phy, "pll-timeout");
		dev_err(phy->dev, "Inno PHY PLL lock timeout\n");
		return -ETIMEDOUT;
	}

	sun55i_a523_inno_set(phy, INNO_DR1, INNO_DR1_CH_LDO_EN);	/* LDO */
	sun55i_a523_inno_set(phy, INNO_DR3, INNO_DR3_CH_SERI_EN); /* serializer */
	sun55i_a523_inno_set(phy, INNO_DR0, INNO_DR0_CH_DR_EN);	/* driver */

	sun55i_a523_inno_config_drive(phy, tmds_khz);

	/* digital reset + data sync */
	sun55i_a523_inno_clr(phy, INNO_CTL0, INNO_CTL0_RST_DI);
	mdelay(10);
	sun55i_a523_inno_set(phy, INNO_CTL0, INNO_CTL0_RST_DI);
	sun55i_a523_inno_clr(phy, INNO_CTL0, INNO_CTL0_DATA_SYNC);
	mdelay(1);
	sun55i_a523_inno_set(phy, INNO_CTL0, INNO_CTL0_DATA_SYNC);

	dw_hdmi_phy_gen2_txpwron(hdmi, 1);
	dw_hdmi_phy_gen2_pddq(hdmi, 0);

	return 0;
}

static void sun55i_a523_hdmi_phy_disable(struct dw_hdmi *hdmi, void *data)
{
	struct sun8i_hdmi_phy *phy = data;

	dw_hdmi_phy_gen2_txpwron(hdmi, 0);
	dw_hdmi_phy_gen2_pddq(hdmi, 1);
	sun55i_a523_inno_turn_off(phy);
}

static const struct dw_hdmi_phy_ops sun55i_a523_hdmi_phy_ops = {
	.init		= sun55i_a523_hdmi_phy_init,
	.disable	= sun55i_a523_hdmi_phy_disable,
	.read_hpd	= dw_hdmi_phy_read_hpd,
	.update_hpd	= dw_hdmi_phy_update_hpd,
	.setup_hpd	= dw_hdmi_phy_setup_hpd,
};

static const struct regmap_config sun55i_a523_hdmi_phy_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 8,
	.reg_stride	= 1,
	.max_register	= 0x10000,
	.name		= "phy"
};

static const struct sun8i_hdmi_phy_variant sun8i_a83t_hdmi_phy = {
	.phy_ops = &sun8i_a83t_hdmi_phy_ops,
	.phy_init = &sun8i_hdmi_phy_init_a83t,
};

static const struct sun8i_hdmi_phy_variant sun8i_h3_hdmi_phy = {
	.has_phy_clk = true,
	.phy_ops = &sun8i_h3_hdmi_phy_ops,
	.phy_init = &sun8i_hdmi_phy_init_h3,
};

static const struct sun8i_hdmi_phy_variant sun8i_r40_hdmi_phy = {
	.has_phy_clk = true,
	.has_second_pll = true,
	.phy_ops = &sun8i_h3_hdmi_phy_ops,
	.phy_init = &sun8i_hdmi_phy_init_h3,
};

static const struct sun8i_hdmi_phy_variant sun50i_a64_hdmi_phy = {
	.has_phy_clk = true,
	.phy_ops = &sun8i_h3_hdmi_phy_ops,
	.phy_init = &sun8i_hdmi_phy_init_h3,
};

static const struct sun8i_hdmi_phy_variant sun50i_h6_hdmi_phy = {
	.cur_ctr  = sun50i_h6_cur_ctr,
	.mpll_cfg = sun50i_h6_mpll_cfg,
	.phy_cfg  = sun50i_h6_phy_config,
	.phy_init = &sun50i_hdmi_phy_init_h6,
};

static const struct sun8i_hdmi_phy_variant sun50i_h616_hdmi_phy = {
	.cur_ctr  = sun50i_h616_cur_ctr,
	.mpll_cfg = sun50i_h616_mpll_cfg,
	.phy_cfg  = sun50i_h616_phy_config,
	.phy_init = &sun50i_hdmi_phy_init_h6,
};


static const struct sun8i_hdmi_phy_variant sun55i_a523_hdmi_phy = {
	.phy_ops	= &sun55i_a523_hdmi_phy_ops,
	.regmap_config	= &sun55i_a523_hdmi_phy_regmap_config,
};

static const struct of_device_id sun8i_hdmi_phy_of_table[] = {
	{
		.compatible = "allwinner,sun8i-a83t-hdmi-phy",
		.data = &sun8i_a83t_hdmi_phy,
	},
	{
		.compatible = "allwinner,sun8i-h3-hdmi-phy",
		.data = &sun8i_h3_hdmi_phy,
	},
	{
		.compatible = "allwinner,sun8i-r40-hdmi-phy",
		.data = &sun8i_r40_hdmi_phy,
	},
	{
		.compatible = "allwinner,sun50i-a64-hdmi-phy",
		.data = &sun50i_a64_hdmi_phy,
	},
	{
		.compatible = "allwinner,sun50i-h6-hdmi-phy",
		.data = &sun50i_h6_hdmi_phy,
	},
	{
		.compatible = "allwinner,sun50i-h616-hdmi-phy",
		.data = &sun50i_h616_hdmi_phy,
	},

	{
		.compatible = "allwinner,sun55i-a523-hdmi-phy",
		.data = &sun55i_a523_hdmi_phy,
	},
	{ /* sentinel */ }
};

int sun8i_hdmi_phy_get(struct sun8i_dw_hdmi *hdmi, struct device_node *node)
{
	struct platform_device *pdev = of_find_device_by_node(node);
	struct sun8i_hdmi_phy *phy;

	if (!pdev)
		return -EPROBE_DEFER;

	phy = platform_get_drvdata(pdev);
	if (!phy) {
		put_device(&pdev->dev);
		return -EPROBE_DEFER;
	}

	hdmi->phy = phy;

	put_device(&pdev->dev);

	return 0;
}

static int sun8i_hdmi_phy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sun8i_hdmi_phy *phy;
	void __iomem *regs;

	phy = devm_kzalloc(dev, sizeof(*phy), GFP_KERNEL);
	if (!phy)
		return -ENOMEM;

	phy->variant = of_device_get_match_data(dev);
	phy->dev = dev;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs),
				     "Couldn't map the HDMI PHY registers\n");

	phy->regs = devm_regmap_init_mmio(dev, regs,
					  phy->variant->regmap_config ?:
					  &sun8i_hdmi_phy_regmap_config);
	if (IS_ERR(phy->regs))
		return dev_err_probe(dev, PTR_ERR(phy->regs),
				     "Couldn't create the HDMI PHY regmap\n");

	phy->clk_bus = devm_clk_get(dev, "bus");
	if (IS_ERR(phy->clk_bus))
		return dev_err_probe(dev, PTR_ERR(phy->clk_bus),
				     "Could not get bus clock\n");

	phy->clk_mod = devm_clk_get(dev, "mod");
	if (IS_ERR(phy->clk_mod))
		return dev_err_probe(dev, PTR_ERR(phy->clk_mod),
				     "Could not get mod clock\n");

	if (phy->variant->has_phy_clk) {
		phy->clk_pll0 = devm_clk_get(dev, "pll-0");
		if (IS_ERR(phy->clk_pll0))
			return dev_err_probe(dev, PTR_ERR(phy->clk_pll0),
					     "Could not get pll-0 clock\n");

		if (phy->variant->has_second_pll) {
			phy->clk_pll1 = devm_clk_get(dev, "pll-1");
			if (IS_ERR(phy->clk_pll1))
				return dev_err_probe(dev, PTR_ERR(phy->clk_pll1),
						     "Could not get pll-1 clock\n");
		}
	}

	phy->rst_phy = devm_reset_control_get_shared(dev, "phy");
	if (IS_ERR(phy->rst_phy))
		return dev_err_probe(dev, PTR_ERR(phy->rst_phy),
				     "Could not get phy reset control\n");

	platform_set_drvdata(pdev, phy);

	return 0;
}

struct platform_driver sun8i_hdmi_phy_driver = {
	.probe  = sun8i_hdmi_phy_probe,
	.driver = {
		.name = "sun8i-hdmi-phy",
		.of_match_table = sun8i_hdmi_phy_of_table,
	},
};
