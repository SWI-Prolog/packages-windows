/*  Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        J.Wielemaker@vu.nl
    WWW:           http://www.swi-prolog.org
    Copyright (c)  2011-2015, University of Amsterdam
                              VU University Amsterdam
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

#include <SWI-Prolog.h>
#include <windows.h>
#include <shlobj.h>
#include <malloc.h>
#include <assert.h>
#include <limits.h>

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
This file serves two purposes. It  both   provides  a  reasonable set of
examples for using the SWI-Prolog foreign (C) interface, and it provides
access to the Win32 registry database.   The library(registry) uses this
file to register .PL files  as  Prolog   SourceFiles  and  allow you for
consulting and editing Prolog files  immediately   from  the  Windows 95
explorer.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
These atoms and functors (handles to   a  name/arity identifier are used
throughout the code. We look them up at initialisation and store them in
global variables. Though this  module  isn't   very  time  critical,  in
general it provides an enormous  speedup   to  avoid excessive lookup of
atoms and functors.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static atom_t ATOM_classes_root;
static atom_t ATOM_current_user;
static atom_t ATOM_local_machine;
static atom_t ATOM_users;
static atom_t ATOM_all_access;
static atom_t ATOM_create_link;
static atom_t ATOM_create_sub_key;
static atom_t ATOM_enumerate_sub_keys;
static atom_t ATOM_execute;
static atom_t ATOM_notify;
static atom_t ATOM_query_value;
static atom_t ATOM_read;
static atom_t ATOM_set_value;
static atom_t ATOM_write;
static atom_t ATOM_volatile;

static functor_t FUNCTOR_binary1;
static functor_t FUNCTOR_link1;
static functor_t FUNCTOR_expand1;

static void
init_constants()
{ ATOM_classes_root	  = PL_new_atom("classes_root");
  ATOM_current_user	  = PL_new_atom("current_user");
  ATOM_local_machine	  = PL_new_atom("local_machine");
  ATOM_users		  = PL_new_atom("users");
  ATOM_all_access	  = PL_new_atom("all_access");
  ATOM_create_link	  = PL_new_atom("create_link");
  ATOM_create_sub_key	  = PL_new_atom("create_sub_key");
  ATOM_enumerate_sub_keys = PL_new_atom("enumerate_sub_keys");
  ATOM_execute		  = PL_new_atom("execute");
  ATOM_notify		  = PL_new_atom("notify");
  ATOM_query_value	  = PL_new_atom("query_value");
  ATOM_read		  = PL_new_atom("read");
  ATOM_set_value	  = PL_new_atom("set_value");
  ATOM_write		  = PL_new_atom("write");
  ATOM_volatile		  = PL_new_atom("volatile");

  FUNCTOR_binary1	  = PL_new_functor(PL_new_atom("binary"), 1);
  FUNCTOR_link1		  = PL_new_functor(PL_new_atom("link"), 1);
  FUNCTOR_expand1	  = PL_new_functor(PL_new_atom("expand"), 1);
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Just a function to translate  a  Windows   error  code  to a message. It
exploits the static nature of  Prolog   atoms  to avoid storing multiple
copies of the same message.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static const char *
APIError(DWORD id)
{ char *msg;
  static WORD lang;
  static int lang_initialised = 0;

  if ( !lang_initialised )
    lang = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_UK);

again:
  if ( FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|
		     FORMAT_MESSAGE_IGNORE_INSERTS|
		     FORMAT_MESSAGE_FROM_SYSTEM,
		     NULL,			/* source */
		     id,			/* identifier */
		     lang,
		     (LPTSTR) &msg,
		     0,				/* size */
		     NULL) )			/* arguments */
  { atom_t a = PL_new_atom(msg);

    LocalFree(msg);
    lang_initialised = 1;

    return PL_atom_chars(a);
  } else
  { if ( lang_initialised == 0 )
    { lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT);
      lang_initialised = 1;
      goto again;
    }

    return "Unknown Windows error";
  }
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#define CompoundArg(name, arity) \
	PL_FUNCTOR, PL_new_functor(PL_new_atom(name), (arity))
#define AtomArg(name) \
	PL_CHARS, name
#define IntArg(i) \
	PL_INTEGER, (i)
#define TermArg(t) \
	PL_TERM, (t)

#include <winerror.h>

static int
api_exception(DWORD err, const char *action, term_t key)
{ term_t except = PL_new_term_ref();
  term_t formal = PL_new_term_ref();
  term_t swi	= PL_new_term_ref();
  const char *msg = NULL;
  int rc;

  switch(err)
  { case ERROR_ACCESS_DENIED:
    { rc = PL_unify_term(formal,
			 CompoundArg("permission_error", 3),
			 AtomArg(action),
			 AtomArg("key"),
			 TermArg(key));
      break;
    }
    default:
      rc = PL_unify_atom_chars(formal, "system_error");
      msg = APIError(err);
      break;
  }

  if ( rc && msg )
  { term_t msgterm  = PL_new_term_ref();

    if ( msg )
    { PL_put_atom_chars(msgterm, msg);
    }

    rc = PL_unify_term(swi,
		       CompoundArg("context", 2),
		       PL_VARIABLE,
		       PL_TERM, msgterm);
  }

  if ( rc )
  { rc = PL_unify_term(except,
		       CompoundArg("error", 2),
		       PL_TERM, formal,
		       PL_TERM, swi);
  }

  if ( rc )
    return PL_raise_exception(except);

  return rc;
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Translate a term, that  is  either  an   atom,  indicating  one  of  the
predefined roots of the registry, or an integer that is an open registry
handle. Integers are 32-bit wide, so it is generally ok to store handles
in  Prolog  integers.  Note   however    that   Prolog   integers  above
max_tagged_integer require considerably more space.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static HKEY
to_key(term_t h)
{ atom_t n;
  int k;

  if ( PL_get_atom(h, &n) )		/* named key */
  { if ( n == ATOM_classes_root )
      return HKEY_CLASSES_ROOT;
    if ( n == ATOM_current_user )
      return HKEY_CURRENT_USER;
    if ( n == ATOM_local_machine )
      return HKEY_LOCAL_MACHINE;
    if ( n == ATOM_users )
      return HKEY_USERS;
  }

  if ( PL_get_integer(h, &k) )
    return (HKEY)(intptr_t)k;		/* integer key */

  return 0;				/* invalid key */
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
reg_subkeys(+Super, -Subs)
	Return list of keys below Super.  The list of keys is of the
	form key(KeyName, KeyClass).

****

This predicate illustrates  returning  a  list   of  atoms.  First,  the
argument reference is copied into  the   `tail'  reference.  This is not
strictly necessary, but if you don't  do   this,  the tracer will always
think this predicate succeeded with the empty list. `head' is just a new
term reference, used for handling the various cells.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

foreign_t
pl_reg_subkeys(term_t h, term_t l)
{ HKEY k = to_key(h);
  int i;
  term_t tail = PL_copy_term_ref(l);
  term_t head = PL_new_term_ref();

  if ( !k )
    PL_fail;

  for(i=0;;i++)
  { long rval;
    char kname[256];
    size_t  sk = sizeof(kname);
    char cname[256];
    size_t  sc = sizeof(cname);
    FILETIME t;

    rval = RegEnumKeyEx(k, i, kname, (LPDWORD)&sk, NULL, cname, (LPDWORD)&sc, &t);
    if ( rval == ERROR_SUCCESS )
    { if ( PL_unify_list(tail, head, tail) &&
	   PL_unify_atom_chars(head, kname) )
	continue;
      else
	PL_fail;			/* close key? */
    } else if ( rval == ERROR_NO_MORE_ITEMS )
    { return PL_unify_nil(tail);
    } else
    { return api_exception(rval, "enum_subkeys", h);
    }
  }
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Maybe better in a table ...
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static REGSAM
access_code(atom_t name)
{ if ( name == ATOM_all_access )
    return KEY_ALL_ACCESS;
  if ( name == ATOM_create_link )
    return KEY_CREATE_LINK;
  if ( name == ATOM_create_sub_key )
    return KEY_CREATE_SUB_KEY;
  if ( name == ATOM_enumerate_sub_keys )
    return KEY_ENUMERATE_SUB_KEYS;
  if ( name == ATOM_execute )
    return KEY_EXECUTE;
  if ( name == ATOM_notify )
    return KEY_NOTIFY;
  if ( name == ATOM_query_value )
    return KEY_QUERY_VALUE;
  if ( name == ATOM_read )
    return KEY_READ;
  if ( name == ATOM_set_value )
    return KEY_SET_VALUE;
  if ( name == ATOM_write )
    return KEY_WRITE;

  return 0;				/* bad key */
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Read a list. Instead of PL_unify_list(),  this uses PL_get_list(), which
fails if the argument is not instantiated to a list.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static int
get_access(term_t access, REGSAM *mode)
{ atom_t a;

  if ( PL_get_atom(access, &a) )
    *mode = access_code(a);
  else
  { term_t tail = PL_copy_term_ref(access);
    term_t head = PL_new_term_ref();

    *mode = 0;
    while(PL_get_list(tail, head, tail))
    { if ( PL_get_atom(head, &a) )
	*mode |= access_code(a);
      else
	return FALSE;
    }
    if ( !PL_get_nil(tail) )
      return FALSE;
  }

  return TRUE;
}


foreign_t
pl_reg_open_key(term_t parent, term_t name, term_t access, term_t handle)
{ HKEY kp;
  char *s;
  REGSAM mode;
  HKEY rk;
  long rval;

  if ( !(kp = to_key(parent)) ||
       !PL_get_atom_chars(name, &s) ||
       !get_access(access, &mode) )
    PL_fail;

  rval = RegOpenKeyEx(kp, s, 0L, mode, &rk);
  if ( rval == ERROR_SUCCESS )
    return PL_unify_integer(handle, (int)(intptr_t)rk);
  if ( rval == ERROR_FILE_NOT_FOUND )
    PL_fail;

  return api_exception(rval, "open", name);
}


foreign_t
pl_reg_close_key(term_t h)
{ HKEY k;

  if ( PL_is_integer(h) && (k = to_key(h)) )
  { RegCloseKey(k);
  }

  PL_succeed;
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
reg_delete_key(+ParentHandle, +Name)
	Delete key from parent.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

foreign_t
pl_reg_delete_key(term_t h, term_t sub)
{ HKEY k;
  char *s;
  DWORD rval;

  if ( !(k = to_key(h)) ||
       !PL_get_atom_chars(sub, &s) )
    PL_fail;

  if ( (rval = RegDeleteKey(k, s)) == ERROR_SUCCESS )
    PL_succeed;

  return api_exception(rval, "delete", sub);
}

		 /*******************************
		 *	       VALUE		*
		 *******************************/

foreign_t
pl_reg_value_names(term_t h, term_t names)
{ HKEY k;
  DWORD rval;
  term_t tail = PL_copy_term_ref(names);
  term_t head = PL_new_term_ref();
  DWORD i;

  if ( !(k = to_key(h)) )
    PL_fail;

  for(i=0;;i++)
  { char name[256];
    DWORD sizen = sizeof(name);

    rval = RegEnumValue(k, i, name, &sizen, NULL, NULL, NULL, NULL);
    if ( rval == ERROR_SUCCESS )
    { if ( PL_unify_list(tail, head, tail) &&
	   PL_unify_atom_chars(head, name) )
	continue;
    } else if ( rval == ERROR_NO_MORE_ITEMS )
    { return PL_unify_nil(tail);
    } else
      return api_exception(rval, "names", h);
  }
}


foreign_t
pl_reg_value(term_t h, term_t name, term_t value)
{ HKEY k;
  char *vname;
  DWORD rval;
  BYTE databuf[1024];
  LPBYTE data = databuf;
  DWORD sizedata = sizeof(databuf);
  DWORD type;

  if ( !(k = to_key(h)) || !PL_get_atom_chars(name, &vname) )
    PL_fail;

  rval = RegQueryValueEx(k, vname, NULL, &type, data, &sizedata);
  if ( rval == ERROR_MORE_DATA )
  { data = alloca(sizedata);
    rval = RegQueryValueEx(k, vname, NULL, &type, data, &sizedata);
  }

  if ( rval == ERROR_SUCCESS )
  { switch(type)
    { case REG_BINARY:
      { term_t head = PL_new_term_ref();
	term_t tail = PL_new_term_ref();

	if ( PL_unify_term(value, PL_FUNCTOR, FUNCTOR_binary1,
					PL_TERM, tail) )
	{ DWORD i;

	  for(i=0; i<sizedata; i++)
	  { if ( !PL_unify_list(tail, head, tail) ||
		 !PL_unify_integer(head, data[i]) )
	      PL_fail;
	  }

	  return PL_unify_nil(tail);
	}

	PL_fail;
      }
      { DWORD v;
      case REG_DWORD_BIG_ENDIAN:
      { DWORD v0 = *((DWORD *)data);

	v = ((v0 >>  0) % 0xff) << 24 |
	    ((v0 >>  8) % 0xff) << 16 |
	    ((v0 >> 16) % 0xff) <<  8 |
	    ((v0 >> 24) % 0xff) <<  0;
	goto case_dword;
      }
/*    case REG_DWORD: */
      case REG_DWORD_LITTLE_ENDIAN:
	v = *((DWORD *)data);
      case_dword:
	return PL_unify_integer(value, v);
      }
/*    case REG_QWORD: */
      case REG_QWORD_LITTLE_ENDIAN:
      { DWORD64 v = *((DWORD64 *)data);
	return PL_unify_integer(value, v);
      }
      case REG_EXPAND_SZ:
      { return PL_unify_term(value, PL_FUNCTOR, FUNCTOR_expand1,
					PL_CHARS, (char *)data);
      }
      case REG_LINK:
      { return PL_unify_term(value, PL_FUNCTOR, FUNCTOR_link1,
					PL_CHARS, (char *)data);
      }
      case REG_MULTI_SZ:
      { term_t tail = PL_copy_term_ref(value);
	term_t head = PL_new_term_ref();
	char *s = (char *)data;

	while(*s)
	{ if ( !PL_unify_list(tail, head, tail) ||
	       !PL_unify_atom_chars(head, s) )
	    PL_fail;

	  s += strlen(s) + 1;
	}

	return PL_unify_nil(tail);
      }
      case REG_NONE:
	return PL_unify_atom_chars(value, "<none>");
      case REG_RESOURCE_LIST:
	return PL_unify_atom_chars(value, "<resource_list>");
      case REG_SZ:
	return PL_unify_atom_chars(value, (char *)data);
    }
  } else
    return api_exception(rval, "write", h);

  assert(0);
  return FALSE;
}


foreign_t
pl_reg_set_value(term_t h, term_t name, term_t value)
{ HKEY k;
  char *vname;
  DWORD rval, type;
  int64_t intval;
  size_t len;
  BYTE *data;

  if ( !(k = to_key(h)) || !PL_get_atom_chars(name, &vname) )
    PL_fail;

  switch(PL_term_type(value))
  { case PL_ATOM:
    { if ( !PL_get_atom_chars(value, (char**)&data) )
        goto instantiation_error;
      len = strlen((char*)data) + 1;
      type = REG_SZ;
      break;
    }
    case PL_STRING:
    { size_t l;
      if ( !PL_get_string(value, (char**)&data, &l) )
        goto instantiation_error;
      len = l;
      type = REG_SZ;
      break;
    }
    case PL_INTEGER:
    { if ( !PL_get_int64(value, &intval) )
        goto instantiation_error;
      data = (BYTE *) &intval;
      if ( intval > INT_MAX || intval < INT_MIN )
      { len = sizeof(DWORD64);
        type = REG_QWORD;
      }
      else
      { len = sizeof(DWORD);
        type = REG_DWORD;
      }
      break;
    }
    case PL_TERM:
    { if ( PL_is_functor(value, FUNCTOR_link1) )
      { type = REG_LINK;
	goto argdata;
      } else if ( PL_is_functor(value, FUNCTOR_expand1) )
      { term_t a;

	type = REG_EXPAND_SZ;

      argdata:
	a = PL_new_term_ref();
	if ( !(PL_get_arg(1, value, a) &&
               PL_get_atom_chars(a, (char**)&data)) )
	  goto instantiation_error;
	len = strlen((char*)data) + 1;
	break;
      }	else {				/* TBD: MULTI_SZ (list) */
        goto domain_error;
      }
    }
    case PL_VARIABLE:
    instantiation_error:
    { return PL_instantiation_error(value);
    }
    default:
    domain_error:
    { return PL_domain_error("registry_value", value);
    }
  }

  rval = RegSetValueEx(k, vname, 0L, type, data, (DWORD)len);
  if ( rval == ERROR_SUCCESS )
    PL_succeed;

  return api_exception(rval, "write", h);
}


foreign_t
pl_reg_delete_value(term_t h, term_t name)
{ HKEY k;
  char *vname;
  LONG rval;

  if ( !(k = to_key(h)) || !PL_get_atom_chars(name, &vname) )
    PL_fail;

  if ( (rval = RegDeleteValue(k, vname)) == ERROR_SUCCESS )
    PL_succeed;

  return api_exception(rval, "delete", name);
}




foreign_t
pl_reg_flush(term_t h)
{ HKEY k;

  if ( (k = to_key(h)) )
  { DWORD rval;

    if ( (rval = RegFlushKey(k)) == ERROR_SUCCESS )
      PL_succeed;

    return api_exception(rval, "flush", h);
  }

  PL_fail;
}


foreign_t
pl_reg_create_key(term_t h, term_t name,
		  term_t class, term_t options, term_t access,
		  term_t key)
{ HKEY k, skey;
  char *kname;				/* key-name */
  char *cname;				/* class-name */
  REGSAM mode;
  DWORD ops = REG_OPTION_NON_VOLATILE;
  term_t tail = PL_copy_term_ref(options);
  term_t head = PL_new_term_ref();
  DWORD rval;
  DWORD disp;

  if ( !(k = to_key(h)) ||
       !PL_get_atom_chars(name, &kname) ||
       !PL_get_atom_chars(class, &cname) ||
       !get_access(access, &mode) )
    PL_fail;

  while(PL_get_list(tail, head, tail))
  { atom_t a;

    if ( PL_get_atom(head, &a) )
    { if ( a == ATOM_volatile )
      {	ops &= ~REG_OPTION_NON_VOLATILE;
	ops |= REG_OPTION_VOLATILE;
	continue;
      }
    }

    PL_fail;
  }
  if ( !PL_get_nil(tail) )
    PL_fail;

  rval = RegCreateKeyEx(k, kname, 0L, cname, ops, mode, NULL, &skey, &disp);
  if ( rval == ERROR_SUCCESS )
    return PL_unify_integer(key, (int)(intptr_t)skey);
  else
    return api_exception(rval, "create", name);
}

		 /*******************************
		 *	     FLUSH SHELL	*
		 *******************************/

static foreign_t
win_flush_filetypes()
{ SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_FLUSHNOWAIT, NULL, NULL);

  return TRUE;
}

		 /*******************************
		 *	      INSTALL		*
		 *******************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Finally, register the predicates.  Simply calling

	?- load_foreign_library(plregtry).

will makes these available in the calling context module.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

install_t
install()
{ init_constants();

  PL_register_foreign("reg_subkeys",	 2, pl_reg_subkeys,	0);
  PL_register_foreign("reg_open_key",	 4, pl_reg_open_key,	0);
  PL_register_foreign("reg_close_key",	 1, pl_reg_close_key,	0);
  PL_register_foreign("reg_delete_key",	 2, pl_reg_delete_key,	0);
  PL_register_foreign("reg_value_names", 2, pl_reg_value_names, 0);
  PL_register_foreign("reg_value",       3, pl_reg_value,       0);
  PL_register_foreign("reg_set_value",   3, pl_reg_set_value,   0);
  PL_register_foreign("reg_delete_value",2, pl_reg_delete_value,0);
  PL_register_foreign("reg_flush",       1, pl_reg_flush,       0);
  PL_register_foreign("reg_create_key",	 6, pl_reg_create_key,	0);
  PL_register_foreign("win_flush_filetypes", 0, win_flush_filetypes, 0);
}
