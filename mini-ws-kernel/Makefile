######----------------DEFINE---------------------------

OS := OS_LINUX
OS_CC           := gcc
OS_CCFLAGS      := -g -Wall -Wno-deprecated -DOS_LINUX   -DHAVE_CONFIG_H  -D_U_="__attribute__((unused))" -DINET6 -DPLUGIN_DIR=\"wsk_plugins\" -DDATAFILE_DIR=\"wsk_datafile\" 
USRCCFLAG				:= -W -Wextra -Wdeclaration-after-statement -Wendif-labels -Wpointer-arith -Wno-pointer-sign -Wcast-align -Wformat-security
OS_CCDYNAFLAGS  := -fPIC
OS_CCDEBUGFLAGS := -g
OS_CPP          := $(OS_CC) -E
OS_LD           := g++ 
OS_LDDYNAFLAGS  := -shared  
OS_AR           := ar
OS_RM           := rm -f
OS_CP	          := cp
OS_LEX          := flex
OS_LEXFLAGS     :=
OS_YACC         := bison
OS_YACCFLAGS    := -b y -d -v
OS_LATEX        := latex
OS_DVI2PS       := dvips
OS_DVI2PDF      := dvipdfm
OS_METAPOST     := mpost

TOPDIR := $(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)
export TOPDIR

WSK_ROOT := $(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)
export WSK_ROOT


CC        := $(CROSSCOMPILE)$(OS_CC)
CCFLAGS   := $(OS_CCFLAGS) $(USRCCFLAG)
CPP       := $(CROSSCOMPILE)$(OS_CPP)
AR        := $(CROSSCOMPILE)$(OS_AR)
LD        := $(CROSSCOMPILE)$(OS_LD)
LDFLAGS   := $(OS_LDDYNAFLAGS)
RM        := $(OS_RM)
CP        := $(OS_CP)
LEX       := $(OS_LEX)
LEXFLAGS  := $(OS_LEXFLAGS)
YACC      := $(OS_YACC)
YACCFLAGS := $(OS_YACCFLAGS)
LATEX     := $(OS_LATEX)
DVI2PS    := $(OS_DVI2PS)
DVI2PDF   := $(OS_DVI2PDF)
MPOST     := $(OS_METAPOST)

BINDIR	  := debug

export CC CCFLAGS CPP AR LD LDFLAGS RM CP LEX LEXFLAGS YACC YACCFLAGS
export LATEX DVI2PS DVI2PDF MPOST
export OS_CCDYNAFLAGS OS_CCDEBUGFLAGS
export BINDIR





LIBINCLUDE    := -I. -I$(WSK_ROOT) -I$(WSK_ROOT)/epan  -I$(WSK_ROOT)/epan/dissectors   -I$(WSK_ROOT)/epan/ftypes -I$(WSK_ROOT)/epan/dfilter -I$(WSK_ROOT)/wiretap -I$(WSK_ROOT)/wsutil -I$(WSK_ROOT)/tools/lemon -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/lib64/glib-2.0 -I/usr/lib64/glib-2.0/include -I/usr/lib/x86_64-linux-gnu/glib-2.0/include
LINKPATH      := -L$(WSk_ROOT)/lib  -lglib-2.0              



#######Please set the parameters for your certain project################################################################		


#SRC directory,not used
SRCDIR	:= ./				

#The directory to store the object file
OBJDIR    :=$(TOPDIR)/#End

#The directory to store the dynamic file 
DYNADIR    :=$(TOPDIR)/#End

#The directory to store the static file
STATICDIR    :=$(TOPDIR)/#End

#The dynamic file name
DYNAFILE	:= libminiwskernel.so#End

#The static file name
STATICFILE	:= libminiwskernel.a#End



#In this makefile the default settings will compile all the cpp files
#So if there is any cpp file which is not necessary to be complied 
#Please add the correct file name here(ie.It wont be complied)

EXCSRC	:=#   

#################Now the parameters have been set well,Let's go ###########################################################


#########################################################################################
#					Notes						#
#	Now make file will deal with all the procedure					#
#	Please be careful to mofiy the following code					#
#											#
#########################################################################################		



LIBCPPOBJS	:=  $(patsubst %.cpp,%.o,$(filter-out $(EXCSRC),$(wildcard *.cpp)))
LIBCOBJS      :=  $(patsubst %.c,%.o,$(filter-out $(EXCSRC),$(wildcard *.c)))
LIBCOBJS      +=  $(patsubst %.c,%.o,$(filter-out $(EXCSRC),$(wildcard epan/*.c)))
LIBCOBJS      +=  $(patsubst %.c,%.o,$(filter-out $(EXCSRC),$(wildcard epan/dfilter/*.c)))
LIBCOBJS      +=  $(patsubst %.c,%.o,$(filter-out $(EXCSRC),$(wildcard epan/crypt/*.c)))
LIBCOBJS      +=  $(patsubst %.c,%.o,$(filter-out $(EXCSRC),$(wildcard epan/ftypes/*.c)))
LIBCOBJS      +=  $(patsubst %.c,%.o,$(filter-out $(EXCSRC),$(wildcard epan/dissectors/*.c)))
#LIBCOBJS      +=  $(patsubst %.c,%.o,$(filter-out $(EXCSRC),$(wildcard wiretap/*.c)))
LIBCOBJS      +=  $(patsubst %.c,%.o,$(filter-out $(EXCSRC),$(wildcard wsutil/*.c)))



DYNALIBRARY	:= $(DYNADIR)/$(DYNAFILE)
STATICLIBRARY := $(STATICDIR)/$(STATICFILE)

all: mkobjdir mkinstalldir $(DYNALIBRARY) $(STATICLIBRARY)

.PHONY : clean
.PHONY : install
.PHONY : uninstall
.PHONY : mkobjdir
.PHONY : mkinstalldir
.PHONY : movefile



install: mkinstalldir
	cp $(DYNALIBRARY) $(DYNADIR)
	cp $(STATICLIBRARY) $(STATICDIR)

uninstall:
	rm -f $(DYNADIR)/$(DYNAFILE) $(STATICDIR)/$(STATICFILE)

clean:
	rm -f $(DYNALIBRARY) $(STATICLIBRARY) $(LIBCOBJS) $(LIBCPPOBJS)

mrproper distclean: clean
	rm -f .depend


mkobjdir:
	if [ ! -d $(OBJDIR) ]; then \
		mkdir -p $(OBJDIR); fi                                                                                                              

mkinstalldir:
	if [ ! -d $(DYNADIR) ]; then\
		mkdir -p $(DYNADIR); fi
	if [ ! -d $(STATICDIR) ]; then\
		mkdir -p $(STATICDIR); fi

$(DYNALIBRARY): .depend $(LIBCOBJS) $(LIBCPPOBJS)
	$(CC) $(LDFLAGS) -Wno-deprecated -o $@ $(LIBCOBJS) $(LIBCPPOBJS) $(LINKPATH)

$(STATICLIBRARY): .depend $(LIBCOBJS) $(LIBCPPOBJS)
	$(AR) rs $@ $(LIBCOBJS) $(LIBCPPOBJS)

.depend: Makefile $(LIBCOBJS:.o=.c) $(LIBCPPOBJS:.o=.cpp)
	$(CC) -MM $(CCFLAGS) -Wno-deprecated $(OS_CCDYNAFLAGS) $(OS_CCDEBUGFLAGS) $(LIBINCLUDE) $(LIBCOBJS:.o=.c) $(LIBCPPOBJS:.o=.cpp) > $@

ifeq (.depend, $(wildcard .depend))
include .depend
endif

%.o:%.c
	$(CC)  $(CCFLAGS) $(OS_CCDYNAFLAGS) $(OS_CCDEBUGFLAGS) $(LIBINCLUDE) -c $< -o $@

%.o:%.cpp
	$(CC)  $(CCFLAGS) $(OS_CCDYNAFLAGS) $(OS_CCDEBUGFLAGS) $(LIBINCLUDE) -c $< -o $@

movefile:
	mv $(LIBCPPOBJS) $(OBJDIR)

