/* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/fb.h>
/* LGE_CHANGE_S [yoonsoo.kim@lge.com] 20120130 : LCD ESD Protection*/
#include <linux/jiffies.h>
/* LGE_CHANGE_E  [yoonsoo.kim@lge.com] 20120130 : LCD ESD Protection*/
#include <asm/system.h>
#include <mach/hardware.h>
#include "mdp.h"
#include "msm_fb.h"
#include "mdp4.h"
#include "mipi_dsi.h"

#define DSI_VIDEO_BASE	0xF0000
#define DMA_P_BASE      0x90000

static int first_pixel_start_x;
static int first_pixel_start_y;

/* LGE_CHANGE_S [yoonsoo.kim@lge.com] 20120130 : LCD ESD Protection*/
/*For LCD ESD detection 27-01-2012*/
#ifdef CONFIG_LGE_LCD_ESD_DETECTION
static struct platform_device *esd_reset_pdev;
#endif
/* LGE_CHANGE_E  [yoonsoo.kim@lge.com]  20120130  :  LCD ESD Protection*/

ssize_t mdp_dma_video_show_event(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;

	INIT_COMPLETION(vsync_cntrl.vsync_wait);

	if (atomic_read(&vsync_cntrl.suspend) > 0 ||
		atomic_read(&vsync_cntrl.vsync_resume) == 0)
		return 0;

	wait_for_completion(&vsync_cntrl.vsync_wait);
	ret = snprintf(buf, PAGE_SIZE, "VSYNC=%llu",
	ktime_to_ns(vsync_cntrl.vsync_time));
	buf[strlen(buf) + 1] = '\0';
	return ret;
}

int mdp_dsi_video_on(struct platform_device *pdev)
{
	int dsi_width;
	int dsi_height;
	int dsi_bpp;
	int dsi_border_clr;
	int dsi_underflow_clr;
	int dsi_hsync_skew;

	int hsync_period;
	int hsync_ctrl;
	int vsync_period;
	int display_hctl;
	int display_v_start;
	int display_v_end;
	int active_hctl;
	int active_h_start;
	int active_h_end;
	int active_v_start;
	int active_v_end;
	int ctrl_polarity;
	int h_back_porch;
	int h_front_porch;
	int v_back_porch;
	int v_front_porch;
	int hsync_pulse_width;
	int vsync_pulse_width;
	int hsync_polarity;
	int vsync_polarity;
	int data_en_polarity;
	int hsync_start_x;
	int hsync_end_x;
	uint8 *buf;
	uint32 dma2_cfg_reg;

	int bpp;
	struct fb_info *fbi;
	struct fb_var_screeninfo *var;
	struct msm_fb_data_type *mfd;
	int ret;
	uint32_t mask, curr;

/* LGE_CHANGE_S : LCD ESD Protection 
 * 2012-01-30, yoonsoo@lge.com
 * LCD ESD Protection
 */	
#ifdef CONFIG_LGE_LCD_ESD_DETECTION
	if( (!esd_reset_pdev) && (pdev))
	{
		esd_reset_pdev = pdev;
	}
#endif
/* LGE_CHANGE_E : LCD ESD Protection*/

	mfd = (struct msm_fb_data_type *)platform_get_drvdata(pdev);

	if (!mfd)
		return -ENODEV;

	if (mfd->key != MFD_KEY)
		return -EINVAL;

	fbi = mfd->fbi;
	var = &fbi->var;

	vsync_cntrl.dev = mfd->fbi->dev;
	atomic_set(&vsync_cntrl.suspend, 0);
	bpp = fbi->var.bits_per_pixel / 8;
	buf = (uint8 *) fbi->fix.smem_start;

	buf += calc_fb_offset(mfd, fbi, bpp);

	dma2_cfg_reg = DMA_PACK_ALIGN_LSB | DMA_OUT_SEL_DSI_VIDEO;

	if (mfd->fb_imgType == MDP_BGR_565)
		dma2_cfg_reg |= DMA_PACK_PATTERN_BGR;
	else if (mfd->fb_imgType == MDP_RGBA_8888)
		dma2_cfg_reg |= DMA_PACK_PATTERN_BGR;
	else
		dma2_cfg_reg |= DMA_PACK_PATTERN_RGB;

	if (bpp == 2)
		dma2_cfg_reg |= DMA_IBUF_FORMAT_RGB565;
	else if (bpp == 3)
		dma2_cfg_reg |= DMA_IBUF_FORMAT_RGB888;
	else
		dma2_cfg_reg |= DMA_IBUF_FORMAT_xRGB8888_OR_ARGB8888;

	switch (mfd->panel_info.bpp) {
	case 24:
		dma2_cfg_reg |= DMA_DSTC0G_8BITS |
			DMA_DSTC1B_8BITS | DMA_DSTC2R_8BITS;
		break;
	case 18:
		dma2_cfg_reg |= DMA_DSTC0G_6BITS |
			DMA_DSTC1B_6BITS | DMA_DSTC2R_6BITS;
		break;
	case 16:
		dma2_cfg_reg |= DMA_DSTC0G_6BITS |
			DMA_DSTC1B_5BITS | DMA_DSTC2R_5BITS;
		break;
	default:
		printk(KERN_ERR "mdp lcdc can't support format %d bpp!\n",
			mfd->panel_info.bpp);
		return -ENODEV;
	}
	/* MDP cmd block enable */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);


	/* starting address */
	MDP_OUTP(MDP_BASE + DMA_P_BASE + 0x8, (uint32) buf);

	/* active window width and height */
	MDP_OUTP(MDP_BASE + DMA_P_BASE + 0x4, ((fbi->var.yres) << 16) |
		(fbi->var.xres));

	/* buffer ystride */
	MDP_OUTP(MDP_BASE + DMA_P_BASE + 0xc, fbi->fix.line_length);

	/* x/y coordinate = always 0 for lcdc */
	MDP_OUTP(MDP_BASE + DMA_P_BASE + 0x10, 0);

	/* dma config */
	curr = inpdw(MDP_BASE + DMA_P_BASE);
	mask = 0x0FFFFFFF;
	dma2_cfg_reg = (dma2_cfg_reg & mask) | (curr & ~mask);
	MDP_OUTP(MDP_BASE + DMA_P_BASE, dma2_cfg_reg);

	/*
	 * DSI timing setting
	 */
	h_back_porch = var->left_margin;
	h_front_porch = var->right_margin;
	v_back_porch = var->upper_margin;
	v_front_porch = var->lower_margin;
	hsync_pulse_width = var->hsync_len;
	vsync_pulse_width = var->vsync_len;
	dsi_border_clr = mfd->panel_info.lcdc.border_clr;
	dsi_underflow_clr = mfd->panel_info.lcdc.underflow_clr;
	dsi_hsync_skew = mfd->panel_info.lcdc.hsync_skew;
	dsi_width = mfd->panel_info.xres;
	dsi_height = mfd->panel_info.yres;
	dsi_bpp = mfd->panel_info.bpp;
	hsync_period = h_back_porch + dsi_width + h_front_porch + 1;
	hsync_ctrl = (hsync_period << 16) | hsync_pulse_width;
	hsync_start_x = h_back_porch;
	hsync_end_x = dsi_width + h_back_porch - 1;
	display_hctl = (hsync_end_x << 16) | hsync_start_x;

	vsync_period =
		(v_back_porch + dsi_height + v_front_porch + 1) * hsync_period;
	display_v_start = v_back_porch * hsync_period + dsi_hsync_skew;
	display_v_end = (dsi_height + v_back_porch) * hsync_period;

	active_h_start = hsync_start_x + first_pixel_start_x;
	active_h_end = active_h_start + var->xres - 1;
	active_hctl = ACTIVE_START_X_EN |
			(active_h_end << 16) | active_h_start;

	active_v_start = display_v_start +
			first_pixel_start_y * hsync_period;
	active_v_end = active_v_start +	(var->yres) * hsync_period - 1;
	active_v_start |= ACTIVE_START_Y_EN;

	dsi_underflow_clr |= 0x80000000;	/* enable recovery */
	hsync_polarity = 0;
	vsync_polarity = 0;
	data_en_polarity = 0;

	ctrl_polarity =	(data_en_polarity << 2) |
		(vsync_polarity << 1) | (hsync_polarity);

/*LGE_CHANGE_S : seven.kim@lge.com to migrate pre-CS kernel*/
#ifndef CONFIG_FB_MSM_EBI2

/*[LGSI_SP4_BSP_BEGIN] [kiran.jainapure@lge.com] - Multiple power off registers. Sometimes display is not wakeup*/
#ifndef CONFIG_FB_MSM_MIPI_DSI_LG4573B
	if (!(mfd->cont_splash_done)) 
	{
		
		mdp_pipe_ctrl(MDP_CMD_BLOCK,
			MDP_BLOCK_POWER_OFF, FALSE);
		MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE, 0);
		mipi_dsi_controller_cfg(0);
	}
#else /* below code is required for smooth boot logo display*/
/*LGE_CHANGE_S, youngbae.choi@lge.com, 12-12-28, for V7 sometimes booting animation is no display*/
/* LGE_CHANGE_S jungrock.oh@lge.com 2013-01-15 add featuring for booting animation sometimes no display*/
#if !defined(CONFIG_MACH_MSM8X25_V7) && !defined(CONFIG_MACH_MSM7X27A_U0)
/* LGE_CHANGE_E jungrock.oh@lge.com 2013-01-15 add fearuring for booting animation sometimes no display*/
		MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE, 0);
		mipi_dsi_controller_cfg(0);
#endif
/*LGE_CHANGE_E, youngbae.choi@lge.com, 12-12-28, for V7 sometimes booting animation is no display*/
#endif /* below code is required for smooth boot logo display*/
/*[LGSI_SP4_BSP_END] [kiran.jainapure@lge.com] */
	
#endif /*CONFIG_FB_MSM_EBI2*/
/*LGE_CHANGE_E : seven.kim@lge.com to migrate pre-CS kernel*/	

	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x4, hsync_ctrl);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x8, vsync_period);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0xc, vsync_pulse_width);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x10, display_hctl);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x14, display_v_start);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x18, display_v_end);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x1c, active_hctl);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x20, active_v_start);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x24, active_v_end);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x28, dsi_border_clr);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x2c, dsi_underflow_clr);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x30, dsi_hsync_skew);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE + 0x38, ctrl_polarity);

	ret = panel_next_on(pdev);
	if (ret == 0) {
		/* enable DSI block */
		MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE, 1);
		/*Turning on DMA_P block*/
		mdp_pipe_ctrl(MDP_DMA2_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	}

	/* MDP cmd block disable */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);

	return ret;
}

int mdp_dsi_video_off(struct platform_device *pdev)
{
	int ret = 0;
	/*[LGSI_SP4_BSP_BEGIN] [kiran.jainapure@lge.com] */
#ifdef CONFIG_FB_MSM_MIPI_DSI_LG4573B
	static boolean firstbootend=true;
#endif

	mdp_histogram_ctrl_all(FALSE);

/* LGE_CHANGE_S : LCD ESD Protection 
 * 2012-01-30, yoonsoo@lge.com
 * LCD ESD Protection
 */
#ifdef CONFIG_LGE_LCD_ESD_DETECTION	
	if( (!esd_reset_pdev) && (pdev))
	{
		esd_reset_pdev = pdev;
	}
#endif
/* LGE_CHANGE_E : LCD ESD Protection*/
	/*[LGSI_SP4_BSP_END] [kiran.jainapure@lge.com] */
	
	/* MDP cmd block enable */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
	MDP_OUTP(MDP_BASE + DSI_VIDEO_BASE, 0);
	/* MDP cmd block disable */
	mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
	/*Turning off DMA_P block*/
	mdp_pipe_ctrl(MDP_DMA2_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);

	ret = panel_next_off(pdev);

	atomic_set(&vsync_cntrl.suspend, 1);
	atomic_set(&vsync_cntrl.vsync_resume, 0);
	complete_all(&vsync_cntrl.vsync_wait);
	/* delay to make sure the last frame finishes */
	msleep(20);

	/*[LGSI_SP4_BSP_BEGIN] [kiran.jainapure@lge.com] : MDP Clock is not disabled in sleep mode since command block is not turned off*/
#ifdef CONFIG_FB_MSM_MIPI_DSI_LG4573B
	if(firstbootend==true)
	{
		mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_OFF, FALSE);
		firstbootend=false;
	}
#endif
	/*[LGSI_SP4_BSP_END] [kiran.jainapure@lge.com] */
	
	return ret;
}

// QCT_PATCH_S, SR#01031271 bohyun.jung@lge.com
// SR 01031271 - 'mdp_disable_irq_nosync: MDP IRQ term-0x1000 is NOT set, mask=1 irq=1' 
#if 1
void mdp_dma_video_vsync_ctrl(int enable)
{
	unsigned long flag;
	int disabled_clocks;
	if (vsync_cntrl.vsync_irq_enabled == enable)
		return;

	spin_lock_irqsave(&mdp_spin_lock, flag);
	if (!enable)
		INIT_COMPLETION(vsync_cntrl.vsync_wait);

	vsync_cntrl.vsync_irq_enabled = enable;
	disabled_clocks = vsync_cntrl.disabled_clocks;
	spin_unlock_irqrestore(&mdp_spin_lock, flag);

	if (enable && disabled_clocks) 
		mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);

	spin_lock_irqsave(&mdp_spin_lock, flag);
	if (enable && vsync_cntrl.disabled_clocks) {
		outp32(MDP_INTR_CLEAR, LCDC_FRAME_START);
		mdp_intr_mask |= LCDC_FRAME_START;
		outp32(MDP_INTR_ENABLE, mdp_intr_mask);
		mdp_enable_irq(MDP_VSYNC_TERM);
		vsync_cntrl.disabled_clocks = 0;
	}
	spin_unlock_irqrestore(&mdp_spin_lock, flag);

	if (vsync_cntrl.vsync_irq_enabled &&
		atomic_read(&vsync_cntrl.suspend) == 0)
		atomic_set(&vsync_cntrl.vsync_resume, 1);
}
#else
void mdp_dma_video_vsync_ctrl(int enable)
{
	unsigned long flag;
	int disabled_clocks;
	if (vsync_cntrl.vsync_irq_enabled == enable)
		return;

	spin_lock_irqsave(&mdp_spin_lock, flag);
	if (!enable)
		INIT_COMPLETION(vsync_cntrl.vsync_wait);

	vsync_cntrl.vsync_irq_enabled = enable;
	if (!enable)
		vsync_cntrl.disabled_clocks = 0;
	disabled_clocks = vsync_cntrl.disabled_clocks;
	spin_unlock_irqrestore(&mdp_spin_lock, flag);

	if (enable && disabled_clocks) {
		mdp_pipe_ctrl(MDP_CMD_BLOCK, MDP_BLOCK_POWER_ON, FALSE);
		spin_lock_irqsave(&mdp_spin_lock, flag);
		outp32(MDP_INTR_CLEAR, LCDC_FRAME_START);
		mdp_intr_mask |= LCDC_FRAME_START;
		outp32(MDP_INTR_ENABLE, mdp_intr_mask);
		mdp_enable_irq(MDP_VSYNC_TERM);
		spin_unlock_irqrestore(&mdp_spin_lock, flag);
	}
	if (vsync_cntrl.vsync_irq_enabled &&
		atomic_read(&vsync_cntrl.suspend) == 0)
		atomic_set(&vsync_cntrl.vsync_resume, 1);
}
#endif	
// QCT_PATCH_E, SR#01031271 bohyun.jung@lge.com

void mdp_dsi_video_update(struct msm_fb_data_type *mfd)
{
	struct fb_info *fbi = mfd->fbi;
	uint8 *buf;
	int bpp;
	unsigned long flag;
	int irq_block = MDP_DMA2_TERM;

	if (!mfd->panel_power_on)
		return;

	down(&mfd->dma->mutex);

	bpp = fbi->var.bits_per_pixel / 8;
	buf = (uint8 *) fbi->fix.smem_start;

	buf += calc_fb_offset(mfd, fbi, bpp);

	/* no need to power on cmd block since it's dsi mode */
	/* starting address */
	MDP_OUTP(MDP_BASE + DMA_P_BASE + 0x8, (uint32) buf);
	/* enable  irq */
	spin_lock_irqsave(&mdp_spin_lock, flag);
	mdp_enable_irq(irq_block);
	INIT_COMPLETION(mfd->dma->comp);
	mfd->dma->waiting = TRUE;

	outp32(MDP_INTR_CLEAR, LCDC_FRAME_START);
	mdp_intr_mask |= LCDC_FRAME_START;
	outp32(MDP_INTR_ENABLE, mdp_intr_mask);

	spin_unlock_irqrestore(&mdp_spin_lock, flag);
	wait_for_completion_killable(&mfd->dma->comp);
	mdp_disable_irq(irq_block);
	up(&mfd->dma->mutex);
}


/* LGE_CHANGE_S : LCD ESD Protection 
 * 2012-01-30, yoonsoo@lge.com
 * LCD ESD Protection
 */
#ifdef CONFIG_LGE_LCD_ESD_DETECTION
/********************************************************************
Function Name  :-  esd_dma_dsi_panel_off
Arguments 	   :-  None
Return Value   :-  None
Functionality  :-  to power off DMA , MIPI DSI & LCD panel.  
dependencies   :-  Should be called after dsi_video_on or off function.
*********************************************************************/
void esd_dma_dsi_panel_off(void)
{
	if(esd_reset_pdev)
	{
		mdp_dsi_video_off(esd_reset_pdev);
	}
}
/********************************************************************
Function Name  :-  esd_dma_dsi_panel_on
Arguments 	   :-  None
Return Value   :-  None
Functionality  :-  to power on DMA , MIPI DSI & LCD panel.  
dependencies   :-  Should be called after dsi_video_on or off function.
*********************************************************************/
void esd_dma_dsi_panel_on(void)
{
	if(esd_reset_pdev)
	{
		mdp_dsi_video_on(esd_reset_pdev);
	}
}
#endif
/* LGE_CHANGE_E : LCD ESD Protection*/
