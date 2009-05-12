/*   
 *   IRC interfaces for La Cogita IRC chatbot
 *   Copyright (C) 2007 Linas Vepstas <linas@linas.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* 
 * Go onto IRC
 * badly hacked.
 * Linas October 2007 
 */

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <getopt.h>

#include <string>
#include <vector>
#include <set>

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "IRC.h"
#include "CogitaConfig.h"

#include "whirr-sockets.h"

using namespace opencog::chatbot;

CogitaConfig cc;

/**
 * Join channel shortly after logging into the irc server.
 */
int end_of_motd(const char* params, irc_reply_data* ird, void* data)
{
	IRC* conn = (IRC*)data; 

	printf("duude par=%s\n", params);
	printf("duude got motd nick=%s ident=%s host=%s target=%s\n", 
		ird->nick, ird->ident, ird->host, ird->target);

	sleep(1);
	conn->join (cc.ircChannels[0].c_str());
	printf("duude done join\n");
	sleep(2);
	conn->notice (cc.ircChannels[0].c_str(), "ola");
	printf("duude done notice\n");
	sleep(2);
	conn->privmsg (cc.ircChannels[0].c_str(), "here we are");
	printf("duude done priv\n");
	return 0;
}

/* return true if not all whitespace */
static bool is_nonblank(const char * str)
{
	size_t len = strlen(str);
	if (0 == len) return false;
	size_t blanks = strspn(str, " \n\r\t\v");
	if (blanks == len) return false;
	return true;
}

/**
 * Handle a message received from IRC.
 */
int got_privmsg(const char* params, irc_reply_data* ird, void* data)
{
	IRC* conn = (IRC*)data; 

	printf("input=%s\n", params);
	printf("nick=%s ident=%s host=%s target=%s\n", ird->nick, ird->ident, ird->host, ird->target);

	typedef enum {ENGLISH=1, SHELL_CMD, SCM_CMD} CmdType;
	CmdType cmd = ENGLISH;

	const char * start = NULL;
	int priv = 0;
	if (!strcmp (ird->target, cc.nick.c_str())) {priv = 1; start = params+1; }

	if (!strncmp (&params[1], cc.nick.c_str(), cc.nick.size())) {
        start = params+1 + cc.nick.size();
    } else if (!strncmp (params, ":cog-sh:", 8)) {
        start = params+8; cmd = SHELL_CMD;
    } else if (!strncmp (params, ":scm:", 5)) {
        start = params+5; cmd = SCM_CMD;
    } else {   
        // Check across all potential 
        foreach (string it, cc.attn) {
            if (! it.compare(0,it.size(),&params[1]) ) {
                start = params + it.size();
                break;
            }
        }
    }

	if (!start) return 0;
	char * msg_target = NULL;
	if (priv)
	{
		msg_target = ird->nick;
	}
	else
	{
		msg_target = ird->target;
	}

	// Reply to request for chat client version
	if ((0x1 == start[0]) && !strncmp (&start[1], "VERSION", 7))
	{
		printf ("VERSION: %s\n", cc.vstring.c_str());
		conn->privmsg (msg_target, cc.vstring.c_str());
		return 0;
	}

	// printf ("duude starting with 0x%x %s\n", start[0], start);
	size_t textlen = strlen(start);
	size_t len = textlen;
	len += strlen ("(say-id-english )");
	len += strlen (ird->nick);
	len += 120;

	char * cmdline = (char *) malloc(sizeof (char) * (len+1));

	if (ENGLISH == cmd)
	{
		// Get into the opencog scheme shell, and run the command
		strcpy (cmdline, "scm hush\n(say-id-english \"");
		strcat (cmdline, ird->nick);
		strcat (cmdline, "\" \"");
		size_t toff = strlen(cmdline);
		strcat (cmdline, start);
		strcat (cmdline, "\")\n");

		// strip out quotation marks, replace with blanks, for now.
		for (size_t i =0; i<textlen; i++)
		{
			if ('\"' == cmdline[toff+i]) cmdline[toff+i] = ' ';
		}
	}

#define ENABLE_SHELL_ESCAPES 0
#ifdef ENABLE_SHELL_ESCAPES
	/*
	 * XXX DANGER DANGER Extreme Caution Advised XXX
	 * Shell escapes are a potential security hole, as they allow access
	 * to the cog-server to total strangers. In particular, the scheme 
	 * interface is a general programming API and can be used to root
	 * the system.
	 */
	else if (SHELL_CMD == cmd)
	{
		strcpy (cmdline, start);
		strcat (cmdline, "\n");
	}
	else if (SCM_CMD == cmd)
	{
		strcpy (cmdline, "scm hush\n");
		strcat (cmdline, start);
		strcat (cmdline, "\n");
	}
#else
	else
	{
		conn->privmsg (msg_target, "Shell escapes disabled in this chatbot version\n");
		return 0;
	}
#endif /* ENABLE_SHELL_ESCAPES */
	
	// printf ("Sending to opencog: %s\n", cmdline);
	char * reply = whirr_sock_io (cmdline);
	printf ("opencog reply: %s\n", reply);

	/* Each newline has to be on its own line */
	/* Limit to 5 replies so we don't get kicked for flooding */
	int cnt = 0;
	char * p = reply;
	while (*p)
	{
		char *ep = strchr (p, '\n');
		if (!ep)
		{
			if (is_nonblank(p))
				conn->privmsg (msg_target, p);
			break;
		}
		ep ++;
		int save = *ep;
		*ep = 0x0;
		if (is_nonblank(p))
			conn->privmsg (msg_target, p);
		*ep = save;
		p = ep;
		cnt ++;

		/* Sleep so that we don't get kicked for flooding */
		if (4 < cnt) sleep(1);
		if (8 < cnt) sleep(1);
	}

	free(reply);
	return 0;
}

/** 
 * @todo allow command line options via tclap http://tclap.sourceforge.net/ -
 * package libtclap-dev in Ubuntu.
 * However, its probably more portable to use plain-old getopt,
 * or maybe getopt_long, lets keep the dependency list minimal.
 * @todo use Config class to store defaults, and retrieve opencog.conf vars.
 */
int main (int argc, char * argv[])
{
	whirr_sock_setup();

	IRC conn;

    if (cc.parseOptions(argc,argv))
        return 0;

	conn.hook_irc_command("376", &end_of_motd);
	conn.hook_irc_command("PRIVMSG", &got_privmsg);

	const char *login = getlogin();

	// The login-name, nick, etc. are there only to make it look 
	// pretty on IRC ident.
	conn.start (cc.ircNetwork.c_str(), cc.ircPort, cc.nick.c_str(), login,
	            "La Cogita OpenCog chatbot", "asdf");

	conn.message_loop();
}

