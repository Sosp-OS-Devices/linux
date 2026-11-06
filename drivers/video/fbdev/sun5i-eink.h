#ifndef SUN5I_EINK_H
#define SUN5I_EINK_H

void eink_ctlstream_fill_data_neon(u32* cmd, u8* fb, int stride,
				   int sources, int gates, u32 masks[4]);

#endif
