# Makefile to build SMPTE~ external for Pure Data.
#
# Needs Makefile.pdlibbuilder as helper makefile for platform-dependent build
# settings and rules.
#
# use : make pdincludepath=/path/to/pure-data/src/
#
# The following command will build the external and install the distributable 
# files into a subdirectory called build/abl_link (useful to package later with deken) :
#
# make install pdincludepath=../pure-data/src/ objectsdir=./build

lib.name = smpte~

smpte~.class.sources = decoder.c encoder.c ltc.c timecode.c smpte~.cpp 

# all extra files to be included in binary distribution of the library
datafiles = smpte~-help.pd 

LINK_INCLUDES ?= 
ASIO_INCLUDES ?= 
cflags = -std=c++11 -I$(LINK_INCLUDES) \
	 -I$(ASIO_INCLUDES) -Wno-multichar
    
suppress-wunused = yes

define forLinux
  cflags += -DLINK_PLATFORM_LINUX=1
endef

define forDarwin
  cflags += -DLINK_PLATFORM_MACOSX=1 -mmacosx-version-min=10.9
endef


define forWindows
  cflags += -DLINK_PLATFORM_WINDOWS=1
  ldlibs += -lws2_32 -liphlpapi -static -lpthread
endef

PDLIBBUILDERDIR ?= .
include $(PDLIBBUILDERDIR)/Makefile.pdlibbuilder

VERSION = $(shell git describe)

update-pdlibbuilder:
	curl https://raw.githubusercontent.com/pure-data/pd-lib-builder/master/Makefile.pdlibbuilder > ./Makefile.pdlibbuilder

deken-source:
	@rm -rf build_src
	@mkdir -p build_src/smpte~
	@cp $(smpte~.class.sources) \
		$(patsubst %.cpp, %.hpp, $(smpte~.class.sources)) \
		$(datafiles) Makefile.pdlibbuilder Makefile \
			build_src/smpte~
	cd build_src/ ; deken upload -v $(VERSION) smpte~

deken-binary:
	@rm -rf build
	@make install objectsdir=./build
	cd build/ ; deken upload -v $(VERSION) smpte~
    
