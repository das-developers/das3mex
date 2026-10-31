##############################################################################
# das3mex on MacOS (untested as of 2026-10-31).  Included by the top level Makefile, which picks this
# file by uname.  See the Makefile for the targets.
#
# Override MATLAB_ROOT if the guess below is wrong, i.e.
#   make MATLAB_ROOT=/opt/MATLAB/R2024a

ifeq ($(MATLAB_ROOT),)
MATLAB_ROOT:=$(shell ls -d /Applications/MATLAB_R20*.app 2>/dev/null | tail -1)
endif
ifeq ($(MATLAB_ROOT),)
$(error MATLAB_ROOT not set and no /Applications/MATLAB_R20*.app found)
endif

# Homebrew puts fftw and openssl@3 off the default search path
BREW_PREFIX:=$(shell brew --prefix 2>/dev/null)
ifneq ($(BREW_PREFIX),)
BREW_FLAGS:=-I$(BREW_PREFIX)/include -I$(BREW_PREFIX)/opt/openssl@3/include \
 -L$(BREW_PREFIX)/lib -L$(BREW_PREFIX)/opt/openssl@3/lib
endif

MEX:=$(MATLAB_ROOT)/bin/mex
MATLAB:=$(MATLAB_ROOT)/bin/matlab
MEXEXT:=$(shell $(MATLAB_ROOT)/bin/mexext)

# Same architecture token the das2C and mgf-toolkit makefiles use
ifeq ($(N_ARCH),)
N_ARCH:=$(shell uname -s).$(shell uname -m)
N_ARCH:=$(subst /,_,$(N_ARCH))
endif

##############################################################################
# das2C, a submodule built in place.  SPICE and CDF programs are not needed
# by the binding; leaving them out skips two large downloads.

DAS2C:=deps/das2C
DAS2C_BD:=$(DAS2C)/build.$(N_ARCH)
DAS2C_LIB:=$(DAS2C_BD)/libdas3.a
DAS2C_OPTS:=SPICE=no CDF=no N_ARCH=$(N_ARCH)

# Shared libraries libdas3.a needs.  MATLAB ships its own copies of some of
# these (libexpat, libssl, libcrypto); so far the system ones load alongside
# without conflict, see co_notes.
DAS2C_LINK:=-lfftw3 -lexpat -lssl -lcrypto -lz -lpthread -lm

##############################################################################
# The mex file

PKG:=+das3
MEXOUT:=$(PKG)/private/das3mex.$(MEXEXT)
SRCS:=src/das3mex.c src/mx_src.c
HDRS:=src/das3mex.h

MEXFLAGS:=CFLAGS='$$CFLAGS -std=c99 -Wall -Wno-unused-function' $(BREW_FLAGS) -I$(DAS2C) -Isrc

.PHONY: all das2c test test_offline test_network clean distclean

all: $(MEXOUT)

# Always recurse: das2C's own make is incremental, and an edit inside the
# submodule must reach the library.  Order-only below so the mex file is
# relinked only when libdas3.a really changed.
das2c: $(DAS2C)/Makefile
	$(MAKE) -C $(DAS2C) $(DAS2C_OPTS)

$(DAS2C_LIB): | das2c

$(DAS2C)/Makefile:
	@echo "deps/das2C is empty, run: git submodule update --init"
	@false

$(MEXOUT): $(SRCS) $(HDRS) $(DAS2C_LIB)
	@mkdir -p $(PKG)/private
	$(MEX) $(MEXFLAGS) -outdir $(PKG)/private -output das3mex $(SRCS) $(DAS2C_LIB) $(DAS2C_LINK)

##############################################################################
# Tests run inside MATLAB.  Network tests are tagged so an offline machine
# can still check the build.

test: $(MEXOUT)
	$(MATLAB) -nodisplay -nosplash -sd $(CURDIR) -batch "addpath(pwd); r = runtests('das3test'); assertSuccess(r);"

test_offline: $(MEXOUT)
	$(MATLAB) -nodisplay -nosplash -sd $(CURDIR) -batch "addpath(pwd); r = runtests('das3test', 'Tag', 'offline'); assertSuccess(r);"

test_network: $(MEXOUT)
	$(MATLAB) -nodisplay -nosplash -sd $(CURDIR) -batch "addpath(pwd); r = runtests('das3test', 'Tag', 'network'); assertSuccess(r);"

clean:
	rm -f $(MEXOUT)

distclean: clean
	$(MAKE) -C $(DAS2C) $(DAS2C_OPTS) clean
