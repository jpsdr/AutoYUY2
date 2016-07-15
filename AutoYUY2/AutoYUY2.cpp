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
		uint8_t *dst,int32_t w);
extern "C" void JPSDR_AutoYUY2_SSE2_1(const uint8_t *scr_y,const uint8_t *src_u,const uint8_t *src_v,
		uint8_t *dst,int32_t w);
extern "C" void JPSDR_AutoYUY2_SSE2_2(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int32_t w);
extern "C" void JPSDR_AutoYUY2_SSE2_3(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int32_t w);
extern "C" void JPSDR_AutoYUY2_SSE2_4(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int32_t w);

extern "C" void JPSDR_AutoYUY2_SSE2_1b(const uint8_t *scr_y,const uint8_t *src_u,const uint8_t *src_v,
		uint8_t *dst,int32_t w);
extern "C" void JPSDR_AutoYUY2_SSE2_2b(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int32_t w);
extern "C" void JPSDR_AutoYUY2_SSE2_3b(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int32_t w);
extern "C" void JPSDR_AutoYUY2_SSE2_4b(const uint8_t *scr_y,const uint8_t *src_u1,const uint8_t *src_u2,
		const uint8_t *src_v1,const uint8_t *src_v2,uint8_t *dst,int32_t w);

extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(const void *scr_1,const void *src_2,void *dst,int w);
extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(const void *scr_1,const void *src_2,void *dst,int w);
extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(const void *scr_1,const void *src_2,void *dst,int w);
extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(const void *scr_1,const void *src_2,void *dst,int w);
extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(const void *scr_1,const void *src_2,void *dst,int w);
extern "C" void JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(const void *scr_1,const void *src_2,void *dst,int w);


#define VERSION "AutoYUY2 3.0.2 JPSDR"
// Inspired from Neuron2 filter

#define Interlaced_Tab_Size 3
#define MAX_MT_THREADS 128

#define myfree(ptr) if (ptr!=NULL) { free(ptr); ptr=NULL;}
#define myCloseHandle(ptr) if (ptr!=NULL) { CloseHandle(ptr); ptr=NULL;}


typedef struct _MT_Data_Info
{
	void *src1,*src2,*src3;
	void *dst1,*dst2,*dst3;
	int src_pitch1,src_pitch2,src_pitch3;
	int dst_pitch1,dst_pitch2,dst_pitch3;
	int32_t src_Y_h_min,src_Y_h_max,src_Y_w;
	int32_t src_UV_h_min,src_UV_h_max,src_UV_w;
	int32_t dst_Y_h_min,dst_Y_h_max,dst_Y_w;
	int32_t dst_UV_h_min,dst_UV_h_max,dst_UV_w;
	bool top,bottom;
} MT_Data_Info;


typedef struct _MT_Data_Thread
{
	void *pClass;
	uint8_t f_process,thread_Id;
	HANDLE nextJob, jobFinished;
} MT_Data_Thread;


static int num_processors()
{
#ifdef _DEBUG
	return 1;
#else
	int pcount = 0;
	ULONG_PTR p_aff=0, s_aff=0;
	GetProcessAffinityMask(GetCurrentProcess(), &p_aff, &s_aff);
	for(; p_aff != 0; p_aff>>=1) 
		pcount += (p_aff&1);
	return pcount;
#endif
}



class AutoYUY2 : public GenericVideoFilter
{
public:
	AutoYUY2(PClip _child, int _threshold, int _mode, int _output,int _threads, IScriptEnvironment* env);
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
	int threads;
	uint16_t lookup_Upscale[768];
	bool *interlaced_tab_U[MAX_MT_THREADS][Interlaced_Tab_Size],*interlaced_tab_V[MAX_MT_THREADS][Interlaced_Tab_Size];
	bool SSE2_Enable;
	size_t Cache_Setting;

	HANDLE thds[MAX_MT_THREADS];
	MT_Data_Thread MT_Thread[MAX_MT_THREADS];
	MT_Data_Info MT_Data[MAX_MT_THREADS];
	DWORD tids[MAX_MT_THREADS];
	uint8_t CPUs_number,threads_number;
	HANDLE ghMutex;
	
	static DWORD WINAPI StaticThreadpool( LPVOID lpParam );

	uint8_t CreateMTData(uint8_t max_threads,int32_t size_x,int32_t size_y);

	void FreeData(void);

	inline void Move_Full(const void *src_, void *dst_, const int32_t w,const int32_t h,
		int src_pitch,int dst_pitch);

	void Convert_Progressive_YUY2(uint8_t thread_num);
	void Convert_Progressive_YUY2_SSE(uint8_t thread_num);
	void Convert_Interlaced_YUY2(uint8_t thread_num);
	void Convert_Interlaced_YUY2_SSE(uint8_t thread_num);
	void Convert_Automatic_YUY2(uint8_t thread_num);
	void Convert_Test_YUY2(uint8_t thread_num);

	void Convert_Progressive_YV16(uint8_t thread_num);
	void Convert_Progressive_YV16_SSE(uint8_t thread_num);
	void Convert_Interlaced_YV16(uint8_t thread_num);
	void Convert_Interlaced_YV16_SSE(uint8_t thread_num);
	void Convert_Automatic_YV16(uint8_t thread_num);
	void Convert_Test_YV16(uint8_t thread_num);
};



uint8_t AutoYUY2::CreateMTData(uint8_t max_threads,int32_t size_x,int32_t size_y)
{
	if ((max_threads<=1) || (max_threads>threads_number))
	{
		MT_Data[0].top=true;
		MT_Data[0].bottom=true;
		MT_Data[0].src_Y_h_min=0;
		MT_Data[0].dst_Y_h_min=0;
		MT_Data[0].src_Y_h_max=size_y;
		MT_Data[0].dst_Y_h_max=size_y;
		MT_Data[0].src_UV_h_min=0;
		MT_Data[0].dst_UV_h_min=0;
		MT_Data[0].src_UV_h_max=size_y >> 1;
		MT_Data[0].dst_UV_h_max=size_y;
		MT_Data[0].src_Y_w=size_x;
		if (output==0) MT_Data[0].dst_Y_w=size_x << 1;
		else MT_Data[0].dst_Y_w=size_x;
		MT_Data[0].src_UV_w=size_x >> 1;
		MT_Data[0].dst_UV_w=size_x >> 1;
		return(1);
	}

	int32_t dh_Y,dh_UV,h_y;
	uint8_t i,max=0;

	dh_Y=(size_y+(int32_t)max_threads-1)/(int32_t)max_threads;
	if (dh_Y<16) dh_Y=16;
	if ((dh_Y & 3)!=0) dh_Y=((dh_Y+3) >> 2) << 2;

	h_y=0;
	while (h_y<(size_y-16))
	{
		max++;
		h_y+=dh_Y;
	}

	if (max==1)
	{
		MT_Data[0].top=true;
		MT_Data[0].bottom=true;
		MT_Data[0].src_Y_h_min=0;
		MT_Data[0].dst_Y_h_min=0;
		MT_Data[0].src_Y_h_max=size_y;
		MT_Data[0].dst_Y_h_max=size_y;
		MT_Data[0].src_UV_h_min=0;
		MT_Data[0].dst_UV_h_min=0;
		MT_Data[0].src_UV_h_max=size_y >> 1;
		MT_Data[0].dst_UV_h_max=size_y;
		MT_Data[0].src_Y_w=size_x;
		if (output==0) MT_Data[0].dst_Y_w=size_x << 1;
		else MT_Data[0].dst_Y_w=size_x;
		MT_Data[0].src_UV_w=size_x >> 1;
		MT_Data[0].dst_UV_w=size_x >> 1;
		return(1);
	}

	dh_UV=dh_Y>>1; 

	MT_Data[0].top=true;
	MT_Data[0].bottom=false;
	MT_Data[0].src_Y_h_min=0;
	MT_Data[0].src_Y_h_max=dh_Y;
	MT_Data[0].dst_Y_h_min=0;
	MT_Data[0].dst_Y_h_max=dh_Y;
	MT_Data[0].src_UV_h_min=0;
	MT_Data[0].src_UV_h_max=dh_UV;
	MT_Data[0].dst_UV_h_min=0;
	MT_Data[0].dst_UV_h_max=dh_Y;

	i=1;
	while (i<max)
	{
		MT_Data[i].top=false;
		MT_Data[i].bottom=false;
		MT_Data[i].src_Y_h_min=MT_Data[i-1].src_Y_h_max;
		MT_Data[i].src_Y_h_max=MT_Data[i].src_Y_h_min+dh_Y;
		MT_Data[i].dst_Y_h_min=MT_Data[i-1].dst_Y_h_max;
		MT_Data[i].dst_Y_h_max=MT_Data[i].dst_Y_h_min+dh_Y;
		MT_Data[i].src_UV_h_min=MT_Data[i-1].src_UV_h_max;
		MT_Data[i].src_UV_h_max=MT_Data[i].src_UV_h_min+dh_UV;
		MT_Data[i].dst_UV_h_min=MT_Data[i-1].dst_UV_h_max;
		MT_Data[i].dst_UV_h_max=MT_Data[i].dst_UV_h_min+dh_Y;
		i++;
	}
	MT_Data[max-1].bottom=true;
	MT_Data[max-1].src_Y_h_max=size_y;
	MT_Data[max-1].dst_Y_h_max=size_y;
	MT_Data[max-1].src_UV_h_max=size_y >> 1;
	MT_Data[max-1].dst_UV_h_max=size_y;
	for (i=0; i<max; i++)
	{
		MT_Data[i].src_Y_w=size_x;
		if (output==0) MT_Data[i].dst_Y_w=size_x << 1;
		else MT_Data[i].dst_Y_w=size_x;
		MT_Data[i].src_UV_w=size_x >> 1;
		MT_Data[i].dst_UV_w=size_x >> 1;
	}
	return(max);
}


AutoYUY2::AutoYUY2(PClip _child, int _threshold, int _mode,  int _output, int _threads, IScriptEnvironment* env) :
										GenericVideoFilter(_child), threshold(_threshold),
										mode(_mode), output(_output), threads(_threads)
{
	bool ok,def_affinity;
	int16_t i,j;

	for (j=0; j<MAX_MT_THREADS; j++)
	{
		for (i=0; i<Interlaced_Tab_Size; i++)
		{
			interlaced_tab_U[j][i]=NULL;
			interlaced_tab_V[j][i]=NULL;
		}
	}

	for (i=0; i<MAX_MT_THREADS; i++)
	{
		MT_Thread[i].pClass=NULL;
		MT_Thread[i].f_process=0;
		MT_Thread[i].thread_Id=(uint8_t)i;
		MT_Thread[i].jobFinished=NULL;
		MT_Thread[i].nextJob=NULL;
		thds[i]=NULL;
	}
	ghMutex=NULL;

	CPUs_number=(uint8_t)num_processors();
	if (CPUs_number>MAX_MT_THREADS) CPUs_number=MAX_MT_THREADS;

	if (vi.height>=32)
	{
		if (threads==0) threads_number=CPUs_number;
		else threads_number=(uint8_t)threads;
	}
	else threads_number=1;

	threads_number=CreateMTData(threads_number,vi.width,vi.height);
	if (threads_number<=CPUs_number) def_affinity=true;
	else def_affinity=false;

	ghMutex=CreateMutex(NULL,FALSE,NULL);
	if (ghMutex==NULL) env->ThrowError("Unable to create Mutex !");

	if (threads_number>1)
	{
		ok=true;
		i=0;
		while ((i<threads_number) && ok)
		{
			MT_Thread[i].pClass=this;
			MT_Thread[i].f_process=0;
			MT_Thread[i].jobFinished=CreateEvent(NULL,TRUE,TRUE,NULL);
			MT_Thread[i].nextJob=CreateEvent(NULL,TRUE,FALSE,NULL);
			ok=ok && ((MT_Thread[i].jobFinished!=NULL) && (MT_Thread[i].nextJob!=NULL));
			i++;
		}
		if (!ok)
		{
			FreeData();
			env->ThrowError("Unable to create events !");
		}

		DWORD_PTR dwpProcessAffinityMask;
		DWORD_PTR dwpSystemAffinityMask;
		DWORD_PTR dwpThreadAffinityMask=1;

		GetProcessAffinityMask(GetCurrentProcess(), &dwpProcessAffinityMask, &dwpSystemAffinityMask);

		ok=true;
		i=0;
		while ((i<threads_number) && ok)
		{
			if (def_affinity)
			{
				if ((dwpProcessAffinityMask & dwpThreadAffinityMask)!=0)
				{
					thds[i]=CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)StaticThreadpool,&MT_Thread[i],CREATE_SUSPENDED,&tids[i]);
					ok=ok && (thds[i]!=NULL);
					if (ok)
					{
						SetThreadAffinityMask(thds[i],dwpThreadAffinityMask);
						ResumeThread(thds[i]);
					}
					i++;
				}
				dwpThreadAffinityMask<<=1;
			}
			else
			{
				thds[i]=CreateThread(NULL,0,(LPTHREAD_START_ROUTINE)StaticThreadpool,&MT_Thread[i],0,&tids[i]);
				ok=ok && (thds[i]!=NULL);
				i++;
			}
		}
		if (!ok)
		{
			FreeData();
			env->ThrowError("Unable to create threads pool !");
		}
	}

	if ((mode==-1) || (mode==2))
	{
		for (j=0; j<threads_number; j++)
		{
			for (i=0; i<Interlaced_Tab_Size; i++)
			{
				interlaced_tab_U[j][i]=(bool*)malloc((vi.width>>1)*sizeof(bool));
				interlaced_tab_V[j][i]=(bool*)malloc((vi.width>>1)*sizeof(bool));
			}
		}

		ok=true;
		for (j=0; j<threads_number; j++)
		{
			for (i=0; i<Interlaced_Tab_Size; i++)
			{
				ok=ok && (interlaced_tab_U[j][i]!=NULL);
				ok=ok && (interlaced_tab_V[j][i]!=NULL);
			}
		}

		if (!ok)
		{
			FreeData();
			env->ThrowError("AutoYUY2 : Memory allocation error.");
		}
	}

	switch (output)
	{
		case 0 : vi.pixel_type = VideoInfo::CS_YUY2; break;
		case 1 : vi.pixel_type = VideoInfo::CS_YV16; break;
		default : break;
	}

	for (uint16_t k=0; k<256; k++)
	{
		lookup_Upscale[k]=3*k;
		lookup_Upscale[k+256]=5*k;
		lookup_Upscale[k+512]=7*k;
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


void AutoYUY2::FreeData(void) 
{
	int16_t i,j;

	for (j=threads_number-1; j>=0; j--)
	{
		for (i=Interlaced_Tab_Size-1; i>=0; i--)
		{
			myfree(interlaced_tab_V[j][i]);
			myfree(interlaced_tab_U[j][i]);
		}
	}

	if (threads_number>1)
	{
		for (i=threads_number-1; i>=0; i--)
		{
			if (thds[i]!=NULL)
			{
				MT_Thread[i].f_process=255;
				SetEvent(MT_Thread[i].nextJob);
				WaitForSingleObject(thds[i],INFINITE);
				myCloseHandle(thds[i]);
			}
		}
		for (i=threads_number-1; i>=0; i--)
		{
			myCloseHandle(MT_Thread[i].nextJob);
			myCloseHandle(MT_Thread[i].jobFinished);
		}
	}
	myCloseHandle(ghMutex);
}


AutoYUY2::~AutoYUY2() 
{
	FreeData();
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


void AutoYUY2::Convert_Interlaced_YV16_SSE(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	uint8_t *dstYp=(uint8_t *)mt_data_inf.dst1;
	uint8_t *dstUp=(uint8_t *)mt_data_inf.dst2;
	uint8_t *dstVp=(uint8_t *)mt_data_inf.dst3;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitch_Y=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch_Y=mt_data_inf.dst_pitch1;
	const int dst_pitch_U=mt_data_inf.dst_pitch2;
	const int dst_pitch_V=mt_data_inf.dst_pitch3;

	const uint8_t *src_U,*src_Up,*src_Upp,*src_Un,*src_Unn,*src_Unnn;
	const uint8_t *src_V,*src_Vp,*src_Vpp,*src_Vn,*src_Vnn,*src_Vnnn;
	bool _align_U=false,_align_V=false;
	const int32_t w_U=dst_w>>1,w_V=dst_w>>1;
	const int32_t w_U4=w_U>>2,w_V4=w_V>>2;
	const int32_t h_4 = mt_data_inf.bottom ? h_Y_max-4:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 4:h_Y_min;

	const ptrdiff_t pitch_U_2=2*src_pitchU;
	const ptrdiff_t pitch_V_2=2*src_pitchV;

	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchU;
	src_Upp=src_U-2*src_pitchU;
	src_Un=src_U+src_pitchU;
	src_Unn=src_U+2*src_pitchU;
	src_Unnn=src_U+3*src_pitchU;
	src_Vp=src_V-src_pitchV;
	src_Vpp=src_V-2*src_pitchV;
	src_Vn=src_V+src_pitchV;
	src_Vnn=src_V+2*src_pitchV;
	src_Vnnn=src_V+3*src_pitchV;

	if ((((size_t)src_U & 0x0F)==0) && (((size_t)dstUp & 0x0F)==0) && ((abs(src_pitchU) & 0x0F)==0) 
		&& ((abs(dst_pitch_U) & 0x0F)==0)) _align_U=true;
	if ((((size_t)src_V & 0x0F)==0) && (((size_t)dstVp & 0x0F)==0) && ((abs(src_pitchV) & 0x0F)==0) 
		&& ((abs(dst_pitch_V) & 0x0F)==0)) _align_V=true;

	Move_Full(srcYp,dstYp,dst_w,h_Y_max-h_Y_min,src_pitch_Y,dst_pitch_Y);

// U Planar
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<4; i+=4)
		{
			A_memcpy(dstUp,src_U,w_U);
			dstUp+=dst_pitch_U;

			A_memcpy(dstUp,src_Un,w_U);
			dstUp+=dst_pitch_U;

			if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Unn,src_U,dstUp,w_U4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Unn,src_U,dstUp,w_U4);
			dstUp+=dst_pitch_U;

			if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Un,src_Unnn,dstUp,w_U4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Un,src_Unnn,dstUp,w_U4);
			dstUp+=dst_pitch_U;

			src_U+=pitch_U_2;
			src_Up+=pitch_U_2;
			src_Upp+=pitch_U_2;
			src_Un+=pitch_U_2;
			src_Unn+=pitch_U_2;
			src_Unnn+=pitch_U_2;
		}
	}

	for(int32_t i=h_0; i<h_4; i+=4)
	{
		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_U,src_Upp,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_U,src_Upp,dstUp,w_U4);
		dstUp+=dst_pitch_U;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Up,src_Un,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Up,src_Un,dstUp,w_U4);
		dstUp+=dst_pitch_U;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Unn,src_U,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Unn,src_U,dstUp,w_U4);
		dstUp+=dst_pitch_U;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Un,src_Unnn,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Un,src_Unnn,dstUp,w_U4);
		dstUp+=dst_pitch_U;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_4; i<h_Y_max; i+=4)
		{
			if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_U,src_Upp,dstUp,w_U4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_U,src_Upp,dstUp,w_U4);
			dstUp+=dst_pitch_U;

			if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Up,src_Un,dstUp,w_U4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Up,src_Un,dstUp,w_U4);
			dstUp+=dst_pitch_U;

			A_memcpy(dstUp,src_U,w_U);
			dstUp+=dst_pitch_U;

			A_memcpy(dstUp,src_Un,w_U);
			dstUp+=dst_pitch_U;

			src_U+=pitch_U_2;
			src_Up+=pitch_U_2;
			src_Upp+=pitch_U_2;
			src_Un+=pitch_U_2;
			src_Unn+=pitch_U_2;
			src_Unnn+=pitch_U_2;
		}
	}


// V Planar
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<4; i+=4)
		{
			A_memcpy(dstVp,src_V,w_V);
			dstVp+=dst_pitch_V;

			A_memcpy(dstVp,src_Vn,w_V);
			dstVp+=dst_pitch_V;

			if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vnn,src_V,dstVp,w_V4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vnn,src_V,dstVp,w_V4);
			dstVp+=dst_pitch_V;

			if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Vn,src_Vnnn,dstVp,w_V4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Vn,src_Vnnn,dstVp,w_V4);
			dstVp+=dst_pitch_V;

			src_V+=pitch_V_2;
			src_Vp+=pitch_V_2;
			src_Vpp+=pitch_V_2;
			src_Vn+=pitch_V_2;
			src_Vnn+=pitch_V_2;
			src_Vnnn+=pitch_V_2;
		}
	}

	for(int32_t i=h_0; i<h_4; i+=4)
	{
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_V,src_Vpp,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_V,src_Vpp,dstVp,w_V4);
		dstVp+=dst_pitch_V;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vp,src_Vn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vp,src_Vn,dstVp,w_V4);
		dstVp+=dst_pitch_V;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vnn,src_V,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vnn,src_V,dstVp,w_V4);
		dstVp+=dst_pitch_V;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_Vn,src_Vnnn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_Vn,src_Vnnn,dstVp,w_V4);
		dstVp+=dst_pitch_V;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_4; i<h_Y_max; i+=4)
		{
			if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3b(src_V,src_Vpp,dstVp,w_V4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_3(src_V,src_Vpp,dstVp,w_V4);
			dstVp+=dst_pitch_V;

			if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2b(src_Vp,src_Vn,dstVp,w_V4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_2(src_Vp,src_Vn,dstVp,w_V4);
			dstVp+=dst_pitch_V;

			A_memcpy(dstVp,src_V,w_V);
			dstVp+=dst_pitch_V;

			A_memcpy(dstVp,src_Vn,w_V);
			dstVp+=dst_pitch_V;

			src_V+=pitch_V_2;
			src_Vp+=pitch_V_2;
			src_Vpp+=pitch_V_2;
			src_Vn+=pitch_V_2;
			src_Vnn+=pitch_V_2;
			src_Vnnn+=pitch_V_2;
		}
	}
}


void AutoYUY2::Convert_Interlaced_YV16(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	uint8_t *dstYp=(uint8_t *)mt_data_inf.dst1;
	uint8_t *dstUp=(uint8_t *)mt_data_inf.dst2;
	uint8_t *dstVp=(uint8_t *)mt_data_inf.dst3;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitch_Y=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch_Y=mt_data_inf.dst_pitch1;
	const int dst_pitch_U=mt_data_inf.dst_pitch2;
	const int dst_pitch_V=mt_data_inf.dst_pitch3;

	const uint8_t *src_U,*src_Up,*src_Upp,*src_Un,*src_Unn,*src_Unnn;
	const uint8_t *src_V,*src_Vp,*src_Vpp,*src_Vn,*src_Vnn,*src_Vnnn;
	uint8_t *dst_V,*dst_U;
	const int32_t w_U=dst_w>>1,w_V=dst_w>>1;
	const uint16_t *lookup=lookup_Upscale;
	const int32_t h_4 = mt_data_inf.bottom ? h_Y_max-4:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 4:h_Y_min;

	const ptrdiff_t pitch_U_2=2*src_pitchU;
	const ptrdiff_t pitch_V_2=2*src_pitchV;

	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchU;
	src_Upp=src_U-2*src_pitchU;
	src_Un=src_U+src_pitchU;
	src_Unn=src_U+2*src_pitchU;
	src_Unnn=src_U+3*src_pitchU;
	src_Vp=src_V-src_pitchV;
	src_Vpp=src_V-2*src_pitchV;
	src_Vn=src_V+src_pitchV;
	src_Vnn=src_V+2*src_pitchV;
	src_Vnnn=src_V+3*src_pitchV;
	dst_U=dstUp;
	dst_V=dstVp;

	Move_Full(srcYp,dstYp,dst_w,h_Y_max-h_Y_min,src_pitch_Y,dst_pitch_Y);


// Planar U
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<4; i+=4)
		{
			A_memcpy(dst_U,src_U,w_U);
			dst_U+=dst_pitch_U;

			A_memcpy(dst_U,src_Un,w_U);
			dst_U+=dst_pitch_U;

			for(int32_t j=0; j<w_U; j++)
				dst_U[j]=(uint8_t)((lookup[src_Unn[j]]+lookup[(uint16_t)src_U[j]+256]+4)>>3);
			dst_U+=dst_pitch_U;

			for(int32_t j=0; j<w_U; j++)
				dst_U[j]=(uint8_t)((lookup[(uint16_t)src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);
			dst_U+=dst_pitch_U;

			src_U+=pitch_U_2;
			src_Up+=pitch_U_2;
			src_Upp+=pitch_U_2;
			src_Un+=pitch_U_2;
			src_Unn+=pitch_U_2;
			src_Unnn+=pitch_U_2;
		}
	}

	for(int32_t i=h_0; i<h_4; i+=4)
	{
		for(int32_t j=0; j<w_U; j++)
			dst_U[j]=(uint8_t)((lookup[(uint16_t)src_U[j]+512]+(uint16_t)src_Upp[j]+4)>>3);
		dst_U+=dst_pitch_U;

		for(int32_t j=0; j<w_U; j++)
			dst_U[j]=(uint8_t)((lookup[src_Up[j]]+lookup[(uint16_t)src_Un[j]+256]+4)>>3);
		dst_U+=dst_pitch_U;

		for(int32_t j=0; j<w_U; j++)
			dst_U[j]=(uint8_t)((lookup[src_Unn[j]]+lookup[(uint16_t)src_U[j]+256]+4)>>3);
		dst_U+=dst_pitch_U;

		for(int32_t j=0; j<w_U; j++)
			dst_U[j]=(uint8_t)((lookup[(uint16_t)src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);
		dst_U+=dst_pitch_U;

		src_U+=pitch_U_2;
		src_Up+=pitch_U_2;
		src_Upp+=pitch_U_2;
		src_Un+=pitch_U_2;
		src_Unn+=pitch_U_2;
		src_Unnn+=pitch_U_2;
	}

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_4; i<h_Y_max; i+=4)
		{
			for(int32_t j=0; j<w_U; j++)
				dst_U[j]=(uint8_t)((lookup[(uint16_t)src_U[j]+512]+(uint16_t)src_Upp[j]+4)>>3);
			dst_U+=dst_pitch_U;

			for(int32_t j=0; j<w_U; j++)
				dst_U[j]=(uint8_t)((lookup[src_Up[j]]+lookup[(uint16_t)src_Un[j]+256]+4)>>3);
			dst_U+=dst_pitch_U;

			A_memcpy(dst_U,src_U,w_U);
			dst_U+=dst_pitch_U;

			A_memcpy(dst_U,src_Un,w_U);
			dst_U+=dst_pitch_U;

			src_U+=pitch_U_2;
			src_Up+=pitch_U_2;
			src_Upp+=pitch_U_2;
			src_Un+=pitch_U_2;
			src_Unn+=pitch_U_2;
			src_Unnn+=pitch_U_2;
		}
	}


// Planar V
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<4; i+=4)
		{
			A_memcpy(dst_V,src_V,w_V);
			dst_V+=dst_pitch_V;

			A_memcpy(dst_V,src_Vn,w_V);
			dst_V+=dst_pitch_V;

			for(int32_t j=0; j<w_V; j++)
				dst_V[j]=(uint8_t)((lookup[src_Vnn[j]]+lookup[(uint16_t)src_V[j]+256]+4)>>3);
			dst_V+=dst_pitch_V;

			for(int32_t j=0; j<w_V; j++)
				dst_V[j]=(uint8_t)((lookup[(uint16_t)src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
			dst_V+=dst_pitch_V;

			src_V+=pitch_V_2;
			src_Vp+=pitch_V_2;
			src_Vpp+=pitch_V_2;
			src_Vn+=pitch_V_2;
			src_Vnn+=pitch_V_2;
			src_Vnnn+=pitch_V_2;
		}
	}

	for(int32_t i=h_0; i<h_4; i+=4)
	{
		for(int32_t j=0; j<w_V; j++)
			dst_V[j]=(uint8_t)((lookup[(uint16_t)src_V[j]+512]+(uint16_t)src_Vpp[j]+4)>>3);
		dst_V+=dst_pitch_V;

		for(int32_t j=0; j<w_V; j++)
			dst_V[j]=(uint8_t)((lookup[src_Vp[j]]+lookup[(uint16_t)src_Vn[j]+256]+4)>>3);
		dst_V+=dst_pitch_V;

		for(int32_t j=0; j<w_V; j++)
			dst_V[j]=(uint8_t)((lookup[src_Vnn[j]]+lookup[(uint16_t)src_V[j]+256]+4)>>3);
		dst_V+=dst_pitch_V;

		for(int32_t j=0; j<w_V; j++)
			dst_V[j]=(uint8_t)((lookup[(uint16_t)src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
		dst_V+=dst_pitch_V;

		src_V+=pitch_V_2;
		src_Vp+=pitch_V_2;
		src_Vpp+=pitch_V_2;
		src_Vn+=pitch_V_2;
		src_Vnn+=pitch_V_2;
		src_Vnnn+=pitch_V_2;
	}

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_4; i<h_Y_max; i+=4)
		{
			for(int32_t j=0; j<w_V; j++)
				dst_V[j]=(uint8_t)((lookup[(uint16_t)src_V[j]+512]+(uint16_t)src_Vpp[j]+4)>>3);
			dst_V+=dst_pitch_V;

			for(int32_t j=0; j<w_V; j++)
				dst_V[j]=(uint8_t)((lookup[src_Vp[j]]+lookup[(uint16_t)src_Vn[j]+256]+4)>>3);
			dst_V+=dst_pitch_V;

			A_memcpy(dst_V,src_V,w_V);
			dst_V+=dst_pitch_V;

			A_memcpy(dst_V,src_Vn,w_V);
			dst_V+=dst_pitch_V;

			src_V+=pitch_V_2;
			src_Vp+=pitch_V_2;
			src_Vpp+=pitch_V_2;
			src_Vn+=pitch_V_2;
			src_Vnn+=pitch_V_2;
			src_Vnnn+=pitch_V_2;
		}
	}
}



void AutoYUY2::Convert_Progressive_YV16_SSE(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	uint8_t *dstYp=(uint8_t *)mt_data_inf.dst1;
	uint8_t *dstUp=(uint8_t *)mt_data_inf.dst2;
	uint8_t *dstVp=(uint8_t *)mt_data_inf.dst3;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitch_Y=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch_Y=mt_data_inf.dst_pitch1;
	const int dst_pitch_U=mt_data_inf.dst_pitch2;
	const int dst_pitch_V=mt_data_inf.dst_pitch3;

	const uint8_t *src_U,*src_Up,*src_Un;
	const uint8_t *src_V,*src_Vp,*src_Vn;
	bool _align_U=false,_align_V=false;
	const int32_t w_U=dst_w>>1,w_V=dst_w>>1;
	const int32_t w_U4=w_U>>2,w_V4=w_V>>2;
	const int32_t h_2 = mt_data_inf.bottom ? h_Y_max-2:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 2:h_Y_min;

	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchU;
	src_Un=src_U+src_pitchU;
	src_Vp=src_V-src_pitchV;
	src_Vn=src_V+src_pitchV;

	if ((((size_t)src_U & 0x0F)==0) && (((size_t)dstUp & 0x0F)==0) && ((abs(src_pitchU) & 0x0F)==0) 
		&& ((abs(dst_pitch_U) & 0x0F)==0)) _align_U=true;
	if ((((size_t)src_V & 0x0F)==0) && (((size_t)dstVp & 0x0F)==0) && ((abs(src_pitchV) & 0x0F)==0) 
		&& ((abs(dst_pitch_V) & 0x0F)==0)) _align_V=true;

	Move_Full(srcYp,dstYp,dst_w,h_Y_max-h_Y_min,src_pitch_Y,dst_pitch_Y);

// Planar U
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<2; i+=2)
		{
			A_memcpy(dstUp,src_U,w_U);
			dstUp+=dst_pitch_U;

			if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Un,dstUp,w_U4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Un,dstUp,w_U4);
			dstUp+=dst_pitch_U;

			src_U+=src_pitchU;
			src_Up+=src_pitchU;
			src_Un+=src_pitchU;
		}
	}

	for(int32_t i=h_0; i<h_2; i+=2)
	{
		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Up,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Up,dstUp,w_U4);
		dstUp+=dst_pitch_U;

		if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Un,dstUp,w_U4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Un,dstUp,w_U4);
		dstUp+=dst_pitch_U;

		src_U+=src_pitchU;
		src_Up+=src_pitchU;
		src_Un+=src_pitchU;
	}

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_2; i<h_Y_max; i+=2)
		{
			if (_align_U) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_U,src_Up,dstUp,w_U4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_U,src_Up,dstUp,w_U4);
			dstUp+=dst_pitch_U;

			A_memcpy(dstUp,src_U,w_U);
			dstUp+=dst_pitch_U;

			src_U+=src_pitchU;
			src_Up+=src_pitchU;
			src_Un+=src_pitchU;
		}
	}


// Planar V
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<2; i+=2)
		{
			A_memcpy(dstVp,src_V,w_V);
			dstVp+=dst_pitch_V;

			if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vn,dstVp,w_V4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vn,dstVp,w_V4);
			dstVp+=dst_pitch_V;

			src_V+=src_pitchV;
			src_Vp+=src_pitchV;
			src_Vn+=src_pitchV;
		}
	}

	for(int32_t i=h_0; i<h_2; i+=2)
	{
		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vp,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vp,dstVp,w_V4);
		dstVp+=dst_pitch_V;

		if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vn,dstVp,w_V4);
		else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vn,dstVp,w_V4);
		dstVp+=dst_pitch_V;

		src_V+=src_pitchV;
		src_Vp+=src_pitchV;
		src_Vn+=src_pitchV;
	}

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_2; i<h_Y_max; i+=2)
		{
			if (_align_V) JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4b(src_V,src_Vp,dstVp,w_V4);
			else JPSDR_AutoYUY2_Convert420_to_Planar422_SSE2_4(src_V,src_Vp,dstVp,w_V4);
			dstVp+=dst_pitch_V;

			A_memcpy(dstVp,src_V,w_V);
			dstVp+=dst_pitch_V;

			src_V+=src_pitchV;
			src_Vp+=src_pitchV;
			src_Vn+=src_pitchV;
		}
	}
}



void AutoYUY2::Convert_Progressive_YV16(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	uint8_t *dstYp=(uint8_t *)mt_data_inf.dst1;
	uint8_t *dstUp=(uint8_t *)mt_data_inf.dst2;
	uint8_t *dstVp=(uint8_t *)mt_data_inf.dst3;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitch_Y=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch_Y=mt_data_inf.dst_pitch1;
	const int dst_pitch_U=mt_data_inf.dst_pitch2;
	const int dst_pitch_V=mt_data_inf.dst_pitch3;

	const uint8_t *src_U,*src_Up,*src_Un;
	const uint8_t *src_V,*src_Vp,*src_Vn;
	uint8_t *dst_U,*dst_V;
	const int32_t w_U=dst_w>>1,w_V=dst_w>>1;
	const uint16_t *lookup=lookup_Upscale;
	const int32_t h_2 = mt_data_inf.bottom ? h_Y_max-2:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 2:h_Y_min;

	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchU;
	src_Un=src_U+src_pitchU;
	src_Vp=src_V-src_pitchV;
	src_Vn=src_V+src_pitchV;
	dst_U=dstUp;
	dst_V=dstVp;

	Move_Full(srcYp,dstYp,dst_w,h_Y_max-h_Y_min,src_pitch_Y,dst_pitch_Y);


// Planar U
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<2; i+=2)
		{
			A_memcpy(dst_U,src_U,w_U);
			dst_U+=dst_pitch_U;

			for(int32_t j=0; j<w_U; j++)
				dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2)>>2);
			dst_U+=dst_pitch_U;

			src_U+=src_pitchU;
			src_Up+=src_pitchU;
			src_Un+=src_pitchU;
		}
	}

	for(int32_t i=h_0; i<h_2; i+=2)
	{
		for(int32_t j=0; j<w_U; j++)
			dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2)>>2);
		dst_U+=dst_pitch_U;

		for(int32_t j=0; j<w_U; j++)
			dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2)>>2);
		dst_U+=dst_pitch_U;

		src_U+=src_pitchU;
		src_Up+=src_pitchU;
		src_Un+=src_pitchU;
	}

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_2; i<h_Y_max; i+=2)
		{
			for(int32_t j=0; j<w_U; j++)
				dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2)>>2);
			dst_U+=dst_pitch_U;

			A_memcpy(dst_U,src_U,w_U);
			dst_U+=dst_pitch_U;

			src_U+=src_pitchU;
			src_Up+=src_pitchU;
			src_Un+=src_pitchU;
		}
	}


// Planar V
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<2; i+=2)
		{
			A_memcpy(dst_V,src_V,w_V);
			dst_V+=dst_pitch_V;

			for(int32_t j=0; j<w_V; j++)
				dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2)>>2);
			dst_V+=dst_pitch_V;

			src_V+=src_pitchV;
			src_Vp+=src_pitchV;
			src_Vn+=src_pitchV;
		}
	}

	for(int32_t i=h_0; i<h_2; i+=2)
	{
		for(int32_t j=0; j<w_V; j++)
			dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2)>>2);
		dst_V+=dst_pitch_V;

		for(int32_t j=0; j<w_V; j++)
			dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2)>>2);
		dst_V+=dst_pitch_V;

		src_V+=src_pitchV;
		src_Vp+=src_pitchV;
		src_Vn+=src_pitchV;
	}

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_2; i<h_Y_max; i+=2)
		{
			for(int32_t j=0; j<w_V; j++)
				dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2)>>2);
			dst_V+=dst_pitch_V;

			A_memcpy(dst_V,src_V,w_V);
			dst_V+=dst_pitch_V;

			src_V+=src_pitchV;
			src_Vp+=src_pitchV;
			src_Vn+=src_pitchV;
		}
	}

}



void AutoYUY2::Convert_Automatic_YV16(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	uint8_t *dstYp=(uint8_t *)mt_data_inf.dst1;
	uint8_t *dstUp=(uint8_t *)mt_data_inf.dst2;
	uint8_t *dstVp=(uint8_t *)mt_data_inf.dst3;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitch_Y=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch_Y=mt_data_inf.dst_pitch1;
	const int dst_pitch_U=mt_data_inf.dst_pitch2;
	const int dst_pitch_V=mt_data_inf.dst_pitch3;

	const uint8_t *src_U,*src_Up,*src_Upp,*src_Un,*src_Unn,*src_Unnn;
	const uint8_t *src_V,*src_Vp,*src_Vpp,*src_Vn,*src_Vnn,*src_Vnnn;
	uint8_t *dst_U,*dst_V;
	uint8_t index_tab_0,index_tab_1,index_tab_2;
	const int16_t threshold_=threshold;
	const int32_t w_UV=dst_w>>1;
	const uint16_t *lookup=lookup_Upscale;
	const int32_t h_4 = mt_data_inf.bottom ? h_Y_max-4:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 4:h_Y_min;

	const ptrdiff_t pitch_U_2=2*src_pitchU;
	const ptrdiff_t pitch_V_2=2*src_pitchV;

	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchU;
	src_Upp=src_U-2*src_pitchU;
	src_Un=src_U+src_pitchU;
	src_Unn=src_U+2*src_pitchU;
	src_Unnn=src_U+3*src_pitchU;
	src_Vp=src_V-src_pitchV;
	src_Vpp=src_V-2*src_pitchV;
	src_Vn=src_V+src_pitchV;
	src_Vnn=src_V+2*src_pitchV;
	src_Vnnn=src_V+3*src_pitchV;
	dst_U=dstUp;
	dst_V=dstVp;

	Move_Full(srcYp,dstYp,dst_w,h_Y_max-h_Y_min,src_pitch_Y,dst_pitch_Y);

// Planar U	
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<4; i+=4)
		{
			A_memcpy(dst_U,src_U,w_UV);
			dst_U+=dst_pitch_U;

			A_memcpy(dst_U,src_Un,w_UV);
			dst_U+=dst_pitch_U;

			for(int32_t j=0; j<w_UV; j++)
				dst_U[j]=(uint8_t)((lookup[src_Unn[j]]+lookup[(uint16_t)src_U[j]+256]+4)>>3);
			dst_U+=dst_pitch_U;

			{
				bool *itabu0=interlaced_tab_U[thread_num][0],*itabu1=interlaced_tab_U[thread_num][1];
				
				for(int32_t j=0; j<w_UV; j++)
				{
					if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
						(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
						(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
						((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))))
						itabu0[j]=true;
					else itabu0[j]=false;
					if (((abs((int16_t)src_Un[j]-(int16_t)src_Unn[j])>=threshold_) &&
						(abs((int16_t)src_Unnn[j]-(int16_t)src_Unn[j])>=threshold_) &&
						(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
						((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))))
						itabu1[j]=true;
					else itabu1[j]=false;
					
					dst_U[j]=(uint8_t)((lookup[(uint16_t)src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);
				}
			}
			dst_U+=dst_pitch_U;

			src_U+=pitch_U_2;
			src_Up+=pitch_U_2;
			src_Upp+=pitch_U_2;
			src_Un+=pitch_U_2;
			src_Unn+=pitch_U_2;
			src_Unnn+=pitch_U_2;
		}
	}
	else
	{
		bool *itabu0=interlaced_tab_U[thread_num][0],*itabu1=interlaced_tab_U[thread_num][1];
				
		for(int32_t j=0; j<w_UV; j++)
		{
			if (((abs((int16_t)src_Upp[j]-(int16_t)src_Up[j])>=threshold_) &&
				(abs((int16_t)src_U[j]-(int16_t)src_Up[j])>=threshold_) &&
				(((src_Upp[j]>src_Up[j]) && (src_U[j]>src_Up[j])) ||
				((src_Upp[j]<src_Up[j]) && (src_U[j]<src_Up[j])))))
				itabu0[j]=true;
			else itabu0[j]=false;
			if (((abs((int16_t)src_Up[j]-(int16_t)src_U[j])>=threshold_) &&
				(abs((int16_t)src_Un[j]-(int16_t)src_U[j])>=threshold_) &&
				(((src_Up[j]>src_U[j]) && (src_Un[j]>src_U[j])) ||
				((src_Up[j]<src_U[j]) && (src_Un[j]<src_U[j])))))
				itabu1[j]=true;
			else itabu1[j]=false;
		}

	}

	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;

	for(int32_t i=h_0; i<h_4; i+=4)
	{
		{
			const bool *itabu0=interlaced_tab_U[thread_num][index_tab_0],*itabu1=interlaced_tab_U[thread_num][index_tab_1];

			// Upsample as needed.
			for(int32_t j=0; j<w_UV; j++)
			{
				if ((itabu0[j]) || (itabu1[j]))
				{
					dst_U[j]=(uint8_t)((lookup[(uint16_t)src_U[j]+512]+(uint16_t)src_Upp[j]+4)>>3);
				}
				else
				{
					dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2) >> 2);
				}
			}
		}
		dst_U+=dst_pitch_U;

		{
			const bool *itabu1=interlaced_tab_U[thread_num][index_tab_1];
			bool *itabu2=interlaced_tab_U[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
					(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
					(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
					((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))))
					itabu2[j]=true;
				else itabu2[j]=false;			

				// Upsample as needed.
				if ((itabu2[j]) || (itabu1[j]))
				{
					dst_U[j]=(uint8_t)((lookup[src_Up[j]]+lookup[(uint16_t)src_Un[j]+256]+4)>>3);
				}
				else
				{
					dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2)>>2);
				}
			}
		}
		dst_U+=dst_pitch_U;

		{
			const bool *itabu1=interlaced_tab_U[thread_num][index_tab_1],*itabu2=interlaced_tab_U[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				// Upsample as needed.
				if ((itabu1[j]) || (itabu2[j]))
				{
					dst_U[j]=(uint8_t)((lookup[src_Unn[j]]+lookup[(uint16_t)src_U[j]+256]+4)>>3);
				}
				else
				{
					dst_U[j]=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_U[j]+2)>>2);
				}
			}
		}
		dst_U+=dst_pitch_U;

		{
			bool *itabu0=interlaced_tab_U[thread_num][index_tab_0];
			const bool *itabu2=interlaced_tab_U[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)src_Un[j]-(int16_t)src_Unn[j])>=threshold_) &&
					(abs((int16_t)src_Unnn[j]-(int16_t)src_Unn[j])>=threshold_) &&
					(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
					((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))))
					itabu0[j]=true;
				else itabu0[j]=false;

				// Upsample as needed.
				if ((itabu0[j]) || (itabu2[j]))
				{
					dst_U[j]=(uint8_t)((lookup[(uint16_t)src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);
				}
				else
				{
					dst_U[j]=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_Unn[j]+2)>>2);
				}
			}
		}
		dst_U+=dst_pitch_U;

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

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_4; i<h_Y_max; i+=4)
		{
			for(int32_t j=0; j<w_UV; j++)
				dst_U[j]=(uint8_t)((lookup[(uint16_t)src_U[j]+512]+(uint16_t)src_Upp[j]+4)>>3);
			dst_U+=dst_pitch_U;

			for(int32_t j=0; j<w_UV; j++)
				dst_U[j]=(uint8_t)((lookup[src_Up[j]]+lookup[(uint16_t)src_Un[j]+256]+4)>>3);
			dst_U+=dst_pitch_U;

			A_memcpy(dst_U,src_U,w_UV);
			dst_U+=dst_pitch_U;

			A_memcpy(dst_U,src_Un,w_UV);
			dst_U+=dst_pitch_U;

			src_U+=pitch_U_2;
			src_Up+=pitch_U_2;
			src_Upp+=pitch_U_2;
			src_Un+=pitch_U_2;
			src_Unn+=pitch_U_2;
			src_Unnn+=pitch_U_2;
		}
	}


// Planar V
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<4; i+=4)
		{
			A_memcpy(dst_V,src_V,w_UV);
			dst_V+=dst_pitch_V;

			A_memcpy(dst_V,src_Vn,w_UV);
			dst_V+=dst_pitch_V;

			for(int32_t j=0; j<w_UV; j++)
				dst_V[j]=(uint8_t)((lookup[src_Vnn[j]]+lookup[(uint16_t)src_V[j]+256]+4)>>3);
			dst_V+=dst_pitch_V;

			{
				bool *itabv0=interlaced_tab_V[thread_num][0],*itabv1=interlaced_tab_V[thread_num][1];

				for(int32_t j=0; j<w_UV; j++)
				{
					if (((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
						(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
						(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
						((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
						itabv0[j]=true;
					else itabv0[j]=false;
					if (((abs((int16_t)src_Vn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
						(abs((int16_t)src_Vnnn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
						(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
						((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
						itabv1[j]=true;
					else itabv1[j]=false;
					
					dst_V[j]=(uint8_t)((lookup[(uint16_t)src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
				}
			}
			dst_V+=dst_pitch_V;

			src_V+=pitch_V_2;
			src_Vp+=pitch_V_2;
			src_Vpp+=pitch_V_2;
			src_Vn+=pitch_V_2;
			src_Vnn+=pitch_V_2;
			src_Vnnn+=pitch_V_2;
		}
	}
	else
	{
		bool *itabv0=interlaced_tab_V[thread_num][0],*itabv1=interlaced_tab_V[thread_num][1];

		for(int32_t j=0; j<w_UV; j++)
		{
			if (((abs((int16_t)src_Vpp[j]-(int16_t)src_Vp[j])>=threshold_) &&
				(abs((int16_t)src_V[j]-(int16_t)src_Vp[j])>=threshold_) &&
				(((src_Vpp[j]>src_Vp[j]) && (src_V[j]>src_Vp[j])) ||
				((src_Vpp[j]<src_Vp[j]) && (src_V[j]<src_Vp[j])))))
				itabv0[j]=true;
			else itabv0[j]=false;
			if (((abs((int16_t)src_Vp[j]-(int16_t)src_V[j])>=threshold_) &&
				(abs((int16_t)src_Vn[j]-(int16_t)src_V[j])>=threshold_) &&
				(((src_Vp[j]>src_V[j]) && (src_Vn[j]>src_V[j])) ||
				((src_Vp[j]<src_V[j]) && (src_Vn[j]<src_V[j])))))
				itabv1[j]=true;
			else itabv1[j]=false;
		}
	}
	
	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;

	for(int32_t i=h_0; i<h_4; i+=4)
	{
		{
			const bool *itabv0=interlaced_tab_V[thread_num][index_tab_0],*itabv1=interlaced_tab_V[thread_num][index_tab_1];

			for(int32_t j=0; j<w_UV; j++)
			{
				// Upsample as needed.
				if ((itabv0[j]) || (itabv1[j]))
				{
					dst_V[j]=(uint8_t)((lookup[(uint16_t)src_V[j]+512]+(uint16_t)src_Vpp[j]+4)>>3);
				}
				else
				{
					dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2) >> 2);
				}
			}
		}
		dst_V+=dst_pitch_V;

		{
			const bool *itabv1=interlaced_tab_V[thread_num][index_tab_1];
			bool *itabv2=interlaced_tab_V[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
					(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
					(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
					((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
					itabv2[j]=true;
				else itabv2[j]=false;			

				// Upsample as needed.
				if ((itabv2[j]) || (itabv1[j]))
				{
					dst_V[j]=(uint8_t)((lookup[src_Vp[j]]+lookup[(uint16_t)src_Vn[j]+256]+4)>>3);
				}
				else
				{
					dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2)>>2);
				}
			}
		}
		dst_V+=dst_pitch_V;

		{
			const bool *itabv1=interlaced_tab_V[thread_num][index_tab_1],*itabv2=interlaced_tab_V[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				// Upsample as needed.
				if ((itabv1[j]) || (itabv2[j]))
				{
					dst_V[j]=(uint8_t)((lookup[src_Vnn[j]]+lookup[(uint16_t)src_V[j]+256]+4)>>3);
				}
				else
				{
					dst_V[j]=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_V[j]+2)>>2);
				}
			}
		}
		dst_V+=dst_pitch_V;

		{
			bool *itabv0=interlaced_tab_V[thread_num][index_tab_0];
			const bool *itabv2=interlaced_tab_V[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)src_Vn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
					(abs((int16_t)src_Vnnn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
					(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
					((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
					itabv0[j]=true;
				else itabv0[j]=false;

				// Upsample as needed.
				if ((itabv0[j]) || (itabv2[j]))
				{
					dst_V[j]=(uint8_t)((lookup[(uint16_t)src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
				}
				else
				{
					dst_V[j]=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_Vnn[j]+2)>>2);
				}
			}
		}
		dst_V+=dst_pitch_V;

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

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_4; i<h_Y_max; i+=4)
		{
			for(int32_t j=0; j<w_UV; j++)
				dst_V[j]=(uint8_t)((lookup[(uint16_t)src_V[j]+512]+(uint16_t)src_Vpp[j]+4)>>3);
			dst_V+=dst_pitch_V;

			for(int32_t j=0; j<w_UV; j++)
				dst_V[j]=(uint8_t)((lookup[src_Vp[j]]+lookup[(uint16_t)src_Vn[j]+256]+4)>>3);
			dst_V+=dst_pitch_V;

			A_memcpy(dst_V,src_V,w_UV);
			dst_V+=dst_pitch_V;

			A_memcpy(dst_V,src_Vn,w_UV);
			dst_V+=dst_pitch_V;

			src_V+=pitch_V_2;
			src_Vp+=pitch_V_2;
			src_Vpp+=pitch_V_2;
			src_Vn+=pitch_V_2;
			src_Vnn+=pitch_V_2;
			src_Vnnn+=pitch_V_2;
		}
	}
}



void AutoYUY2::Convert_Test_YV16(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	uint8_t *dstYp=(uint8_t *)mt_data_inf.dst1;
	uint8_t *dstUp=(uint8_t *)mt_data_inf.dst2;
	uint8_t *dstVp=(uint8_t *)mt_data_inf.dst3;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitch_Y=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch_Y=mt_data_inf.dst_pitch1;
	const int dst_pitch_U=mt_data_inf.dst_pitch2;
	const int dst_pitch_V=mt_data_inf.dst_pitch3;

	const uint8_t *src_U,*src_Up,*src_Upp,*src_Un,*src_Unn,*src_Unnn;
	const uint8_t *src_V,*src_Vp,*src_Vpp,*src_Vn,*src_Vnn,*src_Vnnn;
	uint8_t *dst_U,*dst_V;
	uint8_t index_tab_0,index_tab_1,index_tab_2;
	const int16_t threshold_=threshold;
	const int32_t w_UV=dst_w>>1;
	const uint16_t *lookup=lookup_Upscale;
	const int32_t h_4 = mt_data_inf.bottom ? h_Y_max-4:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 4:h_Y_min;

	const ptrdiff_t pitch_U_2=2*src_pitchU;
	const ptrdiff_t pitch_V_2=2*src_pitchV;

	src_U=srcUp;
	src_V=srcVp;
	src_Up=src_U-src_pitchU;
	src_Upp=src_U-2*src_pitchU;
	src_Un=src_U+src_pitchU;
	src_Unn=src_U+2*src_pitchU;
	src_Unnn=src_U+3*src_pitchU;
	src_Vp=src_V-src_pitchV;
	src_Vpp=src_V-2*src_pitchV;
	src_Vn=src_V+src_pitchV;
	src_Vnn=src_V+2*src_pitchV;
	src_Vnnn=src_V+3*src_pitchV;
	dst_U=dstUp;
	dst_V=dstVp;

	Move_Full(srcYp,dstYp,dst_w,h_Y_max-h_Y_min,src_pitch_Y,dst_pitch_Y);

// Planar U	
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<4; i+=4)
		{
			A_memcpy(dst_U,src_U,w_UV);
			dst_U+=dst_pitch_U;

			A_memcpy(dst_U,src_Un,w_UV);
			dst_U+=dst_pitch_U;

			for(int32_t j=0; j<w_UV; j++)
				dst_U[j]=(uint8_t)((lookup[src_Unn[j]]+lookup[(uint16_t)src_U[j]+256]+4)>>3);
			dst_U+=dst_pitch_U;

			{
				bool *itabu0=interlaced_tab_U[thread_num][0],*itabu1=interlaced_tab_U[thread_num][1];
				
				for(int32_t j=0; j<w_UV; j++)
				{
					if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
						(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
						(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
						((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))))
						itabu0[j]=true;
					else itabu0[j]=false;
					if (((abs((int16_t)src_Un[j]-(int16_t)src_Unn[j])>=threshold_) &&
						(abs((int16_t)src_Unnn[j]-(int16_t)src_Unn[j])>=threshold_) &&
						(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
						((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))))
						itabu1[j]=true;
					else itabu1[j]=false;
					
					dst_U[j]=(uint8_t)((lookup[(uint16_t)src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);
				}
			}
			dst_U+=dst_pitch_U;

			src_U+=pitch_U_2;
			src_Up+=pitch_U_2;
			src_Upp+=pitch_U_2;
			src_Un+=pitch_U_2;
			src_Unn+=pitch_U_2;
			src_Unnn+=pitch_U_2;
		}
	}
	else
	{
		bool *itabu0=interlaced_tab_U[thread_num][0],*itabu1=interlaced_tab_U[thread_num][1];
				
		for(int32_t j=0; j<w_UV; j++)
		{
			if (((abs((int16_t)src_Upp[j]-(int16_t)src_Up[j])>=threshold_) &&
				(abs((int16_t)src_U[j]-(int16_t)src_Up[j])>=threshold_) &&
				(((src_Upp[j]>src_Up[j]) && (src_U[j]>src_Up[j])) ||
				((src_Upp[j]<src_Up[j]) && (src_U[j]<src_Up[j])))))
				itabu0[j]=true;
			else itabu0[j]=false;
			if (((abs((int16_t)src_Up[j]-(int16_t)src_U[j])>=threshold_) &&
				(abs((int16_t)src_Un[j]-(int16_t)src_U[j])>=threshold_) &&
				(((src_Up[j]>src_U[j]) && (src_Un[j]>src_U[j])) ||
				((src_Up[j]<src_U[j]) && (src_Un[j]<src_U[j])))))
				itabu1[j]=true;
			else itabu1[j]=false;
		}

	}

	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;

	for(int32_t i=h_0; i<h_4; i+=4)
	{
		{
			const bool *itabu0=interlaced_tab_U[thread_num][index_tab_0],*itabu1=interlaced_tab_U[thread_num][index_tab_1];

			// Upsample as needed.
			for(int32_t j=0; j<w_UV; j++)
			{
				if ((itabu0[j]) || (itabu1[j]))
				{
					dst_U[j]=239;
				}
				else
				{
					dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2) >> 2);
				}
			}
		}
		dst_U+=dst_pitch_U;

		{
			const bool *itabu1=interlaced_tab_U[thread_num][index_tab_1];
			bool *itabu2=interlaced_tab_U[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
					(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
					(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
					((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))))
					itabu2[j]=true;
				else itabu2[j]=false;			

				// Upsample as needed.
				if ((itabu2[j]) || (itabu1[j]))
				{
					dst_U[j]=239;
				}
				else
				{
					dst_U[j]=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2)>>2);
				}
			}
		}
		dst_U+=dst_pitch_U;

		{
			const bool *itabu1=interlaced_tab_U[thread_num][index_tab_1],*itabu2=interlaced_tab_U[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				// Upsample as needed.
				if ((itabu1[j]) || (itabu2[j]))
				{
					dst_U[j]=239;
				}
				else
				{
					dst_U[j]=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_U[j]+2)>>2);
				}
			}
		}
		dst_U+=dst_pitch_U;

		{
			bool *itabu0=interlaced_tab_U[thread_num][index_tab_0];
			const bool *itabu2=interlaced_tab_U[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)src_Un[j]-(int16_t)src_Unn[j])>=threshold_) &&
					(abs((int16_t)src_Unnn[j]-(int16_t)src_Unn[j])>=threshold_) &&
					(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
					((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))))
					itabu0[j]=true;
				else itabu0[j]=false;

				// Upsample as needed.
				if ((itabu0[j]) || (itabu2[j]))
				{
					dst_U[j]=239;
				}
				else
				{
					dst_U[j]=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_Unn[j]+2)>>2);
				}
			}
		}
		dst_U+=dst_pitch_U;

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

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_4; i<h_Y_max; i+=4)
		{
			for(int32_t j=0; j<w_UV; j++)
				dst_U[j]=(uint8_t)((lookup[(uint16_t)src_U[j]+512]+(uint16_t)src_Upp[j]+4)>>3);
			dst_U+=dst_pitch_U;

			for(int32_t j=0; j<w_UV; j++)
				dst_U[j]=(uint8_t)((lookup[src_Up[j]]+lookup[(uint16_t)src_Un[j]+256]+4)>>3);
			dst_U+=dst_pitch_U;

			A_memcpy(dst_U,src_U,w_UV);
			dst_U+=dst_pitch_U;

			A_memcpy(dst_U,src_Un,w_UV);
			dst_U+=dst_pitch_U;

			src_U+=pitch_U_2;
			src_Up+=pitch_U_2;
			src_Upp+=pitch_U_2;
			src_Un+=pitch_U_2;
			src_Unn+=pitch_U_2;
			src_Unnn+=pitch_U_2;
		}
	}


// Planar V
	if (mt_data_inf.top)
	{
		for(int32_t i=0; i<4; i+=4)
		{
			A_memcpy(dst_V,src_V,w_UV);
			dst_V+=dst_pitch_V;

			A_memcpy(dst_V,src_Vn,w_UV);
			dst_V+=dst_pitch_V;

			for(int32_t j=0; j<w_UV; j++)
				dst_V[j]=(uint8_t)((lookup[src_Vnn[j]]+lookup[(uint16_t)src_V[j]+256]+4)>>3);
			dst_V+=dst_pitch_V;

			{
				bool *itabv0=interlaced_tab_V[thread_num][0],*itabv1=interlaced_tab_V[thread_num][1];

				for(int32_t j=0; j<w_UV; j++)
				{
					if (((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
						(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
						(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
						((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
						itabv0[j]=true;
					else itabv0[j]=false;
					if (((abs((int16_t)src_Vn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
						(abs((int16_t)src_Vnnn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
						(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
						((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
						itabv1[j]=true;
					else itabv1[j]=false;
					
					dst_V[j]=(uint8_t)((lookup[(uint16_t)src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
				}
			}
			dst_V+=dst_pitch_V;

			src_V+=pitch_V_2;
			src_Vp+=pitch_V_2;
			src_Vpp+=pitch_V_2;
			src_Vn+=pitch_V_2;
			src_Vnn+=pitch_V_2;
			src_Vnnn+=pitch_V_2;
		}
	}
	else
	{
		bool *itabv0=interlaced_tab_V[thread_num][0],*itabv1=interlaced_tab_V[thread_num][1];

		for(int32_t j=0; j<w_UV; j++)
		{
			if (((abs((int16_t)src_Vpp[j]-(int16_t)src_Vp[j])>=threshold_) &&
				(abs((int16_t)src_V[j]-(int16_t)src_Vp[j])>=threshold_) &&
				(((src_Vpp[j]>src_Vp[j]) && (src_V[j]>src_Vp[j])) ||
				((src_Vpp[j]<src_Vp[j]) && (src_V[j]<src_Vp[j])))))
				itabv0[j]=true;
			else itabv0[j]=false;
			if (((abs((int16_t)src_Vp[j]-(int16_t)src_V[j])>=threshold_) &&
				(abs((int16_t)src_Vn[j]-(int16_t)src_V[j])>=threshold_) &&
				(((src_Vp[j]>src_V[j]) && (src_Vn[j]>src_V[j])) ||
				((src_Vp[j]<src_V[j]) && (src_Vn[j]<src_V[j])))))
				itabv1[j]=true;
			else itabv1[j]=false;
		}
	}
	
	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;

	for(int32_t i=h_0; i<h_4; i+=4)
	{
		{
			const bool *itabv0=interlaced_tab_V[thread_num][index_tab_0],*itabv1=interlaced_tab_V[thread_num][index_tab_1];

			for(int32_t j=0; j<w_UV; j++)
			{
				// Upsample as needed.
				if ((itabv0[j]) || (itabv1[j]))
				{
					dst_V[j]=239;
				}
				else
				{
					dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2) >> 2);
				}
			}
		}
		dst_V+=dst_pitch_V;

		{
			const bool *itabv1=interlaced_tab_V[thread_num][index_tab_1];
			bool *itabv2=interlaced_tab_V[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
					(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
					(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
					((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
					itabv2[j]=true;
				else itabv2[j]=false;			

				// Upsample as needed.
				if ((itabv2[j]) || (itabv1[j]))
				{
					dst_V[j]=239;
				}
				else
				{
					dst_V[j]=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2)>>2);
				}
			}
		}
		dst_V+=dst_pitch_V;

		{
			const bool *itabv1=interlaced_tab_V[thread_num][index_tab_1],*itabv2=interlaced_tab_V[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				// Upsample as needed.
				if ((itabv1[j]) || (itabv2[j]))
				{
					dst_V[j]=239;
				}
				else
				{
					dst_V[j]=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_V[j]+2)>>2);
				}
			}
		}
		dst_V+=dst_pitch_V;

		{
			bool *itabv0=interlaced_tab_V[thread_num][index_tab_0];
			const bool *itabv2=interlaced_tab_V[thread_num][index_tab_2];

			for(int32_t j=0; j<w_UV; j++)
			{
				if (((abs((int16_t)src_Vn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
					(abs((int16_t)src_Vnnn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
					(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
					((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
					itabv0[j]=true;
				else itabv0[j]=false;

				// Upsample as needed.
				if ((itabv0[j]) || (itabv2[j]))
				{
					dst_V[j]=239;
				}
				else
				{
					dst_V[j]=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_Vnn[j]+2)>>2);
				}
			}
		}
		dst_V+=dst_pitch_V;

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

	if (mt_data_inf.bottom)
	{
		for(int32_t i=h_4; i<h_Y_max; i+=4)
		{
			for(int32_t j=0; j<w_UV; j++)
				dst_V[j]=(uint8_t)((lookup[(uint16_t)src_V[j]+512]+(uint16_t)src_Vpp[j]+4)>>3);
			dst_V+=dst_pitch_V;

			for(int32_t j=0; j<w_UV; j++)
				dst_V[j]=(uint8_t)((lookup[src_Vp[j]]+lookup[(uint16_t)src_Vn[j]+256]+4)>>3);
			dst_V+=dst_pitch_V;

			A_memcpy(dst_V,src_V,w_UV);
			dst_V+=dst_pitch_V;

			A_memcpy(dst_V,src_Vn,w_UV);
			dst_V+=dst_pitch_V;

			src_V+=pitch_V_2;
			src_Vp+=pitch_V_2;
			src_Vpp+=pitch_V_2;
			src_Vn+=pitch_V_2;
			src_Vnn+=pitch_V_2;
			src_Vnnn+=pitch_V_2;
		}
	}
}



void AutoYUY2::Convert_Progressive_YUY2(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	YUYV *dst=(YUYV *)mt_data_inf.dst1;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitchY=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch=mt_data_inf.dst_pitch1;

	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up, *src_Un;
	const uint8_t *src_V,*src_Vp, *src_Vn;
	const int32_t w_UV=dst_w>>2;
	const uint16_t *lookup=lookup_Upscale;
	const int32_t h_2 = mt_data_inf.bottom ? h_Y_max-2:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 2:h_Y_min;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchU;
	src_Un = srcUp + src_pitchU;
	src_V = srcVp;
	src_Vp = srcVp - src_pitchV;
	src_Vn = srcVp + src_pitchV;

	if (mt_data_inf.top)
	{
		for (int32_t y=0; y<2; y+=2)
		{

			JPSDR_AutoYUY2_1(src_Y,src_U,src_V,(uint8_t *)dst,dst_w>>2);
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2) >> 2);
					dst[j].v=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2) >> 2);
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			src_U += src_pitchU;
			src_Up += src_pitchU;
			src_Un += src_pitchU;
			src_V += src_pitchV;
			src_Vp += src_pitchV;
			src_Vn += src_pitchV;
		}
	}

	for (int32_t y = h_0; y < h_2; y+=2)
	{
		{
			int32_t i=0;

			for (int32_t j=0; j<w_UV; j++)
			{
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;
				dst[j].u=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2) >> 2);
				dst[j].v=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2) >> 2);
			}
		}
		
		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;

		{
			int32_t i=0;

			for (int32_t j=0; j<w_UV; j++)
			{
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;
				dst[j].u=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2) >> 2);
				dst[j].v=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2) >> 2);
			}
		}
		
		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;
		
		src_U += src_pitchU;
		src_Up += src_pitchU;
		src_Un += src_pitchU;
		src_V += src_pitchV;
		src_Vp += src_pitchV;
		src_Vn += src_pitchV;	
	}

	if (mt_data_inf.bottom)
	{
		for (int32_t y = h_2; y < h_Y_max; y+=2)
		{
			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2) >> 2);
					dst[j].v=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2) >> 2);
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			JPSDR_AutoYUY2_1(src_Y,src_U,src_V,(uint8_t *)dst,dst_w>>2);
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;
		
			src_U += src_pitchU;
			src_Up += src_pitchU;
			src_Un += src_pitchU;
			src_V += src_pitchV;
			src_Vp += src_pitchV;
			src_Vn += src_pitchV;	
		}
	}
}



void AutoYUY2::Convert_Progressive_YUY2_SSE(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	uint8_t *dstp=(uint8_t *)mt_data_inf.dst1;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitchY=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch=mt_data_inf.dst_pitch1;

	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up, *src_Un;
	const uint8_t *src_V,*src_Vp, *src_Vn;
	const int32_t w_8=dst_w>>3;
	const int32_t h_2 = mt_data_inf.bottom ? h_Y_max-2:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 2:h_Y_min;

	bool _align=false;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchU;
	src_Un = srcUp + src_pitchU;
	src_V = srcVp;
	src_Vp = srcVp - src_pitchV;
	src_Vn = srcVp + src_pitchV;

	if ((((size_t)dstp & 0x0F)==0) && ((abs(dst_pitch) & 0x0F)==0)) _align=true;

	if (mt_data_inf.top)
	{
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

			src_U += src_pitchU;
			src_Up += src_pitchU;
			src_Un += src_pitchU;
			src_V += src_pitchV;
			src_Vp += src_pitchV;
			src_Vn += src_pitchV;
		}
	}

	for (int32_t y = h_0; y < h_2; y+=2)
	{
		if (_align) JPSDR_AutoYUY2_SSE2_4b(src_Y,src_U,src_Up,src_V,src_Vp,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_4(src_Y,src_U,src_Up,src_V,src_Vp,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;

		if (_align) JPSDR_AutoYUY2_SSE2_4b(src_Y,src_U,src_Un,src_V,src_Vn,dstp,w_8);
		else JPSDR_AutoYUY2_SSE2_4(src_Y,src_U,src_Un,src_V,src_Vn,dstp,w_8);
		dstp += dst_pitch;
		src_Y += src_pitchY;
		
		src_U += src_pitchU;
		src_Up += src_pitchU;
		src_Un += src_pitchU;
		src_V += src_pitchV;
		src_Vp += src_pitchV;
		src_Vn += src_pitchV;	
	}

	if (mt_data_inf.bottom)
	{
		for (int32_t y = h_2; y < h_Y_max; y+=2)
		{
			if (_align) JPSDR_AutoYUY2_SSE2_4b(src_Y,src_U,src_Up,src_V,src_Vp,dstp,w_8);
			else JPSDR_AutoYUY2_SSE2_4(src_Y,src_U,src_Up,src_V,src_Vp,dstp,w_8);
			dstp += dst_pitch;
			src_Y += src_pitchY;

			if (_align) JPSDR_AutoYUY2_SSE2_1b(src_Y,src_U,src_V,dstp,w_8);
			else JPSDR_AutoYUY2_SSE2_1(src_Y,src_U,src_V,dstp,w_8);
			dstp += dst_pitch;
			src_Y += src_pitchY;
		
			src_U += src_pitchU;
			src_Up += src_pitchU;
			src_Un += src_pitchU;
			src_V += src_pitchV;
			src_Vp += src_pitchV;
			src_Vn += src_pitchV;	
		}
	}
}



void AutoYUY2::Convert_Interlaced_YUY2(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	YUYV *dst=(YUYV *)mt_data_inf.dst1;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitchY=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch=mt_data_inf.dst_pitch1;

	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up, *src_Un, *src_Unn, *src_Unnn,*src_Upp;
	const uint8_t *src_V,*src_Vp, *src_Vn, *src_Vnn, *src_Vnnn,*src_Vpp;
	const int32_t w_UV=dst_w>>2;
	const uint16_t *lookup=lookup_Upscale;
	const int32_t h_4 = mt_data_inf.bottom ? h_Y_max-4:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 4:h_Y_min;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchU;
	src_Upp = srcUp - (2*src_pitchU);
	src_Un = srcUp + src_pitchU;
	src_Unn = srcUp + (2 * src_pitchU);
	src_Unnn = srcUp + (3 * src_pitchU);
	src_V = srcVp;
	src_Vp = srcVp - src_pitchV;
	src_Vpp = srcVp - (2*src_pitchV);
	src_Vn = srcVp + src_pitchV;
	src_Vnn = srcVp + (2 * src_pitchV);
	src_Vnnn = srcVp + (3 * src_pitchV);
	
	const ptrdiff_t src_pitchU_2=src_pitchU << 1;
	const ptrdiff_t src_pitchV_2=src_pitchV << 1;

	if (mt_data_inf.top)
	{
		for (int32_t y=0; y<4; y+=4)
		{

			JPSDR_AutoYUY2_1(src_Y,src_U,src_V,(uint8_t *)dst,dst_w>>2);
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			JPSDR_AutoYUY2_1(src_Y,src_Un,src_Vn,(uint8_t *)dst,dst_w>>2);
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[src_Unn[j]]+lookup[(uint16_t)src_U[j]+256]+4)>>3);
					dst[j].v=(uint8_t)((lookup[src_Vnn[j]]+lookup[(uint16_t)src_V[j]+256]+4)>>3);
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[(uint16_t)src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);
					dst[j].v=(uint8_t)((lookup[(uint16_t)src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			src_U += src_pitchU_2;
			src_Up += src_pitchU_2;
			src_Upp += src_pitchU_2;
			src_Un += src_pitchU_2;
			src_Unn += src_pitchU_2;
			src_Unnn += src_pitchU_2;
			src_V += src_pitchV_2;
			src_Vp += src_pitchV_2;
			src_Vpp += src_pitchV_2;
			src_Vn += src_pitchV_2;
			src_Vnn += src_pitchV_2;
			src_Vnnn += src_pitchV_2;
		}
	}

	for (int32_t y = h_0; y < h_4; y+=4)
	{
		{
			int32_t i=0;

			for (int32_t j=0; j<w_UV; j++)
			{
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;
				dst[j].u=(uint8_t)((lookup[(uint16_t)src_U[j]+512]+(uint16_t)src_Upp[j]+4) >> 3);
				dst[j].v=(uint8_t)((lookup[(uint16_t)src_V[j]+512]+(uint16_t)src_Vpp[j]+4) >> 3);
			}
		}

		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;

		{
			int32_t i=0;

			for (int32_t j=0; j<w_UV; j++)
			{
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;
				dst[j].u=(uint8_t)((lookup[src_Up[j]]+lookup[(uint16_t)src_Un[j]+256]+4) >> 3);
				dst[j].v=(uint8_t)((lookup[src_Vp[j]]+lookup[(uint16_t)src_Vn[j]+256]+4) >> 3);
			}
		}
		
		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;
		
		{
			int32_t i=0;

			for (int32_t j=0; j<w_UV; j++)
			{
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;
				dst[j].u=(uint8_t)((lookup[src_Unn[j]]+lookup[(uint16_t)src_U[j]+256]+4) >> 3);
				dst[j].v=(uint8_t)((lookup[src_Vnn[j]]+lookup[(uint16_t)src_V[j]+256]+4) >> 3);
			}
		}
		
		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;

		{
			int32_t i=0;

			for (int32_t j=0; j<w_UV; j++)
			{
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;
				dst[j].u=(uint8_t)((lookup[(uint16_t)src_Un[j]+512]+(uint16_t)src_Unnn[j]+4) >> 3);
				dst[j].v=(uint8_t)((lookup[(uint16_t)src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4) >> 3);
			}
		}
		
		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;

		src_U += src_pitchU_2;
		src_Up += src_pitchU_2;
		src_Upp += src_pitchU_2;
		src_Un += src_pitchU_2;
		src_Unn += src_pitchU_2;
		src_Unnn += src_pitchU_2;
		src_V += src_pitchV_2;
		src_Vp += src_pitchV_2;
		src_Vpp += src_pitchV_2;
		src_Vn += src_pitchV_2;
		src_Vnn += src_pitchV_2;
		src_Vnnn += src_pitchV_2;	
	}

	if (mt_data_inf.bottom)
	{
		for (int32_t y = h_4; y < h_Y_max; y+=4)
		{
			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[(uint16_t)src_U[j]+512]+(uint16_t)src_Upp[j]+4) >> 3);
					dst[j].v=(uint8_t)((lookup[(uint16_t)src_V[j]+512]+(uint16_t)src_Vpp[j]+4) >> 3);
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;
			
				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[src_Up[j]]+lookup[(uint16_t)src_Un[j]+256]+4) >> 3);
					dst[j].v=(uint8_t)((lookup[src_Vp[j]]+lookup[(uint16_t)src_Vn[j]+256]+4) >> 3);
				}
			}
		
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;
		
			JPSDR_AutoYUY2_1(src_Y,src_U,src_V,(uint8_t *)dst,dst_w>>2);
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			JPSDR_AutoYUY2_1(src_Y,src_Un,src_Vn,(uint8_t *)dst,dst_w>>2);
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			src_U += src_pitchU_2;
			src_Up += src_pitchU_2;
			src_Upp += src_pitchU_2;
			src_Un += src_pitchU_2;
			src_Unn += src_pitchU_2;
			src_Unnn += src_pitchU_2;
			src_V += src_pitchV_2;
			src_Vp += src_pitchV_2;
			src_Vpp += src_pitchV_2;
			src_Vn += src_pitchV_2;
			src_Vnn += src_pitchV_2;
			src_Vnnn += src_pitchV_2;	
		}
	}
}



void AutoYUY2::Convert_Interlaced_YUY2_SSE(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	uint8_t *dstp=(uint8_t *)mt_data_inf.dst1;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitchY=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch=mt_data_inf.dst_pitch1;

	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up, *src_Un, *src_Unn, *src_Unnn,*src_Upp;
	const uint8_t *src_V,*src_Vp, *src_Vn, *src_Vnn, *src_Vnnn,*src_Vpp;
	const int32_t w_8=dst_w>>3;
	const int32_t h_4 = mt_data_inf.bottom ? h_Y_max-4:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 4:h_Y_min;

	bool _align=false;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchU;
	src_Upp = srcUp - (2*src_pitchU);
	src_Un = srcUp + src_pitchU;
	src_Unn = srcUp + (2 * src_pitchU);
	src_Unnn = srcUp + (3 * src_pitchU);
	src_V = srcVp;
	src_Vp = srcVp - src_pitchV;
	src_Vpp = srcVp - (2*src_pitchV);
	src_Vn = srcVp + src_pitchV;
	src_Vnn = srcVp + (2 * src_pitchV);
	src_Vnnn = srcVp + (3 * src_pitchV);

	const ptrdiff_t src_pitchU_2=src_pitchU << 1;
	const ptrdiff_t src_pitchV_2=src_pitchV << 1;

	if ((((size_t)dstp & 0x0F)==0) && ((abs(dst_pitch) & 0x0F)==0)) _align=true;

	if (mt_data_inf.top)
	{
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

			src_U += src_pitchU_2;
			src_Up += src_pitchU_2;
			src_Upp += src_pitchU_2;
			src_Un += src_pitchU_2;
			src_Unn += src_pitchU_2;
			src_Unnn += src_pitchU_2;
			src_V += src_pitchV_2;
			src_Vp += src_pitchV_2;
			src_Vpp += src_pitchV_2;
			src_Vn += src_pitchV_2;
			src_Vnn += src_pitchV_2;
			src_Vnnn += src_pitchV_2;
		}
	}

	for (int32_t y = h_0; y < h_4; y+=4)
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

		src_U += src_pitchU_2;
		src_Up += src_pitchU_2;
		src_Upp += src_pitchU_2;
		src_Un += src_pitchU_2;
		src_Unn += src_pitchU_2;
		src_Unnn += src_pitchU_2;
		src_V += src_pitchV_2;
		src_Vp += src_pitchV_2;
		src_Vpp += src_pitchV_2;
		src_Vn += src_pitchV_2;
		src_Vnn += src_pitchV_2;
		src_Vnnn += src_pitchV_2;	
	}

	if (mt_data_inf.bottom)
	{
		for (int32_t y = h_4; y < h_Y_max; y+=4)
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

			src_U += src_pitchU_2;
			src_Up += src_pitchU_2;
			src_Upp += src_pitchU_2;
			src_Un += src_pitchU_2;
			src_Unn += src_pitchU_2;
			src_Unnn += src_pitchU_2;
			src_V += src_pitchV_2;
			src_Vp += src_pitchV_2;
			src_Vpp += src_pitchV_2;
			src_Vn += src_pitchV_2;
			src_Vnn += src_pitchV_2;
			src_Vnnn += src_pitchV_2;	
		}
	}
}




void AutoYUY2::Convert_Automatic_YUY2(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	YUYV *dst=(YUYV *)mt_data_inf.dst1;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitchY=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch=mt_data_inf.dst_pitch1;

	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up,*src_Un,*src_Unn,*src_Unnn,*src_Upp;
	const uint8_t *src_V,*src_Vp,*src_Vn,*src_Vnn,*src_Vnnn,*src_Vpp;
	uint8_t index_tab_0,index_tab_1,index_tab_2;
	const int32_t w_UV=dst_w>>2;
	const int16_t threshold_=threshold;
	const uint16_t *lookup=lookup_Upscale;
	const int32_t h_4 = mt_data_inf.bottom ? h_Y_max-4:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 4:h_Y_min;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchU;
	src_Upp = srcUp - (2*src_pitchU);
	src_Un = srcUp + src_pitchU;
	src_Unn = srcUp + (2 * src_pitchU);
	src_Unnn = srcUp + (3 * src_pitchU);
	src_V = srcVp;
	src_Vp = srcVp - src_pitchV;
	src_Vpp = srcVp - (2*src_pitchV);
	src_Vn = srcVp + src_pitchV;
	src_Vnn = srcVp + (2 * src_pitchV);
	src_Vnnn = srcVp + (3 * src_pitchV);
	
	const ptrdiff_t src_pitchU_2=src_pitchU << 1;
	const ptrdiff_t src_pitchV_2=src_pitchV << 1;

	if (mt_data_inf.top)
	{
		for (int32_t y=0; y<4; y+=4)
		{
			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=src_U[j];
					dst[j].v=src_V[j];
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=src_Un[j];
					dst[j].v=src_Vn[j];
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[src_Unn[j]]+lookup[(uint16_t)src_U[j]+256]+4)>>3);
					dst[j].v=(uint8_t)((lookup[src_Vnn[j]]+lookup[(uint16_t)src_V[j]+256]+4)>>3);
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;
				bool *itabu0=interlaced_tab_U[thread_num][0],*itabu1=interlaced_tab_U[thread_num][1];
				bool *itabv0=interlaced_tab_V[thread_num][0],*itabv1=interlaced_tab_V[thread_num][1];

				for (int32_t j=0; j<w_UV; j++)
				{	
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;

					if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
						(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
						(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
						((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))))
						itabu0[j]=true;
					else itabu0[j]=false;
					if (((abs((int16_t)src_Un[j]-(int16_t)src_Unn[j])>=threshold_) &&
						(abs((int16_t)src_Unnn[j]-(int16_t)src_Unn[j])>=threshold_) &&
						(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
						((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))))
						itabu1[j]=true;
					else itabu1[j]=false;

					dst[j].u=(uint8_t)((lookup[(uint16_t)src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);

					if (((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
						(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
						(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
						((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
						itabv0[j]=true;
					else itabv0[j]=false;
					if (((abs((int16_t)src_Vn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
						(abs((int16_t)src_Vnnn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
						(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
						((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
						itabv1[j]=true;
					else itabv1[j]=false;
				
					dst[j].v=(uint8_t)((lookup[(uint16_t)src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			src_U += src_pitchU_2;
			src_Up += src_pitchU_2;
			src_Upp += src_pitchU_2;
			src_Un += src_pitchU_2;
			src_Unn += src_pitchU_2;
			src_Unnn += src_pitchU_2;
			src_V += src_pitchV_2;
			src_Vp += src_pitchV_2;
			src_Vpp += src_pitchV_2;
			src_Vn += src_pitchV_2;
			src_Vnn += src_pitchV_2;
			src_Vnnn += src_pitchV_2;
		}
	}
	else
	{
		bool *itabu0=interlaced_tab_U[thread_num][0],*itabu1=interlaced_tab_U[thread_num][1];
		bool *itabv0=interlaced_tab_V[thread_num][0],*itabv1=interlaced_tab_V[thread_num][1];

		for (int32_t j=0; j<w_UV; j++)
		{	
			if (((abs((int16_t)src_Upp[j]-(int16_t)src_Up[j])>=threshold_) &&
				(abs((int16_t)src_U[j]-(int16_t)src_Up[j])>=threshold_) &&
				(((src_Upp[j]>src_Up[j]) && (src_U[j]>src_Up[j])) ||
				((src_Upp[j]<src_Up[j]) && (src_U[j]<src_Up[j])))))
				itabu0[j]=true;
			else itabu0[j]=false;
			if (((abs((int16_t)src_Up[j]-(int16_t)src_U[j])>=threshold_) &&
				(abs((int16_t)src_Un[j]-(int16_t)src_U[j])>=threshold_) &&
				(((src_Up[j]>src_U[j]) && (src_Un[j]>src_U[j])) ||
				((src_Up[j]<src_U[j]) && (src_Un[j]<src_U[j])))))
				itabu1[j]=true;
			else itabu1[j]=false;

			if (((abs((int16_t)src_Vpp[j]-(int16_t)src_Vp[j])>=threshold_) &&
				(abs((int16_t)src_V[j]-(int16_t)src_Vp[j])>=threshold_) &&
				(((src_Vpp[j]>src_Vp[j]) && (src_V[j]>src_Vp[j])) ||
				((src_Vpp[j]<src_Vp[j]) && (src_V[j]<src_Vp[j])))))
				itabv0[j]=true;
			else itabv0[j]=false;
			if (((abs((int16_t)src_Vp[j]-(int16_t)src_V[j])>=threshold_) &&
				(abs((int16_t)src_Vn[j]-(int16_t)src_V[j])>=threshold_) &&
				(((src_Vp[j]>src_V[j]) && (src_Vn[j]>src_V[j])) ||
				((src_Vp[j]<src_V[j]) && (src_Vn[j]<src_V[j])))))
				itabv1[j]=true;
			else itabv1[j]=false;
		}
	}

	
	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;

	for (int32_t y = h_0; y < h_4; y+=4)
	{
		{
			int32_t i=0;
			const bool *itabu0=interlaced_tab_U[thread_num][index_tab_0],*itabu1=interlaced_tab_U[thread_num][index_tab_1];
			const bool *itabv0=interlaced_tab_V[thread_num][index_tab_0],*itabv1=interlaced_tab_V[thread_num][index_tab_1];

			for (int32_t j=0; j<w_UV; j++)
			{
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;

				// Upsample as needed.
				if ((itabu0[j]) || (itabu1[j]))
				{
					dst[j].u=(uint8_t)((lookup[(uint16_t)src_U[j]+512]+(uint16_t)src_Upp[j]+4) >> 3);
				}
				else
				{
					dst[j].u=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2) >> 2);
				}

				// Upsample as needed.
				if ((itabv0[j]) || (itabv1[j]))
				{
					dst[j].v=(uint8_t)((lookup[(uint16_t)src_V[j]+512]+(uint16_t)src_Vpp[j]+4) >> 3);
				}
				else
				{
					dst[j].v=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2) >> 2);
				}
			}
		}

		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;

		{
			int32_t i=0;
			const bool *itabu1=interlaced_tab_U[thread_num][index_tab_1];
			const bool *itabv1=interlaced_tab_V[thread_num][index_tab_1];
			bool *itabu2=interlaced_tab_U[thread_num][index_tab_2];
			bool *itabv2=interlaced_tab_V[thread_num][index_tab_2];

			for (int32_t j=0; j<w_UV; j++)
			{			
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;

				if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
					(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
					(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
					((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))))
					itabu2[j]=true;
				else itabu2[j]=false;			

				// Upsample as needed.
				if ((itabu2[j]) || (itabu1[j]))
				{
					dst[j].u=(uint8_t)((lookup[src_Up[j]]+lookup[(uint16_t)src_Un[j]+256]+4) >> 3);
				}
				else
				{
					dst[j].u=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2) >> 2);
				}

				if (((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
					(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
					(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
					((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
					itabv2[j]=true;
				else itabv2[j]=false;			

				// Upsample as needed.
				if ((itabv2[j]) || (itabv1[j]))
				{
					dst[j].v=(uint8_t)((lookup[src_Vp[j]]+lookup[(uint16_t)src_Vn[j]+256]+4) >> 3);
				}
				else
				{
					dst[j].v=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2) >> 2);
				}
			}
		}
		
		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;
		
		{
			int32_t i=0;
			const bool *itabu1=interlaced_tab_U[thread_num][index_tab_1],*itabu2=interlaced_tab_U[thread_num][index_tab_2];
			const bool *itabv1=interlaced_tab_V[thread_num][index_tab_1],*itabv2=interlaced_tab_V[thread_num][index_tab_2];

			for (int32_t j=0; j<w_UV; j++)
			{
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;

				// Upsample as needed.
				if ((itabu1[j]) || (itabu2[j]))
				{
					dst[j].u=(uint8_t)((lookup[src_Unn[j]]+lookup[(uint16_t)src_U[j]+256]+4) >> 3);
				}
				else
				{
					dst[j].u=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_U[j]+2) >> 2);
				}

				// Upsample as needed.
				if ((itabv1[j]) || (itabv2[j]))
				{
					dst[j].v=(uint8_t)((lookup[src_Vnn[j]]+lookup[(uint16_t)src_V[j]+256]+4) >> 3);
				}
				else
				{
					dst[j].v=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_V[j]+2) >> 2);
				}
			}
		}
		
		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;

		{
			int32_t i=0;
			const bool *itabu2=interlaced_tab_U[thread_num][index_tab_2];
			const bool *itabv2=interlaced_tab_V[thread_num][index_tab_2];
			bool *itabu0=interlaced_tab_U[thread_num][index_tab_0];
			bool *itabv0=interlaced_tab_V[thread_num][index_tab_0];

			for (int32_t j=0; j<w_UV; j++)
			{			
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;

				if (((abs((int16_t)src_Un[j]-(int16_t)src_Unn[j])>=threshold_) &&
					(abs((int16_t)src_Unnn[j]-(int16_t)src_Unn[j])>=threshold_) &&
					(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
					((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))))
					itabu0[j]=true;
				else itabu0[j]=false;

				// Upsample as needed.
				if ((itabu0[j]) || (itabu2[j]))
				{
					dst[j].u=(uint8_t)((lookup[(uint16_t)src_Un[j]+512]+(uint16_t)src_Unnn[j]+4) >> 3);
				}
				else
				{
					dst[j].u=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_Unn[j]+2) >> 2);
				}

				if (((abs((int16_t)src_Vn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
					(abs((int16_t)src_Vnnn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
					(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
					((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
					itabv0[j]=true;
				else itabv0[j]=false;

				// Upsample as needed.
				if ((itabv0[j]) || (itabv2[j]))
				{
					dst[j].v=(uint8_t)((lookup[(uint16_t)src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4) >> 3);
				}
				else
				{
					dst[j].v=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_Vnn[j]+2) >> 2);
				}
			}
		}
		
		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;
		
		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;

		src_U += src_pitchU_2;
		src_Up += src_pitchU_2;
		src_Upp += src_pitchU_2;
		src_Un += src_pitchU_2;
		src_Unn += src_pitchU_2;
		src_Unnn += src_pitchU_2;
		src_V += src_pitchV_2;
		src_Vp += src_pitchV_2;
		src_Vpp += src_pitchV_2;
		src_Vn += src_pitchV_2;
		src_Vnn += src_pitchV_2;
		src_Vnnn += src_pitchV_2;	
	}

	if (mt_data_inf.bottom)
	{
		for (int32_t y = h_4; y < h_Y_max; y+=4)
		{
			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[(uint16_t)src_U[j]+512]+(uint16_t)src_Upp[j]+4) >> 3);
					dst[j].v=(uint8_t)((lookup[(uint16_t)src_V[j]+512]+(uint16_t)src_Vpp[j]+4) >> 3);
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[src_Up[j]]+lookup[(uint16_t)src_Un[j]+256]+4) >> 3);
					dst[j].v=(uint8_t)((lookup[src_Vp[j]]+lookup[(uint16_t)src_Vn[j]+256]+4) >> 3);
				}
			}
		
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;
		
			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=src_U[j];
					dst[j].v=src_V[j];
				}
			}
		
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=src_Un[j];
					dst[j].v=src_Vn[j];
				}
			}
		
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			src_U += src_pitchU_2;
			src_Up += src_pitchU_2;
			src_Upp += src_pitchU_2;
			src_Un += src_pitchU_2;
			src_Unn += src_pitchU_2;
			src_Unnn += src_pitchU_2;
			src_V += src_pitchV_2;
			src_Vp += src_pitchV_2;
			src_Vpp += src_pitchV_2;
			src_Vn += src_pitchV_2;
			src_Vnn += src_pitchV_2;
			src_Vnnn += src_pitchV_2;
		}
	}
}



void AutoYUY2::Convert_Test_YUY2(uint8_t thread_num)
{
	const MT_Data_Info mt_data_inf=MT_Data[thread_num];

	const uint8_t *srcYp=(const uint8_t *)mt_data_inf.src1;
	const uint8_t *srcUp=(const uint8_t *)mt_data_inf.src2;
	const uint8_t *srcVp=(const uint8_t *)mt_data_inf.src3;
	YUYV *dst=(YUYV *)mt_data_inf.dst1;
	const int32_t dst_w=mt_data_inf.dst_Y_w;
	const int32_t h_Y_min=mt_data_inf.src_Y_h_min;
	const int32_t h_Y_max=mt_data_inf.src_Y_h_max;
	const int src_pitchY=mt_data_inf.src_pitch1;
	const int src_pitchU=mt_data_inf.src_pitch2;
	const int src_pitchV=mt_data_inf.src_pitch3;
	const int dst_pitch=mt_data_inf.dst_pitch1;

	const uint8_t *src_Y;
	const uint8_t *src_U,*src_Up,*src_Un,*src_Unn,*src_Unnn,*src_Upp;
	const uint8_t *src_V,*src_Vp,*src_Vn,*src_Vnn,*src_Vnnn,*src_Vpp;
	uint8_t index_tab_0,index_tab_1,index_tab_2;
	const int32_t w_UV=dst_w>>2;
	const int16_t threshold_=threshold;
	const uint16_t *lookup=lookup_Upscale;
	const int32_t h_4 = mt_data_inf.bottom ? h_Y_max-4:h_Y_max;
	const int32_t h_0 = mt_data_inf.top ? 4:h_Y_min;

	src_Y = srcYp;
	src_U = srcUp;
	src_Up = srcUp - src_pitchU;
	src_Upp = srcUp - (2*src_pitchU);
	src_Un = srcUp + src_pitchU;
	src_Unn = srcUp + (2 * src_pitchU);
	src_Unnn = srcUp + (3 * src_pitchU);
	src_V = srcVp;
	src_Vp = srcVp - src_pitchV;
	src_Vpp = srcVp - (2*src_pitchV);
	src_Vn = srcVp + src_pitchV;
	src_Vnn = srcVp + (2 * src_pitchV);
	src_Vnnn = srcVp + (3 * src_pitchV);
	
	const ptrdiff_t src_pitchU_2=src_pitchU << 1;
	const ptrdiff_t src_pitchV_2=src_pitchV << 1;

	if (mt_data_inf.top)
	{
		for (int32_t y=0; y<4; y+=4)
		{
			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=src_U[j];
					dst[j].v=src_V[j];
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=src_Un[j];
					dst[j].v=src_Vn[j];
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[src_Unn[j]]+lookup[(uint16_t)src_U[j]+256]+4)>>3);
					dst[j].v=(uint8_t)((lookup[src_Vnn[j]]+lookup[(uint16_t)src_V[j]+256]+4)>>3);
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;
				bool *itabu0=interlaced_tab_U[thread_num][0],*itabu1=interlaced_tab_U[thread_num][1];
				bool *itabv0=interlaced_tab_V[thread_num][0],*itabv1=interlaced_tab_V[thread_num][1];

				for (int32_t j=0; j<w_UV; j++)
				{	
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;

					if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
						(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
						(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
						((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))))
						itabu0[j]=true;
					else itabu0[j]=false;
					if (((abs((int16_t)src_Un[j]-(int16_t)src_Unn[j])>=threshold_) &&
						(abs((int16_t)src_Unnn[j]-(int16_t)src_Unn[j])>=threshold_) &&
						(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
						((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))))
						itabu1[j]=true;
					else itabu1[j]=false;

					dst[j].u=(uint8_t)((lookup[(uint16_t)src_Un[j]+512]+(uint16_t)src_Unnn[j]+4)>>3);

					if (((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
						(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
						(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
						((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
						itabv0[j]=true;
					else itabv0[j]=false;
					if (((abs((int16_t)src_Vn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
						(abs((int16_t)src_Vnnn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
						(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
						((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
						itabv1[j]=true;
					else itabv1[j]=false;
				
					dst[j].v=(uint8_t)((lookup[(uint16_t)src_Vn[j]+512]+(uint16_t)src_Vnnn[j]+4)>>3);
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			src_U += src_pitchU_2;
			src_Up += src_pitchU_2;
			src_Upp += src_pitchU_2;
			src_Un += src_pitchU_2;
			src_Unn += src_pitchU_2;
			src_Unnn += src_pitchU_2;
			src_V += src_pitchV_2;
			src_Vp += src_pitchV_2;
			src_Vpp += src_pitchV_2;
			src_Vn += src_pitchV_2;
			src_Vnn += src_pitchV_2;
			src_Vnnn += src_pitchV_2;
		}
	}
	else
	{
		bool *itabu0=interlaced_tab_U[thread_num][0],*itabu1=interlaced_tab_U[thread_num][1];
		bool *itabv0=interlaced_tab_V[thread_num][0],*itabv1=interlaced_tab_V[thread_num][1];

		for (int32_t j=0; j<w_UV; j++)
		{	
			if (((abs((int16_t)src_Upp[j]-(int16_t)src_Up[j])>=threshold_) &&
				(abs((int16_t)src_U[j]-(int16_t)src_Up[j])>=threshold_) &&
				(((src_Upp[j]>src_Up[j]) && (src_U[j]>src_Up[j])) ||
				((src_Upp[j]<src_Up[j]) && (src_U[j]<src_Up[j])))))
				itabu0[j]=true;
			else itabu0[j]=false;
			if (((abs((int16_t)src_Up[j]-(int16_t)src_U[j])>=threshold_) &&
				(abs((int16_t)src_Un[j]-(int16_t)src_U[j])>=threshold_) &&
				(((src_Up[j]>src_U[j]) && (src_Un[j]>src_U[j])) ||
				((src_Up[j]<src_U[j]) && (src_Un[j]<src_U[j])))))
				itabu1[j]=true;
			else itabu1[j]=false;

			if (((abs((int16_t)src_Vpp[j]-(int16_t)src_Vp[j])>=threshold_) &&
				(abs((int16_t)src_V[j]-(int16_t)src_Vp[j])>=threshold_) &&
				(((src_Vpp[j]>src_Vp[j]) && (src_V[j]>src_Vp[j])) ||
				((src_Vpp[j]<src_Vp[j]) && (src_V[j]<src_Vp[j])))))
				itabv0[j]=true;
			else itabv0[j]=false;
			if (((abs((int16_t)src_Vp[j]-(int16_t)src_V[j])>=threshold_) &&
				(abs((int16_t)src_Vn[j]-(int16_t)src_V[j])>=threshold_) &&
				(((src_Vp[j]>src_V[j]) && (src_Vn[j]>src_V[j])) ||
				((src_Vp[j]<src_V[j]) && (src_Vn[j]<src_V[j])))))
				itabv1[j]=true;
			else itabv1[j]=false;
		}
	}

	
	index_tab_0=0;
	index_tab_1=1;
	index_tab_2=2;

	for (int32_t y = h_0; y < h_4; y+=4)
	{
		{
			int32_t i=0;
			const bool *itabu0=interlaced_tab_U[thread_num][index_tab_0],*itabu1=interlaced_tab_U[thread_num][index_tab_1];
			const bool *itabv0=interlaced_tab_V[thread_num][index_tab_0],*itabv1=interlaced_tab_V[thread_num][index_tab_1];

			for (int32_t j=0; j<w_UV; j++)
			{
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;

				// Upsample as needed.
				if ((itabu0[j]) || (itabu1[j]))
				{
					dst[j].u=239;
				}
				else
				{
					dst[j].u=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Up[j]+2) >> 2);
				}

				// Upsample as needed.
				if ((itabv0[j]) || (itabv1[j]))
				{
					dst[j].v=239;
				}
				else
				{
					dst[j].v=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vp[j]+2) >> 2);
				}
			}
		}

		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;

		{
			int32_t i=0;
			const bool *itabu1=interlaced_tab_U[thread_num][index_tab_1];
			const bool *itabv1=interlaced_tab_V[thread_num][index_tab_1];
			bool *itabu2=interlaced_tab_U[thread_num][index_tab_2];
			bool *itabv2=interlaced_tab_V[thread_num][index_tab_2];

			for (int32_t j=0; j<w_UV; j++)
			{			
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;

				if (((abs((int16_t)src_U[j]-(int16_t)src_Un[j])>=threshold_) &&
					(abs((int16_t)src_Unn[j]-(int16_t)src_Un[j])>=threshold_) &&
					(((src_U[j]>src_Un[j]) && (src_Unn[j]>src_Un[j])) ||
					((src_U[j]<src_Un[j]) && (src_Unn[j]<src_Un[j])))))
					itabu2[j]=true;
				else itabu2[j]=false;			

				// Upsample as needed.
				if ((itabu2[j]) || (itabu1[j]))
				{
					dst[j].u=239;
				}
				else
				{
					dst[j].u=(uint8_t)((lookup[src_U[j]]+(uint16_t)src_Un[j]+2) >> 2);
				}

				if (((abs((int16_t)src_V[j]-(int16_t)src_Vn[j])>=threshold_) &&
					(abs((int16_t)src_Vnn[j]-(int16_t)src_Vn[j])>=threshold_) &&
					(((src_V[j]>src_Vn[j]) && (src_Vnn[j]>src_Vn[j])) ||
					((src_V[j]<src_Vn[j]) && (src_Vnn[j]<src_Vn[j])))))
					itabv2[j]=true;
				else itabv2[j]=false;			

				// Upsample as needed.
				if ((itabv2[j]) || (itabv1[j]))
				{
					dst[j].v=239;
				}
				else
				{
					dst[j].v=(uint8_t)((lookup[src_V[j]]+(uint16_t)src_Vn[j]+2) >> 2);
				}
			}
		}
		
		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;
		
		{
			int32_t i=0;
			const bool *itabu1=interlaced_tab_U[thread_num][index_tab_1],*itabu2=interlaced_tab_U[thread_num][index_tab_2];
			const bool *itabv1=interlaced_tab_V[thread_num][index_tab_1],*itabv2=interlaced_tab_V[thread_num][index_tab_2];

			for (int32_t j=0; j<w_UV; j++)
			{
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;

				// Upsample as needed.
				if ((itabu1[j]) || (itabu2[j]))
				{
					dst[j].u=239;
				}
				else
				{
					dst[j].u=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_U[j]+2) >> 2);
				}

				// Upsample as needed.
				if ((itabv1[j]) || (itabv2[j]))
				{
					dst[j].v=239;
				}
				else
				{
					dst[j].v=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_V[j]+2) >> 2);
				}
			}
		}
		
		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;

		{
			int32_t i=0;
			const bool *itabu2=interlaced_tab_U[thread_num][index_tab_2];
			const bool *itabv2=interlaced_tab_V[thread_num][index_tab_2];
			bool *itabu0=interlaced_tab_U[thread_num][index_tab_0];
			bool *itabv0=interlaced_tab_V[thread_num][index_tab_0];

			for (int32_t j=0; j<w_UV; j++)
			{			
				dst[j].y1=src_Y[i];
				dst[j].y2=src_Y[i+1];
				i+=2;

				if (((abs((int16_t)src_Un[j]-(int16_t)src_Unn[j])>=threshold_) &&
					(abs((int16_t)src_Unnn[j]-(int16_t)src_Unn[j])>=threshold_) &&
					(((src_Un[j]>src_Unn[j]) && (src_Unnn[j]>src_Unn[j])) ||
					((src_Un[j]<src_Unn[j]) && (src_Unnn[j]<src_Unn[j])))))
					itabu0[j]=true;
				else itabu0[j]=false;

				// Upsample as needed.
				if ((itabu0[j]) || (itabu2[j]))
				{
					dst[j].u=239;
				}
				else
				{
					dst[j].u=(uint8_t)((lookup[src_Un[j]]+(uint16_t)src_Unn[j]+2) >> 2);
				}

				if (((abs((int16_t)src_Vn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
					(abs((int16_t)src_Vnnn[j]-(int16_t)src_Vnn[j])>=threshold_) &&
					(((src_Vn[j]>src_Vnn[j]) && (src_Vnnn[j]>src_Vnn[j])) ||
					((src_Vn[j]<src_Vnn[j]) && (src_Vnnn[j]<src_Vnn[j])))))
					itabv0[j]=true;
				else itabv0[j]=false;

				// Upsample as needed.
				if ((itabv0[j]) || (itabv2[j]))
				{
					dst[j].v=239;
				}
				else
				{
					dst[j].v=(uint8_t)((lookup[src_Vn[j]]+(uint16_t)src_Vnn[j]+2) >> 2);
				}
			}
		}
		
		index_tab_0=(index_tab_0+2)%Interlaced_Tab_Size;
		index_tab_1=(index_tab_1+2)%Interlaced_Tab_Size;
		index_tab_2=(index_tab_2+2)%Interlaced_Tab_Size;
		
		dst=(YUYV *)((uint8_t *)dst+dst_pitch);
		src_Y += src_pitchY;

		src_U += src_pitchU_2;
		src_Up += src_pitchU_2;
		src_Upp += src_pitchU_2;
		src_Un += src_pitchU_2;
		src_Unn += src_pitchU_2;
		src_Unnn += src_pitchU_2;
		src_V += src_pitchV_2;
		src_Vp += src_pitchV_2;
		src_Vpp += src_pitchV_2;
		src_Vn += src_pitchV_2;
		src_Vnn += src_pitchV_2;
		src_Vnnn += src_pitchV_2;	
	}

	if (mt_data_inf.bottom)
	{
		for (int32_t y = h_4; y < h_Y_max; y+=4)
		{
			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[(uint16_t)src_U[j]+512]+(uint16_t)src_Upp[j]+4) >> 3);
					dst[j].v=(uint8_t)((lookup[(uint16_t)src_V[j]+512]+(uint16_t)src_Vpp[j]+4) >> 3);
				}
			}

			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=(uint8_t)((lookup[src_Up[j]]+lookup[(uint16_t)src_Un[j]+256]+4) >> 3);
					dst[j].v=(uint8_t)((lookup[src_Vp[j]]+lookup[(uint16_t)src_Vn[j]+256]+4) >> 3);
				}
			}
		
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;
		
			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=src_U[j];
					dst[j].v=src_V[j];
				}
			}
		
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			{
				int32_t i=0;

				for (int32_t j=0; j<w_UV; j++)
				{
					dst[j].y1=src_Y[i];
					dst[j].y2=src_Y[i+1];
					i+=2;
					dst[j].u=src_Un[j];
					dst[j].v=src_Vn[j];
				}
			}
		
			dst=(YUYV *)((uint8_t *)dst+dst_pitch);
			src_Y += src_pitchY;

			src_U += src_pitchU_2;
			src_Up += src_pitchU_2;
			src_Upp += src_pitchU_2;
			src_Un += src_pitchU_2;
			src_Unn += src_pitchU_2;
			src_Unnn += src_pitchU_2;
			src_V += src_pitchV_2;
			src_Vp += src_pitchV_2;
			src_Vpp += src_pitchV_2;
			src_Vn += src_pitchV_2;
			src_Vnn += src_pitchV_2;
			src_Vnnn += src_pitchV_2;
		}
	}
}



DWORD WINAPI AutoYUY2::StaticThreadpool( LPVOID lpParam )
{
	const MT_Data_Thread *data=(const MT_Data_Thread *)lpParam;
	AutoYUY2 *ptrClass=(AutoYUY2 *)data->pClass;
	
	while (true)
	{
		WaitForSingleObject(data->nextJob,INFINITE);
		switch(data->f_process)
		{
			case 1 : ptrClass->Convert_Progressive_YUY2(data->thread_Id);
				break;
			case 2 : ptrClass->Convert_Progressive_YUY2_SSE(data->thread_Id);
				break;
			case 3 : ptrClass->Convert_Interlaced_YUY2(data->thread_Id);
				break;
			case 4 : ptrClass->Convert_Interlaced_YUY2_SSE(data->thread_Id);
				break;
			case 5 : ptrClass->Convert_Automatic_YUY2(data->thread_Id);
				break;
			case 6 : ptrClass->Convert_Test_YUY2(data->thread_Id);
				break;
			case 7 : ptrClass->Convert_Progressive_YV16(data->thread_Id);
				break;
			case 8 : ptrClass->Convert_Progressive_YV16_SSE(data->thread_Id);
				break;
			case 9 : ptrClass->Convert_Interlaced_YV16(data->thread_Id);
				break;
			case 10 : ptrClass->Convert_Interlaced_YV16_SSE(data->thread_Id);
				break;
			case 11 : ptrClass->Convert_Automatic_YV16(data->thread_Id);
				break;
			case 12 : ptrClass->Convert_Test_YV16(data->thread_Id);
				break;
			case 255 : return(0); break;
			default : ;
		}
		ResetEvent(data->nextJob);
		SetEvent(data->jobFinished);
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
	int dst_pitch_Y,dst_pitchU,dst_pitchV;
	int src_pitch_Y,src_pitchU,src_pitchV;
	uint8_t f_proc;

	SetMemcpyCacheLimit(Cache_Setting);
	SetMemsetCacheLimit(Cache_Setting);

	WaitForSingleObject(ghMutex,INFINITE);

	switch(output)
	{
		case 0 : 
			dst_Y = dst->GetWritePtr();
			dst_h = dst->GetHeight();
			dst_w = dst->GetRowSize();
			dst_pitch_Y = dst->GetPitch();
			dst_U=NULL;
			dst_V=NULL;
			dst_pitchU=0;
			dst_pitchV=0;
			break;
		case 1 :
			dst_h = dst->GetHeight(PLANAR_Y);
			dst_w = dst->GetRowSize(PLANAR_Y);
			dst_Y = dst->GetWritePtr(PLANAR_Y);
			dst_U=dst->GetWritePtr(PLANAR_U);
			dst_V=dst->GetWritePtr(PLANAR_V);
			dst_pitch_Y = dst->GetPitch(PLANAR_Y);
			dst_pitchU=dst->GetPitch(PLANAR_U);
			dst_pitchV=dst->GetPitch(PLANAR_V);
			break;
		default : break;
	}
		
	src_pitch_Y = src->GetPitch(PLANAR_Y);
	src_pitchU = src->GetPitch(PLANAR_U);
	src_pitchV = src->GetPitch(PLANAR_V);

	for(uint8_t i=0; i<threads_number; i++)
	{
		MT_Data[i].src1=(void *)(srcYp+(MT_Data[i].src_Y_h_min*src_pitch_Y));
		MT_Data[i].src2=(void *)(srcUp+(MT_Data[i].src_UV_h_min*src_pitchU));
		MT_Data[i].src3=(void *)(srcVp+(MT_Data[i].src_UV_h_min*src_pitchV));
		MT_Data[i].src_pitch1=src_pitch_Y;
		MT_Data[i].src_pitch2=src_pitchU;
		MT_Data[i].src_pitch3=src_pitchV;
		MT_Data[i].dst1=(void *)(dst_Y+(MT_Data[i].dst_Y_h_min*dst_pitch_Y));
		MT_Data[i].dst2=(void *)(dst_U+(MT_Data[i].dst_UV_h_min*dst_pitchU));
		MT_Data[i].dst3=(void *)(dst_V+(MT_Data[i].dst_UV_h_min*dst_pitchV));
		MT_Data[i].dst_pitch1=dst_pitch_Y;
		MT_Data[i].dst_pitch2=dst_pitchU;
		MT_Data[i].dst_pitch3=dst_pitchV;
	}

	switch(output)
	{
		case 0 :
			switch(mode)
			{
				case -1 :
					if (threads_number>1) f_proc=5;
					else Convert_Automatic_YUY2(0);
					break;
				case 0 :
					if ((SSE2_Enable) && ((dst_w & 0x7)==0))
					{
						if (threads_number>1) f_proc=2;
						else Convert_Progressive_YUY2_SSE(0);
					}
					else
					{
						if (threads_number>1) f_proc=1;
						else Convert_Progressive_YUY2(0);
					}
					break;
				case 1 :
					if ((SSE2_Enable) && ((dst_w & 0x7)==0))
					{
						if (threads_number>1) f_proc=4;
						else Convert_Interlaced_YUY2_SSE(0);
					}
					else
					{
						if (threads_number>1) f_proc=3;
						else Convert_Interlaced_YUY2(0);
					}
					break;
				case 2 : 
					if (threads_number>1) f_proc=6;
					else Convert_Test_YUY2(0);
					break;
				default : f_proc=0; break;
			}
			break;
		case 1 :
			switch(mode)
			{
				case -1 : 
					if (threads_number>1) f_proc=11;
					else Convert_Automatic_YV16(0);
					break;
				case 0 :
					if ((SSE2_Enable) && ((dst_w & 0x7)==0))
					{
						if (threads_number>1) f_proc=8;
						else Convert_Progressive_YV16_SSE(0);
					}
					else
					{
						if (threads_number>1) f_proc=7;
						else Convert_Progressive_YV16(0);
					}
					break;
				case 1 :
					if ((SSE2_Enable) && ((dst_w & 0x7)==0))
					{
						if (threads_number>1) f_proc=10;
						else Convert_Interlaced_YV16_SSE(0);
					}
					else
					{
						if (threads_number>1) f_proc=9;
						else Convert_Interlaced_YV16(0);
					}
					break;
				case 2 : 
					if (threads_number>1) f_proc=12;
					else Convert_Test_YV16(0);
					break;
				default : f_proc=0; break;
			}
			break;
		default : f_proc=0; break;
	}

	if (threads_number>1)
	{
		for(uint8_t i=0; i<threads_number; i++)
		{
			MT_Thread[i].f_process=f_proc;
			ResetEvent(MT_Thread[i].jobFinished);
			SetEvent(MT_Thread[i].nextJob);
		}
		for(uint8_t i=0; i<threads_number; i++)
			WaitForSingleObject(MT_Thread[i].jobFinished,INFINITE);
		for(uint8_t i=0; i<threads_number; i++)
			MT_Thread[i].f_process=0;
	}

	ReleaseMutex(ghMutex);

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
  threads : int, default value 0
     0 : Automatic.
	 Other value specify the number of threads, max is MAX_MT_THREADS
*/
AVSValue __cdecl Create_AutoYUY2(AVSValue args, void* user_data, IScriptEnvironment* env)
{
	char buffer_in[1024];

	if (!args[0].IsClip()) env->ThrowError("AutoYUY2 : arg 0 must be a clip !");

	VideoInfo vi = args[0].AsClip()->GetVideoInfo();

	if (!vi.IsYV12())
		env->ThrowError("AutoYUY2 : Input format must be YV12 or I420");
	if ((vi.width & 1)!=0)
		env->ThrowError("AutoYUY2 : Input width must be a multiple of 2.");
	if ((vi.height & 3)!=0)
		env->ThrowError("AutoYUY2 : Input height must be a multiple of 4.");
	if (vi.height < 8)
		env->ThrowError("AutoYUY2 : Input height must be at least 8.");

	const int thrs=args[1].AsInt(4);
	const int mode=args[2].AsInt(-1);
	const int output=args[3].AsInt(1);
	const int threads=args[4].AsInt(0);

	if ((mode<-1) || (mode>2))
		env->ThrowError("AutoYUY2 : [mode] must be -1 (Automatic), 0 (Progessive) , 1 (Interlaced) or 2 (Test).");
	if ((output<0) || (output>1))
		env->ThrowError("AutoYUY2 : [output] must be 0 (YUY2) or 1 (YV16)");
	if ((threads<0) || (threads>MAX_MT_THREADS))
	{
		sprintf_s(buffer_in,1024,"AutoYUY2 : [threads] must be between 0 and %ld.",MAX_MT_THREADS);
		env->ThrowError(buffer_in);
	}

	return new AutoYUY2(args[0].AsClip(), thrs, mode, output, threads, env);
}

extern "C" __declspec(dllexport) const char* __stdcall AvisynthPluginInit3(IScriptEnvironment* env, const AVS_Linkage* const vectors)
{
	if (IInstrSet<0) InstructionSet();
	CPU_Cache_Size=DataCacheSize(0)>>2;

	AVS_linkage = vectors;

    env->AddFunction("AutoYUY2", "c[threshold]i[mode]i[output]i[threads]i", Create_AutoYUY2, 0);

	if (env->FunctionExists("SetFilterMTMode"))
	{
		auto env2 = static_cast<IScriptEnvironment2*>(env);

		env2->SetFilterMTMode("AutoYUY2",MT_MULTI_INSTANCE,true);
	}

    return "AutoYUY2 Pluggin";
}
