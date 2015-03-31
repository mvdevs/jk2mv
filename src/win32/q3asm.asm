; ===========================================================================
; Copyright (C) 2011 Thilo Schulz <thilo@tjps.eu>
; 
; This file is part of Quake III Arena source code.
; 
; Quake III Arena source code is free software; you can redistribute it
; and/or modify it under the terms of the GNU General Public License as
; published by the Free Software Foundation; either version 2 of the License,
; or (at your option) any later version.
; 
; Quake III Arena source code is distributed in the hope that it will be
; useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
; 
; You should have received a copy of the GNU General Public License
; along with Quake III Arena source code; if not, write to the Free Software
; Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
; ===========================================================================

; MASM ftol conversion functions using SSE or FPU
; assume __cdecl calling convention is being used for x86, __fastcall for x64

IFNDEF _M_X64
.686p
.xmm
.model flat, c
ENDIF

.data
ALIGN 16
ssemask DWORD 0FFFFFFFFh, 0FFFFFFFFh, 0FFFFFFFFh, 00000000h

.code

; dummy
IFNDEF _M_X64
	.safeseh SEH_handler
	SEH_handler   proc
		ret
	SEH_handler   endp
ENDIF

IFDEF _M_X64
	Sys_SnapVector PROC
		movaps xmm1, ssemask		; initialize the mask register
		movups xmm0, [rcx]			; here is stored our vector. Read 4 values in one go
		movaps xmm2, xmm0			; keep a copy of the original data
		andps xmm0, xmm1			; set the fourth value to zero in xmm0
		andnps xmm1, xmm2			; copy fourth value to xmm1 and set rest to zero
		cvtps2dq xmm0, xmm0			; convert 4 single fp to int
		cvtdq2ps xmm0, xmm0			; convert 4 int to single fp
		orps xmm0, xmm1				; combine all 4 values again
		movups [rcx], xmm0			; write 3 rounded and 1 unchanged values back to memory
		ret
	Sys_SnapVector ENDP

	Q_ftol PROC
		cvttss2si eax, xmm0
		ret
	Q_ftol ENDP

	Q_VMftol PROC
		movss xmm0, dword ptr [rdi + rbx * 4]
		cvttss2si eax, xmm0
		ret
	Q_VMftol ENDP

	qvmcall64 PROC
		push rsi					; push non-volatile registers to stack
		push rdi
		push rbx
		push rcx					; need to save pointer in rcx so we can write back the programData value to caller

		; registers r8 and r9 have correct value already thanx to __fastcall
		xor rbx, rbx				; opStackOfs starts out being 0
		mov rdi, rdx				; opStack
		mov esi, dword ptr [rcx]	; programStack

		call qword ptr [r8]			; instructionPointers[0] is also the entry point

		pop rcx

		mov dword ptr [rcx], esi	; write back the programStack value
		mov al, bl					; return opStack offset

		pop rbx
		pop rdi
		pop rsi

		ret
	qvmcall64 ENDP
ELSE
	Sys_SnapVector PROC
		mov eax, dword ptr 4[esp]	; store address of vector in eax
		movaps xmm1, ssemask		; initialize the mask register for maskmovdqu
		movups xmm0, [eax]			; here is stored our vector. Read 4 values in one go
		movaps xmm2, xmm0			; keep a copy of the original data
		andps xmm0, xmm1			; set the fourth value to zero in xmm0
		andnps xmm1, xmm2			; copy fourth value to xmm1 and set rest to zero
		cvtps2dq xmm0, xmm0			; convert 4 single fp to int
		cvtdq2ps xmm0, xmm0			; convert 4 int to single fp
		orps xmm0, xmm1				; combine all 4 values again
		movups [eax], xmm0			; write 3 rounded and 1 unchanged values back to memory
		ret
	Sys_SnapVector ENDP

	Q_ftol PROC
		movss xmm0, dword ptr [esp + 4]
		cvttss2si eax, xmm0
		ret
	Q_ftol ENDP

	Q_VMftol PROC
		movss xmm0, dword ptr [edi + ebx * 4]
		cvttss2si eax, xmm0
		ret
	Q_VMftol ENDP
ENDIF

end