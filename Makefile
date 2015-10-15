# -*- Makefile -*-

TARGET		= rlm_zeromq
SRCS		= rlm_zeromq.c
#RLM_CFLAGS	=
RLM_LIBS	= -lzmq

include ../rules.mak


