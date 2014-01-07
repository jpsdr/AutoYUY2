.code

;JPSDR_AutoYUY2_1 proc src_y:dword,src_u:dword,src_v:dword,dst:dword,w:dword
; src_y = rcx
; src_u = rdx
; src_v = r8
; dst = r9
	
JPSDR_AutoYUY2_1 proc public frame

w equ dword ptr[rbp+48]

	push rbp
	.pushreg rbp
	mov rbp,rsp
	push rsi
	.pushreg rsi
	push rdi
	.pushreg rdi
	push rbx
	.pushreg rbx
	.endprolog


	mov rsi,rcx
	mov rdi,r9
	mov rbx,r8
	xor rcx,rcx
	mov ecx,w
	cld

SSE2_1_a:
	mov al,byte ptr[rsi+1]		;al=y2
	mov ah,byte ptr[rbx]		;ah=v
	inc rbx
	shl eax,16
	lodsw						;al=y1 ah=y2
	mov ah,byte ptr[rdx]		;ah=u
	inc rdx
	stosd
	loop SSE2_1_a
	
	pop rbx
	pop rdi
	pop rsi
	pop rbp

	ret

JPSDR_AutoYUY2_1 endp


;JPSDR_AutoYUY2_SSE2_2 proc src_y:dword,src1_u:dword,src2_u:dword,src1_v:dword,src2_v:dword,dst:dword,w:dword
; src_y = rcx
; src1_u = rdx
; src2_u = r8
; src1_v = r9

JPSDR_AutoYUY2_SSE2_2 proc public frame

src2_v equ qword ptr[rbp+48]
dst equ qword ptr[rbp+56]
w equ dword ptr[rbp+64]

	push rbp
	.pushreg rbp
	mov rbp,rsp
	push rsi
	.pushreg rsi
	push rdi
	.pushreg rdi
	push rbx
	.pushreg rbx
	sub rsp,32
	.allocstack 32
	movdqu [rsp],xmm6
	.savexmm128 xmm6,0
	movdqu [rsp+16],xmm7
	.savexmm128 xmm7,16
	.endprolog
	
	pxor xmm7,xmm7
	pxor xmm6,xmm6
	pxor xmm5,xmm5
	pxor xmm4,xmm4
	pxor xmm2,xmm2
	pxor xmm1,xmm1
	xor rax,rax
	
	mov rsi,rcx		; rsi = src_y
	mov r10,rdx		; r10=src1_u
	mov rdi,dst
	mov r11,src2_v
	xor rcx,rcx
	mov rdx,2
	mov rbx,8
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
	pinsrw xmm0,eax,4
	lodsb
	pinsrw xmm0,eax,6
	movzx eax,byte ptr[r10]					;al=u1 (1)
	pinsrw xmm1,eax,1
	movzx eax,byte ptr[r10+1]					;al=u1 (2)
	add r10,rdx
	pinsrw xmm1,eax,5
	movzx eax,byte ptr[r8]					;al=u2 (1)
	pinsrw xmm2,eax,1
	movzx eax,byte ptr[r8+1]					;al=u2 (2)
	add r8,rdx
	pinsrw xmm2,eax,5
	movzx eax,byte ptr[r9]					;al=v1 (1)
	pinsrw xmm1,eax,3
	movzx eax,byte ptr[r9+1]					;al=v1 (2)
	add r9,rdx
	pinsrw xmm1,eax,7
	movzx eax,byte ptr[r11]					;al=v2 (1)
	pinsrw xmm2,eax,3
	movzx eax,byte ptr[r11+1]					;al=v2 (2)
	add r11,rdx
	pinsrw xmm2,eax,7
	
	pmullw xmm1,xmm5
	pmullw xmm2,xmm4
	paddsw xmm1,xmm6
	paddsw xmm1,xmm2
	psraw xmm1,3
	paddsw xmm0,xmm1
	packuswb xmm0,xmm7
	movq qword ptr[rdi],xmm0
	add rdi,rbx
	dec ecx
	jnz SSE2_2_a
	
	movdqu xmm7,[rsp+16]
	movdqu xmm6,[rsp]
	add rsp,32
	pop rbx
	pop rdi
	pop rsi
	pop rbp

	ret	

JPSDR_AutoYUY2_SSE2_2 endp


;JPSDR_AutoYUY2_SSE2_3 proc src_y:dword,src1_u:dword,src2_u:dword,src1_v:dword,src2_v:dword,dst:dword,w:dword
; src_y = rcx
; src1_u = rdx
; src2_u = r8
; src1_v = r9

JPSDR_AutoYUY2_SSE2_3 proc public frame

src2_v equ qword ptr[rbp+48]
dst equ qword ptr[rbp+56]
w equ dword ptr[rbp+64]

	push rbp
	.pushreg rbp
	mov rbp,rsp
	push rsi
	.pushreg rsi
	push rdi
	.pushreg rdi
	push rbx
	.pushreg rbx
	sub rsp,32
	.allocstack 32
	movdqu [rsp],xmm6
	.savexmm128 xmm6,0
	movdqu [rsp+16],xmm7
	.savexmm128 xmm7,16
	.endprolog

	pxor xmm7,xmm7
	pxor xmm6,xmm6
	pxor xmm5,xmm5
	pxor xmm1,xmm1
	pxor xmm2,xmm2
	
	mov rsi,rcx		; rsi = src_y
	mov r10,rdx		; r10=src1_u
	mov r11,src2_v
	mov rdi,dst
	xor rcx,rcx
	mov rdx,2
	mov rbx,8
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
	pinsrw xmm0,eax,4
	lodsb
	pinsrw xmm0,eax,6
	movzx eax,byte ptr[r10]					;al=u1 (1)
	pinsrw xmm1,eax,1
	movzx eax,byte ptr[r10+1]					;al=u1 (2)
	add r10,rdx
	pinsrw xmm1,eax,5
	movzx eax,byte ptr[r8]					;al=u2 (1)
	pinsrw xmm2,eax,1
	movzx eax,byte ptr[r8+1]					;al=u2 (2)
	add r8,rdx
	pinsrw xmm2,eax,5
	movzx eax,byte ptr[r9]					;al=v1 (1)
	pinsrw xmm1,eax,3
	movzx eax,byte ptr[r9+1]					;al=v1 (2)
	add r9,rdx
	pinsrw xmm1,eax,7
	movzx eax,byte ptr[r11]					;al=v2 (1)
	pinsrw xmm2,eax,3
	movzx eax,byte ptr[r11+1]					;al=v2 (2)
	add r11,rdx
	pinsrw xmm2,eax,7
	
	pmullw xmm1,xmm5
	paddsw xmm2,xmm6
	paddsw xmm1,xmm2
	psraw xmm1,3
	paddsw xmm0,xmm1
	packuswb xmm0,xmm7
	movq qword ptr[rdi],xmm0
	add rdi,rbx
	dec ecx
	jnz SSE2_3_a
	
	movdqu xmm7,[rsp+16]
	movdqu xmm6,[rsp]
	add rsp,32
	pop rbx
	pop rdi
	pop rsi
	pop rbp

	ret

JPSDR_AutoYUY2_SSE2_3 endp


;JPSDR_AutoYUY2_SSE2_4 proc src_y:dword,src1_u:dword,src2_u:dword,src1_v:dword,src2_v:dword,dst:dword,w:dword
; src_y = rcx
; src1_u = rdx
; src2_u = r8
; src1_v = r9
	
JPSDR_AutoYUY2_SSE2_4 proc public frame	

src2_v equ qword ptr[rbp+48]
dst equ qword ptr[rbp+56]
w equ dword ptr[rbp+64]

	push rbp
	.pushreg rbp
	mov rbp,rsp
	push rsi
	.pushreg rsi
	push rdi
	.pushreg rdi
	push rbx
	.pushreg rbx
	sub rsp,32
	.allocstack 32
	movdqu [rsp],xmm6
	.savexmm128 xmm6,0
	movdqu [rsp+16],xmm7
	.savexmm128 xmm7,16
	.endprolog
	
	pxor xmm7,xmm7
	pxor xmm6,xmm6
	pxor xmm5,xmm5
	pxor xmm2,xmm2
	pxor xmm1,xmm1

	mov rsi,rcx		; rsi = src_y
	mov r10,rdx		; r10=src1_u
	mov r11,src2_v
	mov rdi,dst
	xor rcx,rcx
	mov rdx,2
	mov rbx,8
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
	pinsrw xmm0,eax,4
	lodsb
	pinsrw xmm0,eax,6
	movzx eax,byte ptr[r10]					;al=u1 (1)
	pinsrw xmm1,eax,1
	movzx eax,byte ptr[r10+1]					;al=u1 (2)
	add r10,rdx
	pinsrw xmm1,eax,5
	movzx eax,byte ptr[r8]					;al=u2 (1)
	pinsrw xmm2,eax,1
	movzx eax,byte ptr[r8+1]					;al=u2 (2)
	add r8,rdx
	pinsrw xmm2,eax,5
	movzx eax,byte ptr[r9]					;al=v1 (1)
	pinsrw xmm1,eax,3
	movzx eax,byte ptr[r9+1]					;al=v1 (2)
	add r9,rdx
	pinsrw xmm1,eax,7
	movzx eax,byte ptr[r11]					;al=v2 (1)
	pinsrw xmm2,eax,3
	movzx eax,byte ptr[r11+1]					;al=v2 (2)
	add r11,rdx
	pinsrw xmm2,eax,7
	
	pmullw xmm1,xmm5
	paddsw xmm2,xmm6
	paddsw xmm1,xmm2
	psraw xmm1,2
	paddsw xmm0,xmm1
	packuswb xmm0,xmm7
	movq qword ptr[rdi],xmm0
	add rdi,rbx
	dec ecx
	jnz SSE2_4_a

	movdqu xmm7,[rsp+16]
	movdqu xmm6,[rsp]
	add rsp,32
	pop rbx
	pop rdi
	pop rsi
	pop rbp

	ret

JPSDR_AutoYUY2_SSE2_4 endp


end





