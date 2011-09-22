################################################################
# Build the SWI-Prolog windows package on MS-Windows
#
# Author: Jan Wielemaker
#
# Use:
#	nmake /f Makefile.mak
#	nmake /f Makefile.mak install
################################################################

PLHOME=..\..
!include $(PLHOME)\src\rules.mk
CFLAGS=$(CFLAGS) /D__SWI_PROLOG__

DLLS=	plregtry.dll dlltest.dll

all:	$(DLLS)

plregtry.dll:	plregtry.obj
	$(LD) /dll /out:$@ $(LDFLAGS) plregtry.obj $(PLLIB) $(LIBS)

dlltest.dll:	dlltest.obj
	$(LD) /dll /out:$@ $(LDFLAGS) dlltest.obj $(TERMLIB) $(PLLIB) $(LIBS)


################################################################
# Testing
################################################################

check::

################################################################
# Installation
################################################################

install: idll ilib

idll::
		copy plregtry.dll "$(BINDIR)"
		copy dlltest.dll "$(BINDIR)"
!IF "$(PDB)" == "true"
		copy plregtry.pdb "$(BINDIR)"
		copy dlltest.pdb "$(BINDIR)"
!ENDIF

ilib::
		copy registry.pl "$(PLBASE)\library"
		$(MAKEINDEX)

uninstall::
		del "$(BINDIR)\plregtry.dll"
		del "$(BINDIR)\dlltest.dll"
		del "$(PLBASE)\library\registry.pl"
		$(MAKEINDEX)

html-install::
xpce-install::

################################################################
# Clean
################################################################

clean::
	if exist *.obj del *.obj
	if exist *~ del *~

distclean: clean
	-del *.dll *.lib *.exp *.pdb 2>nul
