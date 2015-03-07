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

extern "C" void JPSDR_AutoYUY2_1(const uint8_t *scr_y,const uint8_t *src_u,const uint8_t *src_v,
		uint8_t *dst,int w);
extern "C" void JPSDR_AutoYUY2_SSE2_2(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int w);
extern "C" void JPSDR_AutoYUY2_SSE2_3(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int w);
extern "C" void JPSDR_AutoYUY2_SSE2_4(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int w);


#define VERSION "AutoYUY2 1.2.1 JPSDR"
// Inspired from Neuron2 filter

#define Interlaced_Tab_Size 3

#define myfree(ptr) if (ptr!=NULL) { free(ptr); ptr=NULL;}


class AutoYUY2 : public GenericVideoFilter
{
public:
	AutoYUY2(PClip _child, int _threshold, int _mode, IScriptEnvironment* env);
	~AutoYUY2();
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);
private:
	int threshold;
	int mode;
	uint16_t lookup[768];
	bool *interlaced_tab[Interlaced_Tab_Size];
	bool SSE2_Enable;

	void Convert_Progressive(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Progressive_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Interlaced(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Interlaced_SSE(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Automatic(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
	void Convert_Test(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV);
};

AutoYUY2::AutoYUY2(PClip _child, int _threshold, int _mode, IScriptEnvironment* env) :
										GenericVideoFilter(_child), threshold(_threshold),
											mode(_mode)
{
	bool ok;
	uint16_t i;

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
	if (!vi.IsYV12())
		env->ThrowError("AutoYUY2 : Input format must be YV12 or I420");
	if (vi.width & 3)
		env->ThrowError("AutoYUY2 : Input width must be a multiple of 4.");
	if (vi.height & 3)
		env->ThrowError("AutoYUY2 : Input height must be a multiple of 4.");
	if (vi.height < 8)
		env->ThrowError("AutoYUY2 : Input height must be at least 8.");
	if ((mode<-1) || (mode>2))
		env->ThrowError("AutoYUY2 : [mode] must be -1, 0, 1 or 2.");

	if (ok)
	{
		for (i=0; i<256; i++)
		{
			lookup[i]=(uint16_t)(3*i);
			lookup[i+256]=(uint16_t)(5*i);
			lookup[i+512]=(uint16_t)(7*i);
		}
	}
		
	vi.pixel_type = VideoInfo::CS_YUY2;
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

void AutoYUY2::Convert_Progressive(const uint8_t *srcYp, const uint8_t *srcUp,
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *srcUpp, *srcUpn;
	const uint8_t *srcVpp, *srcVpn;
	int dst_h_2;

	srcUpp = srcUp - src_pitchUV;
	srcUpn = srcUp + src_pitchUV;
	srcVpp = srcVp - src_pitchUV;
	srcVpn = srcVp + src_pitchUV;

	dst_h_2=dst_h-2;

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
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *srcUpp, *srcUpn;
	const uint8_t *srcVpp, *srcVpn;
	int dst_h_2;

	srcUpp = srcUp - src_pitchUV;
	srcUpn = srcUp + src_pitchUV;
	srcVpp = srcVp - src_pitchUV;
	srcVpn = srcVp + src_pitchUV;

	dst_h_2=dst_h-2;

	for (int y=0; y<2; y+=2)
	{
		JPSDR_AutoYUY2_1(srcYp,srcUp,srcVp,dstp,dst_w<<1);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_SSE2_4(srcYp,srcUp,srcUpn,srcVp,srcVpn,dstp,dst_w);
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
		JPSDR_AutoYUY2_SSE2_4(srcYp,srcUp,srcUpp,srcVp,srcVpp,dstp,dst_w);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_SSE2_4(srcYp,srcUp,srcUpn,srcVp,srcVpn,dstp,dst_w);
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
		JPSDR_AutoYUY2_SSE2_4(srcYp,srcUp,srcUpp,srcVp,srcVpp,dstp,dst_w);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_1(srcYp,srcUp,srcVp,dstp,dst_w<<1);
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
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *srcUpp, *srcUpn, *srcUpnn, *srcUpnnn,*srcUppp;
	const uint8_t *srcVpp, *srcVpn, *srcVpnn, *srcVpnnn,*srcVppp;
	int dst_h_4,src_pitchUV_2;

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
	dst_h_4=dst_h-4;

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
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY, int src_pitchUV)
{
	const uint8_t *srcUpp, *srcUpn, *srcUpnn, *srcUpnnn,*srcUppp;
	const uint8_t *srcVpp, *srcVpn, *srcVpnn, *srcVpnnn,*srcVppp;
	int dst_h_4,src_pitchUV_2;

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
	dst_h_4=dst_h-4;

	for (int y=0; y<4; y+=4)
	{
		JPSDR_AutoYUY2_1(srcYp,srcUp,srcVp,dstp,dst_w<<1);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_1(srcYp,srcUpn,srcVpn,dstp,dst_w<<1);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_SSE2_2(srcYp,srcUpnn,srcUp,srcVpnn,srcVp,dstp,dst_w);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_SSE2_3(srcYp,srcUpn,srcUpnnn,srcVpn,srcVpnnn,dstp,dst_w);
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
		JPSDR_AutoYUY2_SSE2_3(srcYp,srcUp,srcUppp,srcVp,srcVppp,dstp,dst_w);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_SSE2_2(srcYp,srcUpp,srcUpn,srcVpp,srcVpn,dstp,dst_w);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_SSE2_2(srcYp,srcUpnn,srcUp,srcVpnn,srcVp,dstp,dst_w);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_SSE2_3(srcYp,srcUpn,srcUpnnn,srcVpn,srcVpnnn,dstp,dst_w);
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
		JPSDR_AutoYUY2_SSE2_3(srcYp,srcUp,srcUppp,srcVp,srcVppp,dstp,dst_w);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_SSE2_2(srcYp,srcUpp,srcUpn,srcVpp,srcVpn,dstp,dst_w);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_1(srcYp,srcUp,srcVp,dstp,dst_w<<1);
		dstp += dst_pitch;
		srcYp += src_pitchY;

		JPSDR_AutoYUY2_1(srcYp,srcUpn,srcVpn,dstp,dst_w<<1);
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
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY,int src_pitchUV)
{
	const uint8_t *srcUpp, *srcUpn, *srcUpnn, *srcUpnnn,*srcUppp;
	const uint8_t *srcVpp, *srcVpn, *srcVpnn, *srcVpnnn,*srcVppp;
	int dst_h_4,src_pitchUV_2;
	uint8_t index_tab_0,index_tab_1,index_tab_2;

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
	dst_h_4=dst_h-4;

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
		const uint8_t *srcVp, uint8_t *dstp, int dst_h, int dst_w,
		int dst_pitch, int src_pitchY,int src_pitchUV)	
{
	const uint8_t *srcUpp, *srcUpn, *srcUpnn, *srcUpnnn,*srcUppp;
	const uint8_t *srcVpp, *srcVpn, *srcVpnn, *srcVpnnn,*srcVppp;
	int dst_h_4,src_pitchUV_2;
	uint8_t index_tab_0,index_tab_1,index_tab_2;

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
	dst_h_4=dst_h-4;

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
	uint8_t *dstp = dst->GetWritePtr(PLANAR_Y);
	const uint8_t *srcYp = src->GetReadPtr(PLANAR_Y);
	const uint8_t *srcUp = src->GetReadPtr(PLANAR_U);
	const uint8_t *srcVp = src->GetReadPtr(PLANAR_V);
	int dst_h,dst_w,dst_pitch;
	int src_pitchY, src_pitchUV;

	dst_h = dst->GetHeight();
	dst_w = dst->GetRowSize();
	dst_pitch = dst->GetPitch();
	src_pitchY = src->GetPitch(PLANAR_Y);
	src_pitchUV = src->GetPitch(PLANAR_U);

	switch(mode)
	{
		case -1 : Convert_Automatic(srcYp,srcUp,srcVp,dstp,dst_h,dst_w,dst_pitch,src_pitchY,
				  src_pitchUV); break;
		case 0 :
			if (SSE2_Enable)
				Convert_Progressive_SSE(srcYp,srcUp,srcVp,dstp,dst_h,dst_w>>3,dst_pitch,src_pitchY,
					src_pitchUV);
			else
				Convert_Progressive(srcYp,srcUp,srcVp,dstp,dst_h,dst_w,dst_pitch,src_pitchY,
					src_pitchUV);
			break;
		case 1 :
			if (SSE2_Enable)
				Convert_Interlaced_SSE(srcYp,srcUp,srcVp,dstp,dst_h,dst_w>>3,dst_pitch,src_pitchY,
					src_pitchUV);
			else
				Convert_Interlaced(srcYp,srcUp,srcVp,dstp,dst_h,dst_w,dst_pitch,src_pitchY,
					src_pitchUV);
			break;
		case 2 : Convert_Test(srcYp,srcUp,srcVp,dstp,dst_h,dst_w,dst_pitch,src_pitchY,
				  src_pitchUV); break;
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
*/
AVSValue __cdecl Create_AutoYUY2(AVSValue args, void* user_data, IScriptEnvironment* env)
{
  return new AutoYUY2(args[0].AsClip(), args[1].AsInt(4), args[2].AsInt(-1), env);
}

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors)
{
	AVS_linkage = vectors;

    env->AddFunction("AutoYUY2", "c[threshold]i[mode]i", Create_AutoYUY2, 0);

    return "AutoYUY2 Pluggin";
}
