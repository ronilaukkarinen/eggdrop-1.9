/*
 * tcldcc.c -- handles:
 *   Tcl stubs for the dcc commands
 *
 * $Id: tcldcc.c,v 1.41 2001/11/05 03:47:36 stdarg Exp $
 */
/*
 * Copyright (C) 1997 Robey Pointer
 * Copyright (C) 1999, 2000, 2001 Eggheads Development Team
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

#include "main.h"
#include "tandem.h"
#include "modules.h"
#include "script_api.h"
#include "script.h"

extern Tcl_Interp	*interp;
extern struct dcc_t	*dcc;
extern int		 dcc_total, backgrd, parties, make_userfile,
			 do_restart, remote_boots, max_dcc;
extern char		 botnetnick[];
extern party_t		*party;
extern tand_t		*tandbot;
extern time_t		 now;


int			 enable_simul = 0;
static struct portmap	*root = NULL;


/***********************************************************************/

static int script_putdcc(int idx, char *text)
{
  if (idx < 0 ||  idx >= dcc_total || !dcc[idx].type) return(1);
  dumplots(-(dcc[idx].sock), "", text);
  return(0);
}

/* Allows tcl scripts to send out raw data. Can be used for fast server
 * write (idx=-1)
 *
 * usage:
 * 	putdccraw <idx> <size> <rawdata>
 * example:
 * 	putdccraw 6 13 "eggdrop rulz\n"
 *
 * (added by drummer@sophia.jpte.hu)
 */

static int script_putdccraw(int idx, int len, char *text)
{
  int i;

  if (idx == -1) {
    /* -1 means search for the server's idx. */
    for (i = 0; i < dcc_total; i++) {
      if (!strcmp(dcc[i].nick, "(server)")) {
        idx = i;
        break;
      }
    }
  }
  if (idx < 0 || idx >= dcc_total || !dcc[idx].type) return(1);
  tputs(dcc[idx].sock, text, len);
  return(0);
}

static int script_dccsimul(int idx, char *cmd)
{
  int len;
  if (!enable_simul) return(1);
  if (idx < 0 || !dcc->type || !(dcc[idx].type->flags & DCT_SIMUL)
    || !(dcc[idx].type->activity)) return(1);

  len = strlen(cmd);
  if (len > 510) len = 510;

  dcc[idx].type->activity(idx, cmd, len);
  return(0);
}

static int script_dccbroadcast(char *msg)
{
  chatout("*** %s\n", msg);
  botnet_send_chat(-1, botnetnick, msg);
  return(0);
}

static int script_hand2idx(char *nick)
{
  int i;

  for (i = 0; i < dcc_total; i++) {
    if ((dcc[i].type) && (dcc[i].type->flags & DCT_SIMUL) &&
        !strcasecmp(nick, dcc[i].nick)) {
      return(i);
    }
  }
  return(-1);
}

static int script_getchan(int idx)
{
  if (idx < 0 || !(dcc[idx].type) ||
      (dcc[idx].type != &DCC_CHAT && dcc[idx].type != &DCC_SCRIPT)) {
    return(-2);
  }
  if (dcc[idx].type == &DCC_SCRIPT)
    return(dcc[idx].u.script->u.chat->channel);
  else
    return(dcc[idx].u.chat->channel);
}

static int script_setchan(int idx, int chan)
{
  int oldchan;

  if (idx < 0 || !(dcc[idx].type) ||
      (dcc[idx].type != &DCC_CHAT && dcc[idx].type != &DCC_SCRIPT)) {
    return(1);
  }

  if ((chan < -1) || (chan > 199999)) {
    return(1);
  }
  if (dcc[idx].type == &DCC_SCRIPT) {
    dcc[idx].u.script->u.chat->channel = chan;
    return(0);
  }

  oldchan = dcc[idx].u.chat->channel;

  if (oldchan >= 0) {
    if ((chan >= GLOBAL_CHANS) && (oldchan < GLOBAL_CHANS)) botnet_send_part_idx(idx, "*script*");
    check_tcl_chpt(botnetnick, dcc[idx].nick, idx, oldchan);
  }
  dcc[idx].u.chat->channel = chan;
  if (chan < GLOBAL_CHANS) botnet_send_join_idx(idx, oldchan);
  check_tcl_chjn(botnetnick, dcc[idx].nick, chan, geticon(idx),
	   idx, dcc[idx].host);
  return(0);
}

static int script_dccputchan(int chan, char *msg)
{
  if ((chan < 0) || (chan > 199999)) return(1);
  chanout_but(-1, chan, "*** %s\n", msg);
  botnet_send_chan(-1, botnetnick, NULL, chan, msg);
  check_tcl_bcst(botnetnick, chan, msg);
  return(0);
}

static int script_console(script_var_t *retval, int nargs, int idx, char *what)
{
	static char *view[2];
	char str[2];
	int plus;

	str[1] = 0;

	if (idx < 0 || idx >= dcc_total || !dcc[idx].type || dcc[idx].type != &DCC_CHAT) {
		retval->value = "invalid idx";
		retval->len = 10;
		retval->type = SCRIPT_ERROR | SCRIPT_STRING;
	}

	retval->type = SCRIPT_ARRAY | SCRIPT_STRING;
	retval->len = 2;
	view[0] = dcc[idx].u.chat->con_chan;
	view[1] = masktype(dcc[idx].u.chat->con_flags);
	retval->value = (void *)view;

	if (nargs != 2) {
		view[1] = masktype(dcc[idx].u.chat->con_flags);
		return(0); /* Done. */
	}

	/* They want to change something. */
	if (strchr(CHANMETA, what[0]) != NULL) {
		/* The channel. */
		strncpyz(dcc[idx].u.chat->con_chan, what, 80);
		return(0);
	}

	/* The flags. */
	if (*what != '+' && *what != '-') dcc[idx].u.chat->con_flags = 0;
	for (plus = 1; *what; what++) {
		if (*what == '-') plus = 0;
		else if (*what == '+') plus = 1;
		else {
			str[0] = *what;
			if (plus) dcc[idx].u.chat->con_flags |= logmodes(str);
			else dcc[idx].u.chat->con_flags &= (~logmodes(str));
		}
	}
	view[1] = masktype(dcc[idx].u.chat->con_flags);
	return(0);
}

static int script_strip(script_var_t *retval, int nargs, int idx, char *what)
{
	char str[2];
	int plus;

	str[1] = 0;

	if (idx < 0 || idx >= dcc_total || !dcc[idx].type || dcc[idx].type != &DCC_CHAT) {
		retval->value = "invalid idx";
		retval->len = 10;
		retval->type = SCRIPT_ERROR | SCRIPT_STRING;
	}

	retval->len = -1;
	retval->type = SCRIPT_STRING;

	if (nargs == 1) {
		retval->value = stripmasktype(dcc[idx].u.chat->strip_flags);
		return(0);
	}

	/* The flags. */
	if (*what != '+' && *what != '-') dcc[idx].u.chat->strip_flags = 0;
	for (plus = 1; *what; what++) {
		if (*what == '-') plus = 0;
		else if (*what == '+') plus = 1;
		else {
			str[0] = *what;
			if (plus) dcc[idx].u.chat->con_flags |= stripmodes(str);
			else dcc[idx].u.chat->con_flags &= (~stripmodes(str));
		}
	}

	retval->value = stripmasktype(dcc[idx].u.chat->strip_flags);
	return(0);
}

static int script_echo(int nargs, int idx, int status)
{
	if (idx < 0 || idx >= dcc_total || !dcc[idx].type || dcc[idx].type != &DCC_CHAT) return(0);
	if (nargs == 2) {
		if (status) dcc[idx].status |= STAT_ECHO;
		else dcc[idx].status &= (~STAT_ECHO);
	}

	if (dcc[idx].status & STAT_ECHO) return(1);
	return(0);
}

static int script_page(int nargs, int idx, int status)
{
	if (idx < 0 || idx >= dcc_total || !dcc[idx].type || dcc[idx].type != &DCC_CHAT) return(0);

	if (nargs == 2) {
		if (status) {
			dcc[idx].status |= STAT_PAGE;
			dcc[idx].u.chat->max_line = status;
		}
		else dcc[idx].status &= (~STAT_PAGE);
	}
	if (dcc[idx].status & STAT_PAGE) return(dcc[idx].u.chat->max_line);
	return(0);
}

static int tcl_control STDVAR
{
  int idx;
  void *hold;

  BADARGS(3, 3, " idx command");
  idx = findidx(atoi(argv[1]));
  if (idx < 0) {
    Tcl_AppendResult(irp, "invalid idx", NULL);
    return TCL_ERROR;
  }
  if (dcc[idx].type->flags & DCT_CHAT) {
    if (dcc[idx].u.chat->channel >= 0) {
      chanout_but(idx, dcc[idx].u.chat->channel, "*** %s has gone.\n",
		  dcc[idx].nick);
      check_tcl_chpt(botnetnick, dcc[idx].nick, dcc[idx].sock,
		     dcc[idx].u.chat->channel);
      botnet_send_part_idx(idx, "gone");
    }
    check_tcl_chof(dcc[idx].nick, dcc[idx].sock);
  }
  hold = dcc[idx].u.other;
  dcc[idx].u.script = calloc(1, sizeof(struct script_info));
  dcc[idx].u.script->u.other = hold;
  dcc[idx].u.script->type = dcc[idx].type;
  dcc[idx].type = &DCC_SCRIPT;
  /* Do not buffer data anymore. All received and stored data is passed
     over to the dcc functions from now on.  */
  sockoptions(dcc[idx].sock, EGG_OPTION_UNSET, SOCK_BUFFER);
  strncpyz(dcc[idx].u.script->command, argv[2], 120);
  return TCL_OK;
}

static int script_valididx(int idx)
{
	if (idx < 0 || idx >= dcc_total || !dcc[idx].type || !(dcc[idx].type->flags & DCT_VALIDIDX)) return(0);
	return(1);
}

static int tcl_killdcc STDVAR
{
  int idx;

  BADARGS(2, 3, " idx ?reason?");
  idx = findidx(atoi(argv[1]));
  if (idx < 0) {
    Tcl_AppendResult(irp, "invalid idx", NULL);
    return TCL_ERROR;
  }
  /* Don't kill terminal socket */
  if ((dcc[idx].sock == STDOUT) && !backgrd)
    return TCL_OK;
  /* Make sure 'whom' info is updated for other bots */
  if (dcc[idx].type->flags & DCT_CHAT) {
    chanout_but(idx, dcc[idx].u.chat->channel, "*** %s has left the %s%s%s\n",
		dcc[idx].nick, dcc[idx].u.chat ? "channel" : "partyline",
		argc == 3 ? ": " : "", argc == 3 ? argv[2] : "");
    botnet_send_part_idx(idx, argc == 3 ? argv[2] : "");
    if ((dcc[idx].u.chat->channel >= 0) && (dcc[idx].u.chat->channel < GLOBAL_CHANS)) {
      check_tcl_chpt(botnetnick, dcc[idx].nick, dcc[idx].sock, dcc[idx].u.chat->channel);
    }
    check_tcl_chof(dcc[idx].nick, dcc[idx].sock);
    /* Notice is sent to the party line, the script can add a reason. */
  }
  killsock(dcc[idx].sock);
  lostdcc(idx);
  return TCL_OK;
}

static int tcl_putbot STDVAR
{
  int i;
  char msg[401];

  BADARGS(3, 3, " botnick message");
  i = nextbot(argv[1]);
  if (i < 0) {
    Tcl_AppendResult(irp, "bot is not in the botnet", NULL);
    return TCL_ERROR;
  }
  strncpyz(msg, argv[2], sizeof msg);
  botnet_send_zapf(i, botnetnick, argv[1], msg);
  return TCL_OK;
}

static int tcl_putallbots STDVAR
{
  char msg[401];

  BADARGS(2, 2, " message");
  strncpyz(msg, argv[1], sizeof msg);
  botnet_send_zapf_broad(-1, botnetnick, NULL, msg);
  return TCL_OK;
}

static int tcl_idx2hand STDVAR
{
  int idx;

  BADARGS(2, 2, " idx");
  idx = findidx(atoi(argv[1]));
  if (idx < 0) {
    Tcl_AppendResult(irp, "invalid idx", NULL);
    return TCL_ERROR;
  }
  Tcl_AppendResult(irp, dcc[idx].nick, NULL);
  return TCL_OK;
}

static int tcl_islinked STDVAR
{
  int i;

  BADARGS(2, 2, " bot");
  i = nextbot(argv[1]);
  if (i < 0)
     Tcl_AppendResult(irp, "0", NULL);
  else
     Tcl_AppendResult(irp, "1", NULL);
   return TCL_OK;
}

static int tcl_bots STDVAR
{
  tand_t *bot;

  BADARGS(1, 1, "");
  for (bot = tandbot; bot; bot = bot->next)
     Tcl_AppendElement(irp, bot->bot);
   return TCL_OK;
}

static int tcl_botlist STDVAR
{
  tand_t *bot;
  char *list[4], *p;
  char sh[2], string[20];

  BADARGS(1, 1, "");
  sh[1] = 0;
  list[3] = sh;
  list[2] = string;
  for (bot = tandbot; bot; bot = bot->next) {
    list[0] = bot->bot;
    list[1] = (bot->uplink == (tand_t *) 1) ? botnetnick : bot->uplink->bot;
    strncpyz(string, int_to_base10(bot->ver), sizeof string);
    sh[0] = bot->share;
    p = Tcl_Merge(4, list);
    Tcl_AppendElement(irp, p);
    Tcl_Free((char *) p);
  }
  return TCL_OK;
}

/* list of { idx nick host type {other}  timestamp}
 */
static int tcl_dcclist STDVAR
{
  int i;
  char idxstr[10];
  char timestamp[11];
  char *list[6], *p;
  char other[160];

  BADARGS(1, 2, " ?type?");
  for (i = 0; i < dcc_total; i++) {
    if (argc == 1 ||
	(dcc[i].type && !strcasecmp(dcc[i].type->name, argv[1]))) {
      snprintf(idxstr, sizeof idxstr, "%ld", dcc[i].sock);
      snprintf(timestamp, sizeof timestamp, "%ld", dcc[i].timeval);
      if (dcc[i].type && dcc[i].type->display)
	dcc[i].type->display(i, other);
      else {
	snprintf(other, sizeof other, "?:%lX  !! ERROR !!",
		     (long) dcc[i].type);
	break;
      }
      list[0] = idxstr;
      list[1] = dcc[i].nick;
      list[2] = dcc[i].host;
      list[3] = dcc[i].type ? dcc[i].type->name : "*UNKNOWN*";
      list[4] = other;
      list[5] = timestamp;
      p = Tcl_Merge(6, list);
      Tcl_AppendElement(irp, p);
      Tcl_Free((char *) p);
    }
  }
  return TCL_OK;
}

/* list of { nick bot host flag idletime awaymsg [channel]}
 */
static int tcl_whom STDVAR
{
  char c[2], idle[11], work[20], *list[7], *p;
  int chan, i;

  BADARGS(2, 2, " chan");
  if (argv[1][0] == '*')
     chan = -1;
  else {
    if ((argv[1][0] < '0') || (argv[1][0] > '9')) {
      Tcl_SetVar(interp, "chan", argv[1], 0);
      if ((Tcl_VarEval(interp, "assoc ", "$chan", NULL) != TCL_OK) ||
	  !interp->result[0]) {
	Tcl_AppendResult(irp, "channel name is invalid", NULL);
	return TCL_ERROR;
      }
      chan = atoi(interp->result);
    } else
      chan = atoi(argv[1]);
    if ((chan < 0) || (chan > 199999)) {
      Tcl_AppendResult(irp, "channel out of range; must be 0 thru 199999",
		       NULL);
      return TCL_ERROR;
    }
  }
  for (i = 0; i < dcc_total; i++)
    if (dcc[i].type == &DCC_CHAT) {
      if (dcc[i].u.chat->channel == chan || chan == -1) {
	c[0] = geticon(i);
	c[1] = 0;
	snprintf(idle, sizeof idle, "%lu", (now - dcc[i].timeval) / 60);
	list[0] = dcc[i].nick;
	list[1] = botnetnick;
	list[2] = dcc[i].host;
	list[3] = c;
	list[4] = idle;
	list[5] = dcc[i].u.chat->away ? dcc[i].u.chat->away : "";
	if (chan == -1) {
	  snprintf(work, sizeof work, "%d", dcc[i].u.chat->channel);
	  list[6] = work;
	}
	p = Tcl_Merge((chan == -1) ? 7 : 6, list);
	Tcl_AppendElement(irp, p);
	Tcl_Free((char *) p);
      }
    }
  for (i = 0; i < parties; i++) {
    if (party[i].chan == chan || chan == -1) {
      c[0] = party[i].flag;
      c[1] = 0;
      if (party[i].timer == 0L)
	strcpy(idle, "0");
      else
	snprintf(idle, sizeof idle, "%lu", (now - party[i].timer) / 60);
      list[0] = party[i].nick;
      list[1] = party[i].bot;
      list[2] = party[i].from ? party[i].from : "";
      list[3] = c;
      list[4] = idle;
      list[5] = party[i].status & PLSTAT_AWAY ? party[i].away : "";
      if (chan == -1) {
	snprintf(work, sizeof work, "%d", party[i].chan);
	list[6] = work;
      }
      p = Tcl_Merge((chan == -1) ? 7 : 6, list);
      Tcl_AppendElement(irp, p);
      Tcl_Free((char *) p);
    }
  }
  return TCL_OK;
}

static int tcl_dccused STDVAR
{
  char s[20];

  BADARGS(1, 1, "");
  snprintf(s, sizeof s, "%d", dcc_total);
  Tcl_AppendResult(irp, s, NULL);
  return TCL_OK;
}

static int tcl_getdccidle STDVAR
{
  int  x, idx;
  char s[21];

  BADARGS(2, 2, " idx");
  idx = findidx(atoi(argv[1]));
  if (idx < 0) {
    Tcl_AppendResult(irp, "invalid idx", NULL);
    return TCL_ERROR;
  }
  x = (now - dcc[idx].timeval);
  snprintf(s, sizeof s, "%d", x);
  Tcl_AppendElement(irp, s);
  return TCL_OK;
}

static int tcl_getdccaway STDVAR
{
  int idx;

  BADARGS(2, 2, " idx");
  idx = findidx(atol(argv[1]));
  if (idx < 0 || dcc[idx].type != &DCC_CHAT) {
    Tcl_AppendResult(irp, "invalid idx", NULL);
    return TCL_ERROR;
  }
  if (dcc[idx].u.chat->away == NULL)
    return TCL_OK;
  Tcl_AppendResult(irp, dcc[idx].u.chat->away, NULL);
  return TCL_OK;
}

static int tcl_setdccaway STDVAR
{
  int idx;

  BADARGS(3, 3, " idx message");
  idx = findidx(atol(argv[1]));
  if (idx < 0 || dcc[idx].type != &DCC_CHAT) {
    Tcl_AppendResult(irp, "invalid idx", NULL);
    return TCL_ERROR;
  }
  if (!argv[2][0]) {
    /* un-away */
    if (dcc[idx].u.chat->away != NULL)
      not_away(idx);
    return TCL_OK;
  }
  /* away */
  set_away(idx, argv[2]);
  return TCL_OK;
}

static int tcl_link STDVAR
{
  int x, i;
  char bot[HANDLEN + 1], bot2[HANDLEN + 1];

  BADARGS(2, 3, " ?via-bot? bot");
  strncpyz(bot, argv[1], sizeof bot);
  if (argc == 2)
     x = botlink("", -2, bot);
  else {
    x = 1;
    strncpyz(bot2, argv[2], sizeof bot2);
    i = nextbot(bot);
    if (i < 0)
      x = 0;
    else
      botnet_send_link(i, botnetnick, bot, bot2);
  }
  snprintf(bot, sizeof bot, "%d", x);
  Tcl_AppendResult(irp, bot, NULL);
  return TCL_OK;
}

static int tcl_unlink STDVAR
{
  int i, x;
  char bot[HANDLEN + 1];

  BADARGS(2, 3, " bot ?comment?");
  strncpyz(bot, argv[1], sizeof bot);
  i = nextbot(bot);
  if (i < 0)
     x = 0;
  else {
    x = 1;
    if (!strcasecmp(bot, dcc[i].nick))
      x = botunlink(-2, bot, argv[2]);
    else
      botnet_send_unlink(i, botnetnick, lastbot(bot), bot, argv[2]);
  }
  snprintf(bot, sizeof bot, "%d", x);
  Tcl_AppendResult(irp, bot, NULL);
  return TCL_OK;
}

static int tcl_connect STDVAR
{
  int i, z, sock;
  char s[81];

  BADARGS(3, 3, " hostname port");
  if (dcc_total == max_dcc) {
    Tcl_AppendResult(irp, "out of dcc table space", NULL);
    return TCL_ERROR;
  }
  sock = getsock(0);
  if (sock < 0) {
    Tcl_AppendResult(irp, _("No free sockets available."), NULL);
    return TCL_ERROR;
  }
  z = open_telnet_raw(sock, argv[1], atoi(argv[2]));
  if (z < 0) {
    killsock(sock);
    if (z == (-2))
      strncpyz(s, "DNS lookup failed", sizeof s);
    else
      neterror(s);
    Tcl_AppendResult(irp, s, NULL);
    return TCL_ERROR;
  }
  /* Well well well... it worked! */
  i = new_dcc(&DCC_SOCKET, 0);
  dcc[i].sock = sock;
  dcc[i].port = atoi(argv[2]);
  strcpy(dcc[i].nick, "*");
  strncpyz(dcc[i].host, argv[1], UHOSTMAX);
  snprintf(s, sizeof s, "%d", sock);
  Tcl_AppendResult(irp, s, NULL);
  return TCL_OK;
}

/* Create a new listening port (or destroy one)
 *
 * listen <port> bots/all/users [mask]
 * listen <port> script <proc> [flag]
 * listen <port> off
 */
static int tcl_listen STDVAR
{
  int i, j, idx = (-1), port, realport;
  char s[11];
  struct portmap *pmap = NULL, *pold = NULL;
  int af = AF_INET /* af_preferred */;

  BADARGS(3, 6, " ?-4/-6? port type ?mask?/?proc ?flag??");
  if (!strcmp(argv[1], "-4") || !strcmp(argv[1], "-6")) {
      if (argv[1][1] == '4')
          af = AF_INET;
      else
	  af = AF_INET6;
      argv[1] = argv[0]; /* UGLY! */
      argv++;
      argc--;
  }
  BADARGS(3, 6, " ?-4/-6? port type ?mask?/?proc ?flag??");

  port = realport = atoi(argv[1]);
  for (pmap = root; pmap; pold = pmap, pmap = pmap->next)
    if (pmap->realport == port) {
      port = pmap->mappedto;
      break;
    }
  for (i = 0; i < dcc_total; i++)
    if ((dcc[i].type == &DCC_TELNET) && (dcc[i].port == port))
      idx = i;
  if (!strcasecmp(argv[2], "off")) {
    if (pmap) {
      if (pold)
	pold->next = pmap->next;
      else
	root = pmap->next;
      free(pmap);
    }
    /* Remove */
    if (idx < 0) {
      Tcl_AppendResult(irp, "no such listen port is open", NULL);
      return TCL_ERROR;
    }
    killsock(dcc[idx].sock);
    lostdcc(idx);
    return TCL_OK;
  }
  if (idx < 0) {
    /* Make new one */
    if (dcc_total >= max_dcc) {
      Tcl_AppendResult(irp, "no more DCC slots available", NULL);
      return TCL_ERROR;
    }
    /* Try to grab port */
    j = port + 20;
    i = (-1);
    while (port < j && i < 0) {
      i = open_listen(&port, af);
      if (i == -1)
	port++;
      else if (i == -2)
        break;
    }
    if (i == -1) {
      Tcl_AppendResult(irp, "Couldn't grab nearby port", NULL);
      return TCL_ERROR;
    } else if (i == -2) {
      Tcl_AppendResult(irp, "Couldn't assign the requested IP. Please make sure 'my-ip' and/or 'my-ip6' is set properly.", NULL);
      return TCL_ERROR;
    }
    idx = new_dcc(&DCC_TELNET, 0);
    strcpy(dcc[idx].addr, "*"); /* who cares? */
    dcc[idx].port = port;
    dcc[idx].sock = i;
    dcc[idx].timeval = now;
  }
  /* script? */
  if (!strcmp(argv[2], "script")) {
    strcpy(dcc[idx].nick, "(script)");
    if (argc < 4) {
      Tcl_AppendResult(irp, "must give proc name for script listen", NULL);
      killsock(dcc[idx].sock);
      lostdcc(idx);
      return TCL_ERROR;
    }
    if (argc == 5) {
      if (strcmp(argv[4], "pub")) {
	Tcl_AppendResult(irp, "unknown flag: ", argv[4], ". allowed flags: pub",
		         NULL);
	killsock(dcc[idx].sock);
	lostdcc(idx);
	return TCL_ERROR;
      }
      dcc[idx].status = LSTN_PUBLIC;
    }
    strncpyz(dcc[idx].host, argv[3], UHOSTMAX);
    snprintf(s, sizeof s, "%d", port);
    Tcl_AppendResult(irp, s, NULL);
    return TCL_OK;
  }
  /* bots/users/all */
  if (!strcmp(argv[2], "bots"))
    strcpy(dcc[idx].nick, "(bots)");
  else if (!strcmp(argv[2], "users"))
    strcpy(dcc[idx].nick, "(users)");
  else if (!strcmp(argv[2], "all"))
    strcpy(dcc[idx].nick, "(telnet)");
  if (!dcc[idx].nick[0]) {
    Tcl_AppendResult(irp, "illegal listen type: must be one of ",
		     "bots, users, all, off, script", NULL);
    killsock(dcc[idx].sock);
    dcc_total--;
    return TCL_ERROR;
  }
  if (argc == 4) {
    strncpyz(dcc[idx].host, argv[3], UHOSTMAX);
  } else
    strcpy(dcc[idx].host, "*");
  snprintf(s, sizeof s, "%d", port);
  Tcl_AppendResult(irp, s, NULL);
  if (!pmap) {
    pmap = malloc(sizeof(struct portmap));
    pmap->next = root;
    root = pmap;
  }
  pmap->realport = realport;
  pmap->mappedto = port;
  putlog(LOG_MISC, "*", "Listening at telnet port %d (%s)", port, argv[2]);
  return TCL_OK;
}

static int tcl_boot STDVAR
{
  char who[NOTENAMELEN + 1];
  int i, ok = 0;

  BADARGS(2, 3, " user@bot ?reason?");
  strncpyz(who, argv[1], sizeof who);
  if (strchr(who, '@') != NULL) {
    char whonick[HANDLEN + 1];

    splitc(whonick, who, '@');
    whonick[HANDLEN] = 0;
    if (!strcasecmp(who, botnetnick))
       strncpyz(who, whonick, sizeof who);
    else if (remote_boots > 0) {
      i = nextbot(who);
      if (i < 0)
	return TCL_OK;
      botnet_send_reject(i, botnetnick, NULL, whonick, who, argv[2]);
    } else {
      return TCL_OK;
    }
  }
  for (i = 0; i < dcc_total; i++)
    if (!ok && (dcc[i].type->flags & DCT_CANBOOT) &&
        !strcasecmp(dcc[i].nick, who)) {
      do_boot(i, botnetnick, argv[2] ? argv[2] : "");
      ok = 1;
    }
  return TCL_OK;
}

static int tcl_rehash STDVAR
{
  BADARGS(1, 1, " ");
  if (make_userfile) {
    putlog(LOG_MISC, "*", _("Userfile creation not necessary--skipping"));
    make_userfile = 0;
  }
  write_userfile(-1);
  putlog(LOG_MISC, "*", _("Rehashing..."));
  do_restart = -2;
  return TCL_OK;
}

static int tcl_restart STDVAR
{
  BADARGS(1, 1, " ");
  if (!backgrd) {
    Tcl_AppendResult(interp, "You can't restart a -n bot", NULL);
    return TCL_ERROR;
  }
  if (make_userfile) {
    putlog(LOG_MISC, "*", _("Userfile creation not necessary--skipping"));
    make_userfile = 0;
  }
  write_userfile(-1);
  putlog(LOG_MISC, "*", _("Restarting..."));
  do_restart = -1;
  return TCL_OK;
}

script_simple_command_t script_dcc_cmds[] = {
	{"", NULL, NULL, NULL, 0},
	{"putdcc", script_putdcc, "is", "idx text", SCRIPT_INTEGER},
	{"putdccraw", script_putdccraw, "iis", "idx len text", SCRIPT_INTEGER},
	{"dccsimul", script_dccsimul, "is", "idx command", SCRIPT_INTEGER},
	{"dccbroadcast", script_dccbroadcast, "s", "text", SCRIPT_INTEGER},
	{"hand2idx", script_hand2idx, "s", "handle", SCRIPT_INTEGER},
	{"getchan", script_getchan, "i", "idx", SCRIPT_INTEGER},
	{"setchan", script_setchan, "ii", "idx chan", SCRIPT_INTEGER},
	{"dccputchan", script_dccputchan, "is", "chan text", SCRIPT_INTEGER},
	{"valididx", script_valididx, "i", "idx", SCRIPT_INTEGER},
	{0}
};

script_command_t script_full_dcc_cmds[] = {
	{"", "console", script_console, NULL, 1, "is", "idx ?changes?", 0, SCRIPT_PASS_RETVAL|SCRIPT_PASS_COUNT|SCRIPT_VAR_ARGS},
	{"", "strip", script_strip, NULL, 1, "is", "idx ?change?", 0, SCRIPT_PASS_RETVAL|SCRIPT_PASS_COUNT|SCRIPT_VAR_ARGS},
	{"", "echo", script_echo, NULL, 1, "ii", "idx ?status?", SCRIPT_INTEGER, SCRIPT_PASS_COUNT|SCRIPT_VAR_ARGS},
	{"", "page", script_page, NULL, 1, "ii", "idx ?status?", SCRIPT_INTEGER, SCRIPT_PASS_COUNT|SCRIPT_VAR_ARGS},
	{0}
};

tcl_cmds tcldcc_cmds[] =
{
  {"control",		tcl_control},
  {"killdcc",		tcl_killdcc},
  {"putbot",		tcl_putbot},
  {"putallbots",	tcl_putallbots},
  {"idx2hand",		tcl_idx2hand},
  {"bots",		tcl_bots},
  {"botlist",		tcl_botlist},
  {"dcclist",		tcl_dcclist},
  {"whom",		tcl_whom},
  {"dccused",		tcl_dccused},
  {"getdccidle",	tcl_getdccidle},
  {"getdccaway",	tcl_getdccaway},
  {"setdccaway",	tcl_setdccaway},
  {"islinked",		tcl_islinked},
  {"link",		tcl_link},
  {"unlink",		tcl_unlink},
  {"connect",		tcl_connect},
  {"listen",		tcl_listen},
  {"boot",		tcl_boot},
  {"rehash",		tcl_rehash},
  {"restart",		tcl_restart},
  {NULL,		NULL}
};
