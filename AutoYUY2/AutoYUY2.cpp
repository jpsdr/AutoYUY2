/*
 *  AutoYUY2()
 *
 *  Adaptive YV12 upsampling. Progressive picture areas are upsampled
 *  progressively and interlaced areas are upsampled interlaced.
 *  Copyright (C) 2005 Donald A. Graft
 *	
 *  AutoYUY2 is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  AutoYUY2 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */

#include <windows.h>
#include <winreg.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#include "avisynth.h"
#include ".\asmlib\asmlib.h"

extern "C" int IInstrSet;
// Cache size for asmlib function, a little more the size of a 720p YV12 frame
#define MAX_CACHE_SIZE 1400000

static size_t CPU_Cache_Size;

extern "C" void JPSDR_AutoYUY2_1(const uint8_t *scr_y,const uint8_t *src_u,const uint8_t *src_v,
		uint8_t *dst,int w);
extern "C" void JPSDR_AutoYUY2_SSE2_1(const uint8_t *scr_y,const uint8_t *src_u,const uint8_t *src_v,
		uint8_t *dst,int w);
extern "C" void JPSDR_AutoYUY2_SSE2_2(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int w);
extern "C" void JPSDR_AutoYUY2_SSE2_3(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int w);
extern "C" void JPSDR_AutoYUY2_SSE2_4(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int w);

extern "C" void JPSDR_AutoYUY2_SSE2_1b(const uint8_t *scr_y,const uint8_t *src_u,const uint8_t *src_v,
		uint8_t *dst,int w);
extern "C" void JPSDR_AutoYUY2_SSE2_2b(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int w);
extern "C" void JPSDR_AutoYUY2_SSE2_3b(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int w);
extern "C" void JPSDR_AutoYUY2_SSE2_4b(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int w);

extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(const void *scr_1,const void *src_2,void *dst,int w);
extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(const void *scr_1,const void *src_2,void *dst,int w);
extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(const void *scr_1,const void *src_2,void *dst,int w);
extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(const void *scr_1,const void *src_2,void *dst,int w);
extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(const void *scr_1,const void *src_2,void *dst,int w);
extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(const void *scr_1,const void *src_2,void *dst,int w);


#define VERSION "AutoYUY2 1.3.0 JPSDR"
// Inspired from Neuron2 filter

#define Interlaced_Tab_Size 3

#define myfree(ptr) if (ptr!=NULL) { free(ptr); ptr=NULL;}


class AutoYUY2 : public GenericVideoFilter
{
public:
	AutoYUY2(PClip _child, int _threshold, int _mode, int _output, IScriptEnvironment* env);
	~AutoYUY2();
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
private:
	int threshold;
	int mode;
	int output;
	uint16_t lookup[768];
	bool *interlaced_tab[Interlaced_Tab_Size];
	bool SSE2_Enable;

	inline void Move_Full(const void *src, void *dst, const int32_t w,const int32_t h,
		int src_pitch,int dst_pitch);

	void Convert_Progressive(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Progressive_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Interlaced(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Interlaced_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Automatic(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Test(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);

	void Convert_Progressive_YV16(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
		const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV);
	void Convert_Progressive_YV16_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
		const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV);
	void Convert_Interlaced_YV16(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
		const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV);
	void Convert_Interlaced_YV16_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
		const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV);
	void Convert_Automatic_YV16(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
		const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV);
	void Convert_Test_YV16(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
		const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV);
};


AutoYUY2::AutoYUY2(PClip _child, int _threshold, int _mode,  int _output, IScriptEnvironment* env) :
										GenericVideoFilter(_child), threshold(_threshold),
										mode(_mode), output(_output)
{
	bool ok;
	uint16_t i;

	for (i=0; i<Interlaced_Tab_Size; i++)
	{
		interlaced_tab[i]=NULL;
	}

	switch (output)
	{
		case 0 : vi.pixel_type = VideoInfo::CS_YUY2; break;
		case 1 : vi.pixel_type = VideoInfo::CS_YV16; break;
		default : break;
	}

	for (i=0; i<Interlaced_Tab_Size; i++)
	{
		interlaced_tab[i]=(bool*)malloc(vi.width*sizeof(bool));
	}

	ok=true;
	for (i=0; i<Interlaced_Tab_Size; i++)
	{
		ok=ok && (interlaced_tab[i]!=NULL);
	}

	if (!ok)
		env->ThrowError("AutoYUY2 : Memory allocation error.");

	for (i=0; i<256; i++)
	{
		lookup[i]=(uint16_t)(3*i);
		lookup[i+256]=(uint16_t)(5*i);
		lookup[i+512]=(uint16_t)(7*i);
	}
		
	const size_t img_size=vi.BMPSize();

	if (img_size<=MAX_CACHE_SIZE)
	{
		if (CPU_Cache_Size>=img_size)
		{
			SetMemcpyCacheLimit(img_size);
			SetMemsetCacheLimit(img_size);
		}
		else
		{
			SetMemcpyCacheLimit(16);
			SetMemsetCacheLimit(16);
		}
	}
	else
	{
		SetMemcpyCacheLimit(16);
		SetMemsetCacheLimit(16);
	}

	SSE2_Enable=((env->GetCPUFlags()&CPUF_SSE2)!=0);
}

AutoYUY2::~AutoYUY2() 
{
	int16_t i;
	
	for (i=Interlaced_Tab_Size-1; i>=0; i--)
	{
		myfree(interlaced_tab[i]);
	}
}



inline void AutoYUY2::Move_Full(const void *src, void *dst, const int32_t w,const int32_t h,
		int src_pitch,int dst_pitch)
{
	if ((src_pitch==dst_pitch) && (src_pitch=w))
		A_memcpy(dst,src,(size_t)h*(size_t)w);
	else
	{
		const uint8_t *src_=(uint8_t *)src;
		uint8_t *dst_=(uint8_t *)dst;

		for(int i=0; i<h; i++)
		{
			A_memcpy(dst_,src_,w);
			src_+=src_pitch;
			dst_+=dst_pitch;
		}
	}
}


void AutoYUY2::Convert_Interlaced_YV16_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
	const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
	const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV)
{
	const uint8_t *src_U,*src_Up,*src_Upp,*src_Un,*src_Unn,*src_Unnn;
	const uint8_t *src_V,*src_Vp,*src_Vpp,*src_Vn,*src_Vnn,*src_Vnnn;
	ptrdiff_t pitch_U_2,pitch_V_2;
	bool _align_U=false,_align_V=false;
	const int32_t w_U=dst_w>>1,w_V=dst_w>>1;
	const int32_t h_4=dst_h-4,w_U4=w_U>>2,w_V4=w_V>>2;

	pitch_U_2=2*src_pitchUV;
	pitch_V_2=2*src_pitchUV;
	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchUV;
	src_Upp=src_U-2*src_pitchUV;
	src_Un=src_U+src_pitchUV;
	src_Unn=src_U+2*src_pitchUV;
	src_Unnn=src_U+3*src_pitchUV;
	src_Vp=src_V-src_pitchUV;
	src_Vpp=src_V-2*src_pitchUV;
	src_Vn=src_V+src_pitchUV;
	src_Vnn=src_V+2*src_pitchUV;
	src_Vnnn=src_V+3*src_pitchUV;

	if ((((size_t)src_U & 0x0F)==0) && (((size_t)dstUp & 0x0F)==0) && ((abs(src_pitchUV) & 0x0F)==0) 
		&& ((abs(dst_pitch_UV) & 0x0F)==0)) _align_U=true;
	if ((((size_t)src_V & 0x0F)==0) && (((size_t)dstVp & 0x0F)==0) && ((abs(src_pitchUV) & 0x0F)==0) 
		&& ((abs(dst_pitch_UV) & 0x0F)==0)) _align_V=true;

	Move_Full(srcYp,dstYp,dst_w,dst_h,src_pitch_Y,dst_pitch_Y);

	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dstUp,src_U,w_U);
		A_memcpy(dstVp,src_V,w_V);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		A_memcpy(dstUp,src_Un,w_U);
		A_memcpy(dstVp,src_Vn,w_V);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Unn,src_U,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Unn,src_U,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vnn,src_V,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vnn,src_V,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Un,src_Unnn,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Un,src_Unnn,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Vn,src_Vnnn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Vn,src_Vnnn,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=4; i<h_4; i+=4)
	{
		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_U,src_Upp,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_U,src_Upp,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_V,src_Vpp,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_V,src_Vpp,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Up,src_Un,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Up,src_Un,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vp,src_Vn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vp,src_Vn,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Unn,src_U,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Unn,src_U,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vnn,src_V,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vnn,src_V,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Un,src_Unnn,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Un,src_Unnn,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Vn,src_Vnnn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Vn,src_Vnnn,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_U,src_Upp,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_U,src_Upp,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_V,src_Vpp,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_V,src_Vpp,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Up,src_Un,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Up,src_Un,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vp,src_Vn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vp,src_Vn,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		A_memcpy(dstUp,src_U,w_U);
		A_memcpy(dstVp,src_V,w_V);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		A_memcpy(dstUp,src_Un,w_U);
		A_memcpy(dstVp,src_Vn,w_V);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}
}


void AutoYUY2::Convert_Interlaced_YV16(const uint8_t *srcYp, const uint8_t *srcUp,
	const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
	const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV)
{
	const uint8_t *src_U,*src_Up,*src_Upp,*src_Un,*src_Unn,*src_Unnn;
	const uint8_t *src_V,*src_Vp,*src_Vpp,*src_Vn,*src_Vnn,*src_Vnnn;
	uint8_t *dst_V,*dst_U;
	ptrdiff_t pitch_U_2,pitch_V_2;
	const int32_t h_4=dst_h-4;
	const int w_U=dst_w>>1,w_V=dst_w>>1;

	pitch_U_2=2*src_pitchUV;
	pitch_V_2=2*src_pitchUV;
	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchUV;
	src_Upp=src_U-2*src_pitchUV;
	src_Un=src_U+src_pitchUV;
	src_Unn=src_U+2*src_pitchUV;
	src_Unnn=src_U+3*src_pitchUV;
	src_Vp=src_V-src_pitchUV;
	src_Vpp=src_V-2*src_pitchUV;
	src_Vn=src_V+src_pitchUV;
	src_Vnn=src_V+2*src_pitchUV;
	src_Vnnn=src_V+3*src_pitchUV;
	dst_U=dstUp;
	dst_V=dstVp;

	Move_Full(srcYp,dstYp,dst_w,dst_h,src_pitch_Y,dst_pitch_Y);

	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dst_U,src_U,w_U);
		A_memcpy(dst_V,src_V,w_V);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_U);
		A_memcpy(dst_V,src_Vn,w_V);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_Unn[j]]+lookup[src_U[j]+256]+4)>>3);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_Vnn[j]]+lookup[src_V[j]+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=4; i<h_4; i+=4)
	{
		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_U[j]+512]+(uint16_t)src_Upp[j]+4)>>3);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_V[j]+512]+(uint16_t)src_Vpp[j]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_Up[j]]+lookup[src_Un[j]+256]+4)>>3);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_Vp[j]]+lookup[src_Vn[j]+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_Unn[j]]+lookup[src_U[j]+256]+4)>>3);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_Vnn[j]]+lookup[src_V[j]+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_U[j]+512]+(uint16_t)src_Upp[j]+4)>>3);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_V[j]+512]+(uint16_t)src_Vpp[j]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_Up[j]]+lookup[src_Un[j]+256]+4)>>3);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_Vp[j]]+lookup[src_Vn[j]+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_U,w_U);
		A_memcpy(dst_V,src_V,w_V);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_U);
		A_memcpy(dst_V,src_Vn,w_V);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}
}

void AutoYUY2::Convert_Progressive_YV16_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
	const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
	const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV)
{
	const uint8_t *src_U,*src_Up,*src_Un;
	const uint8_t *src_V,*src_Vp,*src_Vn;
	bool _align_U=false,_align_V=false;
	const int32_t w_U=dst_w>>1,w_V=dst_w>>1;
	const int32_t h_2=dst_h-2,w_U4=w_U>>2,w_V4=w_V>>2;

	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchUV;
	src_Un=src_U+src_pitchUV;
	src_Vp=src_V-src_pitchUV;
	src_Vn=src_V+src_pitchUV;

	if ((((size_t)src_U & 0x0F)==0) && (((size_t)dstUp & 0x0F)==0) && ((abs(src_pitchUV) & 0x0F)==0) 
		&& ((abs(dst_pitch_UV) & 0x0F)==0)) _align_U=true;
	if ((((size_t)src_V & 0x0F)==0) && (((size_t)dstVp & 0x0F)==0) && ((abs(src_pitchUV) & 0x0F)==0) 
		&& ((abs(dst_pitch_UV) & 0x0F)==0)) _align_V=true;

	Move_Full(srcYp,dstYp,dst_w,dst_h,src_pitch_Y,dst_pitch_Y);

	for(int32_t i=0; i<2; i+=2)
	{
		A_memcpy(dstUp,src_U,w_U);
		A_memcpy(dstVp,src_V,w_V);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Un,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Un,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vn,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;

		src_V+=src_pitchUV;
		src_Vp+=src_pitchUV;
		src_Vn+=src_pitchUV;
	}

	for(int32_t i=2; i<h_2; i+=2)
	{
		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Up,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Up,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vp,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vp,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Un,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Un,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vn,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;

		src_V+=src_pitchUV;
		src_Vp+=src_pitchUV;
		src_Vn+=src_pitchUV;
	}

	for(int32_t i=h_2; i<dst_h; i+=2)
	{
		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Up,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Up,dstUp,w_U4);
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vp,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vp,dstVp,w_V4);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;


		A_memcpy(dstUp,src_U,w_U);
		A_memcpy(dstVp,src_V,w_V);
		dstUp+=dst_pitch_UV;
		dstVp+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;

		src_V+=src_pitchUV;
		src_Vp+=src_pitchUV;
		src_Vn+=src_pitchUV;
	}
}


void AutoYUY2::Convert_Progressive_YV16(const uint8_t *srcYp, const uint8_t *srcUp,
	const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
	const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV)
{
	const uint8_t *src_U,*src_Up,*src_Un;
	const uint8_t *src_V,*src_Vp,*src_Vn;
	uint8_t *dst_U,*dst_V;
	const int32_t h_2=dst_h-2;
	const int w_U=dst_w>>1,w_V=dst_w>>1;

	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchUV;
	src_Un=src_U+src_pitchUV;
	src_Vp=src_V-src_pitchUV;
	src_Vn=src_V+src_pitchUV;
	dst_U=dstUp;
	dst_V=dstVp;

	Move_Full(srcYp,dstYp,dst_w,dst_h,src_pitch_Y,dst_pitch_Y);

	for(int32_t i=0; i<2; i+=2)
	{
		A_memcpy(dst_U,src_U,w_U);
		A_memcpy(dst_V,src_V,w_V);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2)>>2);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2)>>2);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;

		src_V+=src_pitchUV;
		src_Vp+=src_pitchUV;
		src_Vn+=src_pitchUV;
	}

	for(int32_t i=2; i<h_2; i+=2)
	{
		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2)>>2);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2)>>2);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2)>>2);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2)>>2);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;

		src_V+=src_pitchUV;
		src_Vp+=src_pitchUV;
		src_Vn+=src_pitchUV;
	}

	for(int32_t i=h_2; i<dst_h; i+=2)
	{
		for(int32_t j=0; j<w_U; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2)>>2);
		}
		for(int32_t j=0; j<w_V; j++)
		{
			dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2)>>2);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_U,w_U);
		A_memcpy(dst_V,src_V,w_V);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;

		src_V+=src_pitchUV;
		src_Vp+=src_pitchUV;
		src_Vn+=src_pitchUV;
	}
}



void AutoYUY2::Convert_Automatic_YV16(const uint8_t *srcYp, const uint8_t *srcUp,
	const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
	const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV)
{
	const uint8_t *src_U,*src_Up,*src_Upp,*src_Un,*src_Unn,*src_Unnn;
	const uint8_t *src_V,*src_Vp,*src_Vpp,*src_Vn,*src_Vnn,*src_Vnnn;
	uint8_t *dst_U,*dst_V;
	ptrdiff_t pitch_U_2,pitch_V_2;
	uint8_t index_tab_0,index_tab_1,index_tab_2;
	const int32_t h_4=dst_h-4;
	const int16_t threshold_=threshold;
	const int w_UV=dst_w>>1;

	pitch_U_2=2*src_pitchUV;
	pitch_V_2=2*src_pitchUV;
	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchUV;
	src_Upp=src_U-2*src_pitchUV;
	src_Un=src_U+src_pitchUV;
	src_Unn=src_U+2*src_pitchUV;
	src_Unnn=src_U+3*src_pitchUV;
	src_Vp=src_V-src_pitchUV;
	src_Vpp=src_V-2*src_pitchUV;
	src_Vn=src_V+src_pitchUV;
	src_Vnn=src_V+2*src_pitchUV;
	src_Vnnn=src_V+3*src_pitchUV;
	dst_U=dstUp;
	dst_V=dstVp;

	Move_Full(srcYp,dstYp,dst_w,dst_h,src_pitch_Y,dst_pitch_Y);

	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dst_U,src_U,w_UV);
		A_memcpy(dst_V,src_V,w_UV);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_UV);
		A_memcpy(dst_V,src_Vn,w_UV);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_Unn[j]]+lookup[src_U[j]+256]+4)>>3);
			dst_V[j]=(uint8_t)((lookup[src_Vnn[j]]+lookup[src_V[j]+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			if (((abs((int16_t)src_Un[j]-(int16_t)src_Unn[j])>=threshold_) &&
				(abs((int16_t)src_Unnn[j]-(int16_t)src_Unn[j])>=threshold_) &&
				(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
				((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))) ||
				((abs((int16_t)src_Vn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
				(abs((int16_t)src_Vnnn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
				(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
				((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
				interlaced_tab[1][j]=true;
			else interlaced_tab[1][j]=false;
			if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
				(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
				(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
				((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))) ||
				((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
				(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
				(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
				((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
				interlaced_tab[0][j]=true;
			else interlaced_tab[0][j]=false;

			dst_U[j]=(uint8_t)((lookup[src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);
			dst_V[j]=(uint8_t)((lookup[src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;

	for(int32_t i=4; i<h_4; i+=4)
	{
		for(int32_t j=0; j<w_UV; j++)
		{
			if ((interlaced_tab[index_tab_1][j]) || (interlaced_tab[index_tab_0][j]))
			{
				dst_U[j]=(uint8_t)((lookup[src_U[j]+512]+(uint16_t)src_Upp[j]+4)>>3);
				dst_V[j]=(uint8_t)((lookup[src_V[j]+512]+(uint16_t)src_Vpp[j]+4)>>3);
			}
			else
			{
				dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2) >> 2);
				dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2) >> 2);
			}
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
				(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
				(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
				((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))) ||
				((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
				(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
				(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
				((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
				interlaced_tab[index_tab_2][j]=true;
			else interlaced_tab[index_tab_2][j]=false;			

			// Upsample as needed.
			if ((interlaced_tab[index_tab_1][j]) || (interlaced_tab[index_tab_2][j]))
			{
				dst_U[j]=(uint8_t)((lookup[src_Up[j]]+lookup[src_Un[j]+256]+4)>>3);
				dst_V[j]=(uint8_t)((lookup[src_Vp[j]]+lookup[src_Vn[j]+256]+4)>>3);
			}
			else
			{
				dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2)>>2);
				dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2)>>2);
			}
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			if ((interlaced_tab[index_tab_1][j]) || (interlaced_tab[index_tab_2][j]))
			{
				dst_U[j]=(uint8_t)((lookup[src_Unn[j]]+lookup[src_U[j]+256]+4)>>3);
				dst_V[j]=(uint8_t)((lookup[src_Vnn[j]]+lookup[src_V[j]+256]+4)>>3);
			}
			else
			{
				dst_U[j]=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_U[j]+2)>>2);
				dst_V[j]=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_V[j]+2)>>2);
			}
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			if (((abs((int16_t)src_Un[j]-(int16_t)src_Unn[j])>=threshold_) &&
				(abs((int16_t)src_Unnn[j]-(int16_t)src_Unn[j])>=threshold_) &&
				(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
				((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))) ||
				((abs((int16_t)src_Vn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
				(abs((int16_t)src_Vnnn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
				(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
				((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
				interlaced_tab[index_tab_0][j]=true;
			else interlaced_tab[index_tab_0][j]=false;

			// Upsample as needed.
			if ((interlaced_tab[index_tab_2][j]) || (interlaced_tab[index_tab_0][j]))
			{
				dst_U[j]=(uint8_t)((lookup[src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);
				dst_V[j]=(uint8_t)((lookup[src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
			}
			else
			{
				dst_U[j]=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_Unn[j]+2)>>2);
				dst_V[j]=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_Vnn[j]+2)>>2);
			}
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		for(int32_t j=0; j<w_UV; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_U[j]+512]+(uint16_t)src_Upp[j]+4)>>3);
			dst_V[j]=(uint8_t)((lookup[src_V[j]+512]+(uint16_t)src_Vpp[j]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_Up[j]]+lookup[src_Un[j]+256]+4)>>3);
			dst_V[j]=(uint8_t)((lookup[src_Vp[j]]+lookup[src_Vn[j]+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_U,w_UV);
		A_memcpy(dst_V,src_V,w_UV);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_UV);
		A_memcpy(dst_V,src_Vn,w_UV);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}
}


void AutoYUY2::Convert_Test_YV16(const uint8_t *srcYp, const uint8_t *srcUp,
	const uint8_t *srcVp, uint8_t *dstYp, uint8_t *dstUp,uint8_t *dstVp,const int dst_h,
	const int dst_w,int dst_pitch_Y, int dst_pitch_UV,int src_pitch_Y, int src_pitchUV)
{
	const uint8_t *src_U,*src_Up,*src_Upp,*src_Un,*src_Unn,*src_Unnn;
	const uint8_t *src_V,*src_Vp,*src_Vpp,*src_Vn,*src_Vnn,*src_Vnnn;
	uint8_t *dst_U,*dst_V;
	ptrdiff_t pitch_U_2,pitch_V_2;
	uint8_t index_tab_0,index_tab_1,index_tab_2;
	const int32_t h_4=dst_h-4;
	const int16_t threshold_=threshold;
	const int w_UV=dst_w>>1;

	pitch_U_2=2*src_pitchUV;
	pitch_V_2=2*src_pitchUV;
	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchUV;
	src_Upp=src_U-2*src_pitchUV;
	src_Un=src_U+src_pitchUV;
	src_Unn=src_U+2*src_pitchUV;
	src_Unnn=src_U+3*src_pitchUV;
	src_Vp=src_V-src_pitchUV;
	src_Vpp=src_V-2*src_pitchUV;
	src_Vn=src_V+src_pitchUV;
	src_Vnn=src_V+2*src_pitchUV;
	src_Vnnn=src_V+3*src_pitchUV;
	dst_U=dstUp;
	dst_V=dstVp;

	Move_Full(srcYp,dstYp,dst_w,dst_h,src_pitch_Y,dst_pitch_Y);

	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dst_U,src_U,w_UV);
		A_memcpy(dst_V,src_V,w_UV);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_UV);
		A_memcpy(dst_V,src_Vn,w_UV);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_Unn[j]]+lookup[src_U[j]+256]+4)>>3);
			dst_V[j]=(uint8_t)((lookup[src_Vnn[j]]+lookup[src_V[j]+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			if (((abs((int16_t)src_Un[j]-(int16_t)src_Unn[j])>=threshold_) &&
				(abs((int16_t)src_Unnn[j]-(int16_t)src_Unn[j])>=threshold_) &&
				(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
				((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))) ||
				((abs((int16_t)src_Vn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
				(abs((int16_t)src_Vnnn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
				(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
				((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
				interlaced_tab[1][j]=true;
			else interlaced_tab[1][j]=false;
			if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
				(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
				(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
				((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))) ||
				((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
				(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
				(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
				((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
				interlaced_tab[0][j]=true;
			else interlaced_tab[0][j]=false;

			dst_U[j]=(uint8_t)((lookup[src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);
			dst_V[j]=(uint8_t)((lookup[src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;

	for(int32_t i=4; i<h_4; i+=4)
	{
		for(int32_t j=0; j<w_UV; j++)
		{
			if ((interlaced_tab[index_tab_1][j]) || (interlaced_tab[index_tab_0][j]))
			{
				dst_U[j]=239;
				dst_V[j]=239;
			}
			else
			{
				dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2) >> 2);
				dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2) >> 2);
			}
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
				(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
				(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
				((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))) ||
				((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
				(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
				(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
				((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
				interlaced_tab[index_tab_2][j]=true;
			else interlaced_tab[index_tab_2][j]=false;			

			// Upsample as needed.
			if ((interlaced_tab[index_tab_1][j]) || (interlaced_tab[index_tab_2][j]))
			{
				dst_U[j]=239;
				dst_V[j]=239;
			}
			else
			{
				dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2)>>2);
				dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2)>>2);
			}
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			if ((interlaced_tab[index_tab_1][j]) || (interlaced_tab[index_tab_2][j]))
			{
				dst_U[j]=239;
				dst_V[j]=239;
			}
			else
			{
				dst_U[j]=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_U[j]+2)>>2);
				dst_V[j]=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_V[j]+2)>>2);
			}
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			if (((abs((uint8_t)src_Un[j]-(uint8_t)src_Unn[j])>=threshold_) &&
				(abs((uint8_t)src_Unnn[j]-(uint8_t)src_Unn[j])>=threshold_) &&
				(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
				((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))) ||
				((abs((uint8_t)src_Vn[j]-(uint8_t)src_Vnn[j])>=threshold_) &&
				(abs((uint8_t)src_Vnnn[j]-(uint8_t)src_Vnn[j])>=threshold_) &&
				(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
				((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
				interlaced_tab[index_tab_0][j]=true;
			else interlaced_tab[index_tab_0][j]=false;

			// Upsample as needed.
			if ((interlaced_tab[index_tab_2][j]) || (interlaced_tab[index_tab_0][j]))
			{
				dst_U[j]=239;
				dst_V[j]=239;
			}
			else
			{
				dst_U[j]=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_Unn[j]+2)>>2);
				dst_V[j]=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_Vnn[j]+2)>>2);
			}
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		for(int32_t j=0; j<w_UV; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_U[j]+512]+(uint16_t)src_Upp[j]+4)>>3);
			dst_V[j]=(uint8_t)((lookup[src_V[j]+512]+(uint16_t)src_Vpp[j]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		for(int32_t j=0; j<w_UV; j++)
		{
			dst_U[j]=(uint8_t)((lookup[src_Up[j]]+lookup[src_Un[j]+256]+4)>>3);
			dst_V[j]=(uint8_t)((lookup[src_Vp[j]]+lookup[src_Vn[j]+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_U,w_UV);
		A_memcpy(dst_V,src_V,w_UV);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_UV);
		A_memcpy(dst_V,src_Vn,w_UV);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

}


void AutoYUY2::Convert_Progressive(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *srcUpp, *srcUpn;
	const uint8_t *srcVpp, *srcVpn;
	const int dst_h_2=dst_h-2;

	srcUpp = srcUp - src_pitchUV;
	srcUpn = srcUp + src_pitchUV;
	srcVpp = srcVp - src_pitchUV;
	srcVpn = srcVp + src_pitchUV;

	for (int y=0; y<2; y+=2)
	{

		JPSDR_AutoYUY2_1(srcYp,srcUp,srcVp,dstp,dst_w>>2);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		int x_2=0;
		int x_4=0;
		for (int x=0; x<dst_w; x+=4)
		{
			dstp[x]=srcYp[x_2];
			dstp[x+2]=srcYp[x_2+1];

			dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]]+(uint16_t)srcUpn[x_4]+2) >> 2);
			dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]]+(uint16_t)srcVpn[x_4]+2) >> 2);

			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV;
		srcUpp += src_pitchUV;
		srcUpn += src_pitchUV;
		srcVp += src_pitchUV;
		srcVpp += src_pitchUV;
		srcVpn += src_pitchUV;
	}

	for (int y = 2; y < dst_h_2; y+=2)
	{
		int x_2=0;
		int x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			// Luma.
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]]+(uint16_t)srcUpp[x_4]+2) >> 2);
			dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]]+(uint16_t)srcVpp[x_4]+2) >> 2);
				
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]]+(uint16_t)srcUpn[x_4]+2) >> 2);
			dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]]+(uint16_t)srcVpn[x_4]+2) >> 2);
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;
		
		srcUp += src_pitchUV;
		srcUpp += src_pitchUV;
		srcUpn += src_pitchUV;
		srcVp += src_pitchUV;
		srcVpp += src_pitchUV;
		srcVpn += src_pitchUV;	
	}

	for (int y = dst_h_2; y < dst_h; y+=2)
	{
		int x_2=0;
		int x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			// Luma.
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]]+(uint16_t)srcUpp[x_4]+2) >> 2);
			dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]]+(uint16_t)srcVpp[x_4]+2) >> 2);
				
			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_1(srcYp,srcUp,srcVp,dstp,dst_w>>2);
		dstp += dst_pitch;
		srcYp += src_pitchY;
		
		srcUp += src_pitchUV;
		srcUpp += src_pitchUV;
		srcUpn += src_pitchUV;
		srcVp += src_pitchUV;
		srcVpp += src_pitchUV;
		srcVpn += src_pitchUV;	
	}
}


void AutoYUY2::Convert_Progressive_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *srcUpp, *srcUpn;
	const uint8_t *srcVpp, *srcVpn;
	const int dst_h_2=dst_h-2,w_8=dst_w>>3;

	bool _align=false;

	srcUpp = srcUp - src_pitchUV;
	srcUpn = srcUp + src_pitchUV;
	srcVpp = srcVp - src_pitchUV;
	srcVpn = srcVp + src_pitchUV;

	if ((((size_t)dstp & 0x0F)==0) && ((abs(dst_pitch) & 0x0F)==0)) _align=true;

	for (int y=0; y<2; y+=2)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_1b(srcYp,srcUp,srcVp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(srcYp,srcUp,srcVp,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_4b(srcYp,srcUp,srcUpn,srcVp,srcVpn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_4(srcYp,srcUp,srcUpn,srcVp,srcVpn,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV;
		srcUpp += src_pitchUV;
		srcUpn += src_pitchUV;
		srcVp += src_pitchUV;
		srcVpp += src_pitchUV;
		srcVpn += src_pitchUV;
	}

	for (int y = 2; y < dst_h_2; y+=2)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_4b(srcYp,srcUp,srcUpp,srcVp,srcVpp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_4(srcYp,srcUp,srcUpp,srcVp,srcVpp,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_4b(srcYp,srcUp,srcUpn,srcVp,srcVpn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_4(srcYp,srcUp,srcUpn,srcVp,srcVpn,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;
		
		srcUp += src_pitchUV;
		srcUpp += src_pitchUV;
		srcUpn += src_pitchUV;
		srcVp += src_pitchUV;
		srcVpp += src_pitchUV;
		srcVpn += src_pitchUV;	
	}

	for (int y = dst_h_2; y < dst_h; y+=2)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_4b(srcYp,srcUp,srcUpp,srcVp,srcVpp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_4(srcYp,srcUp,srcUpp,srcVp,srcVpp,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_1b(srcYp,srcUp,srcVp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(srcYp,srcUp,srcVp,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;
		
		srcUp += src_pitchUV;
		srcUpp += src_pitchUV;
		srcUpn += src_pitchUV;
		srcVp += src_pitchUV;
		srcVpp += src_pitchUV;
		srcVpn += src_pitchUV;	
	}
}



void AutoYUY2::Convert_Interlaced(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *srcUpp, *srcUpn, *srcUpnn, *srcUpnnn,*srcUppp;
	const uint8_t *srcVpp, *srcVpn, *srcVpnn, *srcVpnnn,*srcVppp;
	int src_pitchUV_2;
	const int dst_h_4=dst_h-4;

	srcUpp = srcUp - src_pitchUV;
	srcUppp = srcUp - (2*src_pitchUV);
	srcUpn = srcUp + src_pitchUV;
	srcUpnn = srcUp + (2 * src_pitchUV);
	srcUpnnn = srcUp + (3 * src_pitchUV);
	srcVpp = srcVp - src_pitchUV;
	srcVppp = srcVp - (2*src_pitchUV);
	srcVpn = srcVp + src_pitchUV;
	srcVpnn = srcVp + (2 * src_pitchUV);
	srcVpnnn = srcVp + (3 * src_pitchUV);
	
	src_pitchUV_2=src_pitchUV << 1;

	for (int y=0; y<4; y+=4)
	{

		JPSDR_AutoYUY2_1(srcYp,srcUp,srcVp,dstp,dst_w>>2);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_1(srcYp,srcUpn,srcVpn,dstp,dst_w>>2);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		int x_2=0;
		int x_4=0;
		for (int x=0; x<dst_w; x+=4)
		{
			dstp[x]=srcYp[x_2];
			dstp[x+2]=srcYp[x_2+1];

			dstp[x+1]=(uint8_t)((lookup[srcUpnn[x_4]]+lookup[srcUp[x_4]+256]+4)>>3);
			dstp[x+3]=(uint8_t)((lookup[srcVpnn[x_4]]+lookup[srcVp[x_4]+256]+4)>>3);

			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x=0; x<dst_w; x+=4)
		{
			dstp[x]=srcYp[x_2];
			dstp[x+2]=srcYp[x_2+1];

			dstp[x+1]=(uint8_t)((lookup[srcUpn[x_4]+512]+(uint16_t)srcUpnnn[x_4]+4)>>3);
			dstp[x+3]=(uint8_t)((lookup[srcVpn[x_4]+512]+(uint16_t)srcVpnnn[x_4]+4)>>3);

			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;
	}

	for (int y = 4; y < dst_h_4; y+=4)
	{
		int x_2=0;
		int x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			// Luma.
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]+512]+(uint16_t)srcUppp[x_4]+4) >> 3);
			dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]+512]+(uint16_t)srcVppp[x_4]+4) >> 3);
				
			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUpp[x_4]]+lookup[srcUpn[x_4]+256]+4) >> 3);
			dstp[x+3]=(uint8_t)((lookup[srcVpp[x_4]]+lookup[srcVpn[x_4]+256]+4) >> 3);
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;
		
		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUpnn[x_4]]+lookup[srcUp[x_4]+256]+4) >> 3);
			dstp[x+3]=(uint8_t)((lookup[srcVpnn[x_4]]+lookup[srcVp[x_4]+256]+4) >> 3);
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUpn[x_4]+512]+(uint16_t)srcUpnnn[x_4]+4) >> 3);
			dstp[x+3]=(uint8_t)((lookup[srcVpn[x_4]+512]+(uint16_t)srcVpnnn[x_4]+4) >> 3);
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;	
	}

	for (int y = dst_h_4; y < dst_h; y+=4)
	{
		int x_2=0;
		int x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			// Luma.
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]+512]+(uint16_t)srcUppp[x_4]+4) >> 3);
			dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]+512]+(uint16_t)srcVppp[x_4]+4) >> 3);
				
			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUpp[x_4]]+lookup[srcUpn[x_4]+256]+4) >> 3);
			dstp[x+3]=(uint8_t)((lookup[srcVpp[x_4]]+lookup[srcVpn[x_4]+256]+4) >> 3);
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;
		
		JPSDR_AutoYUY2_1(srcYp,srcUp,srcVp,dstp,dst_w>>2);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_1(srcYp,srcUpn,srcVpn,dstp,dst_w>>2);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;	
	}
}


void AutoYUY2::Convert_Interlaced_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *srcUpp, *srcUpn, *srcUpnn, *srcUpnnn,*srcUppp;
	const uint8_t *srcVpp, *srcVpn, *srcVpnn, *srcVpnnn,*srcVppp;
	int src_pitchUV_2;
	const int dst_h_4=dst_h-4,w_8=dst_w>>3;

	bool _align=false;

	srcUpp = srcUp - src_pitchUV;
	srcUppp = srcUp - (2*src_pitchUV);
	srcUpn = srcUp + src_pitchUV;
	srcUpnn = srcUp + (2 * src_pitchUV);
	srcUpnnn = srcUp + (3 * src_pitchUV);
	srcVpp = srcVp - src_pitchUV;
	srcVppp = srcVp - (2*src_pitchUV);
	srcVpn = srcVp + src_pitchUV;
	srcVpnn = srcVp + (2 * src_pitchUV);
	srcVpnnn = srcVp + (3 * src_pitchUV);
	
	src_pitchUV_2=src_pitchUV << 1;

	if ((((size_t)dstp & 0x0F)==0) && ((abs(dst_pitch) & 0x0F)==0)) _align=true;

	for (int y=0; y<4; y+=4)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_1b(srcYp,srcUp,srcVp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(srcYp,srcUp,srcVp,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_1b(srcYp,srcUpn,srcVpn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(srcYp,srcUpn,srcVpn,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_2b(srcYp,srcUpnn,srcUp,srcVpnn,srcVp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_2(srcYp,srcUpnn,srcUp,srcVpnn,srcVp,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_3b(srcYp,srcUpn,srcUpnnn,srcVpn,srcVpnnn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_3(srcYp,srcUpn,srcUpnnn,srcVpn,srcVpnnn,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;
	}

	for (int y = 4; y < dst_h_4; y+=4)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_3b(srcYp,srcUp,srcUppp,srcVp,srcVppp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_3(srcYp,srcUp,srcUppp,srcVp,srcVppp,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_2b(srcYp,srcUpp,srcUpn,srcVpp,srcVpn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_2(srcYp,srcUpp,srcUpn,srcVpp,srcVpn,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_2b(srcYp,srcUpnn,srcUp,srcVpnn,srcVp,dstp,w_8);
		JPSDR_AutoYUY2_SSE2_2(srcYp,srcUpnn,srcUp,srcVpnn,srcVp,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_3b(srcYp,srcUpn,srcUpnnn,srcVpn,srcVpnnn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_3(srcYp,srcUpn,srcUpnnn,srcVpn,srcVpnnn,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;	
	}

	for (int y = dst_h_4; y < dst_h; y+=4)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_3b(srcYp,srcUp,srcUppp,srcVp,srcVppp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_3(srcYp,srcUp,srcUppp,srcVp,srcVppp,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_2b(srcYp,srcUpp,srcUpn,srcVpp,srcVpn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_2(srcYp,srcUpp,srcUpn,srcVpp,srcVpn,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_1b(srcYp,srcUp,srcVp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(srcYp,srcUp,srcVp,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_1b(srcYp,srcUpn,srcVpn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(srcYp,srcUpn,srcVpn,dstp,w_8);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;	
	}
}



void AutoYUY2::Convert_Automatic(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY,int src_pitchUV)
{
	const uint8_t *srcUpp, *srcUpn, *srcUpnn, *srcUpnnn,*srcUppp;
	const uint8_t *srcVpp, *srcVpn, *srcVpnn, *srcVpnnn,*srcVppp;
	int src_pitchUV_2;
	uint8_t index_tab_0,index_tab_1,index_tab_2;
	const int dst_h_4=dst_h-4;

	srcUpp = srcUp - src_pitchUV;
	srcUppp = srcUp - (2*src_pitchUV);
	srcUpn = srcUp + src_pitchUV;
	srcUpnn = srcUp + (2 * src_pitchUV);
	srcUpnnn = srcUp + (3 * src_pitchUV);
	srcVpp = srcVp - src_pitchUV;
	srcVppp = srcVp - (2*src_pitchUV);
	srcVpn = srcVp + src_pitchUV;
	srcVpnn = srcVp + (2 * src_pitchUV);
	srcVpnnn = srcVp + (3 * src_pitchUV);
	
	src_pitchUV_2=src_pitchUV << 1;

	for (int y=0; y<4; y+=4)
	{
		int x_2=0;
		int x_4=0;
		for (int x=0; x<dst_w; x+=4)
		{
			dstp[x]=srcYp[x_2];
			dstp[x+2]=srcYp[x_2+1];

			dstp[x+1]=srcUp[x_4];
			dstp[x+3]=srcVp[x_4];

			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x=0; x<dst_w; x+=4)
		{
			dstp[x]=srcYp[x_2];
			dstp[x+2]=srcYp[x_2+1];

			dstp[x+1]=srcUpn[x_4];
			dstp[x+3]=srcVpn[x_4];

			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x=0; x<dst_w; x+=4)
		{
			dstp[x]=srcYp[x_2];
			dstp[x+2]=srcYp[x_2+1];

			dstp[x+1]=(uint8_t)((lookup[srcUpnn[x_4]]+lookup[srcUp[x_4]+256]+4)>>3);
			dstp[x+3]=(uint8_t)((lookup[srcVpnn[x_4]]+lookup[srcVp[x_4]+256]+4)>>3);

			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x=0; x<dst_w; x+=4)
		{
			dstp[x]=srcYp[x_2];
			dstp[x+2]=srcYp[x_2+1];
			
			if (((abs((int16_t)srcUpn[x_4]-(int16_t)srcUpnn[x_4])>=threshold) &&
				(abs((int16_t)srcUpnnn[x_4]-(int16_t)srcUpnn[x_4])>=threshold) &&
				(((srcUpn[x_4]>srcUpnn[x_4]) && (srcUpnnn[x_4]>srcUpnn[x_4])) ||
				((srcUpn[x_4]<srcUpnn[x_4]) && (srcUpnnn[x_4]<srcUpnn[x_4])))) ||
				((abs((int16_t)srcVpn[x_4]-(int16_t)srcVpnn[x_4])>=threshold) &&
				(abs((int16_t)srcVpnnn[x_4]-(int16_t)srcVpnn[x_4])>=threshold) &&
				(((srcVpn[x_4]>srcVpnn[x_4]) && (srcVpnnn[x_4]>srcVpnn[x_4])) ||
				((srcVpn[x_4]<srcVpnn[x_4]) && (srcVpnnn[x_4]<srcVpnn[x_4])))))
				interlaced_tab[1][x_4]=true;
			else interlaced_tab[1][x_4]=false;
			if (((abs((int16_t)srcUp[x_4]-(int16_t)srcUpn[x_4])>=threshold) &&
				(abs((int16_t)srcUpnn[x_4]-(int16_t)srcUpn[x_4])>=threshold) &&
				(((srcUp[x_4]>srcUpn[x_4]) && (srcUpnn[x_4]>srcUpn[x_4])) ||
				((srcUp[x_4]<srcUpn[x_4]) && (srcUpnn[x_4]<srcUpn[x_4])))) ||
				((abs((int16_t)srcVp[x_4]-(int16_t)srcVpn[x_4])>=threshold) &&
				(abs((int16_t)srcVpnn[x_4]-(int16_t)srcVpn[x_4])>=threshold) &&
				(((srcVp[x_4]>srcVpn[x_4]) && (srcVpnn[x_4]>srcVpn[x_4])) ||
				((srcVp[x_4]<srcVpn[x_4]) && (srcVpnn[x_4]<srcVpn[x_4])))))
				interlaced_tab[0][x_4]=true;
			else interlaced_tab[0][x_4]=false;
			
			dstp[x+1]=(uint8_t)((lookup[srcUpn[x_4]+512]+(uint16_t)srcUpnnn[x_4]+4)>>3);
			dstp[x+3]=(uint8_t)((lookup[srcVpn[x_4]+512]+(uint16_t)srcVpnnn[x_4]+4)>>3);

			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;
	}
	
	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;
	
	for (int y = 4; y < dst_h_4; y+=4)
	{
		int x_2=0;
		int x_4=0;
		
		for (int x = 0; x < dst_w; x+=4)
		{
			// Luma.
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			if ((interlaced_tab[index_tab_1][x_4]) || (interlaced_tab[index_tab_0][x_4]))
			{
				dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]+512]+(uint16_t)srcUppp[x_4]+4) >> 3);
				dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]+512]+(uint16_t)srcVppp[x_4]+4) >> 3);
			}
			else
			{
				dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]]+(uint16_t)srcUpp[x_4]+2) >> 2);
				dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]]+(uint16_t)srcVpp[x_4]+2) >> 2);
			}
						
			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];
			
			if (((abs((int16_t)srcUp[x_4]-(int16_t)srcUpn[x_4])>=threshold) &&
				(abs((int16_t)srcUpnn[x_4]-(int16_t)srcUpn[x_4])>=threshold) &&
				(((srcUp[x_4]>srcUpn[x_4]) && (srcUpnn[x_4]>srcUpn[x_4])) ||
				((srcUp[x_4]<srcUpn[x_4]) && (srcUpnn[x_4]<srcUpn[x_4])))) ||
				((abs((int16_t)srcVp[x_4]-(int16_t)srcVpn[x_4])>=threshold) &&
				(abs((int16_t)srcVpnn[x_4]-(int16_t)srcVpn[x_4])>=threshold) &&
				(((srcVp[x_4]>srcVpn[x_4]) && (srcVpnn[x_4]>srcVpn[x_4])) ||
				((srcVp[x_4]<srcVpn[x_4]) && (srcVpnn[x_4]<srcVpn[x_4])))))
				interlaced_tab[index_tab_2][x_4]=true;
			else interlaced_tab[index_tab_2][x_4]=false;			

			// Upsample as needed.
			if ((interlaced_tab[index_tab_1][x_4]) || (interlaced_tab[index_tab_2][x_4]))
			{
				dstp[x+1]=(uint8_t)((lookup[srcUpp[x_4]]+lookup[srcUpn[x_4]+256]+4) >> 3);
				dstp[x+3]=(uint8_t)((lookup[srcVpp[x_4]]+lookup[srcVpn[x_4]+256]+4) >> 3);
			}
			else
			{
				dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]]+(uint16_t)srcUpn[x_4]+2) >> 2);
				dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]]+(uint16_t)srcVpn[x_4]+2) >> 2);
			}
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;
		
		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			if ((interlaced_tab[index_tab_1][x_4]) || (interlaced_tab[index_tab_2][x_4]))
			{
				dstp[x+1]=(uint8_t)((lookup[srcUpnn[x_4]]+lookup[srcUp[x_4]+256]+4) >> 3);
				dstp[x+3]=(uint8_t)((lookup[srcVpnn[x_4]]+lookup[srcVp[x_4]+256]+4) >> 3);
			}
			else
			{
				dstp[x+1]=(uint8_t)((lookup[srcUpn[x_4]]+(uint16_t)srcUp[x_4]+2) >> 2);
				dstp[x+3]=(uint8_t)((lookup[srcVpn[x_4]]+(uint16_t)srcVp[x_4]+2) >> 2);
			}
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];
			
			if (((abs((int16_t)srcUpn[x_4]-(int16_t)srcUpnn[x_4])>=threshold) &&
				(abs((int16_t)srcUpnnn[x_4]-(int16_t)srcUpnn[x_4])>=threshold) &&
				(((srcUpn[x_4]>srcUpnn[x_4]) && (srcUpnnn[x_4]>srcUpnn[x_4])) ||
				((srcUpn[x_4]<srcUpnn[x_4]) && (srcUpnnn[x_4]<srcUpnn[x_4])))) ||
				((abs((int16_t)srcVpn[x_4]-(int16_t)srcVpnn[x_4])>=threshold) &&
				(abs((int16_t)srcVpnnn[x_4]-(int16_t)srcVpnn[x_4])>=threshold) &&
				(((srcVpn[x_4]>srcVpnn[x_4]) && (srcVpnnn[x_4]>srcVpnn[x_4])) ||
				((srcVpn[x_4]<srcVpnn[x_4]) && (srcVpnnn[x_4]<srcVpnn[x_4])))))
				interlaced_tab[index_tab_0][x_4]=true;
			else interlaced_tab[index_tab_0][x_4]=false;

			// Upsample as needed.
			if ((interlaced_tab[index_tab_2][x_4]) || (interlaced_tab[index_tab_0][x_4]))
			{
				dstp[x+1]=(uint8_t)((lookup[srcUpn[x_4]+512]+(uint16_t)srcUpnnn[x_4]+4) >> 3);
				dstp[x+3]=(uint8_t)((lookup[srcVpn[x_4]+512]+(uint16_t)srcVpnnn[x_4]+4) >> 3);
			}
			else
			{
				dstp[x+1]=(uint8_t)((lookup[srcUpn[x_4]]+(uint16_t)srcUpnn[x_4]+2) >> 2);
				dstp[x+3]=(uint8_t)((lookup[srcVpn[x_4]]+(uint16_t)srcVpnn[x_4]+2) >> 2);
			}
						
			x_2+=2;
			x_4++;
		}
		
		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;
		
		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;	
	}

	for (int y = dst_h_4; y < dst_h; y+=4)
	{
		int x_2=0;
		int x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			// Luma.
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]+512]+(uint16_t)srcUppp[x_4]+4) >> 3);
			dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]+512]+(uint16_t)srcVppp[x_4]+4) >> 3);
				
			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUpp[x_4]]+lookup[srcUpn[x_4]+256]+4) >> 3);
			dstp[x+3]=(uint8_t)((lookup[srcVpp[x_4]]+lookup[srcVpn[x_4]+256]+4) >> 3);
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;
		
		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=srcUp[x_4];
			dstp[x+3]=srcVp[x_4];
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=srcUpn[x_4];
			dstp[x+3]=srcVpn[x_4];
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;
	}
}



void AutoYUY2::Convert_Test(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY,int src_pitchUV)	
{
	const uint8_t *srcUpp, *srcUpn, *srcUpnn, *srcUpnnn,*srcUppp;
	const uint8_t *srcVpp, *srcVpn, *srcVpnn, *srcVpnnn,*srcVppp;
	int src_pitchUV_2;
	uint8_t index_tab_0,index_tab_1,index_tab_2;
	const int dst_h_4=dst_h-4;

	srcUpp = srcUp - src_pitchUV;
	srcUppp = srcUp - (2*src_pitchUV);
	srcUpn = srcUp + src_pitchUV;
	srcUpnn = srcUp + (2 * src_pitchUV);
	srcUpnnn = srcUp + (3 * src_pitchUV);
	srcVpp = srcVp - src_pitchUV;
	srcVppp = srcVp - (2*src_pitchUV);
	srcVpn = srcVp + src_pitchUV;
	srcVpnn = srcVp + (2 * src_pitchUV);
	srcVpnnn = srcVp + (3 * src_pitchUV);
	
	src_pitchUV_2=src_pitchUV << 1;

	for (int y=0; y<4; y+=4)
	{
		int x_2=0;
		int x_4=0;
		for (int x=0; x<dst_w; x+=4)
		{
			dstp[x]=srcYp[x_2];
			dstp[x+2]=srcYp[x_2+1];

			dstp[x+1]=srcUp[x_4];
			dstp[x+3]=srcVp[x_4];

			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x=0; x<dst_w; x+=4)
		{
			dstp[x]=srcYp[x_2];
			dstp[x+2]=srcYp[x_2+1];

			dstp[x+1]=srcUpn[x_4];
			dstp[x+3]=srcVpn[x_4];

			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x=0; x<dst_w; x+=4)
		{
			dstp[x]=srcYp[x_2];
			dstp[x+2]=srcYp[x_2+1];

			dstp[x+1]=(uint8_t)((lookup[srcUpnn[x_4]]+lookup[srcUp[x_4]+256]+4)>>3);
			dstp[x+3]=(uint8_t)((lookup[srcVpnn[x_4]]+lookup[srcVp[x_4]+256]+4)>>3);

			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x=0; x<dst_w; x+=4)
		{
			dstp[x]=srcYp[x_2];
			dstp[x+2]=srcYp[x_2+1];
			
			if (((abs((int16_t)srcUpn[x_4]-(int16_t)srcUpnn[x_4])>=threshold) &&
				(abs((int16_t)srcUpnnn[x_4]-(int16_t)srcUpnn[x_4])>=threshold) &&
				(((srcUpn[x_4]>srcUpnn[x_4]) && (srcUpnnn[x_4]>srcUpnn[x_4])) ||
				((srcUpn[x_4]<srcUpnn[x_4]) && (srcUpnnn[x_4]<srcUpnn[x_4])))) ||
				((abs((int16_t)srcVpn[x_4]-(int16_t)srcVpnn[x_4])>=threshold) &&
				(abs((int16_t)srcVpnnn[x_4]-(int16_t)srcVpnn[x_4])>=threshold) &&
				(((srcVpn[x_4]>srcVpnn[x_4]) && (srcVpnnn[x_4]>srcVpnn[x_4])) ||
				((srcVpn[x_4]<srcVpnn[x_4]) && (srcVpnnn[x_4]<srcVpnn[x_4])))))
				interlaced_tab[1][x_4]=true;
			else interlaced_tab[1][x_4]=false;
			if (((abs((int16_t)srcUp[x_4]-(int16_t)srcUpn[x_4])>=threshold) &&
				(abs((int16_t)srcUpnn[x_4]-(int16_t)srcUpn[x_4])>=threshold) &&
				(((srcUp[x_4]>srcUpn[x_4]) && (srcUpnn[x_4]>srcUpn[x_4])) ||
				((srcUp[x_4]<srcUpn[x_4]) && (srcUpnn[x_4]<srcUpn[x_4])))) ||
				((abs((int16_t)srcVp[x_4]-(int16_t)srcVpn[x_4])>=threshold) &&
				(abs((int16_t)srcVpnn[x_4]-(int16_t)srcVpn[x_4])>=threshold) &&
				(((srcVp[x_4]>srcVpn[x_4]) && (srcVpnn[x_4]>srcVpn[x_4])) ||
				((srcVp[x_4]<srcVpn[x_4]) && (srcVpnn[x_4]<srcVpn[x_4])))))
				interlaced_tab[0][x_4]=true;
			else interlaced_tab[0][x_4]=false;
			
			dstp[x+1]=(uint8_t)((lookup[srcUpn[x_4]+512]+(uint16_t)srcUpnnn[x_4]+4)>>3);
			dstp[x+3]=(uint8_t)((lookup[srcVpn[x_4]+512]+(uint16_t)srcVpnnn[x_4]+4)>>3);

			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;
	}
	
	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;
	
	for (int y = 4; y < dst_h_4; y+=4)
	{
		int x_2=0;
		int x_4=0;
		
		for (int x = 0; x < dst_w; x+=4)
		{
			// Upsample as needed.
			if ((interlaced_tab[index_tab_1][x_4]) || (interlaced_tab[index_tab_0][x_4]))
			{
				dstp[x] = 180;
				dstp[x+2] = 180;
				
				dstp[x+1]=128;
				dstp[x+3]=128;
			}
			else
			{
				dstp[x] = srcYp[x_2];
				dstp[x+2] = srcYp[x_2+1];
				
				dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]]+(uint16_t)srcUpp[x_4]+2) >> 2);
				dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]]+(uint16_t)srcVpp[x_4]+2) >> 2);
			}
						
			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			if (((abs((int16_t)srcUp[x_4]-(int16_t)srcUpn[x_4])>=threshold) &&
				(abs((int16_t)srcUpnn[x_4]-(int16_t)srcUpn[x_4])>=threshold) &&
				(((srcUp[x_4]>srcUpn[x_4]) && (srcUpnn[x_4]>srcUpn[x_4])) ||
				((srcUp[x_4]<srcUpn[x_4]) && (srcUpnn[x_4]<srcUpn[x_4])))) ||
				((abs((int16_t)srcVp[x_4]-(int16_t)srcVpn[x_4])>=threshold) &&
				(abs((int16_t)srcVpnn[x_4]-(int16_t)srcVpn[x_4])>=threshold) &&
				(((srcVp[x_4]>srcVpn[x_4]) && (srcVpnn[x_4]>srcVpn[x_4])) ||
				((srcVp[x_4]<srcVpn[x_4]) && (srcVpnn[x_4]<srcVpn[x_4])))))
				interlaced_tab[index_tab_2][x_4]=true;
			else interlaced_tab[index_tab_2][x_4]=false;			

			// Upsample as needed.
			if ((interlaced_tab[index_tab_1][x_4]) || (interlaced_tab[index_tab_2][x_4]))
			{
				dstp[x] = 180;
				dstp[x+2] = 180;
				
				dstp[x+1]=128;
				dstp[x+3]=128;
			}
			else
			{
				dstp[x] = srcYp[x_2];
				dstp[x+2] = srcYp[x_2+1];
				
				dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]]+(uint16_t)srcUpn[x_4]+2) >> 2);
				dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]]+(uint16_t)srcVpn[x_4]+2) >> 2);
			}
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;
		
		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			// Upsample as needed.
			if ((interlaced_tab[index_tab_1][x_4]) || (interlaced_tab[index_tab_2][x_4]))
			{
				dstp[x] = 180;
				dstp[x+2] = 180;
				
				dstp[x+1]=128;
				dstp[x+3]=128;
			}
			else
			{
				dstp[x] = srcYp[x_2];
				dstp[x+2] = srcYp[x_2+1];
				
				dstp[x+1]=(uint8_t)((lookup[srcUpn[x_4]]+(uint16_t)srcUp[x_4]+2) >> 2);
				dstp[x+3]=(uint8_t)((lookup[srcVpn[x_4]]+(uint16_t)srcVp[x_4]+2) >> 2);
			}
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			if (((abs((int16_t)srcUpn[x_4]-(int16_t)srcUpnn[x_4])>=threshold) &&
				(abs((int16_t)srcUpnnn[x_4]-(int16_t)srcUpnn[x_4])>=threshold) &&
				(((srcUpn[x_4]>srcUpnn[x_4]) && (srcUpnnn[x_4]>srcUpnn[x_4])) ||
				((srcUpn[x_4]<srcUpnn[x_4]) && (srcUpnnn[x_4]<srcUpnn[x_4])))) ||
				((abs((int16_t)srcVpn[x_4]-(int16_t)srcVpnn[x_4])>=threshold) &&
				(abs((int16_t)srcVpnnn[x_4]-(int16_t)srcVpnn[x_4])>=threshold) &&
				(((srcVpn[x_4]>srcVpnn[x_4]) && (srcVpnnn[x_4]>srcVpnn[x_4])) ||
				((srcVpn[x_4]<srcVpnn[x_4]) && (srcVpnnn[x_4]<srcVpnn[x_4])))))
				interlaced_tab[index_tab_0][x_4]=true;
			else interlaced_tab[index_tab_0][x_4]=false;

			// Upsample as needed.
			if ((interlaced_tab[index_tab_2][x_4]) || (interlaced_tab[index_tab_0][x_4]))
			{
				dstp[x] = 180;
				dstp[x+2] = 180;
				
				dstp[x+1]=128;
				dstp[x+3]=128;
			}
			else
			{
				dstp[x] = srcYp[x_2];
				dstp[x+2] = srcYp[x_2+1];
				
				dstp[x+1]=(uint8_t)((lookup[srcUpn[x_4]]+(uint16_t)srcUpnn[x_4]+2) >> 2);
				dstp[x+3]=(uint8_t)((lookup[srcVpn[x_4]]+(uint16_t)srcVpnn[x_4]+2) >> 2);
			}
						
			x_2+=2;
			x_4++;
		}
		
		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;
		
		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;	
	}

	for (int y = dst_h_4; y < dst_h; y+=4)
	{
		int x_2=0;
		int x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			// Luma.
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUp[x_4]+512]+(uint16_t)srcUppp[x_4]+4) >> 3);
			dstp[x+3]=(uint8_t)((lookup[srcVp[x_4]+512]+(uint16_t)srcVppp[x_4]+4) >> 3);
				
			x_2+=2;
			x_4++;
		}

		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=(uint8_t)((lookup[srcUpp[x_4]]+lookup[srcUpn[x_4]+256]+4) >> 3);
			dstp[x+3]=(uint8_t)((lookup[srcVpp[x_4]]+lookup[srcVpn[x_4]+256]+4) >> 3);
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;
		
		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=srcUp[x_4];
			dstp[x+3]=srcVp[x_4];
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;

		x_2=0;
		x_4=0;
		for (int x = 0; x < dst_w; x+=4)
		{
			//Luma
			dstp[x] = srcYp[x_2];
			dstp[x+2] = srcYp[x_2+1];

			// Upsample as needed.
			dstp[x+1]=srcUpn[x_4];
			dstp[x+3]=srcVpn[x_4];
						
			x_2+=2;
			x_4++;
		}
		
		dstp += dst_pitch;
		srcYp += src_pitchY;

		srcUp += src_pitchUV_2;
		srcUpp += src_pitchUV_2;
		srcUppp += src_pitchUV_2;
		srcUpn += src_pitchUV_2;
		srcUpnn += src_pitchUV_2;
		srcUpnnn += src_pitchUV_2;
		srcVp += src_pitchUV_2;
		srcVpp += src_pitchUV_2;
		srcVppp += src_pitchUV_2;
		srcVpn += src_pitchUV_2;
		srcVpnn += src_pitchUV_2;
		srcVpnnn += src_pitchUV_2;
	}
}
		


PVideoFrame __stdcall AutoYUY2::GetFrame(int n, IScriptEnvironment* env) 
{
	PVideoFrame src = child->GetFrame(n,env);
	PVideoFrame dst = env->NewVideoFrame(vi);
	uint8_t *dst_Y,*dst_U,*dst_V;
	const uint8_t *srcYp = src->GetReadPtr(PLANAR_Y);
	const uint8_t *srcUp = src->GetReadPtr(PLANAR_U);
	const uint8_t *srcVp = src->GetReadPtr(PLANAR_V);
	int dst_h,dst_w;
	int dst_pitch_Y,dst_pitchUV;
	int src_pitch_Y,src_pitchUV;

	switch(output)
	{
		case 0 : 
			dst_Y = dst->GetWritePtr();
			dst_h = dst->GetHeight();
			dst_w = dst->GetRowSize();
			dst_pitch_Y = dst->GetPitch();
			dst_U=NULL;
			dst_V=NULL;
			dst_pitchUV=0;
			break;
		case 1 :
			dst_h = dst->GetHeight(PLANAR_Y);
			dst_w = dst->GetRowSize(PLANAR_Y);
			dst_Y = dst->GetWritePtr(PLANAR_Y);
			dst_U=dst->GetWritePtr(PLANAR_U);
			dst_V=dst->GetWritePtr(PLANAR_V);
			dst_pitch_Y = dst->GetPitch(PLANAR_Y);
			dst_pitchUV=dst->GetPitch(PLANAR_U);
			break;
		default : break;
	}
		
	src_pitch_Y = src->GetPitch(PLANAR_Y);
	src_pitchUV = src->GetPitch(PLANAR_U);

	switch(output)
	{
		case 0 :
			switch(mode)
			{
				case -1 : Convert_Automatic(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
						  src_pitchUV); break;
				case 0 :
					if ((SSE2_Enable) && ((dst_w & 0x7)==0))
						Convert_Progressive_SSE(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
							src_pitchUV);
					else
						Convert_Progressive(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
							src_pitchUV);
					break;
				case 1 :
					if ((SSE2_Enable) && ((dst_w & 0x7)==0))
						Convert_Interlaced_SSE(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
							src_pitchUV);
					else
						Convert_Interlaced(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
							src_pitchUV);
					break;
				case 2 : Convert_Test(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
						  src_pitchUV); break;
				default : break;
			}
			break;
		case 1 :
			switch(mode)
			{
				case -1 : Convert_Automatic_YV16(srcYp,srcUp,srcVp,dst_Y,dst_U,dst_V,dst_h,dst_w,dst_pitch_Y,dst_pitchUV,
							  src_pitch_Y,src_pitchUV); break;
				case 0 :
					if ((SSE2_Enable) && ((dst_w & 0x7)==0))
						Convert_Progressive_YV16_SSE(srcYp,srcUp,srcVp,dst_Y,dst_U,dst_V,dst_h,dst_w,dst_pitch_Y,dst_pitchUV,
							  src_pitch_Y,src_pitchUV);
					else
						Convert_Progressive_YV16(srcYp,srcUp,srcVp,dst_Y,dst_U,dst_V,dst_h,dst_w,dst_pitch_Y,dst_pitchUV,
							  src_pitch_Y,src_pitchUV);
					break;
				case 1 :
					if ((SSE2_Enable) && ((dst_w & 0x7)==0))
						Convert_Interlaced_YV16_SSE(srcYp,srcUp,srcVp,dst_Y,dst_U,dst_V,dst_h,dst_w,dst_pitch_Y,dst_pitchUV,
							  src_pitch_Y,src_pitchUV);
					else
						Convert_Interlaced_YV16(srcYp,srcUp,srcVp,dst_Y,dst_U,dst_V,dst_h,dst_w,dst_pitch_Y,dst_pitchUV,
							  src_pitch_Y,src_pitchUV);
					break;
				case 2 : Convert_Test_YV16(srcYp,srcUp,srcVp,dst_Y,dst_U,dst_V,dst_h,dst_w,dst_pitch_Y,dst_pitchUV,
							  src_pitch_Y,src_pitchUV); break;
				default : break;
			}
			break;
		default : break;
	}

	return dst;
}

const AVS_Linkage *AVS_linkage = nullptr;


/*
  threshold : int, default value : 4
     Value for threshold between odd/even lines to consider pixel interlaced.
  mode : int, default value -1.
     Conversion mode YV12 to YUY2.
	 -1 : Automatic detection.
	 0 : Progressive.
	 1 : Interlaced.
	 2 : Test mode (put white/colored pixel on part detected interlaced).
  output : int, default value 1
     0 : Output YUY2
	 1 : Output YV16
*/
AVSValue __cdecl Create_AutoYUY2(AVSValue args, void* user_data, IScriptEnvironment* env)
{

	if (!args[0].IsClip()) env->ThrowError("AutoYUY2 : arg 0 must be a clip !");

	VideoInfo vi = args[0].AsClip()->GetVideoInfo();

	if (!vi.IsYV12())
		env->ThrowError("AutoYUY2 : Input format must be YV12 or I420");
	if (vi.width & 2)
		env->ThrowError("AutoYUY2 : Input width must be a multiple of 2.");
	if (vi.height & 2)
		env->ThrowError("AutoYUY2 : Input height must be a multiple of 2.");
	if (vi.height < 8)
		env->ThrowError("AutoYUY2 : Input height must be at least 8.");

	const int thrs=args[1].AsInt(4);
	const int mode=args[2].AsInt(-1);
	const int output=args[3].AsInt(1);

	if ((mode<-1) || (mode>2))
		env->ThrowError("AutoYUY2 : [mode] must be -1, 0, 1 or 2.");
	if ((output<0) || (output>1))
		env->ThrowError("AutoYUY2 : [output] must be 0 or 1");

  return new AutoYUY2(args[0].AsClip(), thrs, mode, output, env);
}

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors)
{
	if (IInstrSet<0) InstructionSet();
	CPU_Cache_Size=DataCacheSize(0)>>2;

	AVS_linkage = vectors;

    env->AddFunction("AutoYUY2", "c[threshold]i[mode]i[output]i", Create_AutoYUY2, 0);

    return "AutoYUY2 Pluggin";
}
