.586
.model flat,c
.code


JPSDR_AutoYUY2_1 proc src_y:dword,src_u:dword,src_v:dword,dst:dword,w:dword

	public JPSDR_AutoYUY2_1

	push esi
	push edi
	push ebx

	mov esi,src_y
	mov ebx,src_u
	mov edx,src_v
	mov edi,dst
	mov ecx,w
	cld

SSE2_1_a:
	mov al,byte ptr[esi+1]		;al=y2
	mov ah,byte ptr[edx]		;ah=v
	inc edx
	shl eax,16
	lodsw						;al=y1 ah=y2
	mov ah,byte ptr[ebx]		;ah=u
	inc ebx
	stosd
	loop SSE2_1_a
	
	pop ebx
	pop edi
	pop esi

	ret

JPSDR_AutoYUY2_1 endp


.xmm

JPSDR_AutoYUY2_SSE2_2 proc src_y:dword,src1_u:dword,src2_u:dword,src1_v:dword,src2_v:dword,dst:dword,w:dword

	public JPSDR_AutoYUY2_SSE2_2

	push esi
	push edi
	push ebx
	
	pxor xmm7,xmm7
	pxor xmm6,xmm6
	pxor xmm5,xmm5
	pxor xmm4,xmm4
	pxor xmm1,xmm1
	pxor xmm2,xmm2
	
	mov edi,dst
	mov esi,src_y
	mov ecx,w
	cld
	
	mov eax,4
	pinsrw xmm6,eax,1
	pinsrw xmm6,eax,3
	pinsrw xmm6,eax,5
	pinsrw xmm6,eax,7
	mov eax,3
	pinsrw xmm5,eax,1
	pinsrw xmm5,eax,3
	pinsrw xmm5,eax,5
	pinsrw xmm5,eax,7
	mov eax,5
	pinsrw xmm4,eax,1
	pinsrw xmm4,eax,3
	pinsrw xmm4,eax,5
	pinsrw xmm4,eax,7

	xor eax,eax

SSE2_2_a:
	pxor xmm0,xmm0
	lodsb
	pinsrw xmm0,eax,0
	lodsb
	pinsrw xmm0,eax,2
	lodsb
	mov ebx,src1_u
	pinsrw xmm0,eax,4
	lodsb
	mov edx,src2_u	
	pinsrw xmm0,eax,6
	mov al,byte ptr[ebx]					;al=u1 (1)
	add src1_u,2
	pinsrw xmm1,eax,1
	mov al,byte ptr[ebx+1]					;al=u1 (2)
	add src2_u,2
	pinsrw xmm1,eax,5
	mov al,byte ptr[edx]					;al=u2 (1)
	mov ebx,src1_v
	pinsrw xmm2,eax,1
	mov al,byte ptr[edx+1]					;al=u2 (2)
	mov edx,src2_v
	pinsrw xmm2,eax,5
	mov al,byte ptr[ebx]					;al=v1 (1)
	add src1_v,2
	pinsrw xmm1,eax,3
	mov al,byte ptr[ebx+1]					;al=v1 (2)
	add src2_v,2
	pinsrw xmm1,eax,7
	mov al,byte ptr[edx]					;al=v2 (1)
	pinsrw xmm2,eax,3
	mov al,byte ptr[edx+1]					;al=v2 (2)
	pinsrw xmm2,eax,7
	
	pmullw xmm1,xmm5
	pmullw xmm2,xmm4
	paddsw xmm1,xmm6
	paddsw xmm1,xmm2
	psraw xmm1,3
	paddsw xmm0,xmm1
	packuswb xmm0,xmm7
	movq qword ptr[edi],xmm0
	add edi,8
	dec ecx
	jnz SSE2_2_a
	
	pop ebx
	pop edi
	pop esi

	ret

JPSDR_AutoYUY2_SSE2_2 endp


JPSDR_AutoYUY2_SSE2_3 proc src_y:dword,src1_u:dword,src2_u:dword,src1_v:dword,src2_v:dword,dst:dword,w:dword

	public JPSDR_AutoYUY2_SSE2_3
	
	push esi
	push edi
	push ebx
	
	pxor xmm7,xmm7
	pxor xmm6,xmm6
	pxor xmm5,xmm5
	pxor xmm1,xmm1
	pxor xmm2,xmm2
	
	mov edi,dst
	mov esi,src_y
	mov ecx,w
	cld
	
	mov eax,4
	pinsrw xmm6,eax,1
	pinsrw xmm6,eax,3
	pinsrw xmm6,eax,5
	pinsrw xmm6,eax,7
	
	mov eax,7
	pinsrw xmm5,eax,1
	pinsrw xmm5,eax,3
	pinsrw xmm5,eax,5
	pinsrw xmm5,eax,7

	xor eax,eax	

SSE2_3_a:
	pxor xmm0,xmm0
	lodsb
	pinsrw xmm0,eax,0
	lodsb
	pinsrw xmm0,eax,2
	lodsb
	mov ebx,src1_u
	pinsrw xmm0,eax,4
	lodsb
	mov edx,src2_u	
	pinsrw xmm0,eax,6
	mov al,byte ptr[ebx]					;al=u1 (1)
	add src1_u,2
	pinsrw xmm1,eax,1
	mov al,byte ptr[ebx+1]					;al=u1 (2)
	add src2_u,2
	pinsrw xmm1,eax,5
	mov al,byte ptr[edx]					;al=u2 (1)
	mov ebx,src1_v
	pinsrw xmm2,eax,1
	mov al,byte ptr[edx+1]					;al=u2 (2)
	mov edx,src2_v
	pinsrw xmm2,eax,5
	mov al,byte ptr[ebx]					;al=v1 (1)
	add src1_v,2
	pinsrw xmm1,eax,3
	mov al,byte ptr[ebx+1]					;al=v1 (2)
	add src2_v,2
	pinsrw xmm1,eax,7
	mov al,byte ptr[edx]					;al=v2 (1)
	pinsrw xmm2,eax,3
	mov al,byte ptr[edx+1]					;al=v2 (2)
	pinsrw xmm2,eax,7
	
	pmullw xmm1,xmm5
	paddsw xmm2,xmm6
	paddsw xmm1,xmm2
	psraw xmm1,3
	paddsw xmm0,xmm1
	packuswb xmm0,xmm7
	movq qword ptr[edi],xmm0
	add edi,8
	dec ecx
	jnz SSE2_3_a
	
	pop ebx
	pop edi
	pop esi

	ret

JPSDR_AutoYUY2_SSE2_3 endp


JPSDR_AutoYUY2_SSE2_4 proc src_y:dword,src1_u:dword,src2_u:dword,src1_v:dword,src2_v:dword,dst:dword,w:dword

	public JPSDR_AutoYUY2_SSE2_4

	push esi
	push edi
	push ebx
	
	pxor xmm7,xmm7
	pxor xmm6,xmm6
	pxor xmm5,xmm5
	pxor xmm1,xmm1
	pxor xmm2,xmm2
	
	mov edi,dst
	mov esi,src_y
	mov ecx,w
	cld
	
	mov eax,2
	pinsrw xmm6,eax,1
	pinsrw xmm6,eax,3
	pinsrw xmm6,eax,5
	pinsrw xmm6,eax,7
	
	mov eax,3
	pinsrw xmm5,eax,1
	pinsrw xmm5,eax,3
	pinsrw xmm5,eax,5
	pinsrw xmm5,eax,7

	xor eax,eax	

SSE2_4_a:
	pxor xmm0,xmm0
	lodsb
	pinsrw xmm0,eax,0
	lodsb
	pinsrw xmm0,eax,2
	lodsb
	mov ebx,src1_u
	pinsrw xmm0,eax,4
	lodsb
	mov edx,src2_u	
	pinsrw xmm0,eax,6
	mov al,byte ptr[ebx]					;al=u1 (1)
	add src1_u,2
	pinsrw xmm1,eax,1
	mov al,byte ptr[ebx+1]					;al=u1 (2)
	add src2_u,2
	pinsrw xmm1,eax,5
	mov al,byte ptr[edx]					;al=u2 (1)
	mov ebx,src1_v
	pinsrw xmm2,eax,1
	mov al,byte ptr[edx+1]					;al=u2 (2)
	mov edx,src2_v
	pinsrw xmm2,eax,5
	mov al,byte ptr[ebx]					;al=v1 (1)
	add src1_v,2
	pinsrw xmm1,eax,3
	mov al,byte ptr[ebx+1]					;al=v1 (2)
	add src2_v,2
	pinsrw xmm1,eax,7
	mov al,byte ptr[edx]					;al=v2 (1)
	pinsrw xmm2,eax,3
	mov al,byte ptr[edx+1]					;al=v2 (2)
	pinsrw xmm2,eax,7
	
	pmullw xmm1,xmm5
	paddsw xmm2,xmm6
	paddsw xmm1,xmm2
	psraw xmm1,2
	paddsw xmm0,xmm1
	packuswb xmm0,xmm7
	movq qword ptr[edi],xmm0
	add edi,8
	dec ecx
	jnz SSE2_4_a
	
	pop ebx
	pop edi
	pop esi

	ret

JPSDR_AutoYUY2_SSE2_4 endp


end





