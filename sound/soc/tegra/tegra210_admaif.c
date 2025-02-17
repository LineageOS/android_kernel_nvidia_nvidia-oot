// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPDX-FileCopyrightText: Copyright (c) 2020-2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * tegra210_admaif.c - Tegra ADMAIF driver
 */

#include <nvidia/conftest.h>

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <drivers-private/sound/soc/tegra/tegra_cif.h>
#include <drivers-private/sound/soc/tegra/tegra_pcm.h>

#include "tegra210_admaif.h"
#include "tegra_isomgr_bw.h"

#define CH_REG(offset, reg, id)						       \
	((offset) + (reg) + (TEGRA_ADMAIF_CHANNEL_REG_STRIDE * (id)))

#define CH_TX_REG(reg, id) CH_REG(admaif->soc_data->tx_base, reg, id)

#define CH_RX_REG(reg, id) CH_REG(admaif->soc_data->rx_base, reg, id)

#define REG_DEFAULTS(id, rx_ctrl, tx_ctrl, tx_base, rx_base)		       \
	{ CH_REG(rx_base, TEGRA_ADMAIF_RX_INT_MASK, id), 0x00000001 },	       \
	{ CH_REG(rx_base, TEGRA_ADMAIF_CH_ACIF_RX_CTRL, id), 0x00007700 },     \
	{ CH_REG(rx_base, TEGRA_ADMAIF_RX_FIFO_CTRL, id), rx_ctrl },	       \
	{ CH_REG(tx_base, TEGRA_ADMAIF_TX_INT_MASK, id), 0x00000001 },	       \
	{ CH_REG(tx_base, TEGRA_ADMAIF_CH_ACIF_TX_CTRL, id), 0x00007700 },     \
	{ CH_REG(tx_base, TEGRA_ADMAIF_TX_FIFO_CTRL, id), tx_ctrl }

#define ADMAIF_REG_DEFAULTS(id, chip)					       \
	REG_DEFAULTS((id) - 1,						       \
		chip ## _ADMAIF_RX ## id ## _FIFO_CTRL_REG_DEFAULT,	       \
		chip ## _ADMAIF_TX ## id ## _FIFO_CTRL_REG_DEFAULT,	       \
		chip ## _ADMAIF_TX_BASE,				       \
		chip ## _ADMAIF_RX_BASE)

static const struct reg_default tegra186_admaif_reg_defaults[] = {
	{(TEGRA_ADMAIF_GLOBAL_CG_0 + TEGRA186_ADMAIF_GLOBAL_BASE), 0x00000003},
	ADMAIF_REG_DEFAULTS(1, TEGRA186),
	ADMAIF_REG_DEFAULTS(2, TEGRA186),
	ADMAIF_REG_DEFAULTS(3, TEGRA186),
	ADMAIF_REG_DEFAULTS(4, TEGRA186),
	ADMAIF_REG_DEFAULTS(5, TEGRA186),
	ADMAIF_REG_DEFAULTS(6, TEGRA186),
	ADMAIF_REG_DEFAULTS(7, TEGRA186),
	ADMAIF_REG_DEFAULTS(8, TEGRA186),
	ADMAIF_REG_DEFAULTS(9, TEGRA186),
	ADMAIF_REG_DEFAULTS(10, TEGRA186),
	ADMAIF_REG_DEFAULTS(11, TEGRA186),
	ADMAIF_REG_DEFAULTS(12, TEGRA186),
	ADMAIF_REG_DEFAULTS(13, TEGRA186),
	ADMAIF_REG_DEFAULTS(14, TEGRA186),
	ADMAIF_REG_DEFAULTS(15, TEGRA186),
	ADMAIF_REG_DEFAULTS(16, TEGRA186),
	ADMAIF_REG_DEFAULTS(17, TEGRA186),
	ADMAIF_REG_DEFAULTS(18, TEGRA186),
	ADMAIF_REG_DEFAULTS(19, TEGRA186),
	ADMAIF_REG_DEFAULTS(20, TEGRA186)
};

static const struct reg_default tegra210_admaif_reg_defaults[] = {
	{(TEGRA_ADMAIF_GLOBAL_CG_0 + TEGRA210_ADMAIF_GLOBAL_BASE), 0x00000003},
	ADMAIF_REG_DEFAULTS(1, TEGRA210),
	ADMAIF_REG_DEFAULTS(2, TEGRA210),
	ADMAIF_REG_DEFAULTS(3, TEGRA210),
	ADMAIF_REG_DEFAULTS(4, TEGRA210),
	ADMAIF_REG_DEFAULTS(5, TEGRA210),
	ADMAIF_REG_DEFAULTS(6, TEGRA210),
	ADMAIF_REG_DEFAULTS(7, TEGRA210),
	ADMAIF_REG_DEFAULTS(8, TEGRA210),
	ADMAIF_REG_DEFAULTS(9, TEGRA210),
	ADMAIF_REG_DEFAULTS(10, TEGRA210)
};

static bool tegra_admaif_wr_reg(struct device *dev, unsigned int reg)
{
	struct tegra_admaif *admaif = dev_get_drvdata(dev);
	unsigned int ch_stride = TEGRA_ADMAIF_CHANNEL_REG_STRIDE;
	unsigned int num_ch = admaif->soc_data->num_ch;
	unsigned int rx_base = admaif->soc_data->rx_base;
	unsigned int tx_base = admaif->soc_data->tx_base;
	unsigned int global_base = admaif->soc_data->global_base;
	unsigned int reg_max = admaif->soc_data->regmap_conf->max_register;
	unsigned int rx_max = rx_base + (num_ch * ch_stride);
	unsigned int tx_max = tx_base + (num_ch * ch_stride);

	if ((reg >= rx_base) && (reg < rx_max)) {
		reg = (reg - rx_base) % ch_stride;
		if ((reg == TEGRA_ADMAIF_RX_ENABLE) ||
		    (reg == TEGRA_ADMAIF_RX_FIFO_CTRL) ||
		    (reg == TEGRA_ADMAIF_RX_SOFT_RESET) ||
		    (reg == TEGRA_ADMAIF_CH_ACIF_RX_CTRL))
			return true;
	} else if ((reg >= tx_base) && (reg < tx_max)) {
		reg = (reg - tx_base) % ch_stride;
		if ((reg == TEGRA_ADMAIF_TX_ENABLE) ||
		    (reg == TEGRA_ADMAIF_TX_FIFO_CTRL) ||
		    (reg == TEGRA_ADMAIF_TX_SOFT_RESET) ||
		    (reg == TEGRA_ADMAIF_CH_ACIF_TX_CTRL))
			return true;
	} else if ((reg >= global_base) && (reg < reg_max)) {
		if (reg == (global_base + TEGRA_ADMAIF_GLOBAL_ENABLE))
			return true;
	}

	return false;
}

static bool tegra_admaif_rd_reg(struct device *dev, unsigned int reg)
{
	struct tegra_admaif *admaif = dev_get_drvdata(dev);
	unsigned int ch_stride = TEGRA_ADMAIF_CHANNEL_REG_STRIDE;
	unsigned int num_ch = admaif->soc_data->num_ch;
	unsigned int rx_base = admaif->soc_data->rx_base;
	unsigned int tx_base = admaif->soc_data->tx_base;
	unsigned int global_base = admaif->soc_data->global_base;
	unsigned int reg_max = admaif->soc_data->regmap_conf->max_register;
	unsigned int rx_max = rx_base + (num_ch * ch_stride);
	unsigned int tx_max = tx_base + (num_ch * ch_stride);

	if ((reg >= rx_base) && (reg < rx_max)) {
		reg = (reg - rx_base) % ch_stride;
		if ((reg == TEGRA_ADMAIF_RX_ENABLE) ||
		    (reg == TEGRA_ADMAIF_RX_STATUS) ||
		    (reg == TEGRA_ADMAIF_RX_INT_STATUS) ||
		    (reg == TEGRA_ADMAIF_RX_FIFO_CTRL) ||
		    (reg == TEGRA_ADMAIF_RX_SOFT_RESET) ||
		    (reg == TEGRA_ADMAIF_CH_ACIF_RX_CTRL))
			return true;
	} else if ((reg >= tx_base) && (reg < tx_max)) {
		reg = (reg - tx_base) % ch_stride;
		if ((reg == TEGRA_ADMAIF_TX_ENABLE) ||
		    (reg == TEGRA_ADMAIF_TX_STATUS) ||
		    (reg == TEGRA_ADMAIF_TX_INT_STATUS) ||
		    (reg == TEGRA_ADMAIF_TX_FIFO_CTRL) ||
		    (reg == TEGRA_ADMAIF_TX_SOFT_RESET) ||
		    (reg == TEGRA_ADMAIF_CH_ACIF_TX_CTRL))
			return true;
	} else if ((reg >= global_base) && (reg < reg_max)) {
		if ((reg == (global_base + TEGRA_ADMAIF_GLOBAL_ENABLE)) ||
		    (reg == (global_base + TEGRA_ADMAIF_GLOBAL_CG_0)) ||
		    (reg == (global_base + TEGRA_ADMAIF_GLOBAL_STATUS)) ||
		    (reg == (global_base +
				TEGRA_ADMAIF_GLOBAL_RX_ENABLE_STATUS)) ||
		    (reg == (global_base +
				TEGRA_ADMAIF_GLOBAL_TX_ENABLE_STATUS)))
			return true;
	}

	return false;
}

static bool tegra_admaif_volatile_reg(struct device *dev, unsigned int reg)
{
	struct tegra_admaif *admaif = dev_get_drvdata(dev);
	unsigned int ch_stride = TEGRA_ADMAIF_CHANNEL_REG_STRIDE;
	unsigned int num_ch = admaif->soc_data->num_ch;
	unsigned int rx_base = admaif->soc_data->rx_base;
	unsigned int tx_base = admaif->soc_data->tx_base;
	unsigned int global_base = admaif->soc_data->global_base;
	unsigned int reg_max = admaif->soc_data->regmap_conf->max_register;
	unsigned int rx_max = rx_base + (num_ch * ch_stride);
	unsigned int tx_max = tx_base + (num_ch * ch_stride);

	if ((reg >= rx_base) && (reg < rx_max)) {
		reg = (reg - rx_base) % ch_stride;
		if ((reg == TEGRA_ADMAIF_RX_ENABLE) ||
		    (reg == TEGRA_ADMAIF_RX_STATUS) ||
		    (reg == TEGRA_ADMAIF_RX_INT_STATUS) ||
		    (reg == TEGRA_ADMAIF_RX_SOFT_RESET))
			return true;
	} else if ((reg >= tx_base) && (reg < tx_max)) {
		reg = (reg - tx_base) % ch_stride;
		if ((reg == TEGRA_ADMAIF_TX_ENABLE) ||
		    (reg == TEGRA_ADMAIF_TX_STATUS) ||
		    (reg == TEGRA_ADMAIF_TX_INT_STATUS) ||
		    (reg == TEGRA_ADMAIF_TX_SOFT_RESET))
			return true;
	} else if ((reg >= global_base) && (reg < reg_max)) {
		if ((reg == (global_base + TEGRA_ADMAIF_GLOBAL_STATUS)) ||
		    (reg == (global_base +
				TEGRA_ADMAIF_GLOBAL_RX_ENABLE_STATUS)) ||
		    (reg == (global_base +
				TEGRA_ADMAIF_GLOBAL_TX_ENABLE_STATUS)))
			return true;
	}

	return false;
}

static const struct regmap_config tegra210_admaif_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA210_ADMAIF_LAST_REG,
	.writeable_reg		= tegra_admaif_wr_reg,
	.readable_reg		= tegra_admaif_rd_reg,
	.volatile_reg		= tegra_admaif_volatile_reg,
	.reg_defaults		= tegra210_admaif_reg_defaults,
	.num_reg_defaults	= TEGRA210_ADMAIF_CHANNEL_COUNT * 6 + 1,
	.cache_type		= REGCACHE_FLAT,
};

static const struct regmap_config tegra186_admaif_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= TEGRA186_ADMAIF_LAST_REG,
	.writeable_reg		= tegra_admaif_wr_reg,
	.readable_reg		= tegra_admaif_rd_reg,
	.volatile_reg		= tegra_admaif_volatile_reg,
	.reg_defaults		= tegra186_admaif_reg_defaults,
	.num_reg_defaults	= TEGRA186_ADMAIF_CHANNEL_COUNT * 6 + 1,
	.cache_type		= REGCACHE_FLAT,
};

static int __maybe_unused tegra_admaif_runtime_suspend(struct device *dev)
{
	struct tegra_admaif *admaif = dev_get_drvdata(dev);

	regcache_cache_only(admaif->regmap, true);
	regcache_mark_dirty(admaif->regmap);

	return 0;
}

static int __maybe_unused tegra_admaif_runtime_resume(struct device *dev)
{
	struct tegra_admaif *admaif = dev_get_drvdata(dev);

	regcache_cache_only(admaif->regmap, false);
	regcache_sync(admaif->regmap);

	return 0;
}

static int tegra_admaif_set_pack_mode(struct regmap *map, unsigned int reg,
				      int valid_bit)
{
	switch (valid_bit) {
	case DATA_8BIT:
		regmap_update_bits(map, reg, PACK8_EN_MASK, PACK8_EN);
		regmap_update_bits(map, reg, PACK16_EN_MASK, 0);
		break;
	case DATA_16BIT:
		regmap_update_bits(map, reg, PACK16_EN_MASK, PACK16_EN);
		regmap_update_bits(map, reg, PACK8_EN_MASK, 0);
		break;
	case DATA_32BIT:
		regmap_update_bits(map, reg, PACK16_EN_MASK, 0);
		regmap_update_bits(map, reg, PACK8_EN_MASK, 0);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int tegra_admaif_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	tegra_isomgr_adma_setbw(substream, true);

	return 0;
}

static void tegra_admaif_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	tegra_isomgr_adma_setbw(substream, false);
}

static int tegra_admaif_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct device *dev = dai->dev;
	struct tegra_admaif *admaif = snd_soc_dai_get_drvdata(dai);
	struct tegra_cif_conf cif_conf;
	unsigned int reg, path;
	int valid_bit, channels;

	memset(&cif_conf, 0, sizeof(struct tegra_cif_conf));

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		cif_conf.audio_bits = TEGRA_ACIF_BITS_8;
		cif_conf.client_bits = TEGRA_ACIF_BITS_8;
		valid_bit = DATA_8BIT;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		cif_conf.audio_bits = TEGRA_ACIF_BITS_16;
		cif_conf.client_bits = TEGRA_ACIF_BITS_16;
		valid_bit = DATA_16BIT;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		cif_conf.audio_bits = TEGRA_ACIF_BITS_32;
		cif_conf.client_bits = TEGRA_ACIF_BITS_24;
		valid_bit = DATA_32BIT;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		cif_conf.audio_bits = TEGRA_ACIF_BITS_32;
		cif_conf.client_bits = TEGRA_ACIF_BITS_32;
		valid_bit  = DATA_32BIT;
		break;
	default:
		dev_err(dev, "unsupported format!\n");
		return -EOPNOTSUPP;
	}

	channels = params_channels(params);
	cif_conf.client_ch = channels;
	cif_conf.audio_ch = channels;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		path = ADMAIF_TX_PATH;
		reg = CH_TX_REG(TEGRA_ADMAIF_CH_ACIF_TX_CTRL, dai->id);
	} else {
		path = ADMAIF_RX_PATH;
		reg = CH_RX_REG(TEGRA_ADMAIF_CH_ACIF_RX_CTRL, dai->id);
	}

	if (admaif->audio_ch_override[path][dai->id])
		cif_conf.audio_ch = admaif->audio_ch_override[path][dai->id];

	if (admaif->client_ch_override[path][dai->id])
		cif_conf.client_ch = admaif->client_ch_override[path][dai->id];

	cif_conf.mono_conv = admaif->mono_to_stereo[path][dai->id];
	cif_conf.stereo_conv = admaif->stereo_to_mono[path][dai->id];

	tegra_admaif_set_pack_mode(admaif->regmap, reg, valid_bit);

	tegra_set_cif(admaif->regmap, reg, &cif_conf);

	return 0;
}

static int tegra_admaif_start(struct snd_soc_dai *dai, int direction)
{
	struct tegra_admaif *admaif = snd_soc_dai_get_drvdata(dai);
	unsigned int reg, mask, val;

	switch (direction) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		mask = TX_ENABLE_MASK;
		val = TX_ENABLE;
		reg = CH_TX_REG(TEGRA_ADMAIF_TX_ENABLE, dai->id);
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		mask = RX_ENABLE_MASK;
		val = RX_ENABLE;
		reg = CH_RX_REG(TEGRA_ADMAIF_RX_ENABLE, dai->id);
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(admaif->regmap, reg, mask, val);

	return 0;
}

static int tegra_admaif_stop(struct snd_soc_dai *dai, int direction)
{
	struct tegra_admaif *admaif = snd_soc_dai_get_drvdata(dai);
	unsigned int enable_reg, status_reg, reset_reg, mask, val;
	char *dir_name;
	int err, enable;

	switch (direction) {
	case SNDRV_PCM_STREAM_PLAYBACK:
		mask = TX_ENABLE_MASK;
		enable = TX_ENABLE;
		dir_name = "TX";
		enable_reg = CH_TX_REG(TEGRA_ADMAIF_TX_ENABLE, dai->id);
		status_reg = CH_TX_REG(TEGRA_ADMAIF_TX_STATUS, dai->id);
		reset_reg = CH_TX_REG(TEGRA_ADMAIF_TX_SOFT_RESET, dai->id);
		break;
	case SNDRV_PCM_STREAM_CAPTURE:
		mask = RX_ENABLE_MASK;
		enable = RX_ENABLE;
		dir_name = "RX";
		enable_reg = CH_RX_REG(TEGRA_ADMAIF_RX_ENABLE, dai->id);
		status_reg = CH_RX_REG(TEGRA_ADMAIF_RX_STATUS, dai->id);
		reset_reg = CH_RX_REG(TEGRA_ADMAIF_RX_SOFT_RESET, dai->id);
		break;
	default:
		return -EINVAL;
	}

	/* Disable TX/RX channel */
	regmap_update_bits(admaif->regmap, enable_reg, mask, ~enable);

	/* Wait until ADMAIF TX/RX status is disabled */
	err = regmap_read_poll_timeout_atomic(admaif->regmap, status_reg, val,
					      !(val & enable), 10, 10000);
	if (err < 0)
		dev_warn(dai->dev, "timeout: failed to disable ADMAIF%d_%s\n",
			 dai->id + 1, dir_name);

	/* SW reset */
	regmap_update_bits(admaif->regmap, reset_reg, SW_RESET_MASK, SW_RESET);

	/* Wait till SW reset is complete */
	err = regmap_read_poll_timeout_atomic(admaif->regmap, reset_reg, val,
					      !(val & SW_RESET_MASK & SW_RESET),
					      10, 10000);
	if (err) {
		dev_err(dai->dev, "timeout: SW reset failed for ADMAIF%d_%s\n",
			dai->id + 1, dir_name);
		return err;
	}

	return 0;
}

static int tegra_admaif_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	int err;

	err = snd_dmaengine_pcm_trigger(substream, cmd);
	if (err)
		return err;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		return tegra_admaif_start(dai, substream->stream);
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		return tegra_admaif_stop(dai, substream->stream);
	default:
		return -EINVAL;
	}
}

static void tegra_admaif_reg_dump(struct device *dev)
{
	struct tegra_admaif *admaif = dev_get_drvdata(dev);
	int tx_offset = admaif->soc_data->tx_base;
	int i, stride, ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "parent get_sync failed: %d\n", ret);
		return;
	}

	dev_info(dev, "=========ADMAIF reg dump=========\n");

	for (i = 0; i < admaif->soc_data->num_ch; i++) {
		stride = (i * TEGRA_ADMAIF_CHANNEL_REG_STRIDE);

		dev_info(dev, "RX%d_Enable	= %#x\n", i + 1,
			 readl(admaif->base_addr +
			       TEGRA_ADMAIF_RX_ENABLE + stride));

		dev_info(dev, "RX%d_STATUS	= %#x\n", i + 1,
			 readl(admaif->base_addr +
			       TEGRA_ADMAIF_RX_STATUS + stride));

		dev_info(dev, "RX%d_CIF_CTRL	= %#x\n", i + 1,
			readl(admaif->base_addr +
			      TEGRA_ADMAIF_CH_ACIF_RX_CTRL + stride));

		dev_info(dev, "RX%d_FIFO_CTRL = %#x\n", i + 1,
			 readl(admaif->base_addr +
			       TEGRA_ADMAIF_RX_FIFO_CTRL + stride));

		dev_info(dev, "TX%d_Enable	= %#x\n", i + 1,
			 readl(admaif->base_addr + tx_offset +
			       TEGRA_ADMAIF_TX_ENABLE + stride));

		dev_info(dev, "TX%d_STATUS	= %#x\n", i + 1,
			 readl(admaif->base_addr + tx_offset +
			       TEGRA_ADMAIF_TX_STATUS + stride));

		dev_info(dev, "TX%d_CIF_CTRL	= %#x\n", i + 1,
			readl(admaif->base_addr + tx_offset +
			      TEGRA_ADMAIF_CH_ACIF_TX_CTRL + stride));

		dev_info(dev, "TX%d_FIFO_CTRL = %#x\n", i + 1,
			readl(admaif->base_addr + tx_offset +
			      TEGRA_ADMAIF_TX_FIFO_CTRL + stride));
	}

	pm_runtime_put_sync(dev);
}

static int tegra210_admaif_pget_audio_ch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	ucontrol->value.integer.value[0] =
		admaif->audio_ch_override[ADMAIF_TX_PATH][mc->reg];

	return 0;
}

static int tegra210_admaif_pput_audio_ch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int value = ucontrol->value.integer.value[0];

	if (value == admaif->audio_ch_override[ADMAIF_TX_PATH][mc->reg])
		return 0;

	admaif->audio_ch_override[ADMAIF_TX_PATH][mc->reg] = value;

	return 1;
}

static int tegra210_admaif_cget_audio_ch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	ucontrol->value.integer.value[0] =
		admaif->audio_ch_override[ADMAIF_RX_PATH][mc->reg];

	return 0;
}

static int tegra210_admaif_cput_audio_ch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int value = ucontrol->value.integer.value[0];

	if (admaif->audio_ch_override[ADMAIF_RX_PATH][mc->reg] == value)
		return 0;

	admaif->audio_ch_override[ADMAIF_RX_PATH][mc->reg] = value;

	return 1;
}

static int tegra210_admaif_pget_client_ch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	ucontrol->value.integer.value[0] =
		admaif->client_ch_override[ADMAIF_TX_PATH][mc->reg];

	return 0;
}

static int tegra210_admaif_pput_client_ch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int value = ucontrol->value.integer.value[0];

	if (admaif->client_ch_override[ADMAIF_TX_PATH][mc->reg] == value)
		return 0;

	admaif->client_ch_override[ADMAIF_TX_PATH][mc->reg] = value;

	return 1;
}

static int tegra210_admaif_cget_client_ch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;

	ucontrol->value.integer.value[0] =
		admaif->client_ch_override[ADMAIF_RX_PATH][mc->reg];

	return 0;
}

static int tegra210_admaif_cput_client_ch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	int value = ucontrol->value.integer.value[0];

	if (admaif->client_ch_override[ADMAIF_RX_PATH][mc->reg] == value)
		return 0;

	admaif->client_ch_override[ADMAIF_RX_PATH][mc->reg] = value;

	return 1;
}

static int tegra210_admaif_get_reg_dump(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);

	ucontrol->value.integer.value[0] = admaif->reg_dump_flag;

	return 0;
}

static int tegra210_admaif_put_reg_dump(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	int value = ucontrol->value.integer.value[0];

	if (admaif->reg_dump_flag == value)
		return 0;

	admaif->reg_dump_flag = ucontrol->value.integer.value[0];

	if (admaif->reg_dump_flag) {
		/*
		 * Below call is implemented by downstream ADMA driver.
		 * For Kernel OOT this causes build errors since upstream
		 * ADMA driver is used there. Disable below call for now
		 * until future for this is decided (Bug 3798682).
		 */
//#if IS_ENABLED(CONFIG_TEGRA210_ADMA)
//		tegra_adma_dump_ch_reg();
//#endif

		tegra_admaif_reg_dump(cmpnt->dev);
	}

	return 1;
}

static int tegra210_admaif_pget_mono_to_stereo(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *ec = (struct soc_enum *)kcontrol->private_value;

	ucontrol->value.enumerated.item[0] =
		admaif->mono_to_stereo[ADMAIF_TX_PATH][ec->reg];

	return 0;
}

static int tegra210_admaif_pput_mono_to_stereo(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *ec = (struct soc_enum *)kcontrol->private_value;
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == admaif->mono_to_stereo[ADMAIF_TX_PATH][ec->reg])
		return 0;

	admaif->mono_to_stereo[ADMAIF_TX_PATH][ec->reg] = value;

	return 1;
}

static int tegra210_admaif_cget_mono_to_stereo(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *ec = (struct soc_enum *)kcontrol->private_value;

	ucontrol->value.enumerated.item[0] =
		admaif->mono_to_stereo[ADMAIF_RX_PATH][ec->reg];

	return 0;
}

static int tegra210_admaif_cput_mono_to_stereo(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *ec = (struct soc_enum *)kcontrol->private_value;
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == admaif->mono_to_stereo[ADMAIF_RX_PATH][ec->reg])
		return 0;

	admaif->mono_to_stereo[ADMAIF_RX_PATH][ec->reg] = value;

	return 1;
}

static int tegra210_admaif_pget_stereo_to_mono(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *ec = (struct soc_enum *)kcontrol->private_value;

	ucontrol->value.enumerated.item[0] =
		admaif->stereo_to_mono[ADMAIF_TX_PATH][ec->reg];

	return 0;
}

static int tegra210_admaif_pput_stereo_to_mono(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *ec = (struct soc_enum *)kcontrol->private_value;
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == admaif->stereo_to_mono[ADMAIF_TX_PATH][ec->reg])
		return 0;

	admaif->stereo_to_mono[ADMAIF_TX_PATH][ec->reg] = value;

	return 1;
}

static int tegra210_admaif_cget_stereo_to_mono(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *ec = (struct soc_enum *)kcontrol->private_value;

	ucontrol->value.enumerated.item[0] =
		admaif->stereo_to_mono[ADMAIF_RX_PATH][ec->reg];

	return 0;
}

static int tegra210_admaif_cput_stereo_to_mono(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *cmpnt = snd_soc_kcontrol_component(kcontrol);
	struct tegra_admaif *admaif = snd_soc_component_get_drvdata(cmpnt);
	struct soc_enum *ec = (struct soc_enum *)kcontrol->private_value;
	unsigned int value = ucontrol->value.enumerated.item[0];

	if (value == admaif->stereo_to_mono[ADMAIF_RX_PATH][ec->reg])
		return 0;

	admaif->stereo_to_mono[ADMAIF_RX_PATH][ec->reg] = value;

	return 1;
}

static int tegra_admaif_dai_probe(struct snd_soc_dai *dai)
{
	struct tegra_admaif *admaif = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &admaif->playback_dma_data[dai->id],
				  &admaif->capture_dma_data[dai->id]);

	return 0;
}

static const struct snd_soc_dai_ops tegra_admaif_dai_ops = {
#if defined(NV_SND_SOC_DAI_OPS_STRUCT_HAS_PROBE_PRESENT) /* Linux 6.5 */
	.probe = tegra_admaif_dai_probe,
#endif
	.hw_params	= tegra_admaif_hw_params,
	.trigger	= tegra_admaif_trigger,
	.shutdown	= tegra_admaif_shutdown,
	.prepare	= tegra_admaif_prepare,
};

#if defined(NV_SND_SOC_DAI_OPS_STRUCT_HAS_PROBE_PRESENT) /* Linux 6.5 */
#define DAI(dai_name)					\
	{							\
		.name = dai_name,				\
		.playback = {					\
			.stream_name = dai_name " Playback",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_KNOT,		\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = dai_name " Capture",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_KNOT,		\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.ops = &tegra_admaif_dai_ops,			\
	}
#else
#define DAI(dai_name)					\
	{							\
		.name = dai_name,				\
		.probe = tegra_admaif_dai_probe,		\
		.playback = {					\
			.stream_name = dai_name " Playback",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_KNOT,		\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.capture = {					\
			.stream_name = dai_name " Capture",	\
			.channels_min = 1,			\
			.channels_max = 16,			\
			.rates = SNDRV_PCM_RATE_KNOT,		\
			.formats = SNDRV_PCM_FMTBIT_S8 |	\
				SNDRV_PCM_FMTBIT_S16_LE |	\
				SNDRV_PCM_FMTBIT_S24_LE |	\
				SNDRV_PCM_FMTBIT_S32_LE,	\
		},						\
		.ops = &tegra_admaif_dai_ops,			\
	}
#endif


#define ADMAIF_CODEC_FIFO_DAI(id)                                       \
	{								\
		.name = "ADMAIF" #id " FIFO",				\
		.playback = {						\
			.stream_name = "ADMAIF" #id " FIFO Transmit",	\
			.channels_min = 1,				\
			.channels_max = 16,				\
			.rates = SNDRV_PCM_RATE_KNOT,			\
			.formats = SNDRV_PCM_FMTBIT_S8 |		\
				SNDRV_PCM_FMTBIT_S16_LE |		\
				SNDRV_PCM_FMTBIT_S24_LE |		\
				SNDRV_PCM_FMTBIT_S32_LE,		\
		},							\
		.capture = {						\
			.stream_name = "ADMAIF" #id " FIFO Receive",	\
			.channels_min = 1,				\
			.channels_max = 16,				\
			.rates = SNDRV_PCM_RATE_KNOT,			\
			.formats = SNDRV_PCM_FMTBIT_S8 |		\
				SNDRV_PCM_FMTBIT_S16_LE |		\
				SNDRV_PCM_FMTBIT_S24_LE |		\
				SNDRV_PCM_FMTBIT_S32_LE,		\
		},							\
		.ops = &tegra_admaif_dai_ops,				\
	}

#define ADMAIF_CODEC_CIF_DAI(id)					\
	{								\
		.name = "ADMAIF" #id " CIF",				\
		.playback = {						\
			.stream_name = "ADMAIF" #id " CIF Transmit",	\
			.channels_min = 1,				\
			.channels_max = 16,				\
			.rates = SNDRV_PCM_RATE_KNOT,			\
			.formats = SNDRV_PCM_FMTBIT_S8 |		\
				SNDRV_PCM_FMTBIT_S16_LE |		\
				SNDRV_PCM_FMTBIT_S24_LE |		\
				SNDRV_PCM_FMTBIT_S32_LE,		\
		},							\
		.capture = {						\
			.stream_name = "ADMAIF" #id " CIF Receive",	\
			.channels_min = 1,				\
			.channels_max = 16,				\
			.rates = SNDRV_PCM_RATE_KNOT,			\
			.formats = SNDRV_PCM_FMTBIT_S8 |		\
				SNDRV_PCM_FMTBIT_S16_LE |		\
				SNDRV_PCM_FMTBIT_S24_LE |		\
				SNDRV_PCM_FMTBIT_S32_LE,		\
		},							\
	}

static struct snd_soc_dai_driver tegra210_admaif_cmpnt_dais[] = {
	DAI("ADMAIF1"),
	DAI("ADMAIF2"),
	DAI("ADMAIF3"),
	DAI("ADMAIF4"),
	DAI("ADMAIF5"),
	DAI("ADMAIF6"),
	DAI("ADMAIF7"),
	DAI("ADMAIF8"),
	DAI("ADMAIF9"),
	DAI("ADMAIF10"),
	ADMAIF_CODEC_FIFO_DAI(1),
	ADMAIF_CODEC_FIFO_DAI(2),
	ADMAIF_CODEC_FIFO_DAI(3),
	ADMAIF_CODEC_FIFO_DAI(4),
	ADMAIF_CODEC_FIFO_DAI(5),
	ADMAIF_CODEC_FIFO_DAI(6),
	ADMAIF_CODEC_FIFO_DAI(7),
	ADMAIF_CODEC_FIFO_DAI(8),
	ADMAIF_CODEC_FIFO_DAI(9),
	ADMAIF_CODEC_FIFO_DAI(10),
	ADMAIF_CODEC_CIF_DAI(1),
	ADMAIF_CODEC_CIF_DAI(2),
	ADMAIF_CODEC_CIF_DAI(3),
	ADMAIF_CODEC_CIF_DAI(4),
	ADMAIF_CODEC_CIF_DAI(5),
	ADMAIF_CODEC_CIF_DAI(6),
	ADMAIF_CODEC_CIF_DAI(7),
	ADMAIF_CODEC_CIF_DAI(8),
	ADMAIF_CODEC_CIF_DAI(9),
	ADMAIF_CODEC_CIF_DAI(10),
};

static struct snd_soc_dai_driver tegra186_admaif_cmpnt_dais[] = {
	DAI("ADMAIF1"),
	DAI("ADMAIF2"),
	DAI("ADMAIF3"),
	DAI("ADMAIF4"),
	DAI("ADMAIF5"),
	DAI("ADMAIF6"),
	DAI("ADMAIF7"),
	DAI("ADMAIF8"),
	DAI("ADMAIF9"),
	DAI("ADMAIF10"),
	DAI("ADMAIF11"),
	DAI("ADMAIF12"),
	DAI("ADMAIF13"),
	DAI("ADMAIF14"),
	DAI("ADMAIF15"),
	DAI("ADMAIF16"),
	DAI("ADMAIF17"),
	DAI("ADMAIF18"),
	DAI("ADMAIF19"),
	DAI("ADMAIF20"),
	ADMAIF_CODEC_FIFO_DAI(1),
	ADMAIF_CODEC_FIFO_DAI(2),
	ADMAIF_CODEC_FIFO_DAI(3),
	ADMAIF_CODEC_FIFO_DAI(4),
	ADMAIF_CODEC_FIFO_DAI(5),
	ADMAIF_CODEC_FIFO_DAI(6),
	ADMAIF_CODEC_FIFO_DAI(7),
	ADMAIF_CODEC_FIFO_DAI(8),
	ADMAIF_CODEC_FIFO_DAI(9),
	ADMAIF_CODEC_FIFO_DAI(10),
	ADMAIF_CODEC_FIFO_DAI(11),
	ADMAIF_CODEC_FIFO_DAI(12),
	ADMAIF_CODEC_FIFO_DAI(13),
	ADMAIF_CODEC_FIFO_DAI(14),
	ADMAIF_CODEC_FIFO_DAI(15),
	ADMAIF_CODEC_FIFO_DAI(16),
	ADMAIF_CODEC_FIFO_DAI(17),
	ADMAIF_CODEC_FIFO_DAI(18),
	ADMAIF_CODEC_FIFO_DAI(19),
	ADMAIF_CODEC_FIFO_DAI(20),
	ADMAIF_CODEC_CIF_DAI(1),
	ADMAIF_CODEC_CIF_DAI(2),
	ADMAIF_CODEC_CIF_DAI(3),
	ADMAIF_CODEC_CIF_DAI(4),
	ADMAIF_CODEC_CIF_DAI(5),
	ADMAIF_CODEC_CIF_DAI(6),
	ADMAIF_CODEC_CIF_DAI(7),
	ADMAIF_CODEC_CIF_DAI(8),
	ADMAIF_CODEC_CIF_DAI(9),
	ADMAIF_CODEC_CIF_DAI(10),
	ADMAIF_CODEC_CIF_DAI(11),
	ADMAIF_CODEC_CIF_DAI(12),
	ADMAIF_CODEC_CIF_DAI(13),
	ADMAIF_CODEC_CIF_DAI(14),
	ADMAIF_CODEC_CIF_DAI(15),
	ADMAIF_CODEC_CIF_DAI(16),
	ADMAIF_CODEC_CIF_DAI(17),
	ADMAIF_CODEC_CIF_DAI(18),
	ADMAIF_CODEC_CIF_DAI(19),
	ADMAIF_CODEC_CIF_DAI(20),
};

#define ADMAIF_WIDGETS(id)					\
	SND_SOC_DAPM_AIF_IN("ADMAIF" #id " FIFO RX", NULL, 0,	\
		SND_SOC_NOPM, 0, 0),				\
	SND_SOC_DAPM_AIF_OUT("ADMAIF" #id " FIFO TX", NULL, 0,	\
		SND_SOC_NOPM, 0, 0),				\
	SND_SOC_DAPM_AIF_IN("ADMAIF" #id " CIF RX", NULL, 0,	\
		SND_SOC_NOPM, 0, 0),				\
	SND_SOC_DAPM_AIF_OUT("ADMAIF" #id " CIF TX", NULL, 0,	\
		SND_SOC_NOPM, 0, 0)

static const struct snd_soc_dapm_widget tegra_admaif_widgets[] = {
	ADMAIF_WIDGETS(1),
	ADMAIF_WIDGETS(2),
	ADMAIF_WIDGETS(3),
	ADMAIF_WIDGETS(4),
	ADMAIF_WIDGETS(5),
	ADMAIF_WIDGETS(6),
	ADMAIF_WIDGETS(7),
	ADMAIF_WIDGETS(8),
	ADMAIF_WIDGETS(9),
	ADMAIF_WIDGETS(10),
	ADMAIF_WIDGETS(11),
	ADMAIF_WIDGETS(12),
	ADMAIF_WIDGETS(13),
	ADMAIF_WIDGETS(14),
	ADMAIF_WIDGETS(15),
	ADMAIF_WIDGETS(16),
	ADMAIF_WIDGETS(17),
	ADMAIF_WIDGETS(18),
	ADMAIF_WIDGETS(19),
	ADMAIF_WIDGETS(20)
};

#define ADMAIF_ROUTES(id)						       \
	{ "ADMAIF" #id " FIFO RX",	NULL, "ADMAIF" #id " FIFO Transmit" }, \
	{ "ADMAIF" #id " CIF TX",	NULL, "ADMAIF" #id " FIFO RX" },       \
	{ "ADMAIF" #id " CIF Receive",	NULL, "ADMAIF" #id " CIF TX" },	       \
	{ "ADMAIF" #id " CIF RX",	NULL, "ADMAIF" #id " CIF Transmit" },  \
	{ "ADMAIF" #id " FIFO TX",	NULL, "ADMAIF" #id " CIF RX" },	       \
	{ "ADMAIF" #id " FIFO Receive", NULL, "ADMAIF" #id " FIFO TX" }	       \

static const struct snd_soc_dapm_route tegra_admaif_routes[] = {
	ADMAIF_ROUTES(1),
	ADMAIF_ROUTES(2),
	ADMAIF_ROUTES(3),
	ADMAIF_ROUTES(4),
	ADMAIF_ROUTES(5),
	ADMAIF_ROUTES(6),
	ADMAIF_ROUTES(7),
	ADMAIF_ROUTES(8),
	ADMAIF_ROUTES(9),
	ADMAIF_ROUTES(10),
	ADMAIF_ROUTES(11),
	ADMAIF_ROUTES(12),
	ADMAIF_ROUTES(13),
	ADMAIF_ROUTES(14),
	ADMAIF_ROUTES(15),
	ADMAIF_ROUTES(16),
	ADMAIF_ROUTES(17),
	ADMAIF_ROUTES(18),
	ADMAIF_ROUTES(19),
	ADMAIF_ROUTES(20)
};

static const char * const tegra_admaif_stereo_conv_text[] = {
	"CH0", "CH1", "AVG",
};

static const char * const tegra_admaif_mono_conv_text[] = {
	"Zero", "Copy",
};

#define TEGRA_ADMAIF_CHANNEL_CTRL(reg)					  \
	SOC_SINGLE_EXT("ADMAIF" #reg " Playback Audio Channels", reg - 1, \
		       0, 16, 0, tegra210_admaif_pget_audio_ch,		  \
		       tegra210_admaif_pput_audio_ch),			  \
	SOC_SINGLE_EXT("ADMAIF" #reg " Capture Audio Channels", reg - 1,  \
		       0, 16, 0, tegra210_admaif_cget_audio_ch,		  \
		       tegra210_admaif_cput_audio_ch),			  \
	SOC_SINGLE_EXT("ADMAIF" #reg " Playback Client Channels", reg - 1,\
		       0, 16, 0, tegra210_admaif_pget_client_ch,	  \
		       tegra210_admaif_pput_client_ch),			  \
	SOC_SINGLE_EXT("ADMAIF" #reg " Capture Client Channels", reg - 1, \
		       0, 16, 0, tegra210_admaif_cget_client_ch,	  \
		       tegra210_admaif_cput_client_ch)

/*
 * Below macro is added to avoid looping over all ADMAIFx controls related
 * to mono/stereo conversions in get()/put() callbacks.
 */
#define NV_SOC_ENUM_EXT(xname, xreg, xhandler_get, xhandler_put, xenum_text)   \
{									       \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,				       \
	.info = snd_soc_info_enum_double,				       \
	.name = xname,							       \
	.get = xhandler_get,						       \
	.put = xhandler_put,						       \
	.private_value = (unsigned long)&(struct soc_enum)		       \
		SOC_ENUM_SINGLE(xreg, 0, ARRAY_SIZE(xenum_text), xenum_text)   \
}

#define TEGRA_ADMAIF_CIF_CTRL(reg)					       \
	NV_SOC_ENUM_EXT("ADMAIF" #reg " Playback Mono To Stereo", reg - 1,     \
			tegra210_admaif_pget_mono_to_stereo,		       \
			tegra210_admaif_pput_mono_to_stereo,		       \
			tegra_admaif_mono_conv_text),			       \
	NV_SOC_ENUM_EXT("ADMAIF" #reg " Playback Stereo To Mono", reg - 1,     \
			tegra210_admaif_pget_stereo_to_mono,		       \
			tegra210_admaif_pput_stereo_to_mono,		       \
			tegra_admaif_stereo_conv_text),			       \
	NV_SOC_ENUM_EXT("ADMAIF" #reg " Capture Mono To Stereo", reg - 1,      \
			tegra210_admaif_cget_mono_to_stereo,		       \
			tegra210_admaif_cput_mono_to_stereo,		       \
			tegra_admaif_mono_conv_text),			       \
	NV_SOC_ENUM_EXT("ADMAIF" #reg " Capture Stereo To Mono", reg - 1,      \
			tegra210_admaif_cget_stereo_to_mono,		       \
			tegra210_admaif_cput_stereo_to_mono,		       \
			tegra_admaif_stereo_conv_text)

static struct snd_kcontrol_new tegra210_admaif_controls[] = {
	TEGRA_ADMAIF_CHANNEL_CTRL(1),
	TEGRA_ADMAIF_CHANNEL_CTRL(2),
	TEGRA_ADMAIF_CHANNEL_CTRL(3),
	TEGRA_ADMAIF_CHANNEL_CTRL(4),
	TEGRA_ADMAIF_CHANNEL_CTRL(5),
	TEGRA_ADMAIF_CHANNEL_CTRL(6),
	TEGRA_ADMAIF_CHANNEL_CTRL(7),
	TEGRA_ADMAIF_CHANNEL_CTRL(8),
	TEGRA_ADMAIF_CHANNEL_CTRL(9),
	TEGRA_ADMAIF_CHANNEL_CTRL(10),
	TEGRA_ADMAIF_CIF_CTRL(1),
	TEGRA_ADMAIF_CIF_CTRL(2),
	TEGRA_ADMAIF_CIF_CTRL(3),
	TEGRA_ADMAIF_CIF_CTRL(4),
	TEGRA_ADMAIF_CIF_CTRL(5),
	TEGRA_ADMAIF_CIF_CTRL(6),
	TEGRA_ADMAIF_CIF_CTRL(7),
	TEGRA_ADMAIF_CIF_CTRL(8),
	TEGRA_ADMAIF_CIF_CTRL(9),
	TEGRA_ADMAIF_CIF_CTRL(10),
	SOC_SINGLE_EXT("APE Reg Dump", SND_SOC_NOPM, 0, 1, 0,
		       tegra210_admaif_get_reg_dump,
		       tegra210_admaif_put_reg_dump),
};

static struct snd_kcontrol_new tegra186_admaif_controls[] = {
	TEGRA_ADMAIF_CHANNEL_CTRL(1),
	TEGRA_ADMAIF_CHANNEL_CTRL(2),
	TEGRA_ADMAIF_CHANNEL_CTRL(3),
	TEGRA_ADMAIF_CHANNEL_CTRL(4),
	TEGRA_ADMAIF_CHANNEL_CTRL(5),
	TEGRA_ADMAIF_CHANNEL_CTRL(6),
	TEGRA_ADMAIF_CHANNEL_CTRL(7),
	TEGRA_ADMAIF_CHANNEL_CTRL(8),
	TEGRA_ADMAIF_CHANNEL_CTRL(9),
	TEGRA_ADMAIF_CHANNEL_CTRL(10),
	TEGRA_ADMAIF_CHANNEL_CTRL(11),
	TEGRA_ADMAIF_CHANNEL_CTRL(12),
	TEGRA_ADMAIF_CHANNEL_CTRL(13),
	TEGRA_ADMAIF_CHANNEL_CTRL(14),
	TEGRA_ADMAIF_CHANNEL_CTRL(15),
	TEGRA_ADMAIF_CHANNEL_CTRL(16),
	TEGRA_ADMAIF_CHANNEL_CTRL(17),
	TEGRA_ADMAIF_CHANNEL_CTRL(18),
	TEGRA_ADMAIF_CHANNEL_CTRL(19),
	TEGRA_ADMAIF_CHANNEL_CTRL(20),
	TEGRA_ADMAIF_CIF_CTRL(1),
	TEGRA_ADMAIF_CIF_CTRL(2),
	TEGRA_ADMAIF_CIF_CTRL(3),
	TEGRA_ADMAIF_CIF_CTRL(4),
	TEGRA_ADMAIF_CIF_CTRL(5),
	TEGRA_ADMAIF_CIF_CTRL(6),
	TEGRA_ADMAIF_CIF_CTRL(7),
	TEGRA_ADMAIF_CIF_CTRL(8),
	TEGRA_ADMAIF_CIF_CTRL(9),
	TEGRA_ADMAIF_CIF_CTRL(10),
	TEGRA_ADMAIF_CIF_CTRL(11),
	TEGRA_ADMAIF_CIF_CTRL(12),
	TEGRA_ADMAIF_CIF_CTRL(13),
	TEGRA_ADMAIF_CIF_CTRL(14),
	TEGRA_ADMAIF_CIF_CTRL(15),
	TEGRA_ADMAIF_CIF_CTRL(16),
	TEGRA_ADMAIF_CIF_CTRL(17),
	TEGRA_ADMAIF_CIF_CTRL(18),
	TEGRA_ADMAIF_CIF_CTRL(19),
	TEGRA_ADMAIF_CIF_CTRL(20),
	SOC_SINGLE_EXT("APE Reg Dump", SND_SOC_NOPM, 0, 1, 0,
		       tegra210_admaif_get_reg_dump,
		       tegra210_admaif_put_reg_dump),
};

static const struct snd_soc_component_driver tegra210_admaif_cmpnt = {
	.dapm_widgets		= tegra_admaif_widgets,
	.num_dapm_widgets	= TEGRA210_ADMAIF_CHANNEL_COUNT * 4,
	.dapm_routes		= tegra_admaif_routes,
	.num_dapm_routes	= TEGRA210_ADMAIF_CHANNEL_COUNT * 6,
	.controls		= tegra210_admaif_controls,
	.num_controls		= ARRAY_SIZE(tegra210_admaif_controls),
	.pcm_construct		= tegra_pcm_construct,
	.open			= tegra_pcm_open,
	.close			= tegra_pcm_close,
	.hw_params		= tegra_pcm_hw_params,
	.pointer		= tegra_pcm_pointer,
	.use_dai_pcm_id		= 1,
};

static const struct snd_soc_component_driver tegra186_admaif_cmpnt = {
	.dapm_widgets		= tegra_admaif_widgets,
	.num_dapm_widgets	= TEGRA186_ADMAIF_CHANNEL_COUNT * 4,
	.dapm_routes		= tegra_admaif_routes,
	.num_dapm_routes        = TEGRA186_ADMAIF_CHANNEL_COUNT * 6,
	.controls		= tegra186_admaif_controls,
	.num_controls		= ARRAY_SIZE(tegra186_admaif_controls),
	.pcm_construct		= tegra_pcm_construct,
	.open			= tegra_pcm_open,
	.close			= tegra_pcm_close,
	.hw_params		= tegra_pcm_hw_params,
	.pointer		= tegra_pcm_pointer,
	.use_dai_pcm_id		= 1,
};

static const struct tegra_admaif_soc_data soc_data_tegra210 = {
	.num_ch		= TEGRA210_ADMAIF_CHANNEL_COUNT,
	.cmpnt		= &tegra210_admaif_cmpnt,
	.dais		= tegra210_admaif_cmpnt_dais,
	.regmap_conf	= &tegra210_admaif_regmap_config,
	.global_base	= TEGRA210_ADMAIF_GLOBAL_BASE,
	.tx_base	= TEGRA210_ADMAIF_TX_BASE,
	.rx_base	= TEGRA210_ADMAIF_RX_BASE,
};

static const struct tegra_admaif_soc_data soc_data_tegra186 = {
	.num_ch		= TEGRA186_ADMAIF_CHANNEL_COUNT,
	.cmpnt		= &tegra186_admaif_cmpnt,
	.dais		= tegra186_admaif_cmpnt_dais,
	.regmap_conf	= &tegra186_admaif_regmap_config,
	.global_base	= TEGRA186_ADMAIF_GLOBAL_BASE,
	.tx_base	= TEGRA186_ADMAIF_TX_BASE,
	.rx_base	= TEGRA186_ADMAIF_RX_BASE,
};

static const struct of_device_id tegra_admaif_of_match[] = {
	{ .compatible = "nvidia,tegra210-admaif", .data = &soc_data_tegra210 },
	{ .compatible = "nvidia,tegra186-admaif", .data = &soc_data_tegra186 },
	{},
};
MODULE_DEVICE_TABLE(of, tegra_admaif_of_match);

static int tegra_admaif_probe(struct platform_device *pdev)
{
	struct tegra_admaif *admaif;
	void __iomem *regs;
	struct resource *res;
	int err, i;

	admaif = devm_kzalloc(&pdev->dev, sizeof(*admaif), GFP_KERNEL);
	if (!admaif)
		return -ENOMEM;

	admaif->soc_data = of_device_get_match_data(&pdev->dev);

	dev_set_drvdata(&pdev->dev, admaif);

	admaif->capture_dma_data =
		devm_kcalloc(&pdev->dev,
			     admaif->soc_data->num_ch,
			     sizeof(struct snd_dmaengine_dai_dma_data),
			     GFP_KERNEL);
	if (!admaif->capture_dma_data)
		return -ENOMEM;

	admaif->playback_dma_data =
		devm_kcalloc(&pdev->dev,
			     admaif->soc_data->num_ch,
			     sizeof(struct snd_dmaengine_dai_dma_data),
			     GFP_KERNEL);
	if (!admaif->playback_dma_data)
		return -ENOMEM;

	for (i = 0; i < ADMAIF_PATHS; i++) {
		admaif->audio_ch_override[i] =
			devm_kcalloc(&pdev->dev, admaif->soc_data->num_ch,
				     sizeof(unsigned int), GFP_KERNEL);
		if (!admaif->audio_ch_override[i])
			return -ENOMEM;

		admaif->client_ch_override[i] =
			devm_kcalloc(&pdev->dev, admaif->soc_data->num_ch,
				     sizeof(unsigned int), GFP_KERNEL);
		if (!admaif->client_ch_override[i])
			return -ENOMEM;

		admaif->mono_to_stereo[i] =
			devm_kcalloc(&pdev->dev, admaif->soc_data->num_ch,
				     sizeof(unsigned int), GFP_KERNEL);
		if (!admaif->mono_to_stereo[i])
			return -ENOMEM;

		admaif->stereo_to_mono[i] =
			devm_kcalloc(&pdev->dev, admaif->soc_data->num_ch,
				     sizeof(unsigned int), GFP_KERNEL);
		if (!admaif->stereo_to_mono[i])
			return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	admaif->base_addr = regs;

	admaif->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					       admaif->soc_data->regmap_conf);
	if (IS_ERR(admaif->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		return PTR_ERR(admaif->regmap);
	}

	regcache_cache_only(admaif->regmap, true);

	tegra_isomgr_adma_register(&pdev->dev);

	regmap_update_bits(admaif->regmap, admaif->soc_data->global_base +
			   TEGRA_ADMAIF_GLOBAL_ENABLE, 1, 1);

	for (i = 0; i < admaif->soc_data->num_ch; i++) {
		admaif->playback_dma_data[i].addr = res->start +
			CH_TX_REG(TEGRA_ADMAIF_TX_FIFO_WRITE, i);

		admaif->capture_dma_data[i].addr = res->start +
			CH_RX_REG(TEGRA_ADMAIF_RX_FIFO_READ, i);

		admaif->playback_dma_data[i].addr_width = 32;

		if (of_property_read_string_index(pdev->dev.of_node,
				"dma-names", (i * 2) + 1,
				&admaif->playback_dma_data[i].chan_name) < 0) {
			dev_err(&pdev->dev,
				"missing property nvidia,dma-names\n");

			return -ENODEV;
		}

		admaif->capture_dma_data[i].addr_width = 32;

		if (of_property_read_string_index(pdev->dev.of_node,
				"dma-names",
				(i * 2),
				&admaif->capture_dma_data[i].chan_name) < 0) {
			dev_err(&pdev->dev,
				"missing property nvidia,dma-names\n");

			return -ENODEV;
		}
	}

	err = devm_snd_soc_register_component(&pdev->dev,
					      admaif->soc_data->cmpnt,
					      admaif->soc_data->dais,
					      admaif->soc_data->num_ch * 3);
	if (err) {
		dev_err(&pdev->dev,
			"can't register ADMAIF component, err: %d\n", err);
		return err;
	}

	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int tegra_admaif_remove(struct platform_device *pdev)
{
	tegra_isomgr_adma_unregister(&pdev->dev);

	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops tegra_admaif_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_admaif_runtime_suspend,
			   tegra_admaif_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

#if defined(NV_PLATFORM_DRIVER_STRUCT_REMOVE_RETURNS_VOID) /* Linux v6.11 */
static inline void tegra_admaif_remove_wrapper(struct platform_device *pdev)
{
	tegra_admaif_remove(pdev);
}
#else
static inline int tegra_admaif_remove_wrapper(struct platform_device *pdev)
{
	return tegra_admaif_remove(pdev);
}
#endif

static struct platform_driver tegra_admaif_driver = {
	.probe = tegra_admaif_probe,
	.remove = tegra_admaif_remove_wrapper,
	.driver = {
		.name = "tegra210-admaif",
		.of_match_table = tegra_admaif_of_match,
		.pm = &tegra_admaif_pm_ops,
	},
};
module_platform_driver(tegra_admaif_driver);

MODULE_AUTHOR("Songhee Baek <sbaek@nvidia.com>");
MODULE_DESCRIPTION("Tegra210 ASoC ADMAIF driver");
MODULE_LICENSE("GPL v2");
