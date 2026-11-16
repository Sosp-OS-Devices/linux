/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Allwinner sun55iw3 (A523/A527/T527/H728) v35x display engine ("DE33"/v350).
 *
 * Unlike the H616 DE33, the v35x DE commits its datapath (blender, output
 * formatter, layers, ...) ONLY through the RCQ (Register Config Queue): a DMA
 * engine that, on each frame-start pulse from the bound TCON, walks an array
 * of 16-byte "heads" and copies marked register blocks from DRAM into the live
 * DE register space. Plain MMIO writes to those datapath registers land in a
 * shadow that never latches on its own.
 *
 * Only the "de_top" control registers (RT_EN, OUT_SIZE, AUTO_CLK, DE2TCON_MUX,
 * the RCQ head pointer and RCQ_CTL itself) are plain MMIO and latch directly.
 *
 * This backend drives that engine for a minimal pipeline (top + blender + fmt),
 * which is enough to scan out a solid background-color frame. Layers are added
 * on top of this foundation.
 */

#ifndef _SUN55I_DE_H_
#define _SUN55I_DE_H_

#include <linux/hrtimer.h>
#include <linux/spinlock.h>
#include <linux/types.h>

struct sun8i_mixer;
struct sun8i_layer;
struct drm_display_mode;
struct drm_plane_state;

/* one RCQ head: if dirty, DMA @len bytes from phys -> DE_base + reg_offset */
struct sun55i_de_rcq_head {
	__le32 low_addr;	/* data block phys [31:0] */
	__le32 dw0;		/* len[23:0] | high_addr[31:24] (phys [39:32]) */
	__le32 dirty;		/* bit0: copy this block this frame */
	__le32 reg_offset;	/* dest offset from DE base (0x5000000) */
};

/* a shadow register block in the coherent DMA pool, mirrored to reg_offset */
struct sun55i_de_block {
	u32				reg_off;	/* offset from DE base */
	u32				size;		/* bytes (2-byte aligned) */
	void				*shadow;	/* CPU view in DMA pool */
	dma_addr_t			phys;		/* DMA phys of @shadow */
	struct sun55i_de_rcq_head	*head;		/* its head in the array */
};

/*
 * UI channels: the mixer map is {0, 6, 8, 7} - logical 0 is the VI channel,
 * logical 1..3 are the UI channels at physical 6..8. The first UI channel is
 * the DRM primary plane, the last one carries the X hardware cursor.
 */
#define SUN55I_DE_UI_CHANNELS		3
#define SUN55I_DE_OVL_UI_PHYS_CHN	6	/* first UI channel (phys) */

/* shared datapath blocks (one set per display pipe) */
enum {
	SUN55I_DE_BLK_BLD_ATTR,	/* blender pipe enables / per-pipe attrs */
	SUN55I_DE_BLK_BLD_CTL,	/* blender route / premul / bg color / out size */
	SUN55I_DE_BLK_BLD_CK,	/* blender color-key / out color ctl */
	SUN55I_DE_BLK_FMT,	/* output formatter */
	SUN55I_DE_BLK_UI_BASE_IDX,
};

/* per-UI-channel block kinds; address them with SUN55I_DE_BLK_CH(c, k) */
#define SUN55I_DE_BLK_PER_UI_CH	16
enum {
	SUN55I_DE_BLKCH_OVL_LAY0,	/* layer-0 regs */
	SUN55I_DE_BLKCH_OVL_LAY1,	/* layers 1-3: staged zeroed (disabled) */
	SUN55I_DE_BLKCH_OVL_LAY2,
	SUN55I_DE_BLKCH_OVL_LAY3,
	SUN55I_DE_BLKCH_OVL_PARA,	/* top_haddr / win_size */
	SUN55I_DE_BLKCH_OVL_DS,		/* coarse down-sample: zeroed (bypass) */
	SUN55I_DE_BLKCH_VSU_CTL,	/* ctl.en + scale_mode */
	SUN55I_DE_BLKCH_VSU_ATTR,	/* out_size + glb_alpha */
	SUN55I_DE_BLKCH_VSU_YPARA,	/* y in_size + h/v step + phase */
	SUN55I_DE_BLKCH_VSU_CPARA,	/* c in_size + h/v step + phase */
	SUN55I_DE_BLKCH_VSU_COEFF0,	/* y_hori_coeff (unity) */
	SUN55I_DE_BLKCH_VSU_COEFF1,	/* y_vert_coeff (unity) */
	SUN55I_DE_BLKCH_VSU_COEFF2,	/* c_hori_coeff (unity) */
	SUN55I_DE_BLKCH_TFBD_CTL,	/* tiled-FB decoder ctrl: zeroed */
	SUN55I_DE_BLKCH_CCSC_CTL,	/* channel CSC ctl: zeroed (bypass) */
	SUN55I_DE_BLKCH_CDC_CTL,	/* channel CDC colour ctl: zeroed */
};

#define SUN55I_DE_BLK_CH(c, k)	(SUN55I_DE_BLK_UI_BASE_IDX + \
				 (c) * SUN55I_DE_BLK_PER_UI_CH + (k))
#define SUN55I_DE_BLK_NUM	(SUN55I_DE_BLK_UI_BASE_IDX + \
				 SUN55I_DE_UI_CHANNELS * SUN55I_DE_BLK_PER_UI_CH)

/**
 * struct sun55i_de - v35x RCQ engine state (one display pipe)
 * @heads:	CPU view of the RCQ head array (head[0..nblocks))
 * @heads_dma:	DMA address of the head array (programmed into RCQ_HEAD_*)
 * @pool:	base of the whole coherent allocation (heads + block data)
 * @pool_dma:	DMA address of @pool
 * @pool_size:	size of @pool
 * @nheads:	number of heads programmed (block count, padded even)
 * @blocks:	per-block shadow descriptors
 * @tcon_id:	hardware TCON index this pipe drives (HDMI = tcon2)
 */
struct sun55i_de {
	struct sun55i_de_rcq_head	*heads;
	dma_addr_t			heads_dma;
	void				*pool;
	dma_addr_t			pool_dma;
	size_t				pool_size;
	unsigned int			nheads;
	struct sun55i_de_block		blocks[SUN55I_DE_BLK_NUM];
	unsigned int			tcon_id;
	struct sun8i_mixer		*mixer;		/* back-ref for the timer */

	/*
	 * Beam-gated arm. The RCQ latches ~immediately when armed and is not
	 * frame-gated by hardware, so the arm (RCQ_CTL=1) must be issued while
	 * the beam is in the vertical blanking region or the new datapath -
	 * including the new framebuffer address - tears in mid-frame. The
	 * vblank IRQ that drives the arm is serviced with large, load-dependent
	 * latency, so arming straight from it drops the load at a random
	 * scanline. Instead we replicate the vendor BSP (disp_mgr_protect_reg_
	 * for_rcq): read the TCON's current scanline, and either arm now if the
	 * beam is safely inside the leading blanking, or schedule an hrtimer to
	 * fire at the computed time the beam next re-enters blanking and arm
	 * there. Either way the arm lands in blanking by construction,
	 * regardless of IRQ-servicing jitter.
	 */
	spinlock_t			arm_lock;	/* arm_pending + arm_sched + timer */
	struct hrtimer			arm_timer;	/* fires in next blanking */
	bool				arm_pending;	/* RCQ staged, not armed */
	bool				arm_sched;	/* hrtimer queued for the arm */
	bool				armed_once;	/* datapath fully loaded once */
	/*
	 * Full-reload arms left to issue after a modeset. A single full-reload
	 * RCQ load does not reliably latch every block (live-verified: the
	 * first apply after boot lands nothing; a repeat apply of the same
	 * list lands everything), so the first arms after a mode_set re-issue
	 * the full-dirty list on consecutive vblanks until this drains.
	 */
	unsigned int			full_arms_left;
	u32				line_total;	/* TCON line counter modulus */
	u32				line_active;	/* line where active video starts */
	u32				arm_target;	/* safe arm line (vendor calc) */
	u32				ns_per_line;	/* counter-tick period (ns) */

	/* TEMP instrumentation — revert with the dev_info diagnostics */
	u32				commit_seq;	/* commits staged */
	u32				arm_seq;	/* RCQ arms issued */
	u32				coalesced;	/* stages overwritten pre-arm */
	u32				arm_deferred;	/* arms pushed to the timer */
};

int sun55i_de_init(struct sun8i_mixer *mixer);
void sun55i_de_mode_set(struct sun8i_mixer *mixer,
			const struct drm_display_mode *mode);
void sun55i_de_layer_update(struct sun8i_mixer *mixer, struct sun8i_layer *layer,
			    struct drm_plane_state *state);
void sun55i_de_commit(struct sun8i_mixer *mixer);
void sun55i_de_vblank_quirk(struct sun8i_mixer *mixer, unsigned int cur_line);

#endif /* _SUN55I_DE_H_ */
