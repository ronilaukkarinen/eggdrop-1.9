# $Id: Makefile.am,v 1.1 2001/10/28 13:30:32 ite Exp $

## libcompat is built as convenience library

MAINTAINERCLEANFILES	= Makefile.in

INCLUDES		= -I$(top_builddir) -I$(top_srcdir) \
			-I$(top_builddir)/lib/compat \
			-I$(top_srcdir)/lib/compat

noinst_LTLIBRARIES	= libadns.la
libadns_la_SOURCES	= adns.h \
			check.c \
			dlist.h \
			event.c \
			general.c \
			internal.h \
			parse.c \
			query.c \
			reply.c \
			setup.c \
			transmit.c \
			tvarith.h \
			types.c
