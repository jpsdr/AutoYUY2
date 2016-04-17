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


#define VERSION "AutoYUY2 2.0.0 JPSDR"
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
	typedef struct _YUYV
	{
		uint8_t y1;
		uint8_t u;
		uint8_t y2;
		uint8_t v;
	} YUYV;

	int threshold;
	int mode;
	int output;
	uint16_t lookup_Upscale[768];
	bool *interlaced_tab_U[Interlaced_Tab_Size],*interlaced_tab_V[Interlaced_Tab_Size];
	bool SSE2_Enable;
	size_t Cache_Setting;

	inline void Move_Full(const void *src_, void *dst_, const int32_t w,const int32_t h,
		int src_pitch,int dst_pitch);

	void Convert_Progressive_YUY2(const uint8_t *srcY, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Progressive_YUY2_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Interlaced_YUY2(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Interlaced_YUY2_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Automatic_YUY2(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Test_YUY2(const uint8_t *srcYp, const uint8_t *srcUp,
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

	for (uint16_t i=0; i<Interlaced_Tab_Size; i++)
	{
		interlaced_tab_U[i]=NULL;
		interlaced_tab_V[i]=NULL;
	}

	switch (output)
	{
		case 0 : vi.pixel_type = VideoInfo::CS_YUY2; break;
		case 1 : vi.pixel_type = VideoInfo::CS_YV16; break;
		default : break;
	}

	for (uint16_t i=0; i<Interlaced_Tab_Size; i++)
	{
		interlaced_tab_U[i]=(bool*)malloc(vi.width*sizeof(bool));
		interlaced_tab_V[i]=(bool*)malloc(vi.width*sizeof(bool));
	}

	ok=true;
	for (uint16_t i=0; i<Interlaced_Tab_Size; i++)
	{
		ok=ok && (interlaced_tab_U[i]!=NULL);
		ok=ok && (interlaced_tab_V[i]!=NULL);
	}

	if (!ok)
		env->ThrowError("AutoYUY2 : Memory allocation error.");

	for (uint16_t i=0; i<256; i++)
	{
		lookup_Upscale[i]=3*i;
		lookup_Upscale[i+256]=5*i;
		lookup_Upscale[i+512]=7*i;
	}

	SSE2_Enable=((env->GetCPUFlags()&CPUF_SSE2)!=0);

	const size_t img_size=vi.BMPSize();

	if (img_size<=MAX_CACHE_SIZE)
	{
		if (CPU_Cache_Size>=img_size) Cache_Setting=img_size;
		else Cache_Setting=16;
	}
	else Cache_Setting=16;

}

AutoYUY2::~AutoYUY2() 
{
	for (int16_t i=Interlaced_Tab_Size-1; i>=0; i--)
	{
		myfree(interlaced_tab_V[i]);
		myfree(interlaced_tab_U[i]);
	}
}



inline void AutoYUY2::Move_Full(const void *src_, void *dst_, const int32_t w,const int32_t h,
		int src_pitch,int dst_pitch)
{
	const uint8_t *src=(uint8_t *)src_;
	uint8_t *dst=(uint8_t *)dst_;

	if ((src_pitch==dst_pitch) && (abs(src_pitch)==w))
	{
		if (src_pitch<0)
		{
			src+=(h-1)*src_pitch;
			dst+=(h-1)*dst_pitch;
		}
		A_memcpy(dst,src,(size_t)h*(size_t)w);
	}
	else
	{
		for(int i=0; i<h; i++)
		{
			A_memcpy(dst,src,w);
			src+=src_pitch;
			dst+=dst_pitch;
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

// U Planar
	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dstUp,src_U,w_U);
		dstUp+=dst_pitch_UV;

		A_memcpy(dstUp,src_Un,w_U);
		dstUp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Unn,src_U,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Unn,src_U,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Un,src_Unnn,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Un,src_Unnn,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}

	for(int32_t i=4; i<h_4; i+=4)
	{
		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_U,src_Upp,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_U,src_Upp,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Up,src_Un,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Up,src_Un,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Unn,src_U,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Unn,src_U,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Un,src_Unnn,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Un,src_Unnn,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_U,src_Upp,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_U,src_Upp,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Up,src_Un,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Up,src_Un,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		A_memcpy(dstUp,src_U,w_U);
		dstUp+=dst_pitch_UV;

		A_memcpy(dstUp,src_Un,w_U);
		dstUp+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}


// V Planar
	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dstVp,src_V,w_V);
		dstVp+=dst_pitch_UV;

		A_memcpy(dstVp,src_Vn,w_V);
		dstVp+=dst_pitch_UV;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vnn,src_V,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vnn,src_V,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Vn,src_Vnnn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Vn,src_Vnnn,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=4; i<h_4; i+=4)
	{
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_V,src_Vpp,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_V,src_Vpp,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vp,src_Vn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vp,src_Vn,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vnn,src_V,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vnn,src_V,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Vn,src_Vnnn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Vn,src_Vnnn,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_V,src_Vpp,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_V,src_Vpp,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vp,src_Vn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vp,src_Vn,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		A_memcpy(dstVp,src_V,w_V);
		dstVp+=dst_pitch_UV;

		A_memcpy(dstVp,src_Vn,w_V);
		dstVp+=dst_pitch_UV;

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
	const uint16_t *lookup=lookup_Upscale;

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


// Planar U
	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dst_U,src_U,w_U);
		dst_U+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_U);
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUnn=src_Unn;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[*srcUnn++]+lookup[(uint16_t)(*srcU++)+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcUn=src_Un,*srcUnnn=src_Unnn;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcUn++)+512]+(uint16_t)(*srcUnnn++)+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}

	for(int32_t i=4; i<h_4; i+=4)
	{
		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUpp=src_Upp;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcU++)+512]+(uint16_t)(*srcUpp++)+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcUn=src_Un,*srcUp=src_Up;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[*srcUp++]+lookup[(uint16_t)(*srcUn++)+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcUnn=src_Unn,*srcU=src_U;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[*srcUnn++]+lookup[(uint16_t)(*srcU++)+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcUnnn=src_Unnn,*srcUn=src_Un;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcUn++)+512]+(uint16_t)(*srcUnnn++)+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUpp=src_Upp;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcU++)+512]+(uint16_t)(*srcUpp++)+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcUn=src_Un,*srcUp=src_Up;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[*srcUp++]+lookup[(uint16_t)(*srcUn++)+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		A_memcpy(dst_U,src_U,w_U);
		dst_U+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_U);
		dst_U+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}


// Planar V
	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dst_V,src_V,w_V);
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_V,src_Vn,w_V);
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcVnn=src_Vnn,*srcV=src_V;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[*srcVnn++]+lookup[(uint16_t)(*srcV++)+256]+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcVnnn=src_Vnnn,*srcVn=src_Vn;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcVn++)+512]+(uint16_t)(*srcVnnn++)+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=4; i<h_4; i+=4)
	{
		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVpp=src_Vpp;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcV++)+512]+(uint16_t)(*srcVpp++)+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcVn=src_Vn,*srcVp=src_Vp;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[*srcVp++]+lookup[(uint16_t)(*srcVn++)+256]+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcVnn=src_Vnn,*srcV=src_V;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[*srcVnn++]+lookup[(uint16_t)(*srcV++)+256]+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcVnnn=src_Vnnn,*srcVn=src_Vn;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcVn++)+512]+(uint16_t)(*srcVnnn++)+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVpp=src_Vpp;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcV++)+512]+(uint16_t)(*srcVpp++)+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcVn=src_Vn,*srcVp=src_Vp;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[*srcVp++]+lookup[(uint16_t)(*srcVn++)+256]+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_V,src_V,w_V);
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_V,src_Vn,w_V);
		dst_V+=dst_pitch_UV;

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

// Planar U
	for(int32_t i=0; i<2; i+=2)
	{
		A_memcpy(dstUp,src_U,w_U);
		dstUp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Un,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Un,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;
	}

	for(int32_t i=2; i<h_2; i+=2)
	{
		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Up,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Up,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Un,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Un,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;
	}

	for(int32_t i=h_2; i<dst_h; i+=2)
	{
		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Up,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Up,dstUp,w_U4);
		dstUp+=dst_pitch_UV;

		A_memcpy(dstUp,src_U,w_U);
		dstUp+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;
	}


// Planar V
	for(int32_t i=0; i<2; i+=2)
	{
		A_memcpy(dstVp,src_V,w_V);
		dstVp+=dst_pitch_UV;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vn,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		src_V+=src_pitchUV;
		src_Vp+=src_pitchUV;
		src_Vn+=src_pitchUV;
	}

	for(int32_t i=2; i<h_2; i+=2)
	{
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vp,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vp,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vn,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		src_V+=src_pitchUV;
		src_Vp+=src_pitchUV;
		src_Vn+=src_pitchUV;
	}

	for(int32_t i=h_2; i<dst_h; i+=2)
	{
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vp,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vp,dstVp,w_V4);
		dstVp+=dst_pitch_UV;

		A_memcpy(dstVp,src_V,w_V);
		dstVp+=dst_pitch_UV;

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
	const uint16_t *lookup=lookup_Upscale;

	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchUV;
	src_Un=src_U+src_pitchUV;
	src_Vp=src_V-src_pitchUV;
	src_Vn=src_V+src_pitchUV;
	dst_U=dstUp;
	dst_V=dstVp;

	Move_Full(srcYp,dstYp,dst_w,dst_h,src_pitch_Y,dst_pitch_Y);


// Planar U
	for(int32_t i=0; i<2; i+=2)
	{
		A_memcpy(dst_U,src_U,w_U);
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUn=src_Un;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUn++)+2)>>2);
		}
		dst_U+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;
	}

	for(int32_t i=2; i<h_2; i+=2)
	{
		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUp=src_Up;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUp++)+2)>>2);
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUn=src_Un;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUn++)+2)>>2);
		}
		dst_U+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;
	}

	for(int32_t i=h_2; i<dst_h; i+=2)
	{
		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUp=src_Up;

			for(int32_t j=0; j<w_U; j++)
				*dst++=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUp++)+2)>>2);
		}
		dst_U+=dst_pitch_UV;

		A_memcpy(dst_U,src_U,w_U);
		dst_U+=dst_pitch_UV;

		src_U+=src_pitchUV;
		src_Up+=src_pitchUV;
		src_Un+=src_pitchUV;
	}


// Planar V
	for(int32_t i=0; i<2; i+=2)
	{
		A_memcpy(dst_V,src_V,w_V);
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVn=src_Vn;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVn++)+2)>>2);
		}
		dst_V+=dst_pitch_UV;

		src_V+=src_pitchUV;
		src_Vp+=src_pitchUV;
		src_Vn+=src_pitchUV;
	}

	for(int32_t i=2; i<h_2; i+=2)
	{
		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVp=src_Vp;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVp++)+2)>>2);
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVn=src_Vn;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVn++)+2)>>2);
		}
		dst_V+=dst_pitch_UV;

		src_V+=src_pitchUV;
		src_Vp+=src_pitchUV;
		src_Vn+=src_pitchUV;
	}

	for(int32_t i=h_2; i<dst_h; i+=2)
	{
		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVp=src_Vp;

			for(int32_t j=0; j<w_V; j++)
				*dst++=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVp++)+2)>>2);
		}
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_V,src_V,w_V);
		dst_V+=dst_pitch_UV;

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
	const uint16_t *lookup=lookup_Upscale;

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

// Planar U	
	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dst_U,src_U,w_UV);
		dst_U+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_UV);
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUnn=src_Unn;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[*srcUnn++]+lookup[(uint16_t)(*srcU++)+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUn=src_Un,*srcUnn=src_Unn,*srcUnnn=src_Unnn;
			bool *itabu0=interlaced_tab_U[0],*itabu1=interlaced_tab_U[1];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcU-(int16_t)*srcUn)>=threshold_) &&
					(abs((int16_t)*srcUnn-(int16_t)*srcUn)>=threshold_) &&
					(((*srcU>*srcUn) && (*srcUnn>*srcUn)) ||
					((*srcU<*srcUn) && (*srcUnn<*srcUn)))))
					*itabu0++=true;
				else *itabu0++=false;
				if (((abs((int16_t)*srcUn-(int16_t)*srcUnn)>=threshold_) &&
					(abs((int16_t)*srcUnnn-(int16_t)*srcUnn)>=threshold_) &&
					(((*srcUn>*srcUnn) && (*srcUnnn>*srcUnn)) ||
					((*srcUn<*srcUnn) && (*srcUnnn<*srcUnn)))))
					*itabu1++=true;
				else *itabu1++=false;

				*dst++=(uint8_t)((lookup[(uint16_t)(*srcUn++)+512]+(uint16_t)(*srcUnnn++)+4)>>3);

				srcU++;
				srcUnn++;
			}
		}
		dst_U+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}

	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;

	for(int32_t i=4; i<h_4; i+=4)
	{
		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUp=src_Up,*srcUpp=src_Upp;
			const bool *itabu0=interlaced_tab_U[index_tab_0],*itabu1=interlaced_tab_U[index_tab_1];

			for(int32_t j=0; j<w_UV; j++)
			{
				if ((*itabu0++) || (*itabu1))
				{
					*dst++=(uint8_t)((lookup[(uint16_t)(*srcU++)+512]+(uint16_t)(*srcUpp++)+4)>>3);
					srcUp++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUp++)+2) >> 2);
					srcUpp++;
				}
				itabu1++;
			}
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUp=src_Up,*srcUn=src_Un,*srcUnn=src_Unn;
			const bool *itabu1=interlaced_tab_U[index_tab_1];
			bool *itabu2=interlaced_tab_U[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcU-(int16_t)*srcUn)>=threshold_) &&
					(abs((int16_t)*srcUnn-(int16_t)*srcUn)>=threshold_) &&
					(((*srcU>*srcUn) && (*srcUnn>*srcUn)) ||
					((*srcU<*srcUn) && (*srcUnn<*srcUn)))))
					*itabu2=true;
				else *itabu2=false;			

				// Upsample as needed.
				if ((*itabu1++) || (*itabu2))
				{
					*dst++=(uint8_t)((lookup[*srcUp++]+lookup[(uint16_t)(*srcUn++)+256]+4)>>3);
					srcU++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUn++)+2)>>2);
					srcUp++;
				}

				itabu2++;
				srcUnn++;
			}
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUn=src_Un,*srcUnn=src_Unn;
			const bool *itabu1=interlaced_tab_U[index_tab_1],*itabu2=interlaced_tab_U[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if ((*itabu1++) || (*itabu2))
				{
					*dst++=(uint8_t)((lookup[*srcUnn++]+lookup[(uint16_t)(*srcU++)+256]+4)>>3);
					srcUn++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcUn++]+(uint16_t)(*srcU++)+2)>>2);
					srcUnn++;
				}
				itabu2++;
			}
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcUn=src_Un,*srcUnn=src_Unn,*srcUnnn=src_Unnn;
			bool *itabu0=interlaced_tab_U[index_tab_0];
			const bool *itabu2=interlaced_tab_U[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcUn-(int16_t)*srcUnn)>=threshold_) &&
					(abs((int16_t)*srcUnnn-(int16_t)*srcUnn)>=threshold_) &&
					(((*srcUn>*srcUnn) && (*srcUnnn>*srcUnn)) ||
					((*srcUn<*srcUnn) && (*srcUnnn<*srcUnn)))))
					*itabu0=true;
				else *itabu0=false;

				// Upsample as needed.
				if ((*itabu0++) || (*itabu2))
				{
					*dst++=(uint8_t)((lookup[(uint16_t)(*srcUn++)+512]+(uint16_t)(*srcUnnn++)+4)>>3);
					srcUnn++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcUn++]+(uint16_t)(*srcUnn++)+2)>>2);
					srcUnnn++;
				}
				itabu2++;
			}
		}
		dst_U+=dst_pitch_UV;

		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUpp=src_Upp;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcU++)+512]+(uint16_t)(*srcUpp++)+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcUn=src_Un,*srcUp=src_Up;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[*srcUp++]+lookup[(uint16_t)(*srcUn++)+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		A_memcpy(dst_U,src_U,w_UV);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_UV);
		dst_U+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}


// Planar V
	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dst_V,src_V,w_UV);
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_V,src_Vn,w_UV);
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVnn=src_Vnn;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[*srcVnn++]+lookup[(uint16_t)(*srcV++)+256]+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVn=src_Vn,*srcVnn=src_Vnn,*srcVnnn=src_Vnnn;
			bool *itabv0=interlaced_tab_V[0],*itabv1=interlaced_tab_V[1];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcV-(int16_t)*srcVn)>=threshold_) &&
					(abs((int16_t)*srcVnn-(int16_t)*srcVn)>=threshold_) &&
					(((*srcV>*srcVn) && (*srcVnn>*srcVn)) ||
					((*srcV<*srcVn) && (*srcVnn<*srcVn)))))
					*itabv0++=true;
				else *itabv0++=false;
				if (((abs((int16_t)*srcVn-(int16_t)*srcVnn)>=threshold_) &&
					(abs((int16_t)*srcVnnn-(int16_t)*srcVnn)>=threshold_) &&
					(((*srcVn>*srcVnn) && (*srcVnnn>*srcVnn)) ||
					((*srcVn<*srcVnn) && (*srcVnnn<*srcVnn)))))
					*itabv1++=true;
				else *itabv1++=false;

				*dst++=(uint8_t)((lookup[(uint16_t)(*srcVn++)+512]+(uint16_t)(*srcVnnn++)+4)>>3);

				srcVnn++;
				srcV++;
			}
		}
		dst_V+=dst_pitch_UV;

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
		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVp=src_Vp,*srcVpp=src_Vpp;
			const bool *itabv0=interlaced_tab_V[index_tab_0],*itabv1=interlaced_tab_V[index_tab_1];

			for(int32_t j=0; j<w_UV; j++)
			{
				// Upsample as needed.
				if ((*itabv0++) || (*itabv1))
				{
					*dst++=(uint8_t)((lookup[(uint16_t)(*srcV++)+512]+(uint16_t)(*srcVpp++)+4)>>3);
					srcVp++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVp++)+2) >> 2);
					srcVpp++;
				}
				itabv1++;
			}
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVp=src_Vp,*srcVn=src_Vn,*srcVnn=src_Vnn;
			const bool *itabv1=interlaced_tab_V[index_tab_1];
			bool *itabv2=interlaced_tab_V[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcV-(int16_t)*srcVn)>=threshold_) &&
					(abs((int16_t)*srcVnn-(int16_t)*srcVn)>=threshold_) &&
					(((*srcV>*srcVn) && (*srcVnn>*srcVn)) ||
					((*srcV<*srcVn) && (*srcVnn<*srcVn)))))
					*itabv2=true;
				else *itabv2=false;			

				// Upsample as needed.
				if ((*itabv1++) || (*itabv2))
				{
					*dst++=(uint8_t)((lookup[*srcVp++]+lookup[(uint16_t)(*srcVn++)+256]+4)>>3);
					srcV++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVn++)+2)>>2);
					srcVp++;
				}
				itabv2++;

				srcVnn++;
			}
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVn=src_Vn,*srcVnn=src_Vnn;
			const bool *itabv1=interlaced_tab_V[index_tab_1],*itabv2=interlaced_tab_V[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				// Upsample as needed.
				if ((*itabv1++) || (*itabv2))
				{
					*dst++=(uint8_t)((lookup[*srcVnn++]+lookup[(uint16_t)(*srcV++)+256]+4)>>3);
					srcVn++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcVn++]+(uint16_t)(*srcV++)+2)>>2);
					srcVnn++;
				}
				itabv2++;
			}
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcVn=src_Vn,*srcVnn=src_Vnn,*srcVnnn=src_Vnnn;
			bool *itabv0=interlaced_tab_V[index_tab_0];
			const bool *itabv2=interlaced_tab_V[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcVn-(int16_t)*srcVnn)>=threshold_) &&
					(abs((int16_t)*srcVnnn-(int16_t)*srcVnn)>=threshold_) &&
					(((*srcVn>*srcVnn) && (*srcVnnn>*srcVnn)) ||
					((*srcVn<*srcVnn) && (*srcVnnn<*srcVnn)))))
					*itabv0=true;
				else *itabv0=false;

				// Upsample as needed.
				if ((*itabv0++) || (*itabv2))
				{
					*dst++=(uint8_t)((lookup[(uint16_t)(*srcVn++)+512]+(uint16_t)(*srcVnnn++)+4)>>3);
					srcVnn++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcVn++]+(uint16_t)(*srcVnn++)+2)>>2);
					srcVnnn++;
				}
				itabv2++;
			}
		}
		dst_V+=dst_pitch_UV;

		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVpp=src_Vpp;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcV++)+512]+(uint16_t)(*srcVpp++)+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcVn=src_Vn,*srcVp=src_Vp;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[*srcVp++]+lookup[(uint16_t)(*srcVn++)+256]+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_V,src_V,w_UV);
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_V,src_Vn,w_UV);
		dst_V+=dst_pitch_UV;

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
	const uint16_t *lookup=lookup_Upscale;

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

// Planar U	
	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dst_U,src_U,w_UV);
		dst_U+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_UV);
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUnn=src_Unn;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[*srcUnn++]+lookup[(uint16_t)(*srcU++)+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUn=src_Un,*srcUnn=src_Unn,*srcUnnn=src_Unnn;
			bool *itabu0=interlaced_tab_U[0],*itabu1=interlaced_tab_U[1];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcU-(int16_t)*srcUn)>=threshold_) &&
					(abs((int16_t)*srcUnn-(int16_t)*srcUn)>=threshold_) &&
					(((*srcU>*srcUn) && (*srcUnn>*srcUn)) ||
					((*srcU<*srcUn) && (*srcUnn<*srcUn)))))
					*itabu0++=true;
				else *itabu0++=false;
				if (((abs((int16_t)*srcUn-(int16_t)*srcUnn)>=threshold_) &&
					(abs((int16_t)*srcUnnn-(int16_t)*srcUnn)>=threshold_) &&
					(((*srcUn>*srcUnn) && (*srcUnnn>*srcUnn)) ||
					((*srcUn<*srcUnn) && (*srcUnnn<*srcUnn)))))
					*itabu1++=true;
				else *itabu1++=false;

				*dst++=(uint8_t)((lookup[(uint16_t)(*srcUn++)+512]+(uint16_t)(*srcUnnn++)+4)>>3);

				srcU++;
				srcUnn++;
			}
		}
		dst_U+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}

	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;

	for(int32_t i=4; i<h_4; i+=4)
	{
		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUp=src_Up;
			const bool *itabu0=interlaced_tab_U[index_tab_0],*itabu1=interlaced_tab_U[index_tab_1];

			for(int32_t j=0; j<w_UV; j++)
			{
				if ((*itabu0++) || (*itabu1))
				{
					*dst++=239;
					srcU++;
					srcUp++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUp++)+2) >> 2);
				}
				itabu1++;
			}
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUn=src_Un,*srcUnn=src_Unn;
			const bool *itabu1=interlaced_tab_U[index_tab_1];
			bool *itabu2=interlaced_tab_U[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcU-(int16_t)*srcUn)>=threshold_) &&
					(abs((int16_t)*srcUnn-(int16_t)*srcUn)>=threshold_) &&
					(((*srcU>*srcUn) && (*srcUnn>*srcUn)) ||
					((*srcU<*srcUn) && (*srcUnn<*srcUn)))))
					*itabu2=true;
				else *itabu2=false;			

				// Upsample as needed.
				if ((*itabu1++) || (*itabu2))
				{
					*dst++=239;
					srcU++;
					srcUn++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUn++)+2)>>2);
				}
				itabu2++;

				srcUnn++;
			}
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUn=src_Un;
			const bool *itabu1=interlaced_tab_U[index_tab_1],*itabu2=interlaced_tab_U[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if ((*itabu1++) || (*itabu2))
				{
					*dst++=239;
					srcU++;
					srcUn++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcUn++]+(uint16_t)(*srcU++)+2)>>2);
				}
				itabu2++;
			}
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcUn=src_Un,*srcUnn=src_Unn,*srcUnnn=src_Unnn;
			bool *itabu0=interlaced_tab_U[index_tab_0];
			const bool *itabu2=interlaced_tab_U[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcUn-(int16_t)*srcUnn)>=threshold_) &&
					(abs((int16_t)*srcUnnn-(int16_t)*srcUnn)>=threshold_) &&
					(((*srcUn>*srcUnn) && (*srcUnnn>*srcUnn)) ||
					((*srcUn<*srcUnn) && (*srcUnnn<*srcUnn)))))
					*itabu0=true;
				else *itabu0=false;

				// Upsample as needed.
				if ((*itabu0++) || (*itabu2))
				{
					*dst++=239;
					srcUn++;
					srcUnn++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcUn++]+(uint16_t)(*srcUnn++)+2)>>2);
				}
				itabu2++;

				srcUnnn++;
			}
		}
		dst_U+=dst_pitch_UV;

		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		{
			uint8_t *dst=dst_U;
			const uint8_t *srcU=src_U,*srcUpp=src_Upp;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcU++)+512]+(uint16_t)(*srcUpp++)+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		{
			uint8_t *dst=dst_U;
			const uint8_t *srcUn=src_Un,*srcUp=src_Up;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[*srcUp++]+lookup[(uint16_t)(*srcUn++)+256]+4)>>3);
		}
		dst_U+=dst_pitch_UV;

		A_memcpy(dst_U,src_U,w_UV);
		dst_U+=dst_pitch_UV;
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_U,src_Un,w_UV);
		dst_U+=dst_pitch_UV;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}


// Planar V
	for(int32_t i=0; i<4; i+=4)
	{
		A_memcpy(dst_V,src_V,w_UV);
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_V,src_Vn,w_UV);
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVnn=src_Vnn;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[*srcVnn++]+lookup[(uint16_t)(*srcV++)+256]+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVn=src_Vn,*srcVnn=src_Vnn,*srcVnnn=src_Vnnn;
			bool *itabv0=interlaced_tab_V[0],*itabv1=interlaced_tab_V[1];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcV-(int16_t)*srcVn)>=threshold_) &&
					(abs((int16_t)*srcVnn-(int16_t)*srcVn)>=threshold_) &&
					(((*srcV>*srcVn) && (*srcVnn>*srcVn)) ||
					((*srcV<*srcVn) && (*srcVnn<*srcVn)))))
					*itabv0++=true;
				else *itabv0++=false;
				if (((abs((int16_t)*srcVn-(int16_t)*srcVnn)>=threshold_) &&
					(abs((int16_t)*srcVnnn-(int16_t)*srcVnn)>=threshold_) &&
					(((*srcVn>*srcVnn) && (*srcVnnn>*srcVnn)) ||
					((*srcVn<*srcVnn) && (*srcVnnn<*srcVnn)))))
					*itabv1++=true;
				else *itabv1++=false;

				*dst++=(uint8_t)((lookup[(uint16_t)(*srcVn++)+512]+(uint16_t)(*srcVnnn++)+4)>>3);

				srcVnn++;
				srcV++;
			}
		}
		dst_V+=dst_pitch_UV;

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
		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVp=src_Vp;
			const bool *itabv0=interlaced_tab_V[index_tab_0],*itabv1=interlaced_tab_V[index_tab_1];

			for(int32_t j=0; j<w_UV; j++)
			{
				// Upsample as needed.
				if ((*itabv0++) || (*itabv1))
				{
					*dst++=239;
					srcV++;
					srcVp++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVp++)+2) >> 2);
				}
				itabv1++;
			}
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVn=src_Vn,*srcVnn=src_Vnn;
			const bool *itabv1=interlaced_tab_V[index_tab_1];
			bool *itabv2=interlaced_tab_V[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcV-(int16_t)*srcVn)>=threshold_) &&
					(abs((int16_t)*srcVnn-(int16_t)*srcVn)>=threshold_) &&
					(((*srcV>*srcVn) && (*srcVnn>*srcVn)) ||
					((*srcV<*srcVn) && (*srcVnn<*srcVn)))))
					*itabv2=true;
				else *itabv2=false;			

				// Upsample as needed.
				if ((*itabv1++) || (*itabv2))
				{
					*dst++=239;
					srcV++;
					srcVn++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVn++)+2)>>2);
				}
				itabv2++;

				srcVnn++;
			}
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVn=src_Vn;
			const bool *itabv1=interlaced_tab_V[index_tab_1],*itabv2=interlaced_tab_V[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				// Upsample as needed.
				if ((*itabv1++) || (*itabv2))
				{
					*dst++=239;
					srcV++;
					srcVn++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcVn++]+(uint16_t)(*srcV++)+2)>>2);
				}
				itabv2++;
			}
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcVn=src_Vn,*srcVnn=src_Vnn,*srcVnnn=src_Vnnn;
			bool *itabv0=interlaced_tab_V[index_tab_0];
			const bool *itabv2=interlaced_tab_V[index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)*srcVn-(int16_t)*srcVnn)>=threshold_) &&
					(abs((int16_t)*srcVnnn-(int16_t)*srcVnn)>=threshold_) &&
					(((*srcVn>*srcVnn) && (*srcVnnn>*srcVnn)) ||
					((*srcVn<*srcVnn) && (*srcVnnn<*srcVnn)))))
					*itabv0=true;
				else *itabv0=false;

				// Upsample as needed.
				if ((*itabv0++) || (*itabv2))
				{
					*dst++=239;
					srcVn++;
					srcVnn++;
				}
				else
				{
					*dst++=(uint8_t)((lookup[*srcVn++]+(uint16_t)(*srcVnn++)+2)>>2);
				}
				itabv2++;

				srcVnnn++;
			}
		}
		dst_V+=dst_pitch_UV;

		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	for(int32_t i=h_4; i<dst_h; i+=4)
	{
		{
			uint8_t *dst=dst_V;
			const uint8_t *srcV=src_V,*srcVpp=src_Vpp;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[(uint16_t)(*srcV++)+512]+(uint16_t)(*srcVpp++)+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		{
			uint8_t *dst=dst_V;
			const uint8_t *srcVn=src_Vn,*srcVp=src_Vp;

			for(int32_t j=0; j<w_UV; j++)
				*dst++=(uint8_t)((lookup[*srcVp++]+lookup[(uint16_t)(*srcVn++)+256]+4)>>3);
		}
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_V,src_V,w_UV);
		dst_V+=dst_pitch_UV;

		A_memcpy(dst_V,src_Vn,w_UV);
		dst_V+=dst_pitch_UV;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}
}



void AutoYUY2::Convert_Progressive_YUY2(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up, *src_Un;
	const uint8_t *src_V,*src_Vp, *src_Vn;
	const int dst_h_2=dst_h-2;
	const uint16_t *lookup=lookup_Upscale;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchUV;
	src_Un = srcUp + src_pitchUV;
	src_V = srcVp;
	src_Vp = srcVp - src_pitchUV;
	src_Vn = srcVp + src_pitchUV;

	for (int32_t y=0; y<2; y+=2)
	{

		JPSDR_AutoYUY2_1(src_Y,src_U,src_V,dstp,dst_w>>2);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUn=src_Un;
			const uint8_t *srcV=src_V,*srcVn=src_Vn;

			for (int32_t x=0; x<dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUn++)+2) >> 2);
				(dst++)->v=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVn++)+2) >> 2);
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV;
		src_Up += src_pitchUV;
		src_Un += src_pitchUV;
		src_V += src_pitchUV;
		src_Vp += src_pitchUV;
		src_Vn += src_pitchUV;
	}

	for (int32_t y = 2; y < dst_h_2; y+=2)
	{
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUp=src_Up;
			const uint8_t *srcV=src_V,*srcVp=src_Vp;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUp++)+2) >> 2);
				(dst++)->v=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVp++)+2) >> 2);
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUn=src_Un;
			const uint8_t *srcV=src_V,*srcVn=src_Vn;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUn++)+2) >> 2);
				(dst++)->v=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVn++)+2) >> 2);
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;
		
		src_U += src_pitchUV;
		src_Up += src_pitchUV;
		src_Un += src_pitchUV;
		src_V += src_pitchUV;
		src_Vp += src_pitchUV;
		src_Vn += src_pitchUV;	
	}

	for (int32_t y = dst_h_2; y < dst_h; y+=2)
	{
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUp=src_Up;
			const uint8_t *srcV=src_V,*srcVp=src_Vp;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUp++)+2) >> 2);
				(dst++)->v=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVp++)+2) >> 2);
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		JPSDR_AutoYUY2_1(src_Y,src_U,src_V,dstp,dst_w>>2);
		dstp += dst_pitch;
		src_Y += src_pitchY;
		
		src_U += src_pitchUV;
		src_Up += src_pitchUV;
		src_Un += src_pitchUV;
		src_V += src_pitchUV;
		src_Vp += src_pitchUV;
		src_Vn += src_pitchUV;	
	}
}


void AutoYUY2::Convert_Progressive_YUY2_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up, *src_Un;
	const uint8_t *src_V,*src_Vp, *src_Vn;
	const int dst_h_2=dst_h-2,w_8=dst_w>>3;

	bool _align=false;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchUV;
	src_Un = srcUp + src_pitchUV;
	src_V = srcVp;
	src_Vp = srcVp - src_pitchUV;
	src_Vn = srcVp + src_pitchUV;

	if ((((size_t)dstp & 0x0F)==0) && ((abs(dst_pitch) & 0x0F)==0)) _align=true;

	for (int32_t y=0; y<2; y+=2)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_1b(src_Y,src_U,src_V,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(src_Y,src_U,src_V,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_4b(src_Y,src_U,src_Un,src_V,src_Vn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_4(src_Y,src_U,src_Un,src_V,src_Vn,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV;
		src_Up += src_pitchUV;
		src_Un += src_pitchUV;
		src_V += src_pitchUV;
		src_Vp += src_pitchUV;
		src_Vn += src_pitchUV;
	}

	for (int32_t y = 2; y < dst_h_2; y+=2)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_4b(src_Y,src_U,src_Up,src_V,src_Vp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_4(src_Y,src_U,src_Up,src_V,src_Vp,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_4b(src_Y,src_U,src_Un,src_V,src_Vn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_4(src_Y,src_U,src_Un,src_V,src_Vn,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;
		
		src_U += src_pitchUV;
		src_Up += src_pitchUV;
		src_Un += src_pitchUV;
		src_V += src_pitchUV;
		src_Vp += src_pitchUV;
		src_Vn += src_pitchUV;	
	}

	for (int32_t y = dst_h_2; y < dst_h; y+=2)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_4b(src_Y,src_U,src_Up,src_V,src_Vp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_4(src_Y,src_U,src_Up,src_V,src_Vp,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_1b(src_Y,src_U,src_V,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(src_Y,src_U,src_V,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;
		
		src_U += src_pitchUV;
		src_Up += src_pitchUV;
		src_Un += src_pitchUV;
		src_V += src_pitchUV;
		src_Vp += src_pitchUV;
		src_Vn += src_pitchUV;	
	}
}



void AutoYUY2::Convert_Interlaced_YUY2(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up, *src_Un, *src_Unn, *src_Unnn,*src_Upp;
	const uint8_t *src_V,*src_Vp, *src_Vn, *src_Vnn, *src_Vnnn,*src_Vpp;
	int src_pitchUV_2;
	const int dst_h_4=dst_h-4;
	const uint16_t *lookup=lookup_Upscale;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchUV;
	src_Upp = srcUp - (2*src_pitchUV);
	src_Un = srcUp + src_pitchUV;
	src_Unn = srcUp + (2 * src_pitchUV);
	src_Unnn = srcUp + (3 * src_pitchUV);
	src_V = srcVp;
	src_Vp = srcVp - src_pitchUV;
	src_Vpp = srcVp - (2*src_pitchUV);
	src_Vn = srcVp + src_pitchUV;
	src_Vnn = srcVp + (2 * src_pitchUV);
	src_Vnnn = srcVp + (3 * src_pitchUV);
	
	src_pitchUV_2=src_pitchUV << 1;

	for (int32_t y=0; y<4; y+=4)
	{

		JPSDR_AutoYUY2_1(src_Y,src_U,src_V,dstp,dst_w>>2);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		JPSDR_AutoYUY2_1(src_Y,src_Un,src_Vn,dstp,dst_w>>2);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUnn=src_Unn;
			const uint8_t *srcV=src_V,*srcVnn=src_Vnn;

			for (int32_t x=0; x<dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcUnn++]+lookup[(uint16_t)(*srcU++)+256]+4)>>3);
				(dst++)->v=(uint8_t)((lookup[*srcVnn++]+lookup[(uint16_t)(*srcV++)+256]+4)>>3);
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUn=src_Un,*srcUnnn=src_Unnn;
			const uint8_t *srcVn=src_Vn,*srcVnnn=src_Vnnn;

			for (int32_t x=0; x<dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[(uint16_t)(*srcUn++)+512]+(uint16_t)(*srcUnnn++)+4)>>3);
				(dst++)->v=(uint8_t)((lookup[(uint16_t)(*srcVn++)+512]+(uint16_t)(*srcVnnn++)+4)>>3);
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;
	}

	for (int32_t y = 4; y < dst_h_4; y+=4)
	{
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUpp=src_Upp;
			const uint8_t *srcV=src_V,*srcVpp=src_Vpp;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[(uint16_t)(*srcU++)+512]+(uint16_t)(*srcUpp++)+4) >> 3);
				(dst++)->v=(uint8_t)((lookup[(uint16_t)(*srcV++)+512]+(uint16_t)(*srcVpp++)+4) >> 3);
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUn=src_Un,*srcUp=src_Up;
			const uint8_t *srcVn=src_Vn,*srcVp=src_Vp;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcUp++]+lookup[(uint16_t)(*srcUn++)+256]+4) >> 3);
				(dst++)->v=(uint8_t)((lookup[*srcVp++]+lookup[(uint16_t)(*srcVn++)+256]+4) >> 3);
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;
		
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUnn=src_Unn,*srcU=src_U;
			const uint8_t *srcVnn=src_Vnn,*srcV=src_V;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcUnn++]+lookup[(uint16_t)(*srcU++)+256]+4) >> 3);
				(dst++)->v=(uint8_t)((lookup[*srcVnn++]+lookup[(uint16_t)(*srcV++)+256]+4) >> 3);
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUnnn=src_Unnn,*srcUn=src_Un;
			const uint8_t *srcVnnn=src_Vnnn,*srcVn=src_Vn;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[(uint16_t)(*srcUn++)+512]+(uint16_t)(*srcUnnn++)+4) >> 3);
				(dst++)->v=(uint8_t)((lookup[(uint16_t)(*srcVn++)+512]+(uint16_t)(*srcVnnn++)+4) >> 3);
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;	
	}

	for (int32_t y = dst_h_4; y < dst_h; y+=4)
	{
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUpp=src_Upp;
			const uint8_t *srcV=src_V,*srcVpp=src_Vpp;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[(uint16_t)(*srcU++)+512]+(uint16_t)(*srcUpp++)+4) >> 3);
				(dst++)->v=(uint8_t)((lookup[(uint16_t)(*srcV++)+512]+(uint16_t)(*srcVpp++)+4) >> 3);
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUn=src_Un,*srcUp=src_Up;
			const uint8_t *srcVn=src_Vn,*srcVp=src_Vp;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcUp++]+lookup[(uint16_t)(*srcUn++)+256]+4) >> 3);
				(dst++)->v=(uint8_t)((lookup[*srcVp++]+lookup[(uint16_t)(*srcVn++)+256]+4) >> 3);
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;
		
		JPSDR_AutoYUY2_1(src_Y,src_U,src_V,dstp,dst_w>>2);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		JPSDR_AutoYUY2_1(src_Y,src_Un,src_Vn,dstp,dst_w>>2);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;	
	}
}


void AutoYUY2::Convert_Interlaced_YUY2_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up, *src_Un, *src_Unn, *src_Unnn,*src_Upp;
	const uint8_t *src_V,*src_Vp, *src_Vn, *src_Vnn, *src_Vnnn,*src_Vpp;
	int src_pitchUV_2;
	const int dst_h_4=dst_h-4,w_8=dst_w>>3;

	bool _align=false;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchUV;
	src_Upp = srcUp - (2*src_pitchUV);
	src_Un = srcUp + src_pitchUV;
	src_Unn = srcUp + (2 * src_pitchUV);
	src_Unnn = srcUp + (3 * src_pitchUV);
	src_V = srcVp;
	src_Vp = srcVp - src_pitchUV;
	src_Vpp = srcVp - (2*src_pitchUV);
	src_Vn = srcVp + src_pitchUV;
	src_Vnn = srcVp + (2 * src_pitchUV);
	src_Vnnn = srcVp + (3 * src_pitchUV);

	src_pitchUV_2=src_pitchUV << 1;

	if ((((size_t)dstp & 0x0F)==0) && ((abs(dst_pitch) & 0x0F)==0)) _align=true;

	for (int32_t y=0; y<4; y+=4)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_1b(src_Y,src_U,src_V,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(src_Y,src_U,src_V,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_1b(src_Y,src_Un,src_Vn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(src_Y,src_Un,src_Vn,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_2b(src_Y,src_Unn,srcUp,src_Vnn,srcVp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_2(src_Y,src_Unn,srcUp,src_Vnn,srcVp,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_3b(src_Y,src_Un,src_Unnn,src_Vn,src_Vnnn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_3(src_Y,src_Un,src_Unnn,src_Vn,src_Vnnn,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;
	}

	for (int32_t y = 4; y < dst_h_4; y+=4)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_3b(src_Y,src_U,src_Upp,src_V,src_Vpp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_3(src_Y,src_U,src_Upp,src_V,src_Vpp,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_2b(src_Y,src_Up,src_Un,src_Vp,src_Vn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_2(src_Y,src_Up,src_Un,src_Vp,src_Vn,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_2b(src_Y,src_Unn,srcUp,src_Vnn,srcVp,dstp,w_8);
		JPSDR_AutoYUY2_SSE2_2(src_Y,src_Unn,srcUp,src_Vnn,srcVp,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_3b(src_Y,src_Un,src_Unnn,src_Vn,src_Vnnn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_3(src_Y,src_Un,src_Unnn,src_Vn,src_Vnnn,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;	
	}

	for (int32_t y = dst_h_4; y < dst_h; y+=4)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_3b(src_Y,src_U,src_Upp,src_V,src_Vpp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_3(src_Y,src_U,src_Upp,src_V,src_Vpp,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_2b(src_Y,src_Up,src_Un,src_Vp,src_Vn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_2(src_Y,src_Up,src_Un,src_Vp,src_Vn,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_1b(src_Y,src_U,src_V,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(src_Y,src_U,src_V,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_1b(src_Y,src_Un,src_Vn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_1(src_Y,src_Un,src_Vn,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;	
	}
}



void AutoYUY2::Convert_Automatic_YUY2(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY,int src_pitchUV)
{
	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up,*src_Un,*src_Unn,*src_Unnn,*src_Upp;
	const uint8_t *src_V,*src_Vp,*src_Vn,*src_Vnn,*src_Vnnn,*src_Vpp;
	int src_pitchUV_2;
	uint8_t index_tab_0,index_tab_1,index_tab_2;
	const int dst_h_4=dst_h-4;
	const uint16_t *lookup=lookup_Upscale;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchUV;
	src_Upp = srcUp - (2*src_pitchUV);
	src_Un = srcUp + src_pitchUV;
	src_Unn = srcUp + (2 * src_pitchUV);
	src_Unnn = srcUp + (3 * src_pitchUV);
	src_V = srcVp;
	src_Vp = srcVp - src_pitchUV;
	src_Vpp = srcVp - (2*src_pitchUV);
	src_Vn = srcVp + src_pitchUV;
	src_Vnn = srcVp + (2 * src_pitchUV);
	src_Vnnn = srcVp + (3 * src_pitchUV);
	
	src_pitchUV_2=src_pitchUV << 1;

	for (int32_t y=0; y<4; y+=4)
	{
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U;
			const uint8_t *srcV=src_V;

			for (int32_t x=0; x<dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=*srcU++;
				(dst++)->v=*srcV++;
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUn=src_Un;
			const uint8_t *srcVn=src_Vn;

			for (int32_t x=0; x<dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=*srcUn++;
				(dst++)->v=*srcVn++;
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUnn=src_Unn;
			const uint8_t *srcV=src_V,*srcVnn=src_Vnn;

			for (int32_t x=0; x<dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcUnn++]+lookup[(uint16_t)(*srcU++)+256]+4)>>3);
				(dst++)->v=(uint8_t)((lookup[*srcVnn++]+lookup[(uint16_t)(*srcV++)+256]+4)>>3);
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUn=src_Un,*srcUnn=src_Unn,*srcUnnn=src_Unnn;
			const uint8_t *srcV=src_V,*srcVn=src_Vn,*srcVnn=src_Vnn,*srcVnnn=src_Vnnn;
			bool *itabu0=interlaced_tab_U[0],*itabu1=interlaced_tab_U[1];
			bool *itabv0=interlaced_tab_V[0],*itabv1=interlaced_tab_V[1];

			for (int32_t x=0; x<dst_w; x+=4)
			{	
				if (((abs((int16_t)*srcU-(int16_t)*srcUn)>=threshold) &&
					(abs((int16_t)*srcUnn-(int16_t)*srcUn)>=threshold) &&
					(((*srcU>*srcUn) && (*srcUnn>*srcUn)) ||
					((*srcU<*srcUn) && (*srcUnn<*srcUn)))))
					*itabu0++=true;
				else *itabu0++=false;
				if (((abs((int16_t)*srcUn-(int16_t)*srcUnn)>=threshold) &&
					(abs((int16_t)*srcUnnn-(int16_t)*srcUnn)>=threshold) &&
					(((*srcUn>*srcUnn) && (*srcUnnn>*srcUnn)) ||
					((*srcUn<*srcUnn) && (*srcUnnn<*srcUnn)))))
					*itabu1++=true;
				else *itabu1++=false;
				if (((abs((int16_t)*srcV-(int16_t)*srcVn)>=threshold) &&
					(abs((int16_t)*srcVnn-(int16_t)*srcVn)>=threshold) &&
					(((*srcV>*srcVn) && (*srcVnn>*srcVn)) ||
					((*srcV<*srcVn) && (*srcVnn<*srcVn)))))
					*itabv0++=true;
				else *itabv0++=false;
				if (((abs((int16_t)*srcVn-(int16_t)*srcVnn)>=threshold) &&
					(abs((int16_t)*srcVnnn-(int16_t)*srcVnn)>=threshold) &&
					(((*srcVn>*srcVnn) && (*srcVnnn>*srcVnn)) ||
					((*srcVn<*srcVnn) && (*srcVnnn<*srcVnn)))))
					*itabv1++=true;
				else *itabv1++=false;
			
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[(uint16_t)(*srcUn++)+512]+(uint16_t)(*srcUnnn++)+4)>>3);
				(dst++)->v=(uint8_t)((lookup[(uint16_t)(*srcVn++)+512]+(uint16_t)(*srcVnnn++)+4)>>3);

				srcU++;
				srcUnn++;
				srcV++;
				srcVnn++;
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;
	}
	
	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;
	for (int32_t y = 4; y < dst_h_4; y+=4)
	{
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUp=src_Up,*srcUpp=src_Upp;
			const uint8_t *srcV=src_V,*srcVp=src_Vp,*srcVpp=src_Vpp;
			const bool *itabu0=interlaced_tab_U[index_tab_0],*itabu1=interlaced_tab_U[index_tab_1];
			const bool *itabv0=interlaced_tab_V[index_tab_0],*itabv1=interlaced_tab_V[index_tab_1];

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;

				// Upsample as needed.
				if ((*itabu0++) || (*itabu1))
				{
					dst->u=(uint8_t)((lookup[(uint16_t)(*srcU++)+512]+(uint16_t)(*srcUpp++)+4) >> 3);
					srcUp++;
				}
				else
				{
					dst->u=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUp++)+2) >> 2);
					srcUpp++;
				}
				itabu1++;

				// Upsample as needed.
				if ((*itabv0++) || (*itabv1))
				{
					(dst++)->v=(uint8_t)((lookup[(uint16_t)(*srcV++)+512]+(uint16_t)(*srcVpp++)+4) >> 3);
					srcVp++;
				}
				else
				{
					(dst++)->v=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVp++)+2) >> 2);
					srcVpp++;
				}
				itabv1++;
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUp=src_Up,*srcUn=src_Un,*srcUnn=src_Unn;
			const uint8_t *srcV=src_V,*srcVp=src_Vp,*srcVn=src_Vn,*srcVnn=src_Vnn;
			const bool *itabu1=interlaced_tab_U[index_tab_1];
			const bool *itabv1=interlaced_tab_V[index_tab_1];
			bool *itabu2=interlaced_tab_U[index_tab_2];
			bool *itabv2=interlaced_tab_V[index_tab_2];

			for (int32_t x = 0; x < dst_w; x+=4)
			{			
				if (((abs((int16_t)*srcU-(int16_t)*srcUn)>=threshold) &&
					(abs((int16_t)*srcUnn-(int16_t)*srcUn)>=threshold) &&
					(((*srcU>*srcUn) && (*srcUnn>*srcUn)) ||
					((*srcU<*srcUn) && (*srcUnn<*srcUn)))))
					*itabu2=true;
				else *itabu2=false;			
				if (((abs((int16_t)*srcV-(int16_t)*srcVn)>=threshold) &&
					(abs((int16_t)*srcVnn-(int16_t)*srcVn)>=threshold) &&
					(((*srcV>*srcVn) && (*srcVnn>*srcVn)) ||
					((*srcV<*srcVn) && (*srcVnn<*srcVn)))))
					*itabv2=true;
				else *itabv2=false;			

				dst->y1=*srcY++;
				dst->y2=*srcY++;

				// Upsample as needed.
				if ((*itabu1++) || (*itabu2))
				{
					dst->u=(uint8_t)((lookup[*srcUp++]+lookup[(uint16_t)(*srcUn++)+256]+4) >> 3);
					srcU++;
				}
				else
				{
					dst->u=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUn++)+2) >> 2);
					srcUp++;
				}
				itabu2++;

				// Upsample as needed.
				if ((*itabv1++) || (*itabv2))
				{
					(dst++)->v=(uint8_t)((lookup[*srcVp++]+lookup[(uint16_t)(*srcVn++)+256]+4) >> 3);
					srcV++;
				}
				else
				{
					(dst++)->v=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVn++)+2) >> 2);
					srcVp++;
				}
				itabv2++;

				srcUnn++;
				srcVnn++;				
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;
		
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUn=src_Un,*srcUnn=src_Unn;
			const uint8_t *srcV=src_V,*srcVn=src_Vn,*srcVnn=src_Vnn;
			const bool *itabu1=interlaced_tab_U[index_tab_1],*itabu2=interlaced_tab_U[index_tab_2];
			const bool *itabv1=interlaced_tab_V[index_tab_1],*itabv2=interlaced_tab_V[index_tab_2];

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;

				// Upsample as needed.
				if ((*itabu1++) || (*itabu2))
				{
					dst->u=(uint8_t)((lookup[*srcUnn++]+lookup[(uint16_t)(*srcU++)+256]+4) >> 3);
					srcUn++;
				}
				else
				{
					dst->u=(uint8_t)((lookup[*srcUn++]+(uint16_t)(*srcU++)+2) >> 2);
					srcUnn++;
				}
				itabu2++;

				// Upsample as needed.
				if ((*itabv1++) || (*itabv2))
				{
					(dst++)->v=(uint8_t)((lookup[*srcVnn++]+lookup[(uint16_t)(*srcV++)+256]+4) >> 3);
					srcVn++;
				}
				else
				{
					(dst++)->v=(uint8_t)((lookup[*srcVn++]+(uint16_t)(*srcV++)+2) >> 2);
					srcVnn++;
				}
				itabv2++;
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUn=src_Un,*srcUnn=src_Unn,*srcUnnn=src_Unnn;
			const uint8_t *srcVn=src_Vn,*srcVnn=src_Vnn,*srcVnnn=src_Vnnn;
			const bool *itabu2=interlaced_tab_U[index_tab_2];
			const bool *itabv2=interlaced_tab_V[index_tab_2];
			bool *itabu0=interlaced_tab_U[index_tab_0];
			bool *itabv0=interlaced_tab_V[index_tab_0];

			for (int32_t x = 0; x < dst_w; x+=4)
			{			
				if (((abs((int16_t)*srcUn-(int16_t)*srcUnn)>=threshold) &&
					(abs((int16_t)*srcUnnn-(int16_t)*srcUnn)>=threshold) &&
					(((*srcUn>*srcUnn) && (*srcUnnn>*srcUnn)) ||
					((*srcUn<*srcUnn) && (*srcUnnn<*srcUnn)))))
					*itabu0=true;
				else *itabu0=false;
				if (((abs((int16_t)*srcVn-(int16_t)*srcVnn)>=threshold) &&
					(abs((int16_t)*srcVnnn-(int16_t)*srcVnn)>=threshold) &&
					(((*srcVn>*srcVnn) && (*srcVnnn>*srcVnn)) ||
					((*srcVn<*srcVnn) && (*srcVnnn<*srcVnn)))))
					*itabv0=true;
				else *itabv0=false;

				dst->y1=*srcY++;
				dst->y2=*srcY++;

				// Upsample as needed.
				if ((*itabu0++) || (*itabu2))
				{
					dst->u=(uint8_t)((lookup[(uint16_t)(*srcUn++)+512]+(uint16_t)(*srcUnnn++)+4) >> 3);
					srcUnn++;
				}
				else
				{
					dst->u=(uint8_t)((lookup[*srcUn++]+(uint16_t)(*srcUnn++)+2) >> 2);
					srcUnnn++;
				}
				itabu2++;

				// Upsample as needed.
				if ((*itabv0++) || (*itabv2))
				{
					(dst++)->v=(uint8_t)((lookup[(uint16_t)(*srcVn++)+512]+(uint16_t)(*srcVnnn++)+4) >> 3);
					srcVnn++;
				}
				else
				{
					(dst++)->v=(uint8_t)((lookup[*srcVn++]+(uint16_t)(*srcVnn++)+2) >> 2);
					srcVnnn++;
				}
				itabv2++;
			}
		}
		
		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;
		
		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;	
	}

	for (int32_t y = dst_h_4; y < dst_h; y+=4)
	{
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUpp=src_Upp;
			const uint8_t *srcV=src_V,*srcVpp=src_Vpp;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[(uint16_t)(*srcU++)+512]+(uint16_t)(*srcUpp++)+4) >> 3);
				(dst++)->v=(uint8_t)((lookup[(uint16_t)(*srcV++)+512]+(uint16_t)(*srcVpp++)+4) >> 3);
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUn=src_Un,*srcUp=src_Up;
			const uint8_t *srcVn=src_Vn,*srcVp=src_Vp;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcUp++]+lookup[(uint16_t)(*srcUn++)+256]+4) >> 3);
				(dst++)->v=(uint8_t)((lookup[*srcVp++]+lookup[(uint16_t)(*srcVn++)+256]+4) >> 3);
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;
		
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U;
			const uint8_t *srcV=src_V;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=*srcU++;
				(dst++)->v=*srcV++;
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUn=src_Un;
			const uint8_t *srcVn=src_Vn;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=*srcUn++;
				(dst++)->v=*srcVn++;
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;
	}
}




void AutoYUY2::Convert_Test_YUY2(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp,const int dst_h,const int dst_w,
		int dst_pitch, int src_pitchY,int src_pitchUV)
{
	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up,*src_Un,*src_Unn,*src_Unnn,*src_Upp;
	const uint8_t *src_V,*src_Vp,*src_Vn,*src_Vnn,*src_Vnnn,*src_Vpp;
	int src_pitchUV_2;
	uint8_t index_tab_0,index_tab_1,index_tab_2;
	const int dst_h_4=dst_h-4;
	const uint16_t *lookup=lookup_Upscale;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchUV;
	src_Upp = srcUp - (2*src_pitchUV);
	src_Un = srcUp + src_pitchUV;
	src_Unn = srcUp + (2 * src_pitchUV);
	src_Unnn = srcUp + (3 * src_pitchUV);
	src_V = srcVp;
	src_Vp = srcVp - src_pitchUV;
	src_Vpp = srcVp - (2*src_pitchUV);
	src_Vn = srcVp + src_pitchUV;
	src_Vnn = srcVp + (2 * src_pitchUV);
	src_Vnnn = srcVp + (3 * src_pitchUV);
	
	src_pitchUV_2=src_pitchUV << 1;

	for (int32_t y=0; y<4; y+=4)
	{
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U;
			const uint8_t *srcV=src_V;

			for (int32_t x=0; x<dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=*srcU++;
				(dst++)->v=*srcV++;
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUn=src_Un;
			const uint8_t *srcVn=src_Vn;

			for (int32_t x=0; x<dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=*srcUn++;
				(dst++)->v=*srcVn++;
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUnn=src_Unn;
			const uint8_t *srcV=src_V,*srcVnn=src_Vnn;

			for (int32_t x=0; x<dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcUnn++]+lookup[(uint16_t)(*srcU++)+256]+4)>>3);
				(dst++)->v=(uint8_t)((lookup[*srcVnn++]+lookup[(uint16_t)(*srcV++)+256]+4)>>3);
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUn=src_Un,*srcUnn=src_Unn,*srcUnnn=src_Unnn;
			const uint8_t *srcV=src_V,*srcVn=src_Vn,*srcVnn=src_Vnn,*srcVnnn=src_Vnnn;
			bool *itabu0=interlaced_tab_U[0],*itabu1=interlaced_tab_U[1];
			bool *itabv0=interlaced_tab_V[0],*itabv1=interlaced_tab_V[1];

			for (int32_t x=0; x<dst_w; x+=4)
			{	
				if (((abs((int16_t)*srcU-(int16_t)*srcUn)>=threshold) &&
					(abs((int16_t)*srcUnn-(int16_t)*srcUn)>=threshold) &&
					(((*srcU>*srcUn) && (*srcUnn>*srcUn)) ||
					((*srcU<*srcUn) && (*srcUnn<*srcUn)))))
					*itabu0++=true;
				else *itabu0++=false;
				if (((abs((int16_t)*srcUn-(int16_t)*srcUnn)>=threshold) &&
					(abs((int16_t)*srcUnnn-(int16_t)*srcUnn)>=threshold) &&
					(((*srcUn>*srcUnn) && (*srcUnnn>*srcUnn)) ||
					((*srcUn<*srcUnn) && (*srcUnnn<*srcUnn)))))
					*itabu1++=true;
				else *itabu1++=false;
				if (((abs((int16_t)*srcV-(int16_t)*srcVn)>=threshold) &&
					(abs((int16_t)*srcVnn-(int16_t)*srcVn)>=threshold) &&
					(((*srcV>*srcVn) && (*srcVnn>*srcVn)) ||
					((*srcV<*srcVn) && (*srcVnn<*srcVn)))))
					*itabv0++=true;
				else *itabv0++=false;
				if (((abs((int16_t)*srcVn-(int16_t)*srcVnn)>=threshold) &&
					(abs((int16_t)*srcVnnn-(int16_t)*srcVnn)>=threshold) &&
					(((*srcVn>*srcVnn) && (*srcVnnn>*srcVnn)) ||
					((*srcVn<*srcVnn) && (*srcVnnn<*srcVnn)))))
					*itabv1++=true;
				else *itabv1++=false;
			
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[(uint16_t)(*srcUn++)+512]+(uint16_t)(*srcUnnn++)+4)>>3);
				(dst++)->v=(uint8_t)((lookup[(uint16_t)(*srcVn++)+512]+(uint16_t)(*srcVnnn++)+4)>>3);

				srcU++;
				srcUnn++;
				srcV++;
				srcVnn++;
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;
	}
	
	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;
	for (int32_t y = 4; y < dst_h_4; y+=4)
	{
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUp=src_Up;
			const uint8_t *srcV=src_V,*srcVp=src_Vp;
			const bool *itabu0=interlaced_tab_U[index_tab_0],*itabu1=interlaced_tab_U[index_tab_1];
			const bool *itabv0=interlaced_tab_V[index_tab_0],*itabv1=interlaced_tab_V[index_tab_1];

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;

				// Upsample as needed.
				if ((*itabu0++) || (*itabu1))
				{
					dst->u=239;
					srcU++;
					srcUp++;
				}
				else
				{
					dst->u=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUp++)+2) >> 2);
				}
				itabu1++;

				// Upsample as needed.
				if ((*itabv0++) || (*itabv1))
				{
					(dst++)->v=239;
					srcV++;
					srcVp++;
				}
				else
				{
					(dst++)->v=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVp++)+2) >> 2);
				}
				itabv1++;
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUn=src_Un,*srcUnn=src_Unn;
			const uint8_t *srcV=src_V,*srcVn=src_Vn,*srcVnn=src_Vnn;
			const bool *itabu1=interlaced_tab_U[index_tab_1];
			const bool *itabv1=interlaced_tab_V[index_tab_1];
			bool *itabu2=interlaced_tab_U[index_tab_2];
			bool *itabv2=interlaced_tab_V[index_tab_2];

			for (int32_t x = 0; x < dst_w; x+=4)
			{			
				if (((abs((int16_t)*srcU-(int16_t)*srcUn)>=threshold) &&
					(abs((int16_t)*srcUnn-(int16_t)*srcUn)>=threshold) &&
					(((*srcU>*srcUn) && (*srcUnn>*srcUn)) ||
					((*srcU<*srcUn) && (*srcUnn<*srcUn)))))
					*itabu2=true;
				else *itabu2=false;			
				if (((abs((int16_t)*srcV-(int16_t)*srcVn)>=threshold) &&
					(abs((int16_t)*srcVnn-(int16_t)*srcVn)>=threshold) &&
					(((*srcV>*srcVn) && (*srcVnn>*srcVn)) ||
					((*srcV<*srcVn) && (*srcVnn<*srcVn)))))
					*itabv2=true;
				else *itabv2=false;			

				dst->y1=*srcY++;
				dst->y2=*srcY++;

				// Upsample as needed.
				if ((*itabu1++) || (*itabu2))
				{
					dst->u=239;
					srcU++;
					srcUn++;
				}
				else
				{
					dst->u=(uint8_t)((lookup[*srcU++]+(uint16_t)(*srcUn++)+2) >> 2);
				}
				itabu2++;

				// Upsample as needed.
				if ((*itabv1++) || (*itabv2))
				{
					(dst++)->v=239;
					srcV++;
					srcVn++;
				}
				else
				{
					(dst++)->v=(uint8_t)((lookup[*srcV++]+(uint16_t)(*srcVn++)+2) >> 2);
				}
				itabv2++;

				srcUnn++;
				srcVnn++;				
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;
		
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUn=src_Un;
			const uint8_t *srcV=src_V,*srcVn=src_Vn;
			const bool *itabu1=interlaced_tab_U[index_tab_1],*itabu2=interlaced_tab_U[index_tab_2];
			const bool *itabv1=interlaced_tab_V[index_tab_1],*itabv2=interlaced_tab_V[index_tab_2];

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;

				// Upsample as needed.
				if ((*itabu1++) || (*itabu2))
				{
					dst->u=239;
					srcU++;
					srcUn++;
				}
				else
				{
					dst->u=(uint8_t)((lookup[*srcUn++]+(uint16_t)(*srcU++)+2) >> 2);
				}
				itabu2++;

				// Upsample as needed.
				if ((*itabv1++) || (*itabv2))
				{
					(dst++)->v=239;
					srcV++;
					srcVn++;
				}
				else
				{
					(dst++)->v=(uint8_t)((lookup[*srcVn++]+(uint16_t)(*srcV++)+2) >> 2);
				}
				itabv2++;
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUn=src_Un,*srcUnn=src_Unn,*srcUnnn=src_Unnn;
			const uint8_t *srcVn=src_Vn,*srcVnn=src_Vnn,*srcVnnn=src_Vnnn;
			const bool *itabu2=interlaced_tab_U[index_tab_2];
			const bool *itabv2=interlaced_tab_V[index_tab_2];
			bool *itabu0=interlaced_tab_U[index_tab_0];
			bool *itabv0=interlaced_tab_V[index_tab_0];

			for (int32_t x = 0; x < dst_w; x+=4)
			{			
				if (((abs((int16_t)*srcUn-(int16_t)*srcUnn)>=threshold) &&
					(abs((int16_t)*srcUnnn-(int16_t)*srcUnn)>=threshold) &&
					(((*srcUn>*srcUnn) && (*srcUnnn>*srcUnn)) ||
					((*srcUn<*srcUnn) && (*srcUnnn<*srcUnn)))))
					*itabu0=true;
				else *itabu0=false;
				if (((abs((int16_t)*srcVn-(int16_t)*srcVnn)>=threshold) &&
					(abs((int16_t)*srcVnnn-(int16_t)*srcVnn)>=threshold) &&
					(((*srcVn>*srcVnn) && (*srcVnnn>*srcVnn)) ||
					((*srcVn<*srcVnn) && (*srcVnnn<*srcVnn)))))
					*itabv0=true;
				else *itabv0=false;

				dst->y1=*srcY++;
				dst->y2=*srcY++;

				// Upsample as needed.
				if ((*itabu0++) || (*itabu2))
				{
					dst->u=239;
					srcUn++;
					srcUnn++;
				}
				else
				{
					dst->u=(uint8_t)((lookup[*srcUn++]+(uint16_t)(*srcUnn++)+2) >> 2);
				}
				itabu2++;

				// Upsample as needed.
				if ((*itabv0++) || (*itabv2))
				{
					(dst++)->v=239;
					srcVn++;
					srcVnn++;
				}
				else
				{
					(dst++)->v=(uint8_t)((lookup[*srcVn++]+(uint16_t)(*srcVnn++)+2) >> 2);
				}
				itabv2++;

				srcUnnn++;
				srcVnnn++;
			}
		}
		
		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;
		
		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;	
	}

	for (int32_t y = dst_h_4; y < dst_h; y+=4)
	{
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U,*srcUpp=src_Upp;
			const uint8_t *srcV=src_V,*srcVpp=src_Vpp;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[(uint16_t)(*srcU++)+512]+(uint16_t)(*srcUpp++)+4) >> 3);
				(dst++)->v=(uint8_t)((lookup[(uint16_t)(*srcV++)+512]+(uint16_t)(*srcVpp++)+4) >> 3);
			}
		}

		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUn=src_Un,*srcUp=src_Up;
			const uint8_t *srcVn=src_Vn,*srcVp=src_Vp;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=(uint8_t)((lookup[*srcUp++]+lookup[(uint16_t)(*srcUn++)+256]+4) >> 3);
				(dst++)->v=(uint8_t)((lookup[*srcVp++]+lookup[(uint16_t)(*srcVn++)+256]+4) >> 3);
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;
		
		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcU=src_U;
			const uint8_t *srcV=src_V;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=*srcU++;
				(dst++)->v=*srcV++;
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;

		{
			YUYV *dst=(YUYV *)dstp;
			const uint8_t *srcY=src_Y;
			const uint8_t *srcUn=src_Un;
			const uint8_t *srcVn=src_Vn;

			for (int32_t x = 0; x < dst_w; x+=4)
			{
				dst->y1=*srcY++;
				dst->y2=*srcY++;
				dst->u=*srcUn++;
				(dst++)->v=*srcVn++;
			}
		}
		
		dstp += dst_pitch;
		src_Y += src_pitchY;

		src_U += src_pitchUV_2;
		src_Up += src_pitchUV_2;
		src_Upp += src_pitchUV_2;
		src_Un += src_pitchUV_2;
		src_Unn += src_pitchUV_2;
		src_Unnn += src_pitchUV_2;
		src_V += src_pitchUV_2;
		src_Vp += src_pitchUV_2;
		src_Vpp += src_pitchUV_2;
		src_Vn += src_pitchUV_2;
		src_Vnn += src_pitchUV_2;
		src_Vnnn += src_pitchUV_2;
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

	SetMemcpyCacheLimit(Cache_Setting);
	SetMemsetCacheLimit(Cache_Setting);

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
				case -1 : Convert_Automatic_YUY2(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
						  src_pitchUV); break;
				case 0 :
					if ((SSE2_Enable) && ((dst_w & 0x7)==0))
						Convert_Progressive_YUY2_SSE(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
							src_pitchUV);
					else
						Convert_Progressive_YUY2(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
							src_pitchUV);
					break;
				case 1 :
					if ((SSE2_Enable) && ((dst_w & 0x7)==0))
						Convert_Interlaced_YUY2_SSE(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
							src_pitchUV);
					else
						Convert_Interlaced_YUY2(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
							src_pitchUV);
					break;
				case 2 : Convert_Test_YUY2(srcYp,srcUp,srcVp,dst_Y,dst_h,dst_w,dst_pitch_Y,src_pitch_Y,
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
		env->ThrowError("AutoYUY2 : [mode] must be -1 (Automatic), 0 (Progessive) , 1 (Interlaced) or 2 (Test).");
	if ((output<0) || (output>1))
		env->ThrowError("AutoYUY2 : [output] must be 0 (YUY2) or 1 (YV16)");

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
