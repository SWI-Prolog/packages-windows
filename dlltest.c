/*  Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        J.Wielemaker@vu.nl
    WWW:           http://www.swi-prolog.org
    Copyright (c)  2011-2013, University of Amsterdam
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in
       the documentation and/or other materials provided with the
       distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
    FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
    COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
    LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
    ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#include <windows.h>
#include "../../src/win32/console/console.h"
#include <SWI-Prolog.h>
#include <stdio.h>
#include <sys/timeb.h>

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
pl_say_hello()  illustrates  a   simple    foreign   language  predicate
implementation  calling  a  Windows  function.     By  convention,  such
functions are called pl_<name_of_predicate>.  Their   type  is foreign_t
and all arguments are of type term_t.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static foreign_t
pl_say_hello(term_t to)
{ char *msg;

  if ( PL_get_atom_chars(to, &msg) )
  { MessageBox(NULL, msg, "DLL test", MB_OK|MB_TASKMODAL);

    PL_succeed;
  }

  PL_fail;
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Interface function to modify the console:
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static foreign_t
pl_rlc_color(term_t which, term_t r, term_t b, term_t g)
{ int w;
  char *s;
  int tr, tb, tg;

  if ( PL_get_atom_chars(which, &s) )
  { if ( strcmp(s, "window") == 0 )
      w = RLC_WINDOW;
    else if ( strcmp(s, "text") == 0 )
      w = RLC_TEXT;
    else if ( strcmp(s, "highlight") == 0 )
      w = RLC_HIGHLIGHT;
    else if ( strcmp(s, "highlighttext") == 0 )
      w = RLC_HIGHLIGHTTEXT;
    else
      goto usage;
  } else
    goto usage;

  if ( PL_get_integer(r, &tr) &&
       PL_get_integer(b, &tb) &&
       PL_get_integer(g, &tg) )
  { if ( tr < 0 || tr > 255 || (tb < 0) || tb > 255 || tg < 0 || tg > 255 )
      goto usage;

    rlc_color(NULL, w, RGB(tr,tb,tg));
    PL_succeed;
  }

usage:
  PL_warning("rlc_color({window,text,highlight,highlighttext}, R, G, B)");
  PL_fail;
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
This function is a handle  called   from  abort/1.   The function should
perform cleanup as Prolog is going to   perform a long_jmp() back to the
toplevel.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static void
my_abort(void)
{ MessageBox(NULL,
	     "Execution aborted", "Abort handle test",
	     MB_OK|MB_TASKMODAL);
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Define mclock/1 to query time since Prolog was started in milliseconds.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static struct _timeb epoch;

void
initMClock()
{ _ftime(&epoch);
}


int64_t
mclock()
{ struct _timeb now;

  _ftime(&now);
  return (now.time - epoch.time) * 1000 +
	 (now.millitm - epoch.millitm);
}


foreign_t
pl_mclock(term_t msecs)
{ return PL_unify_int64(msecs, mclock());
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
(un)install functions.  Predicates registered with PL_register_foreign()
donot  need  to  be  uninstalled   as    the   Prolog   toplevel  driver
unload_foreign_library/[1,2] will to this automatically for you.

As only hooks need to be uninstalled,  you won't need this function very
often.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

install_t
install()
{ PL_register_foreign("say_hello", 1, pl_say_hello, 0);
  PL_register_foreign("rlc_color", 4, pl_rlc_color, 0);
  PL_register_foreign("mclock",    1, pl_mclock,    0);

  initMClock();
  PL_abort_hook(my_abort);
}


install_t
uninstall()
{ PL_abort_unhook(my_abort);
}
