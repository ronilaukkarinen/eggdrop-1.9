/*
 * tcl.c --
 *
 *	the code for every command eggdrop adds to Tcl
 *	Tcl initialization
 *	getting and setting Tcl/eggdrop variables
 */
/*
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999, 2000, 2001, 2002, 2003 Eggheads Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef lint
static const char rcsid[] = "$Id: tcl.c,v 1.95 2003/02/03 11:41:34 wcc Exp $";
#endif

#include <stdlib.h>		/* getenv()				*/
#include <locale.h>		/* setlocale()				*/
#include "main.h"
#include "logfile.h"
#include "misc.h"
#include "chanprog.h"		/* masktype				*/

/* Used for read/write to internal strings */
typedef struct {
  char *str;			/* Pointer to actual string in eggdrop	     */
  int max;			/* max length (negative: read-only var
				   when protect is on) (0: read-only ALWAYS) */
  int flags;			/* 1 = directory			     */
} strinfo;

typedef struct {
  int *var;
  int ro;
} intinfo;


extern time_t online_since;

extern int flood_telnet_thr, flood_telnet_time, learn_users, default_flags,
           conmask, firewallport, notify_users_at, ignore_time,
           reserved_port_min, reserved_port_max, die_on_sighup, die_on_sigterm,
           dcc_total, raw_log, identtimeout, egg_numver, userfile_perm,
           default_uflags, strict_host;

extern char botuser[], motdfile[], admin[], userfile[], firewall[], helpdir[],
            notify_new[], myip[], moddir[], tempdir[], owner[], network[],
            myname[], bannerfile[], egg_version[], natip[], configfile[],
            textdir[], myip6[], pid_file[];
	
extern struct dcc_t *dcc;

int protect_readonly = 0; /* turn on/off readonly protection */
char whois_fields[1025] = ""; /* fields to display in a .whois */
Tcl_Interp *interp; /* eggdrop always uses the same interpreter */
int use_invites = 0;
int use_exempts = 0;
int force_expire = 0;
int copy_to_tmp = 0;
int par_telnet_flood = 1;
int handlen = HANDLEN;

/* Prototypes for tcl */
Tcl_Interp *Tcl_CreateInterp();


static void botnet_change(char *new)
{
  if (strcasecmp(myname, new)) {
     if (myname[0])
       putlog(LOG_MISC, "*", "* IDENTITY CHANGE: %s -> %s", myname, new);
     strcpy(myname, new);
  }
}


/*
 *     Vars, traces, misc
 */

/* Used for read/write to integer couplets */
typedef struct {
  int *left;			/* left side of couplet */
  int *right;			/* right side */
} coupletinfo;

/* Read/write integer couplets (int1:int2) */
static char *tcl_eggcouplet(ClientData cdata, Tcl_Interp *irp, char *name1,
			    char *name2, int flags)
{
  char *s, s1[41];
  coupletinfo *cp = (coupletinfo *) cdata;

  if (flags & (TCL_TRACE_READS | TCL_TRACE_UNSETS)) {
    snprintf(s1, sizeof s1, "%d:%d", *(cp->left), *(cp->right));
    Tcl_SetVar2(interp, name1, name2, s1, TCL_GLOBAL_ONLY);
    if (flags & TCL_TRACE_UNSETS)
      Tcl_TraceVar(interp, name1,
		   TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		   tcl_eggcouplet, cdata);
  } else {			/* writes */
    s = Tcl_GetVar2(interp, name1, name2, TCL_GLOBAL_ONLY);
    if (s != NULL) {
      int nr1, nr2;

      if (strlen(s) > 40)
	s[40] = 0;
      sscanf(s, "%d%*c%d", &nr1, &nr2);
      *(cp->left) = nr1;
      *(cp->right) = nr2;
    }
  }
  return NULL;
}

/* Read or write normal integer.
 */
static char *tcl_eggint(ClientData cdata, Tcl_Interp *irp, char *name1,
			char *name2, int flags)
{
  char *s, s1[40];
  long l;
  intinfo *ii = (intinfo *) cdata;

  if (flags & (TCL_TRACE_READS | TCL_TRACE_UNSETS)) {
    /* Special cases */
    if ((int *) ii->var == &conmask)
      strcpy(s1, masktype(conmask));
    else if ((int *) ii->var == &default_flags) {
      struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};
      fr.global = default_flags;
      fr.udef_global = default_uflags;
      build_flags(s1, &fr, 0);
    } else if ((int *) ii->var == &userfile_perm) {
      snprintf(s1, sizeof s1, "0%o", userfile_perm);
    } else
      snprintf(s1, sizeof s1, "%d", *(int *) ii->var);
    Tcl_SetVar2(interp, name1, name2, s1, TCL_GLOBAL_ONLY);
    if (flags & TCL_TRACE_UNSETS)
      Tcl_TraceVar(interp, name1,
		   TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		   tcl_eggint, cdata);
    return NULL;
  } else {			/* Writes */
    s = Tcl_GetVar2(interp, name1, name2, TCL_GLOBAL_ONLY);
    if (s != NULL) {
      if ((int *) ii->var == &conmask) {
	if (s[0])
	  conmask = logmodes(s);
	else
	  conmask = 0x7fffffff;
      } else if ((int *) ii->var == &default_flags) {
	struct flag_record fr = {FR_GLOBAL, 0, 0, 0, 0, 0};

	break_down_flags(s, &fr, 0);
	default_flags = sanity_check(fr.global); /* drummer */
	default_uflags = fr.udef_global;
      } else if ((ii->ro == 2) || ((ii->ro == 1) && protect_readonly)) {
	return "read-only variable";
      } else {
	if (Tcl_ExprLong(interp, s, &l) == TCL_ERROR)
	  return interp->result;
	else
	  *(ii->var) = (int) l;
      }
    }
    return NULL;
  }
}

/* Read/write normal string variable
 */
static char *tcl_eggstr(ClientData cdata, Tcl_Interp *irp, char *name1,
			char *name2, int flags)
{
  char *s;
  strinfo *st = (strinfo *) cdata;

  if (flags & (TCL_TRACE_READS | TCL_TRACE_UNSETS)) {
    if ((st->str == firewall) && (firewall[0])) {
      char s1[127];

      snprintf(s1, sizeof s1, "%s:%d", firewall, firewallport);
      Tcl_SetVar2(interp, name1, name2, s1, TCL_GLOBAL_ONLY);
    } else
      Tcl_SetVar2(interp, name1, name2, st->str, TCL_GLOBAL_ONLY);
    if (flags & TCL_TRACE_UNSETS) {
      Tcl_TraceVar(interp, name1, TCL_TRACE_READS | TCL_TRACE_WRITES |
		   TCL_TRACE_UNSETS, tcl_eggstr, cdata);
      if ((st->max <= 0) && (protect_readonly || (st->max == 0)))
	return "read-only variable"; /* it won't return the error... */
    }
    return NULL;
  } else {			/* writes */
    if ((st->max <= 0) && (protect_readonly || (st->max == 0))) {
      Tcl_SetVar2(interp, name1, name2, st->str, TCL_GLOBAL_ONLY);
      return "read-only variable";
    }
    s = Tcl_GetVar2(interp, name1, name2, TCL_GLOBAL_ONLY);
    if (s != NULL) {
      if (strlen(s) > abs(st->max))
	s[abs(st->max)] = 0;
      if (st->str == myname)
	myname_change(s);
      else if (st->str == firewall) {
	splitc(firewall, s, ':');
	if (!firewall[0])
	  strcpy(firewall, s);
	else
	  firewallport = atoi(s);
      } else
	strcpy(st->str, s);
      if ((st->flags) && (s[0])) {
	if (st->str[strlen(st->str) - 1] != '/')
	  strcat(st->str, "/");
      }
    }
    return NULL;
  }
}

/* Add/remove tcl commands
 */
void add_tcl_commands(tcl_cmds *tab)
{
  int i;

  for (i = 0; tab[i].name; i++)
    Tcl_CreateCommand(interp, tab[i].name, tab[i].func, NULL, NULL);
}

void rem_tcl_commands(tcl_cmds *tab)
{
  int i;

  for (i = 0; tab[i].name; i++)
    Tcl_DeleteCommand(interp, tab[i].name);
}

/* Strings */
static tcl_strings def_tcl_strings[] =
{
  {"myname",            myname,         HANDLEN,        0},
  {"userfile",		userfile,	120,		STR_PROTECT},
  {"motd",		motdfile,	120,		STR_PROTECT},
  {"admin",		admin,		120,		0},
  {"help_path",		helpdir,	120,		STR_DIR | STR_PROTECT},
  {"temp_path",		tempdir,	120,		STR_DIR | STR_PROTECT},
  {"text_path",		textdir,	120,		STR_DIR | STR_PROTECT},
  {"mod_path",		moddir,		120,		STR_DIR | STR_PROTECT},
  {"notify_newusers",	notify_new,	120,		0},
  {"owner",		owner,		120,		STR_PROTECT},
  {"my_ip",		myip,		120,		0},
  {"my_ip6",		myip6,		120,		0},
  {"network",		network,	40,		0},
  {"whois_fields",	whois_fields,	1024,		0},
  {"nat_ip",		natip,		120,		0},
  {"username",		botuser,	10,		0},
  {"version",		egg_version,	0,		0},
  {"firewall",		firewall,	120,		0},
/* confvar patch by aaronwl */
  {"config",		configfile,	0,		0},
  {"telnet_banner",	bannerfile,	120,		STR_PROTECT},
  {"pidfile",		pid_file,	120,		STR_PROTECT},
  {NULL,		NULL,		0,		0}
};

/* Ints */
static tcl_ints def_tcl_ints[] =
{
  {"ignore_time",		&ignore_time,		0},
  {"handlen",                   &handlen,               2},
  {"hourly_updates",		&notify_users_at,	0},
  {"learn_users",		&learn_users,		0},
  {"uptime",			(int *) &online_since,	2},
  {"console",			&conmask,		0},
  {"default_flags",		&default_flags,		0},
  {"numversion",		&egg_numver,		2},
  {"die_on_sighup",		&die_on_sighup,		1},
  {"die_on_sigterm",		&die_on_sigterm,	1},
  {"raw_log",			&raw_log,		1},
  {"use_exempts",		&use_exempts,		0},
  {"use_invites",		&use_invites,		0},
  {"force_expire",		&force_expire,		0},
  {"strict_host",		&strict_host,		0},
  {"userfile_perm",		&userfile_perm,		0},
  {"copy-to-tmp",		&copy_to_tmp,		0},
  {NULL,			NULL,			0}
};

static tcl_coups def_tcl_coups[] =
{
  {"telnet_flood",	&flood_telnet_thr,	&flood_telnet_time},
  {"reserved_portrange", &reserved_port_min, &reserved_port_max},
  {NULL,		NULL,			NULL}
};

/* Set up Tcl variables that will hook into eggdrop internal vars via
 * trace callbacks.
 */
static void init_traces()
{
  add_tcl_coups(def_tcl_coups);
  add_tcl_strings(def_tcl_strings);
  add_tcl_ints(def_tcl_ints);
}

void kill_tcl()
{
  rem_tcl_coups(def_tcl_coups);
  rem_tcl_strings(def_tcl_strings);
  rem_tcl_ints(def_tcl_ints);
  Tcl_DeleteInterp(interp);
}

extern tcl_cmds tcldcc_cmds[];
extern script_command_t script_dcc_cmds[];
extern script_command_t script_user_cmds[];
extern script_command_t script_misc_cmds[];

/* Not going through Tcl's crazy main() system (what on earth was he
 * smoking?!) so we gotta initialize the Tcl interpreter
 */
void init_tcl(int argc, char **argv)
{
#ifndef HAVE_PRE7_5_TCL
  int j;
  char pver[1024] = "";
#endif

/* This must be done *BEFORE* Tcl_SetSystemEncoding(),
 * or Tcl_SetSystemEncoding() will cause a segfault.
 */
#ifndef HAVE_PRE7_5_TCL
  /* This is used for 'info nameofexecutable'.
   * The filename in argv[0] must exist in a directory listed in
   * the environment variable PATH for it to register anything.
   */
  Tcl_FindExecutable(argv[0]);
#endif

  /* Initialize the interpreter */
  interp = Tcl_CreateInterp();

#ifdef DEBUG
  /* Initialize Tcl's memory debugging if we want it */
  Tcl_InitMemory(interp);
#endif

  /* Set Tcl variable tcl_interactive to 0 */
  Tcl_SetVar(interp, "tcl_interactive", "0", TCL_GLOBAL_ONLY);

  /* Setup script library facility */
  Tcl_Init(interp);

#ifndef HAVE_PRE7_5_TCL
  /* Add eggdrop to Tcl's package list */
  for (j = 0; j <= strlen(egg_version); j++) {
    if ((egg_version[j] == ' ') || (egg_version[j] == '+'))
      break;
    pver[strlen(pver)] = egg_version[j];
  }
  Tcl_PkgProvide(interp, "eggdrop", pver);
#endif

  /* Initialize traces */
  init_traces();

  /* Add new commands */
  script_create_commands(script_dcc_cmds);
  script_create_commands(script_user_cmds);
  script_create_commands(script_misc_cmds);
}

void do_tcl(char *whatzit, char *script)
{
  int code;

  code = Tcl_Eval(interp, script);
  if (code != TCL_OK) {
    putlog(LOG_MISC, "*", "TCL error in script for '%s':", whatzit);
    putlog(LOG_MISC, "*", "%s", interp->result);
  }
}

/* Interpret tcl file fname.
 *
 * returns:   1 - if everything was okay
 */
int readtclprog(char *fname)
{
  FILE	*f;

  /* Check whether file is readable. */
  if ((f = fopen(fname, "r")) == NULL)
    return 0;
  fclose(f);

  if (Tcl_EvalFile(interp, fname) != TCL_OK) {
    putlog(LOG_MISC, "*", "TCL error in file '%s':", fname);
    putlog(LOG_MISC, "*", "%s",
	   Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY));
    return 0;
  }

  /* Refresh internal variables */
  return 1;
}

void add_tcl_strings(tcl_strings *list)
{
  int i;
  strinfo *st;
  int tmp;

  for (i = 0; list[i].name; i++) {
    st = (strinfo *) malloc(sizeof(strinfo));
    st->max = list[i].length - (list[i].flags & STR_DIR);
    if (list[i].flags & STR_PROTECT)
      st->max = -st->max;
    st->str = list[i].buf;
    st->flags = (list[i].flags & STR_DIR);
    tmp = protect_readonly;
    protect_readonly = 0;
    tcl_eggstr((ClientData) st, interp, list[i].name, NULL, TCL_TRACE_WRITES);
    protect_readonly = tmp;
    tcl_eggstr((ClientData) st, interp, list[i].name, NULL, TCL_TRACE_READS);
    Tcl_TraceVar(interp, list[i].name, TCL_TRACE_READS | TCL_TRACE_WRITES |
		 TCL_TRACE_UNSETS, tcl_eggstr, (ClientData) st);
  }
}

void rem_tcl_strings(tcl_strings *list)
{
  int i;
  strinfo *st;

  for (i = 0; list[i].name; i++) {
    st = (strinfo *) Tcl_VarTraceInfo(interp, list[i].name,
				      TCL_TRACE_READS |
				      TCL_TRACE_WRITES |
				      TCL_TRACE_UNSETS,
				      tcl_eggstr, NULL);
    Tcl_UntraceVar(interp, list[i].name,
		   TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		   tcl_eggstr, st);
    if (st != NULL)
      free(st);
  }
}

void add_tcl_ints(tcl_ints *list)
{
  int i, tmp;
  intinfo *ii;

  for (i = 0; list[i].name; i++) {
    ii = malloc(sizeof(intinfo));
    ii->var = list[i].val;
    ii->ro = list[i].readonly;
    tmp = protect_readonly;
    protect_readonly = 0;
    tcl_eggint((ClientData) ii, interp, list[i].name, NULL, TCL_TRACE_WRITES);
    protect_readonly = tmp;
    tcl_eggint((ClientData) ii, interp, list[i].name, NULL, TCL_TRACE_READS);
    Tcl_TraceVar(interp, list[i].name,
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 tcl_eggint, (ClientData) ii);
  }

}

void rem_tcl_ints(tcl_ints *list)
{
  int i;
  intinfo *ii;

  for (i = 0; list[i].name; i++) {
    ii = (intinfo *) Tcl_VarTraceInfo(interp, list[i].name,
				      TCL_TRACE_READS |
				      TCL_TRACE_WRITES |
				      TCL_TRACE_UNSETS,
				      tcl_eggint, NULL);
    Tcl_UntraceVar(interp, list[i].name,
		   TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		   tcl_eggint, (ClientData) ii);
    if (ii)
      free(ii);
  }
}

/* Allocate couplet space for tracing couplets
 */
void add_tcl_coups(tcl_coups *list)
{
  coupletinfo *cp;
  int i;

  for (i = 0; list[i].name; i++) {
    cp = (coupletinfo *) malloc(sizeof(coupletinfo));
    cp->left = list[i].lptr;
    cp->right = list[i].rptr;
    tcl_eggcouplet((ClientData) cp, interp, list[i].name, NULL,
		   TCL_TRACE_WRITES);
    tcl_eggcouplet((ClientData) cp, interp, list[i].name, NULL,
		   TCL_TRACE_READS);
    Tcl_TraceVar(interp, list[i].name,
		 TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		 tcl_eggcouplet, (ClientData) cp);
  }
}

void rem_tcl_coups(tcl_coups * list)
{
  coupletinfo *cp;
  int i;

  for (i = 0; list[i].name; i++) {
    cp = (coupletinfo *) Tcl_VarTraceInfo(interp, list[i].name,
					  TCL_TRACE_READS |
					  TCL_TRACE_WRITES |
					  TCL_TRACE_UNSETS,
					  tcl_eggcouplet, NULL);
    Tcl_UntraceVar(interp, list[i].name,
		   TCL_TRACE_READS | TCL_TRACE_WRITES | TCL_TRACE_UNSETS,
		   tcl_eggcouplet, (ClientData) cp);
    free(cp);
  }
}
