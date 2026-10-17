/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Copyright (C) 2026 Rinaldo Jonathan (LangitCinta! StageLabs) — StudioKEYS changes and the VST3 companion */
/* Same entry stub as StudioKEYS. The SPL maps app.bin at 0x02000120 and
 * requires `goto +4; rts` as the first instruction. Interrupts stay off. */
	.section .text.crt0,"ax",@progbits
	.globl _start
_start:
	goto 1f
	rts
1:	cli
	sp = _ustack_top
	ssp = _sstack_top
	call fm1_cstart
	goto .
