// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Rockchip ISP1 Driver - MIPI additional-data capture device
 *
 * Copyright (C) 2026 Ondrej Jirman <megi@xff.cz>
 *
 * The MIPI receiver can divert CSI-2 long packets whose (VC, DT) matches one
 * of four additional-data selectors into a small FIFO, entirely separate from
 * the image path. This is the only out-of-band data path this ISP generation
 * has - later Rockchip ISPs grew a DMA engine for it, ISP_V10 did not. The
 * IMX258 delivers its shield-pixel (PDAF) samples as such packets, with data
 * type 0x2f, during line blanking.
 *
 * The FIFO holds 2048 dwords (measured; the TRM does not say) and has no
 * DMA: it is drained with one register read per 4 bytes, in hard interrupt
 * context.
 *
 * Draining happens exclusively while the sensor readout is quiet, from the
 * ISP frame interrupt, in one until-empty loop per frame. Hardware testing
 * on the PinePhone Pro (RK3399) established the rule the hard way: a FIFO
 * pop issued while the sensor is reading out stalls the register bus and
 * hard-wedges the machine - the popping CPU freezes on the unretired load,
 * immune even to pseudo-NMIs, and other cores follow as they touch the
 * wedged interconnect. That killed a 1 ms timer drain and an overflow-ISR
 * drain, with every additional-data interrupt masked or not - the
 * interrupts themselves are innocent (the FIFO can overflow and sit full
 * indefinitely with nothing reading it, the overflow interrupt fires one
 * clean edge). Pops any time from the MIPI frame-end interrupt onward are
 * stable; the drain runs at the later ISP frame interrupt for margin, and
 * because the shared handler runs it before the MIPI ISR, which closes the
 * capture on the frame it belongs to.
 *
 * The overflow interrupt is therefore only counted and flagged, never
 * drained from, and the FIFO depth is a hard cap on the data recoverable
 * per frame: userspace must size the sensor's AF window so a frame's
 * output fits into 8 KB, or accept the hardware-truncated capture the
 * buffer flags report. The hardware's fill-level machinery cannot lift
 * the cap either: the threshold register compares against a level that
 * only a mid-readout drain could keep down, and its interrupt is a
 * latching level condition that storms if ever unmasked (see the register
 * definitions).
 *
 * Captured data is delivered through the rkisp1_addata metadata node, one
 * buffer per frame: struct rkisp1_addata_hdr followed by the verbatim FIFO
 * content. Each packet appears as its 4-byte MIPI long-packet header
 * followed by the payload padded to a dword boundary (measured; the TRM
 * does not document the framing either). The kernel walks the headers only
 * to cut the stream on a whole packet and to detect desync - the payload
 * semantics belong to userspace.
 */

#include <linux/ktime.h>
#include <linux/sizes.h>
#include <linux/unaligned.h>

#include <media/v4l2-common.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>

#include "rkisp1-common.h"

#define RKISP1_ADDATA_DEV_NAME	RKISP1_DRIVER_NAME "_addata"

#define RKISP1_ADDATA_REQ_BUFS_MIN	2
#define RKISP1_ADDATA_REQ_BUFS_MAX	8

#define RKISP1_ADDATA_SIZE_MIN		SZ_8K
#define RKISP1_ADDATA_SIZE_DEFAULT	SZ_32K
#define RKISP1_ADDATA_SIZE_MAX		SZ_1M

/*
 * Iteration bound of the per-frame drain: the FIFO depth plus slack. The
 * drain normally ends by emptying the FIFO; hitting the bound means the
 * ADD_DATA_AVAIL bit is stuck or data is somehow still arriving, and
 * bailing out then costs one counted short drain per frame instead of a
 * wedged CPU.
 */
#define RKISP1_ADDATA_DRAIN_BOUND	(2048 + 256)

/*
 * Debug override for the first (VC, DT) selector: -1 uses the sensor's
 * frame descriptor, 0x00..0x3f captures that data type on VC 0 regardless
 * of what the sensor advertises. Read when the CSI receiver starts.
 */
static int add_data_dt = -1;
module_param(add_data_dt, int, 0644);
MODULE_PARM_DESC(add_data_dt,
		 "Additional-data DT override, -1 = from the sensor frame descriptor");

/* ----------------------------------------------------------------------------
 * Selector programming (called by the CSI receiver, hardware powered)
 */

static void rkisp1_addata_program(struct rkisp1_device *rkisp1)
{
	struct rkisp1_addata *addata = &rkisp1->addata;
	unsigned int i;

	for (i = 0; i < 4; i++)
		rkisp1_write(rkisp1, RKISP1_CIF_MIPI_ADD_DATA_SEL_1 + i * 4,
			     i < addata->num_sel ? addata->sel[i] :
			     RKISP1_CIF_MIPI_ADD_DATA_SEL_DISABLED);
}

static void rkisp1_addata_park(struct rkisp1_device *rkisp1)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		rkisp1_write(rkisp1, RKISP1_CIF_MIPI_ADD_DATA_SEL_1 + i * 4,
			     RKISP1_CIF_MIPI_ADD_DATA_SEL_DISABLED);
}

static void rkisp1_addata_flush_fifo(struct rkisp1_device *rkisp1)
{
	u32 val = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CTRL);

	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_CTRL,
		     val | RKISP1_CIF_MIPI_CTRL_FLUSH_FIFO);
	rkisp1_write(rkisp1, RKISP1_CIF_MIPI_CTRL,
		     val & ~RKISP1_CIF_MIPI_CTRL_FLUSH_FIFO);
}

/*
 * Collect the non-image (VC, DT) entries of the sensor's frame descriptor,
 * or the module-parameter override, into addata->sel[].
 */
static void rkisp1_addata_fill_selectors(struct rkisp1_device *rkisp1,
					 struct v4l2_subdev *source,
					 unsigned int pad, u32 image_dt)
{
	struct rkisp1_addata *addata = &rkisp1->addata;
	struct v4l2_mbus_frame_desc fd = {};
	unsigned int i;
	int ret;

	addata->num_sel = 0;

	if (add_data_dt >= 0) {
		addata->sel[addata->num_sel++] =
			RKISP1_CIF_MIPI_DATA_SEL_VC(0) |
			RKISP1_CIF_MIPI_DATA_SEL_DT(add_data_dt & 0x3f);
		return;
	}

	ret = v4l2_subdev_call(source, pad, get_frame_desc, pad, &fd);
	if (ret)
		return;

	if (fd.type != V4L2_MBUS_FRAME_DESC_TYPE_CSI2)
		return;

	for (i = 0; i < fd.num_entries && addata->num_sel < 4; i++) {
		const struct v4l2_mbus_frame_desc_entry *e = &fd.entry[i];

		if (e->bus.csi2.dt == image_dt)
			continue;

		addata->sel[addata->num_sel++] =
			RKISP1_CIF_MIPI_DATA_SEL_VC(e->bus.csi2.vc) |
			RKISP1_CIF_MIPI_DATA_SEL_DT(e->bus.csi2.dt);
	}
}

/* called with addata->lock held, when both streaming and csi.active */
static void rkisp1_addata_capture_on(struct rkisp1_device *rkisp1)
{
	rkisp1_addata_flush_fifo(rkisp1);
	rkisp1_addata_program(rkisp1);
}

void rkisp1_addata_csi_start(struct rkisp1_device *rkisp1,
			     struct v4l2_subdev *source, unsigned int pad,
			     u32 image_dt)
{
	struct rkisp1_addata *addata = &rkisp1->addata;

	rkisp1_addata_fill_selectors(rkisp1, source, pad, image_dt);

	spin_lock_irq(&addata->lock);

	rkisp1->csi.active = true;
	if (addata->streaming) {
		rkisp1_addata_capture_on(rkisp1);
	} else {
		rkisp1_addata_flush_fifo(rkisp1);
		rkisp1_addata_park(rkisp1);
	}

	spin_unlock_irq(&addata->lock);
}

/* fail a capture in flight; called with addata->lock held */
static void rkisp1_addata_abort_frame(struct rkisp1_device *rkisp1)
{
	struct rkisp1_addata *addata = &rkisp1->addata;

	if (!addata->curr)
		return;

	vb2_buffer_done(&addata->curr->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	addata->curr = NULL;
	addata->curr_flags = 0;
	addata->curr_dropped = 0;
	addata->frame_nobuf = false;
}

void rkisp1_addata_csi_stop(struct rkisp1_device *rkisp1)
{
	struct rkisp1_addata *addata = &rkisp1->addata;

	spin_lock_irq(&addata->lock);
	rkisp1->csi.active = false;
	rkisp1_addata_park(rkisp1);
	rkisp1_addata_abort_frame(rkisp1);
	spin_unlock_irq(&addata->lock);
}

/* ----------------------------------------------------------------------------
 * Interrupt-side capture
 */

/* called with addata->lock held */
static void rkisp1_addata_open_frame(struct rkisp1_device *rkisp1)
{
	struct rkisp1_addata *addata = &rkisp1->addata;
	struct rkisp1_buffer *buf;

	buf = list_first_entry_or_null(&addata->buf_queue,
				       struct rkisp1_buffer, queue);
	if (!buf) {
		addata->frame_nobuf = true;
		rkisp1->debug.addata_nobuf++;
		return;
	}
	list_del(&buf->queue);

	addata->curr = buf;
	addata->curr_hdr = vb2_plane_vaddr(&buf->vb.vb2_buf, 0);
	addata->curr_data = (u8 *)(addata->curr_hdr + 1);
	addata->curr_len = 0;
	addata->curr_sequence = rkisp1->isp.frame_sequence;
	/*
	 * curr_flags and curr_dropped are per-frame, not per-buffer: an
	 * overflow may be recorded before any word was drained into a
	 * buffer. They are reset when the frame closes.
	 */
}

/* called with addata->lock held */
static void rkisp1_addata_drain(struct rkisp1_device *rkisp1)
{
	struct rkisp1_addata *addata = &rkisp1->addata;
	u32 max_len = addata->buffersize - sizeof(struct rkisp1_addata_hdr);
	unsigned int bound = RKISP1_ADDATA_DRAIN_BOUND;

	while (rkisp1_read(rkisp1, RKISP1_CIF_MIPI_STATUS) &
	       RKISP1_CIF_MIPI_STATUS_ADD_DATA_AVAIL) {
		u32 word;

		if (!bound--) {
			rkisp1->debug.addata_budget++;
			return;
		}

		word = rkisp1_read(rkisp1, RKISP1_CIF_MIPI_ADD_DATA_FIFO);

		if (!addata->streaming)
			continue;

		if (!addata->curr && !addata->frame_nobuf)
			rkisp1_addata_open_frame(rkisp1);

		if (!addata->curr) {
			rkisp1->debug.addata_dropped += 4;
			continue;
		}

		if (addata->curr_len + 4 > max_len) {
			addata->curr_flags |= RKISP1_ADDATA_FLAG_TRUNCATED;
			addata->curr_dropped += 4;
			continue;
		}

		put_unaligned_le32(word, addata->curr_data + addata->curr_len);
		addata->curr_len += 4;
	}
}

/*
 * Walk the packet framing: each packet is a 4-byte MIPI long-packet header
 * whose bits [23:8] are the payload byte count, followed by that payload
 * padded to a dword. Returns the number of whole packets and trims @len to
 * the last whole packet.
 */
static unsigned int rkisp1_addata_walk(const u8 *data, u32 *len)
{
	unsigned int packets = 0;
	u32 off = 0;

	while (off + 4 <= *len) {
		u32 hdr = get_unaligned_le32(data + off);
		u32 adv = 4 + ALIGN((hdr >> 8) & 0xffff, 4);

		if (off + adv > *len)
			break;

		off += adv;
		packets++;
	}

	*len = off;
	return packets;
}

/* called with addata->lock held, on the ISP frame interrupt */
static void rkisp1_addata_close_frame(struct rkisp1_device *rkisp1)
{
	struct rkisp1_addata *addata = &rkisp1->addata;
	struct rkisp1_addata_hdr *hdr;
	u32 data_len, leftover;

	addata->frame_nobuf = false;

	/* a frame with no data still produces an (empty) buffer */
	if (!addata->curr) {
		rkisp1_addata_open_frame(rkisp1);
		if (!addata->curr) {
			addata->curr_flags = 0;
			addata->curr_dropped = 0;
			return;
		}
	}

	data_len = addata->curr_len;
	hdr = addata->curr_hdr;

	hdr->num_packets = rkisp1_addata_walk(addata->curr_data, &data_len);
	leftover = addata->curr_len - data_len;

	/*
	 * A trailing partial packet is expected when data was lost mid-frame;
	 * without a loss it means the framing model is wrong or the FIFO
	 * desynchronised, and the stream cannot be trusted.
	 */
	if (leftover &&
	    !(addata->curr_flags & (RKISP1_ADDATA_FLAG_TRUNCATED |
				    RKISP1_ADDATA_FLAG_HW_OVERFLOW)))
		addata->curr_flags |= RKISP1_ADDATA_FLAG_SYNC_LOST;

	if (addata->curr_flags & (RKISP1_ADDATA_FLAG_HW_OVERFLOW |
				  RKISP1_ADDATA_FLAG_SYNC_LOST))
		rkisp1_addata_flush_fifo(rkisp1);

	hdr->version = 1;
	hdr->flags = addata->curr_flags;
	hdr->data_bytes = data_len;
	hdr->dropped_bytes = addata->curr_dropped + leftover;
	memset(hdr->reserved, 0, sizeof(hdr->reserved));

	rkisp1->debug.addata_frames++;
	rkisp1->debug.addata_bytes += data_len;
	rkisp1->debug.addata_dropped += addata->curr_dropped + leftover;
	rkisp1->debug.addata_last_id =
		rkisp1_read(rkisp1, RKISP1_CIF_MIPI_CUR_DATA_ID);

	vb2_set_plane_payload(&addata->curr->vb.vb2_buf, 0,
			      sizeof(*hdr) + data_len);
	addata->curr->vb.sequence = addata->curr_sequence;
	addata->curr->vb.vb2_buf.timestamp = ktime_get_ns();
	vb2_buffer_done(&addata->curr->vb.vb2_buf, VB2_BUF_STATE_DONE);
	addata->curr = NULL;
	addata->curr_flags = 0;
	addata->curr_dropped = 0;
}

/*
 * Called from the MIPI ISR on the additional-data overflow interrupt.
 * Only records the loss - draining from here would pop the FIFO while the
 * receiver is writing it, which wedges the machine (see the top comment).
 */
void rkisp1_addata_isr(struct rkisp1_device *rkisp1, u32 status)
{
	struct rkisp1_addata *addata = &rkisp1->addata;

	spin_lock(&addata->lock);

	if (status & RKISP1_CIF_MIPI_ADD_DATA_OVFLW) {
		rkisp1->debug.addata_overflow++;
		addata->curr_flags |= RKISP1_ADDATA_FLAG_HW_OVERFLOW;
	}

	spin_unlock(&addata->lock);
}

/*
 * Called from the ISP ISR's frame branch - in vertical blanking, after the
 * receiver went quiet, which is the only safe time to pop the FIFO. It
 * runs before rkisp1_csi_isr() in the shared interrupt handler, so the
 * capture closes on the frame it belongs to.
 */
void rkisp1_addata_frame_isr(struct rkisp1_device *rkisp1)
{
	struct rkisp1_addata *addata = &rkisp1->addata;

	spin_lock(&addata->lock);

	if (rkisp1->csi.active) {
		rkisp1_addata_drain(rkisp1);
		if (addata->streaming)
			rkisp1_addata_close_frame(rkisp1);
	}

	spin_unlock(&addata->lock);
}

/* ----------------------------------------------------------------------------
 * V4L2 ioctls
 */

static int rkisp1_addata_enum_fmt_meta_cap(struct file *file, void *priv,
					   struct v4l2_fmtdesc *f)
{
	struct video_device *video = video_devdata(file);

	if (f->index > 0 || f->type != video->queue->type)
		return -EINVAL;

	f->pixelformat = V4L2_META_FMT_RK_ISP1_ADDATA;
	return 0;
}

static int rkisp1_addata_g_fmt_meta_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp1_addata *addata = video_get_drvdata(video);
	struct v4l2_meta_format *meta = &f->fmt.meta;

	if (f->type != video->queue->type)
		return -EINVAL;

	memset(meta, 0, sizeof(*meta));
	meta->dataformat = V4L2_META_FMT_RK_ISP1_ADDATA;
	meta->buffersize = addata->buffersize;

	return 0;
}

static int rkisp1_addata_try_fmt_meta_cap(struct file *file, void *priv,
					  struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct v4l2_meta_format *meta = &f->fmt.meta;
	u32 buffersize = meta->buffersize;

	if (f->type != video->queue->type)
		return -EINVAL;

	if (!buffersize)
		buffersize = RKISP1_ADDATA_SIZE_DEFAULT;

	memset(meta, 0, sizeof(*meta));
	meta->dataformat = V4L2_META_FMT_RK_ISP1_ADDATA;
	meta->buffersize = clamp_t(u32, ALIGN(buffersize, 4),
				   RKISP1_ADDATA_SIZE_MIN,
				   RKISP1_ADDATA_SIZE_MAX);

	return 0;
}

static int rkisp1_addata_s_fmt_meta_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct video_device *video = video_devdata(file);
	struct rkisp1_addata *addata = video_get_drvdata(video);
	int ret;

	if (vb2_is_busy(video->queue))
		return -EBUSY;

	ret = rkisp1_addata_try_fmt_meta_cap(file, priv, f);
	if (ret)
		return ret;

	addata->buffersize = f->fmt.meta.buffersize;

	return 0;
}

static int rkisp1_addata_querycap(struct file *file,
				  void *priv, struct v4l2_capability *cap)
{
	struct video_device *vdev = video_devdata(file);

	strscpy(cap->driver, RKISP1_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, vdev->name, sizeof(cap->card));
	strscpy(cap->bus_info, RKISP1_BUS_INFO, sizeof(cap->bus_info));

	return 0;
}

static const struct v4l2_ioctl_ops rkisp1_addata_ioctl = {
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_enum_fmt_meta_cap = rkisp1_addata_enum_fmt_meta_cap,
	.vidioc_g_fmt_meta_cap = rkisp1_addata_g_fmt_meta_cap,
	.vidioc_s_fmt_meta_cap = rkisp1_addata_s_fmt_meta_cap,
	.vidioc_try_fmt_meta_cap = rkisp1_addata_try_fmt_meta_cap,
	.vidioc_querycap = rkisp1_addata_querycap,
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations rkisp1_addata_fops = {
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
	.poll = vb2_fop_poll,
	.open = v4l2_fh_open,
	.release = vb2_fop_release
};

/* ----------------------------------------------------------------------------
 * vb2 queue
 */

static int rkisp1_addata_vb2_queue_setup(struct vb2_queue *vq,
					 unsigned int *num_buffers,
					 unsigned int *num_planes,
					 unsigned int sizes[],
					 struct device *alloc_devs[])
{
	struct rkisp1_addata *addata = vq->drv_priv;

	*num_planes = 1;

	*num_buffers = clamp_t(u32, *num_buffers, RKISP1_ADDATA_REQ_BUFS_MIN,
			       RKISP1_ADDATA_REQ_BUFS_MAX);

	sizes[0] = addata->buffersize;

	return 0;
}

static int rkisp1_addata_vb2_buf_prepare(struct vb2_buffer *vb)
{
	struct rkisp1_addata *addata = vb->vb2_queue->drv_priv;

	if (vb2_plane_size(vb, 0) < addata->buffersize)
		return -EINVAL;

	return 0;
}

static void rkisp1_addata_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct rkisp1_buffer *buf =
		container_of(vbuf, struct rkisp1_buffer, vb);
	struct rkisp1_addata *addata = vb->vb2_queue->drv_priv;

	spin_lock_irq(&addata->lock);
	list_add_tail(&buf->queue, &addata->buf_queue);
	spin_unlock_irq(&addata->lock);
}

static int rkisp1_addata_vb2_start_streaming(struct vb2_queue *vq,
					     unsigned int count)
{
	struct rkisp1_addata *addata = vq->drv_priv;
	struct rkisp1_device *rkisp1 = addata->rkisp1;

	spin_lock_irq(&addata->lock);
	addata->streaming = true;
	addata->curr_flags = 0;
	addata->curr_dropped = 0;
	addata->frame_nobuf = false;
	/*
	 * If the CSI receiver is already running the capture starts here;
	 * otherwise rkisp1_addata_csi_start() starts it when the receiver
	 * starts, which is also the only time the sensor's frame descriptor
	 * is queried - starting this node mid-stream uses the descriptor
	 * from the receiver start.
	 */
	if (rkisp1->csi.active)
		rkisp1_addata_capture_on(rkisp1);
	spin_unlock_irq(&addata->lock);

	return 0;
}

static void rkisp1_addata_vb2_stop_streaming(struct vb2_queue *vq)
{
	struct rkisp1_addata *addata = vq->drv_priv;
	struct rkisp1_device *rkisp1 = addata->rkisp1;
	struct rkisp1_buffer *buf;

	spin_lock_irq(&addata->lock);

	addata->streaming = false;
	if (rkisp1->csi.active) {
		rkisp1_addata_park(rkisp1);
		rkisp1_addata_flush_fifo(rkisp1);
	}
	rkisp1_addata_abort_frame(rkisp1);

	while ((buf = list_first_entry_or_null(&addata->buf_queue,
					       struct rkisp1_buffer, queue))) {
		list_del(&buf->queue);
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
	}

	spin_unlock_irq(&addata->lock);
}

static const struct vb2_ops rkisp1_addata_vb2_ops = {
	.queue_setup = rkisp1_addata_vb2_queue_setup,
	.buf_prepare = rkisp1_addata_vb2_buf_prepare,
	.buf_queue = rkisp1_addata_vb2_buf_queue,
	.start_streaming = rkisp1_addata_vb2_start_streaming,
	.stop_streaming = rkisp1_addata_vb2_stop_streaming,
};

/* ----------------------------------------------------------------------------
 * Registration
 */

int rkisp1_addata_register(struct rkisp1_device *rkisp1)
{
	struct rkisp1_addata *addata = &rkisp1->addata;
	struct rkisp1_vdev_node *node = &addata->vnode;
	struct video_device *vdev = &node->vdev;
	struct vb2_queue *q = &node->buf_queue;
	int ret;

	addata->rkisp1 = rkisp1;
	addata->buffersize = RKISP1_ADDATA_SIZE_DEFAULT;
	mutex_init(&node->vlock);
	INIT_LIST_HEAD(&addata->buf_queue);
	spin_lock_init(&addata->lock);

	strscpy(vdev->name, RKISP1_ADDATA_DEV_NAME, sizeof(vdev->name));

	vdev->ioctl_ops = &rkisp1_addata_ioctl;
	vdev->fops = &rkisp1_addata_fops;
	vdev->release = video_device_release_empty;
	vdev->lock = &node->vlock;
	vdev->v4l2_dev = &rkisp1->v4l2_dev;
	vdev->queue = q;
	vdev->device_caps = V4L2_CAP_META_CAPTURE | V4L2_CAP_STREAMING;
	vdev->vfl_dir = VFL_DIR_RX;

	q->type = V4L2_BUF_TYPE_META_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	q->drv_priv = addata;
	q->ops = &rkisp1_addata_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->buf_struct_size = sizeof(struct rkisp1_buffer);
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->lock = &node->vlock;

	ret = vb2_queue_init(q);
	if (ret)
		goto err_mutex;

	video_set_drvdata(vdev, addata);

	node->pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, &node->pad);
	if (ret)
		goto err_mutex;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(&vdev->dev,
			"failed to register %s, ret=%d\n", vdev->name, ret);
		goto err_entity;
	}

	return 0;

err_entity:
	media_entity_cleanup(&vdev->entity);
err_mutex:
	mutex_destroy(&node->vlock);
	addata->rkisp1 = NULL;
	return ret;
}

void rkisp1_addata_unregister(struct rkisp1_device *rkisp1)
{
	struct rkisp1_addata *addata = &rkisp1->addata;
	struct rkisp1_vdev_node *node = &addata->vnode;
	struct video_device *vdev = &node->vdev;

	if (!addata->rkisp1)
		return;

	vb2_video_unregister_device(vdev);
	media_entity_cleanup(&vdev->entity);
	mutex_destroy(&node->vlock);
}
