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

#include "avisynth.h"
#include "ThreadPoolInterface.h"

#define AUTOYUY2_VERSION "AutoYUY2 3.2.11 JPSDR"
// Inspired from Neuron2 filter

#define Interlaced_Tab_Size 3

#define myfree(ptr) if (ptr!=NULL) { free(ptr); ptr=NULL;}
#define myCloseHandle(ptr) if (ptr!=NULL) { CloseHandle(ptr); ptr=NULL;}


typedef struct _MT_Data_Info_AutoYUY2
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
} MT_Data_Info_AutoYUY2;


class AutoYUY2 : public GenericVideoFilter
{
public:
	AutoYUY2(PClip _child, int _threshold, int _mode, int _output,uint8_t _threads, bool _sleep, IScriptEnvironment* env);
	virtual ~AutoYUY2();
    PVideoFrame __stdcall GetFrame(int n, IScriptEnvironment* env);

	int __stdcall SetCacheHints(int cachehints, int frame_range);

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
	bool sleep;
	uint16_t lookup_Upscale[768];
	bool *interlaced_tab_U[MAX_MT_THREADS][Interlaced_Tab_Size],*interlaced_tab_V[MAX_MT_THREADS][Interlaced_Tab_Size];
	bool SSE2_Enable,AVX_Enable;

	Public_MT_Data_Thread MT_Thread[MAX_MT_THREADS];
	MT_Data_Info_AutoYUY2 MT_Data[MAX_MT_THREADS];
	uint8_t threads,threads_number;
	uint16_t UserId;
	HANDLE ghMutex;
	
	ThreadPoolFunction StaticThreadpoolF;

	static void StaticThreadpool(void *ptr);

	uint8_t CreateMTData(uint8_t max_threads,int32_t size_x,int32_t size_y);

	void FreeData(void);

	void Convert_Progressive_YUY2(uint8_t thread_num);
	void Convert_Progressive_YUY2_SSE(uint8_t thread_num);
	void Convert_Progressive_YUY2_AVX(uint8_t thread_num);
	void Convert_Interlaced_YUY2(uint8_t thread_num);
	void Convert_Interlaced_YUY2_SSE(uint8_t thread_num);
	void Convert_Interlaced_YUY2_AVX(uint8_t thread_num);
	void Convert_Automatic_YUY2(uint8_t thread_num);
	void Convert_Test_YUY2(uint8_t thread_num);

	void Convert_Progressive_YV16(uint8_t thread_num);
	void Convert_Progressive_YV16_SSE(uint8_t thread_num);
	void Convert_Progressive_YV16_AVX(uint8_t thread_num);
	void Convert_Interlaced_YV16(uint8_t thread_num);
	void Convert_Interlaced_YV16_SSE(uint8_t thread_num);
	void Convert_Interlaced_YV16_AVX(uint8_t thread_num);
	void Convert_Automatic_YV16(uint8_t thread_num);
	void Convert_Test_YV16(uint8_t thread_num);
};


