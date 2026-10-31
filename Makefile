##############################################################################
# das3mex: MATLAB bindings for das2C
#
#   make                 build deps/das2C then +das3/private/das3mex.<mexext>
#   make test            offline and network unit tests via matlab -batch
#   make test_offline    only the tests that need no network
#   make test_network    only the tests that do
#   make clean           remove the mex file
#   make distclean       also clean deps/das2C
#
# The work is in buildfiles/<OS>.mak, one per operating system, picked by
# uname here.  Windows uses nmake with buildfiles/windows.mak (to be written).
# Overrides such as MATLAB_ROOT=... and N_ARCH=... go on the command line.

UNAME:=$(shell uname -s)
include buildfiles/$(UNAME).mak
