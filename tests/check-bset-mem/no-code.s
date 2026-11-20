| An object with no code at all: never a pass by default.
| expect: rc=2 findings=0
| expect --no-code-ok: rc=0 findings=0
	.data
	.long	1, 2, 3
