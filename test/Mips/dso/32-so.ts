; RUN: %MCLinker -mtriple=mipsel-linux-gnueabi -filetype=dso -shared \
; RUN: %p/../../libs/MIPS/Linux/32/crti.o \
; RUN: %p/../../libs/MIPS/Linux/32/crtbeginS.o \
; RUN: %p/foo32.o \
; RUN: %p/../../libs/MIPS/Linux/32/libc_nonshared.a \
; RUN: --as-needed \
; RUN: %p/../../libs/MIPS/Linux/32/ld.so.1 \
; RUN: --no-as-needed \
; RUN: %p/../../libs/MIPS/Linux/32/crtendS.o \
; RUN: %p/../../libs/MIPS/Linux/32/crtn.o \
; RUN: %p/../../libs/MIPS/Linux/32/libc.so.6 \
; RUN: -soname libfoo.so -o libfoo32.so

; RUN: diff -s libfoo32.so %p/libfoo32.golden.so
