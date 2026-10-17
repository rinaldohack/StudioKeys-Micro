/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* StudioKEYS Micro update loader entry. The SPL decompresses the loader to its load
 * address (0x01C0A800) and calls its first byte. Interrupts off,
 * our own stacks, clear .bss, then C. Never returns. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	.ascii "SKM-LOADER-1.0.0"	/* 16 bytes: the updater recognises its loader in a package by this */
	.balign 2
1:	cli
	sp = _stack_top
	ssp = _sstack_top
	call ldr_cstart
	goto .
