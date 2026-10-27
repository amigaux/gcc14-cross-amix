/* amix-abi.h -- AMIX's function-return convention, for GCC's m68k-elf target.

   build-cross.sh copies this into gcc/config/m68k/ and appends it to the
   m68k-*-elf* tm_file in config.gcc, so it is read after m68kemb.h and its
   definitions are the ones that stick.

   m68k-elf is an embedded target, and returns values the embedded way.  AMIX
   (AT&T's cc, and the gcc 2.7.2.3 it ships) uses the SVR4 m68k convention.
   They differ at the return boundary in two places, and both fail silently --
   the program runs and reads a register that holds something else:

   POINTERS.  SVR4 returns a pointer in %a0 and copies it to %d0, and the
   CALLER reads %a0.  m68k-elf returns it in %d0 only, so a gcc 2.7.2.3 caller
   of a GCC 14 function reads whatever %a0 last held.  m68k_function_value
   (m68k.cc) implements the SVR4 rule; the Linux and NetBSD targets use it,
   and FUNCTION_VALUE_REGNO_P then has to admit %a0 as a value register.

   STRUCTS.  SVR4 returns every struct and union in memory, through a buffer
   whose address the caller passes in %a0.  m68k-elf returns 4- and 8-byte
   aggregates in %d0/%d1.  libc's div() and ldiv() return div_t, so GCC 14
   code calling them reads garbage while libc writes through a stale %a0.
   The register for the buffer address is already %a0 here (m68kelf.h); only
   the in-memory rule is missing, and DEFAULT_PCC_STRUCT_RETURN is it.

   Every GCC 14 library built without this header has to be rebuilt: a new
   caller reads a returned pointer from %a0, where an old callee never put it.
   See docs/TOOLCHAIN.md section 4.  */

#undef FUNCTION_VALUE
#define FUNCTION_VALUE(VALTYPE, FUNC) m68k_function_value (VALTYPE, FUNC)

#undef FUNCTION_VALUE_REGNO_P
#define FUNCTION_VALUE_REGNO_P(N) \
  ((N) == D0_REG || (N) == A0_REG || (TARGET_68881 && (N) == FP0_REG))

/* More than one register can carry a value, so __builtin_apply and
   __builtin_return have to save them all.  */
#undef NEEDS_UNTYPED_CALL
#define NEEDS_UNTYPED_CALL 1

#undef DEFAULT_PCC_STRUCT_RETURN
#define DEFAULT_PCC_STRUCT_RETURN 1
