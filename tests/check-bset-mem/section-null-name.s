| Linked, then the code section's name is pointed at string-table offset 0,
| as in the k30 kernel: objdump then prints "Disassembly of section :".  The
| code must still be decoded (the old gate saw nothing and passed).
| link: null-section-0
| expect: rc=1 findings=1
	.text
	.globl	_start
_start:	move.l	4(%sp),%d1
	move.l	8(%sp),%a0
	bset	%d1,(%a0)
	rts
