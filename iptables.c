/* Code to take an iptables-style command line and do it. */

/*
 * Author: Paul.Russell@rustcorp.com.au and mneuling@radlogic.com.au
 *
 * (C) 2000-2002 by the netfilter coreteam <coreteam@netfilter.org>:
 * 		    Paul 'Rusty' Russell <rusty@rustcorp.com.au>
 * 		    Marc Boucher <marc+nf@mbsi.ca>
 * 		    James Morris <jmorris@intercode.com.au>
 * 		    Harald Welte <laforge@gnumonks.org>
 * 		    Jozsef Kadlecsik <kadlec@blackhole.kfki.hu>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <getopt.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>
#include <iptables.h>
#include <xtables.h>
#include <fcntl.h>
#include <sys/utsname.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define FMT_NUMERIC	0x0001
#define FMT_NOCOUNTS	0x0002
#define FMT_KILOMEGAGIGA 0x0004
#define FMT_OPTIONS	0x0008
#define FMT_NOTABLE	0x0010
#define FMT_NOTARGET	0x0020
#define FMT_VIA		0x0040
#define FMT_NONEWLINE	0x0080
#define FMT_LINENUMBERS 0x0100

#define FMT_PRINT_RULE (FMT_NOCOUNTS | FMT_OPTIONS | FMT_VIA \
		| FMT_NUMERIC | FMT_NOTABLE)
#define FMT(tab,notab) ((format) & FMT_NOTABLE ? (notab) : (tab))


#define CMD_NONE		0x0000U
#define CMD_INSERT		0x0001U
#define CMD_DELETE		0x0002U
#define CMD_DELETE_NUM		0x0004U
#define CMD_REPLACE		0x0008U
#define CMD_APPEND		0x0010U
#define CMD_LIST		0x0020U
#define CMD_FLUSH		0x0040U
#define CMD_ZERO		0x0080U
#define CMD_NEW_CHAIN		0x0100U
#define CMD_DELETE_CHAIN	0x0200U
#define CMD_SET_POLICY		0x0400U
#define CMD_RENAME_CHAIN	0x0800U
#define CMD_LIST_RULES		0x1000U
#define CMD_UPDATE		0x2000U
#define NUMBER_OF_CMD	15
static const char cmdflags[NUMBER_OF_CMD] = { 'I', 'D', 'D', 'R', 'A', 'L', 'F', 'Z',
	'N', 'X', 'P', 'E', 'S', 'U' };

#define OPT_NONE	0x00000U
#define OPT_NUMERIC	0x00001U
#define OPT_SOURCE	0x00002U
#define OPT_DESTINATION	0x00004U
#define OPT_PROTOCOL	0x00008U
#define OPT_JUMP	0x00010U
#define OPT_VERBOSE	0x00020U
#define OPT_EXPANDED	0x00040U
#define OPT_VIANAMEIN	0x00080U
#define OPT_VIANAMEOUT	0x00100U
#define OPT_FRAGMENT    0x00200U
#define OPT_LINENUMBERS 0x00400U
#define OPT_COUNTERS	0x00800U
#define NUMBER_OF_OPT	12
static const char optflags[NUMBER_OF_OPT]
= { 'n', 's', 'd', 'p', 'j', 'v', 'x', 'i', 'o', 'f', '0', 'c'};

static struct option original_opts[] = {
	{.name = "append",        .has_arg = 1, .val = 'A'},
	{.name = "delete",        .has_arg = 1, .val = 'D'},
	{.name = "insert",        .has_arg = 1, .val = 'I'},
	{.name = "replace",       .has_arg = 1, .val = 'R'},
	{.name = "list",          .has_arg = 2, .val = 'L'},
	{.name = "list-rules",    .has_arg = 2, .val = 'S'},
	{.name = "flush",         .has_arg = 2, .val = 'F'},
	{.name = "zero",          .has_arg = 2, .val = 'Z'},
	{.name = "new-chain",     .has_arg = 1, .val = 'N'},
	{.name = "delete-chain",  .has_arg = 2, .val = 'X'},
	{.name = "rename-chain",  .has_arg = 1, .val = 'E'},
	{.name = "policy",        .has_arg = 1, .val = 'P'},
	{.name = "source",        .has_arg = 1, .val = 's'},
	{.name = "destination",   .has_arg = 1, .val = 'd'},
	{.name = "src",           .has_arg = 1, .val = 's'}, /* synonym */
	{.name = "dst",           .has_arg = 1, .val = 'd'}, /* synonym */
	{.name = "protocol",      .has_arg = 1, .val = 'p'},
	{.name = "in-interface",  .has_arg = 1, .val = 'i'},
	{.name = "jump",          .has_arg = 1, .val = 'j'},
	{.name = "table",         .has_arg = 1, .val = 't'},
	{.name = "match",         .has_arg = 1, .val = 'm'},
	{.name = "numeric",       .has_arg = 0, .val = 'n'},
	{.name = "out-interface", .has_arg = 1, .val = 'o'},
	{.name = "verbose",       .has_arg = 0, .val = 'v'},
	{.name = "exact",         .has_arg = 0, .val = 'x'},
	{.name = "fragments",     .has_arg = 0, .val = 'f'},
	{.name = "version",       .has_arg = 0, .val = 'V'},
	{.name = "help",          .has_arg = 2, .val = 'h'},
	{.name = "line-numbers",  .has_arg = 0, .val = '0'},
	{.name = "modprobe",      .has_arg = 1, .val = 'M'},
	{.name = "set-counters",  .has_arg = 1, .val = 'c'},
	{.name = "goto",          .has_arg = 1, .val = 'g'},
	{.name = "update",        .has_arg = 1, .val = 'U'},
	{NULL},
};

/* we need this for iptables-restore.  iptables-restore.c sets line to the
 * current line of the input file, in order  to give a more precise error
 * message.  iptables itself doesn't need this, so it is initialized to the
 * magic number of -1 */
int line = -1;

void iptables_exit_error(enum xtables_exittype status, const char *msg, ...) __attribute__((noreturn, format(printf,2,3)));

struct xtables_globals iptables_globals = {
	.option_offset = 0,
	.program_version = IPTABLES_VERSION,
	.opts = original_opts,
	.orig_opts = original_opts,
	.exit_err = iptables_exit_error,
};

/* Table of legal combinations of commands and options.  If any of the
 * given commands make an option legal, that option is legal (applies to
 * CMD_LIST and CMD_ZERO only).
 * Key:
 *  +  compulsory
 *  x  illegal
 *     optional
 */

static char commands_v_options[NUMBER_OF_CMD][NUMBER_OF_OPT] =
/* Well, it's better than "Re: Linux vs FreeBSD" */
{
	/*     -n  -s  -d  -p  -j  -v  -x  -i  -o  -f --line -c */
	/*INSERT*/    {'x',' ',' ',' ',' ',' ','x',' ',' ',' ','x',' '},
	/*DELETE*/    {'x',' ',' ',' ',' ',' ','x',' ',' ',' ','x','x'},
	/*DELETE_NUM*/{'x','x','x','x','x',' ','x','x','x','x','x','x'},
	/*REPLACE*/   {'x',' ',' ',' ',' ',' ','x',' ',' ',' ','x',' '},
	/*APPEND*/    {'x',' ',' ',' ',' ',' ','x',' ',' ',' ','x',' '},
	/*LIST*/      {' ','x','x','x','x',' ',' ','x','x','x',' ','x'},
	/*FLUSH*/     {'x','x','x','x','x',' ','x','x','x','x','x','x'},
	/*ZERO*/      {'x','x','x','x','x',' ','x','x','x','x','x','x'},
	/*NEW_CHAIN*/ {'x','x','x','x','x',' ','x','x','x','x','x','x'},
	/*DEL_CHAIN*/ {'x','x','x','x','x',' ','x','x','x','x','x','x'},
	/*SET_POLICY*/{'x','x','x','x','x',' ','x','x','x','x','x',' '},
	/*RENAME*/    {'x','x','x','x','x',' ','x','x','x','x','x','x'},
	/*LIST_RULES*/{'x','x','x','x','x',' ','x','x','x','x','x','x'},
	/*UPDATE*/    {'x',' ',' ',' ',' ',' ','x',' ',' ',' ','x',' '} 
};

static int inverse_for_options[NUMBER_OF_OPT] =
{
	/* -n */ 0,
	/* -s */ IPT_INV_SRCIP,
	/* -d */ IPT_INV_DSTIP,
	/* -p */ IPT_INV_PROTO,
	/* -j */ 0,
	/* -v */ 0,
	/* -x */ 0,
	/* -i */ IPT_INV_VIA_IN,
	/* -o */ IPT_INV_VIA_OUT,
	/* -f */ IPT_INV_FRAG,
	/*--line*/ 0,
	/* -c */ 0,
};

#define opts iptables_globals.opts
#define prog_name iptables_globals.program_name
#define prog_vers iptables_globals.program_version

int kernel_version;

/* Primitive headers... */
/* defined in netinet/in.h */
#if 0
#ifndef IPPROTO_ESP
#define IPPROTO_ESP 50
#endif
#ifndef IPPROTO_AH
#define IPPROTO_AH 51
#endif
#endif

	static const char *
proto_to_name(u_int8_t proto, int nolookup)
{
	unsigned int i;

	if (proto && !nolookup) {
		struct protoent *pent = getprotobynumber(proto);
		if (pent)
			return pent->p_name;
	}

	for (i = 0; xtables_chain_protos[i].name != NULL; ++i)
		if (xtables_chain_protos[i].num == proto)
			return xtables_chain_protos[i].name;

	return NULL;
}

enum {
	IPT_DOTTED_ADDR = 0,
	IPT_DOTTED_MASK
};

	static void
exit_tryhelp(int status)
{
	if (line != -1)
		fprintf(stderr, "Error occurred at line: %d\n", line);
	fprintf(stderr, "Try `%s -h' or '%s --help' for more information.\n",
			prog_name, prog_name);
	xtables_free_opts(1);
	exit(status);
}

	static void
exit_printhelp(struct xtables_rule_match *matches)
{
	struct xtables_rule_match *matchp = NULL;
	struct xtables_target *t = NULL;

	printf("%s v%s\n\n"
			"Usage: %s -[AD] chain rule-specification [options]\n"
			"       %s -I chain [rulenum] rule-specification [options]\n"
			"       %s -R chain rulenum rule-specification [options]\n"
			"       %s -D chain rulenum [options]\n"
			"       %s -[LS] [chain [rulenum]] [options]\n"
			"       %s -[FZ] [chain] [options]\n"
			"       %s -[NX] chain\n"
			"       %s -E old-chain-name new-chain-name\n"
			"       %s -P chain target [options]\n"
			"       %s -h (print this help information)\n\n",
			prog_name, prog_vers, prog_name, prog_name,
			prog_name, prog_name, prog_name, prog_name,
			prog_name, prog_name, prog_name, prog_name);

	printf(
			"Commands:\n"
			"Either long or short options are allowed.\n"
			"  --append  -A chain		Append to chain\n"
			"  --delete  -D chain		Delete matching rule from chain\n"
			"  --delete  -D chain rulenum\n"
			"				Delete rule rulenum (1 = first) from chain\n"
			"  --insert  -I chain [rulenum]\n"
			"				Insert in chain as rulenum (default 1=first)\n"
			"  --update  -U chain [rulenum]\n"
			"				Try to insert new rule in chain as rulenum (default 1),\n"
			"                               if here no such rule, else zero counters on this rule.\n"
			"  --replace -R chain rulenum\n"
			"				Replace rule rulenum (1 = first) in chain\n"
			"  --list    -L [chain [rulenum]]\n"
			"				List the rules in a chain or all chains\n"
			"  --list-rules -S [chain [rulenum]]\n"
			"				Print the rules in a chain or all chains\n"
			"  --flush   -F [chain]		Delete all rules in  chain or all chains\n"
			"  --zero    -Z [chain]		Zero counters in chain or all chains\n"
			"  --new     -N chain		Create a new user-defined chain\n"
			"  --delete-chain\n"
			"            -X [chain]		Delete a user-defined chain\n"
			"  --policy  -P chain target\n"
			"				Change policy on chain to target\n"
			"  --rename-chain\n"
			"            -E old-chain new-chain\n"
			"				Change chain name, (moving any references)\n"

			"Options:\n"
			"[!] --proto	-p proto	protocol: by number or name, eg. `tcp'\n"
			"[!] --source	-s address[/mask]\n"
			"				source specification\n"
			"[!] --destination -d address[/mask]\n"
			"				destination specification\n"
			"[!] --in-interface -i input name[+]\n"
			"				network interface name ([+] for wildcard)\n"
			" --jump	-j target\n"
			"				target for rule (may load target extension)\n"
#ifdef IPT_F_GOTO
			"  --goto      -g chain\n"
			"                              jump to chain with no return\n"
#endif
			"  --match	-m match\n"
			"				extended match (may load extension)\n"
			"  --numeric	-n		numeric output of addresses and ports\n"
			"[!] --out-interface -o output name[+]\n"
			"				network interface name ([+] for wildcard)\n"
			"  --table	-t table	table to manipulate (default: `filter')\n"
			"  --verbose	-v		verbose mode\n"
			"  --line-numbers		print line numbers when listing\n"
			"  --exact	-x		expand numbers (display exact values)\n"
			"[!] --fragment	-f		match second or further fragments only\n"
			"  --modprobe=<command>		try to insert modules using this command\n"
			"  --set-counters PKTS BYTES	set the counter during insert/append\n"
			"[!] --version	-V		print package version.\n");

	/* Print out any special helps. A user might like to be able
	   to add a --help to the commandline, and see expected
	   results. So we call help for all specified matches & targets */
	for (t = xtables_targets; t ;t = t->next) {
		if (t->used) {
			printf("\n");
			t->help();
		}
	}
	for (matchp = matches; matchp; matchp = matchp->next) {
		printf("\n");
		matchp->match->help();
	}
	exit(0);
}

	void
iptables_exit_error(enum xtables_exittype status, const char *msg, ...)
{
	va_list args;

	va_start(args, msg);
	fprintf(stderr, "%s v%s: ", prog_name, prog_vers);
	vfprintf(stderr, msg, args);
	va_end(args);
	fprintf(stderr, "\n");
	if (status == PARAMETER_PROBLEM)
		exit_tryhelp(status);
	if (status == VERSION_PROBLEM)
		fprintf(stderr,
				"Perhaps iptables or your kernel needs to be upgraded.\n");
	/* On error paths, make sure that we don't leak memory */
	xtables_free_opts(1);
	exit(status);
}

	static void
generic_opt_check(int command, int options)
{
	int i, j, legal = 0;

	/* Check that commands are valid with options.  Complicated by the
	 * fact that if an option is legal with *any* command given, it is
	 * legal overall (ie. -z and -l).
	 */
	for (i = 0; i < NUMBER_OF_OPT; i++) {
		legal = 0; /* -1 => illegal, 1 => legal, 0 => undecided. */

		for (j = 0; j < NUMBER_OF_CMD; j++) {
			if (!(command & (1<<j)))
				continue;
			if (!(options & (1<<i))) {
				if (commands_v_options[j][i] == '+')
					xtables_error(PARAMETER_PROBLEM,
							"You need to supply the `-%c' "
							"option for this command\n",
							optflags[i]);
			} else {
				if (commands_v_options[j][i] != 'x') {
					legal = 1;
				}
				else if (legal == 0) {
					legal = -1;
				}
			}
		}

		if (legal == -1)
			xtables_error(PARAMETER_PROBLEM,
					"Illegal option `-%c' with this command\n",
					optflags[i]);
	}
}

	static char
opt2char(int option)
{
	const char *ptr;
	for (ptr = optflags; option > 1; option >>= 1, ptr++);

	return *ptr;
}

	static char
cmd2char(int option)
{
	const char *ptr;
	for (ptr = cmdflags; option > 1; option >>= 1, ptr++);

	return *ptr;
}

	static void
add_command(unsigned int *cmd, const int newcmd, const int othercmds, 
		int invert)
{
	if (invert)
		xtables_error(PARAMETER_PROBLEM, "unexpected ! flag");
	if (*cmd & (~othercmds))
		xtables_error(PARAMETER_PROBLEM, "Cannot use -%c with -%c\n",
				cmd2char(newcmd), cmd2char(*cmd & (~othercmds)));
	*cmd |= newcmd;
}

/*
 *	All functions starting with "parse" should succeed, otherwise
 *	the program fails.
 *	Most routines return pointers to static data that may change
 *	between calls to the same or other routines with a few exceptions:
 *	"host_to_addr", "parse_hostnetwork", and "parse_hostnetworkmask"
 *	return global static data.
 */

/* Christophe Burki wants `-p 6' to imply `-m tcp'.  */
	static struct xtables_match *
find_proto(const char *pname, enum xtables_tryload tryload,
		int nolookup, struct xtables_rule_match **matches)
{
	unsigned int proto;

	if (xtables_strtoui(pname, NULL, &proto, 0, UINT8_MAX)) {
		const char *protoname = proto_to_name(proto, nolookup);

		if (protoname)
			return xtables_find_match(protoname, tryload, matches);
	} else
		return xtables_find_match(pname, tryload, matches);

	return NULL;
}

/* Can't be zero. */
	static int
parse_rulenumber(const char *rule)
{
	unsigned int rulenum;

	if (!xtables_strtoui(rule, NULL, &rulenum, 1, INT_MAX))
		xtables_error(PARAMETER_PROBLEM,
				"Invalid rule number `%s'", rule);

	return rulenum;
}

	static const char *
parse_target(const char *targetname)
{
	const char *ptr;

	if (strlen(targetname) < 1)
		xtables_error(PARAMETER_PROBLEM,
				"Invalid target name (too short)");

	if (strlen(targetname)+1 > sizeof(ipt_chainlabel))
		xtables_error(PARAMETER_PROBLEM,
				"Invalid target name `%s' (%u chars max)",
				targetname, (unsigned int)sizeof(ipt_chainlabel)-1);

	for (ptr = targetname; *ptr; ptr++)
		if (isspace(*ptr))
			xtables_error(PARAMETER_PROBLEM,
					"Invalid target name `%s'", targetname);
	return targetname;
}

	static void
set_option(unsigned int *options, unsigned int option, u_int8_t *invflg,
		int invert)
{
	if (*options & option)
		xtables_error(PARAMETER_PROBLEM, "multiple -%c flags not allowed",
				opt2char(option));
	*options |= option;

	if (invert) {
		unsigned int i;
		for (i = 0; 1 << i != option; i++);

		if (!inverse_for_options[i])
			xtables_error(PARAMETER_PROBLEM,
					"cannot have ! before -%c",
					opt2char(option));
		*invflg |= inverse_for_options[i];
	}
}

	static void
print_num(u_int64_t number, unsigned int format)
{
	if (format & FMT_KILOMEGAGIGA) {
		if (number > 99999) {
			number = (number + 500) / 1000;
			if (number > 9999) {
				number = (number + 500) / 1000;
				if (number > 9999) {
					number = (number + 500) / 1000;
					if (number > 9999) {
						number = (number + 500) / 1000;
						printf(FMT("%4lluT ","%lluT "), (unsigned long long)number);
					}
					else printf(FMT("%4lluG ","%lluG "), (unsigned long long)number);
				}
				else printf(FMT("%4lluM ","%lluM "), (unsigned long long)number);
			} else
				printf(FMT("%4lluK ","%lluK "), (unsigned long long)number);
		} else
			printf(FMT("%5llu ","%llu "), (unsigned long long)number);
	} else
		printf(FMT("%8llu ","%llu "), (unsigned long long)number);
}


	static void
print_header(unsigned int format, const char *chain, struct iptc_handle *handle)
{
	struct ipt_counters counters;
	const char *pol = iptc_get_policy(chain, &counters, handle);
	printf("Chain %s", chain);
	if (pol) {
		printf(" (policy %s", pol);
		if (!(format & FMT_NOCOUNTS)) {
			fputc(' ', stdout);
			print_num(counters.pcnt, (format|FMT_NOTABLE));
			fputs("packets, ", stdout);
			print_num(counters.bcnt, (format|FMT_NOTABLE));
			fputs("bytes", stdout);
		}
		printf(")\n");
	} else {
		unsigned int refs;
		if (!iptc_get_references(&refs, chain, handle))
			printf(" (ERROR obtaining refs)\n");
		else
			printf(" (%u references)\n", refs);
	}

	if (format & FMT_LINENUMBERS)
		printf(FMT("%-4s ", "%s "), "num");
	if (!(format & FMT_NOCOUNTS)) {
		if (format & FMT_KILOMEGAGIGA) {
			printf(FMT("%5s ","%s "), "pkts");
			printf(FMT("%5s ","%s "), "bytes");
		} else {
			printf(FMT("%8s ","%s "), "pkts");
			printf(FMT("%10s ","%s "), "bytes");
		}
	}
	if (!(format & FMT_NOTARGET))
		printf(FMT("%-9s ","%s "), "target");
	fputs(" prot ", stdout);
	if (format & FMT_OPTIONS)
		fputs("opt", stdout);
	if (format & FMT_VIA) {
		printf(FMT(" %-6s ","%s "), "in");
		printf(FMT("%-6s ","%s "), "out");
	}
	printf(FMT(" %-19s ","%s "), "source");
	printf(FMT(" %-19s "," %s "), "destination");
	printf("\n");
}


	static int
print_match(const struct ipt_entry_match *m,
		const struct ipt_ip *ip,
		int numeric)
{
	struct xtables_match *match =
		xtables_find_match(m->u.user.name, XTF_TRY_LOAD, NULL);

	if (match) {
		if (match->print)
			match->print(ip, m, numeric);
		else
			printf("%s ", match->name);
	} else {
		if (m->u.user.name[0])
			printf("UNKNOWN match `%s' ", m->u.user.name);
	}
	/* Don't stop iterating. */
	return 0;
}

/* e is called `fw' here for historical reasons */
	static void
print_firewall(const struct ipt_entry *fw,
		const char *targname,
		unsigned int num,
		unsigned int format,
		struct iptc_handle *const handle)
{
	struct xtables_target *target = NULL;
	const struct ipt_entry_target *t;
	u_int8_t flags;
	char buf[BUFSIZ];

	if (!iptc_is_chain(targname, handle))
		target = xtables_find_target(targname, XTF_TRY_LOAD);
	else
		target = xtables_find_target(IPT_STANDARD_TARGET,
				XTF_LOAD_MUST_SUCCEED);

	t = ipt_get_target((struct ipt_entry *)fw);
	flags = fw->ip.flags;

	if (format & FMT_LINENUMBERS)
		printf(FMT("%-4u ", "%u "), num);

	if (!(format & FMT_NOCOUNTS)) {
		print_num(fw->counters.pcnt, format);
		print_num(fw->counters.bcnt, format);
	}

	if (!(format & FMT_NOTARGET))
		printf(FMT("%-9s ", "%s "), targname);

	fputc(fw->ip.invflags & IPT_INV_PROTO ? '!' : ' ', stdout);
	{
		const char *pname = proto_to_name(fw->ip.proto, format&FMT_NUMERIC);
		if (pname)
			printf(FMT("%-5s", "%s "), pname);
		else
			printf(FMT("%-5hu", "%hu "), fw->ip.proto);
	}

	if (format & FMT_OPTIONS) {
		if (format & FMT_NOTABLE)
			fputs("opt ", stdout);
		fputc(fw->ip.invflags & IPT_INV_FRAG ? '!' : '-', stdout);
		fputc(flags & IPT_F_FRAG ? 'f' : '-', stdout);
		fputc(' ', stdout);
	}

	if (format & FMT_VIA) {
		char iface[IFNAMSIZ+2];

		if (fw->ip.invflags & IPT_INV_VIA_IN) {
			iface[0] = '!';
			iface[1] = '\0';
		}
		else iface[0] = '\0';

		if (fw->ip.iniface[0] != '\0') {
			strcat(iface, fw->ip.iniface);
		}
		else if (format & FMT_NUMERIC) strcat(iface, "*");
		else strcat(iface, "any");
		printf(FMT(" %-6s ","in %s "), iface);

		if (fw->ip.invflags & IPT_INV_VIA_OUT) {
			iface[0] = '!';
			iface[1] = '\0';
		}
		else iface[0] = '\0';

		if (fw->ip.outiface[0] != '\0') {
			strcat(iface, fw->ip.outiface);
		}
		else if (format & FMT_NUMERIC) strcat(iface, "*");
		else strcat(iface, "any");
		printf(FMT("%-6s ","out %s "), iface);
	}

	fputc(fw->ip.invflags & IPT_INV_SRCIP ? '!' : ' ', stdout);
	if (fw->ip.smsk.s_addr == 0L && !(format & FMT_NUMERIC))
		printf(FMT("%-19s ","%s "), "anywhere");
	else {
		if (format & FMT_NUMERIC)
			strcpy(buf, xtables_ipaddr_to_numeric(&fw->ip.src));
		else
			strcpy(buf, xtables_ipaddr_to_anyname(&fw->ip.src));
		strcat(buf, xtables_ipmask_to_numeric(&fw->ip.smsk));
		printf(FMT("%-19s ","%s "), buf);
	}

	fputc(fw->ip.invflags & IPT_INV_DSTIP ? '!' : ' ', stdout);
	if (fw->ip.dmsk.s_addr == 0L && !(format & FMT_NUMERIC))
		printf(FMT("%-19s ","-> %s"), "anywhere");
	else {
		if (format & FMT_NUMERIC)
			strcpy(buf, xtables_ipaddr_to_numeric(&fw->ip.dst));
		else
			strcpy(buf, xtables_ipaddr_to_anyname(&fw->ip.dst));
		strcat(buf, xtables_ipmask_to_numeric(&fw->ip.dmsk));
		printf(FMT("%-19s ","-> %s"), buf);
	}

	if (format & FMT_NOTABLE)
		fputs("  ", stdout);

#ifdef IPT_F_GOTO
	if(fw->ip.flags & IPT_F_GOTO)
		printf("[goto] ");
#endif

	IPT_MATCH_ITERATE(fw, print_match, &fw->ip, format & FMT_NUMERIC);

	if (target) {
		if (target->print)
			/* Print the target information. */
			target->print(&fw->ip, t, format & FMT_NUMERIC);
	} else if (t->u.target_size != sizeof(*t))
		printf("[%u bytes of unknown target data] ",
				(unsigned int)(t->u.target_size - sizeof(*t)));

	if (!(format & FMT_NONEWLINE))
		fputc('\n', stdout);
}

	static void
print_firewall_line(const struct ipt_entry *fw,
		struct iptc_handle *const h)
{
	struct ipt_entry_target *t;

	t = ipt_get_target((struct ipt_entry *)fw);
	print_firewall(fw, t->u.user.name, 0, FMT_PRINT_RULE, h);
}

	static int
append_entry(const ipt_chainlabel chain,
		struct ipt_entry *fw,
		unsigned int nsaddrs,
		const struct in_addr saddrs[],
		unsigned int ndaddrs,
		const struct in_addr daddrs[],
		int verbose,
		struct iptc_handle *handle)
{
	unsigned int i, j;
	int ret = 1;

	for (i = 0; i < nsaddrs; i++) {
		fw->ip.src.s_addr = saddrs[i].s_addr;
		for (j = 0; j < ndaddrs; j++) {
			fw->ip.dst.s_addr = daddrs[j].s_addr;
			if (verbose)
				print_firewall_line(fw, handle);
			ret &= iptc_append_entry(chain, fw, handle);
		}
	}

	return ret;
}

	static int
replace_entry(const ipt_chainlabel chain,
		struct ipt_entry *fw,
		unsigned int rulenum,
		const struct in_addr *saddr,
		const struct in_addr *daddr,
		int verbose,
		struct iptc_handle *handle)
{
	fw->ip.src.s_addr = saddr->s_addr;
	fw->ip.dst.s_addr = daddr->s_addr;

	if (verbose)
		print_firewall_line(fw, handle);
	return iptc_replace_entry(chain, fw, rulenum, handle);
}

	static int
insert_entry(const ipt_chainlabel chain,
		struct ipt_entry *fw,
		unsigned int rulenum,
		unsigned int nsaddrs,
		const struct in_addr saddrs[],
		unsigned int ndaddrs,
		const struct in_addr daddrs[],
		int verbose,
		struct iptc_handle *handle)
{
	unsigned int i, j;
	int ret = 1;
	printf("It's really insert!\n");
	printf("Input: rulenum %u, nsaddrs %u, ndaddrs %u, verbose %d, fw %p, handle %p\n", rulenum, nsaddrs, ndaddrs, verbose, fw, handle);
	for (i = 0; i < nsaddrs; i++) {
		printf("for i %d\n", i);
		fw->ip.src.s_addr = saddrs[i].s_addr;
		for (j = 0; j < ndaddrs; j++) {
			printf("for j %d\n", j);
			fw->ip.dst.s_addr = daddrs[j].s_addr;
			//if (verbose)
				print_firewall_line(fw, handle);
			ret &= iptc_insert_entry(chain, fw, rulenum, handle);
		}
	}

	return ret;
}

	static int
update_entry(const ipt_chainlabel chain,
		struct ipt_entry *fw,
		unsigned int rulenum,
		unsigned int nsaddrs,
		const struct in_addr saddrs[],
		unsigned int ndaddrs,
		const struct in_addr daddrs[],
		int verbose,
		struct iptc_handle *handle)
{
	unsigned int i, j;
	int ret = 1;
	printf("It's really update!\n");
	printf("Input: rulenum %u, nsaddrs %u, ndaddrs %u, verbose %d, fw %p, handle %p\n", rulenum, nsaddrs, ndaddrs, verbose, fw, handle);

	for (i = 0; i < nsaddrs; i++) {
		printf("for i %d\n", i);
		fw->ip.src.s_addr = saddrs[i].s_addr;
		for (j = 0; j < ndaddrs; j++) {
			printf("for j %d\n", j);
			fw->ip.dst.s_addr = daddrs[j].s_addr;
			// if (verbose)
				print_firewall_line(fw, handle);
			ret &= iptc_insert_entry(chain, fw, rulenum, handle);
		}
	}

	return ret;
}


	static unsigned char *
make_delete_mask(struct ipt_entry *fw, struct xtables_rule_match *matches)
{
	/* Establish mask for comparison */
	unsigned int size;
	struct xtables_rule_match *matchp;
	unsigned char *mask, *mptr;

	size = sizeof(struct ipt_entry);
	for (matchp = matches; matchp; matchp = matchp->next)
		size += IPT_ALIGN(sizeof(struct ipt_entry_match)) + matchp->match->size;

	mask = xtables_calloc(1, size
			+ IPT_ALIGN(sizeof(struct ipt_entry_target))
			+ xtables_targets->size);

	memset(mask, 0xFF, sizeof(struct ipt_entry));
	mptr = mask + sizeof(struct ipt_entry);

	for (matchp = matches; matchp; matchp = matchp->next) {
		memset(mptr, 0xFF,
				IPT_ALIGN(sizeof(struct ipt_entry_match))
				+ matchp->match->userspacesize);
		mptr += IPT_ALIGN(sizeof(struct ipt_entry_match)) + matchp->match->size;
	}

	memset(mptr, 0xFF,
			IPT_ALIGN(sizeof(struct ipt_entry_target))
			+ xtables_targets->userspacesize);

	return mask;
}

	static int
delete_entry(const ipt_chainlabel chain,
		struct ipt_entry *fw,
		unsigned int nsaddrs,
		const struct in_addr saddrs[],
		unsigned int ndaddrs,
		const struct in_addr daddrs[],
		int verbose,
		struct iptc_handle *handle,
		struct xtables_rule_match *matches)
{
	unsigned int i, j;
	int ret = 1;
	unsigned char *mask;

	mask = make_delete_mask(fw, matches);
	for (i = 0; i < nsaddrs; i++) {
		fw->ip.src.s_addr = saddrs[i].s_addr;
		for (j = 0; j < ndaddrs; j++) {
			fw->ip.dst.s_addr = daddrs[j].s_addr;
			if (verbose)
				print_firewall_line(fw, handle);
			ret &= iptc_delete_entry(chain, fw, mask, handle);
		}
	}
	free(mask);

	return ret;
}

	int
for_each_chain(int (*fn)(const ipt_chainlabel, int, struct iptc_handle *),
		int verbose, int builtinstoo, struct iptc_handle *handle)
{
	int ret = 1;
	const char *chain;
	char *chains;
	unsigned int i, chaincount = 0;

	chain = iptc_first_chain(handle);
	while (chain) {
		chaincount++;
		chain = iptc_next_chain(handle);
	}

	chains = xtables_malloc(sizeof(ipt_chainlabel) * chaincount);
	i = 0;
	chain = iptc_first_chain(handle);
	while (chain) {
		strcpy(chains + i*sizeof(ipt_chainlabel), chain);
		i++;
		chain = iptc_next_chain(handle);
	}

	for (i = 0; i < chaincount; i++) {
		if (!builtinstoo
				&& iptc_builtin(chains + i*sizeof(ipt_chainlabel),
					handle) == 1)
			continue;
		ret &= fn(chains + i*sizeof(ipt_chainlabel), verbose, handle);
	}

	free(chains);
	return ret;
}

	int
flush_entries(const ipt_chainlabel chain, int verbose,
		struct iptc_handle *handle)
{
	if (!chain)
		return for_each_chain(flush_entries, verbose, 1, handle);

	if (verbose)
		fprintf(stdout, "Flushing chain `%s'\n", chain);
	return iptc_flush_entries(chain, handle);
}

	static int
zero_entries(const ipt_chainlabel chain, int verbose,
		struct iptc_handle *handle)
{
	if (!chain)
		return for_each_chain(zero_entries, verbose, 1, handle);

	if (verbose)
		fprintf(stdout, "Zeroing chain `%s'\n", chain);
	return iptc_zero_entries(chain, handle);
}

	int
delete_chain(const ipt_chainlabel chain, int verbose,
		struct iptc_handle *handle)
{
	if (!chain)
		return for_each_chain(delete_chain, verbose, 0, handle);

	if (verbose)
		fprintf(stdout, "Deleting chain `%s'\n", chain);
	return iptc_delete_chain(chain, handle);
}

	static int
list_entries(const ipt_chainlabel chain, int rulenum, int verbose, int numeric,
		int expanded, int linenumbers, struct iptc_handle *handle)
{
	int found = 0;
	unsigned int format;
	const char *this;

	format = FMT_OPTIONS;
	if (!verbose)
		format |= FMT_NOCOUNTS;
	else
		format |= FMT_VIA;

	if (numeric)
		format |= FMT_NUMERIC;

	if (!expanded)
		format |= FMT_KILOMEGAGIGA;

	if (linenumbers)
		format |= FMT_LINENUMBERS;

	for (this = iptc_first_chain(handle);
			this;
			this = iptc_next_chain(handle)) {
		const struct ipt_entry *i;
		unsigned int num;

		if (chain && strcmp(chain, this) != 0)
			continue;

		if (found) printf("\n");

		if (!rulenum)
			print_header(format, this, handle);
		i = iptc_first_rule(this, handle);

		num = 0;
		while (i) {
			num++;
			if (!rulenum || num == rulenum)
				print_firewall(i,
						iptc_get_target(i, handle),
						num,
						format,
						handle);
			i = iptc_next_rule(i, handle);
		}
		found = 1;
	}

	errno = ENOENT;
	return found;
}

static void print_proto(u_int16_t proto, int invert)
{
	if (proto) {
		unsigned int i;
		const char *invertstr = invert ? "! " : "";

		struct protoent *pent = getprotobynumber(proto);
		if (pent) {
			printf("%s-p %s ", invertstr, pent->p_name);
			return;
		}

		for (i = 0; xtables_chain_protos[i].name != NULL; ++i)
			if (xtables_chain_protos[i].num == proto) {
				printf("%s-p %s ",
						invertstr, xtables_chain_protos[i].name);
				return;
			}

		printf("%s-p %u ", invertstr, proto);
	}
}

#define IP_PARTS_NATIVE(n)			\
	(unsigned int)((n)>>24)&0xFF,			\
(unsigned int)((n)>>16)&0xFF,			\
(unsigned int)((n)>>8)&0xFF,			\
(unsigned int)((n)&0xFF)

#define IP_PARTS(n) IP_PARTS_NATIVE(ntohl(n))

/* This assumes that mask is contiguous, and byte-bounded. */
	static void
print_iface(char letter, const char *iface, const unsigned char *mask,
		int invert)
{
	unsigned int i;

	if (mask[0] == 0)
		return;

	printf("%s-%c ", invert ? "! " : "", letter);

	for (i = 0; i < IFNAMSIZ; i++) {
		if (mask[i] != 0) {
			if (iface[i] != '\0')
				printf("%c", iface[i]);
		} else {
			/* we can access iface[i-1] here, because
			 * a few lines above we make sure that mask[0] != 0 */
			if (iface[i-1] != '\0')
				printf("+");
			break;
		}
	}

	printf(" ");
}

static int print_match_save(const struct ipt_entry_match *e,
		const struct ipt_ip *ip)
{
	struct xtables_match *match =
		xtables_find_match(e->u.user.name, XTF_TRY_LOAD, NULL);

	if (match) {
		printf("-m %s ", e->u.user.name);

		/* some matches don't provide a save function */
		if (match->save)
			match->save(ip, e);
	} else {
		if (e->u.match_size) {
			fprintf(stderr,
					"Can't find library for match `%s'\n",
					e->u.user.name);
			exit(1);
		}
	}
	return 0;
}

/* print a given ip including mask if neccessary */
static void print_ip(char *prefix, u_int32_t ip, u_int32_t mask, int invert)
{
	u_int32_t bits, hmask = ntohl(mask);
	int i;

	if (!mask && !ip && !invert)
		return;

	printf("%s%s %u.%u.%u.%u",
			invert ? "! " : "",
			prefix,
			IP_PARTS(ip));

	if (mask == 0xFFFFFFFFU) {
		printf("/32 ");
		return;
	}

	i    = 32;
	bits = 0xFFFFFFFEU;
	while (--i >= 0 && hmask != bits)
		bits <<= 1;
	if (i >= 0)
		printf("/%u ", i);
	else
		printf("/%u.%u.%u.%u ", IP_PARTS(mask));
}

/* We want this to be readable, so only print out neccessary fields.
 * Because that's the kind of world I want to live in.  */
void print_rule(const struct ipt_entry *e,
		struct iptc_handle *h, const char *chain, int counters)
{
	struct ipt_entry_target *t;
	const char *target_name;

	/* print counters for iptables-save */
	if (counters > 0)
		printf("[%llu:%llu] ", (unsigned long long)e->counters.pcnt, (unsigned long long)e->counters.bcnt);

	/* print chain name */
	printf("-A %s ", chain);

	/* Print IP part. */
	print_ip("-s", e->ip.src.s_addr,e->ip.smsk.s_addr,
			e->ip.invflags & IPT_INV_SRCIP);

	print_ip("-d", e->ip.dst.s_addr, e->ip.dmsk.s_addr,
			e->ip.invflags & IPT_INV_DSTIP);

	print_iface('i', e->ip.iniface, e->ip.iniface_mask,
			e->ip.invflags & IPT_INV_VIA_IN);

	print_iface('o', e->ip.outiface, e->ip.outiface_mask,
			e->ip.invflags & IPT_INV_VIA_OUT);

	print_proto(e->ip.proto, e->ip.invflags & IPT_INV_PROTO);

	if (e->ip.flags & IPT_F_FRAG)
		printf("%s-f ",
				e->ip.invflags & IPT_INV_FRAG ? "! " : "");

	/* Print matchinfo part */
	if (e->target_offset) {
		IPT_MATCH_ITERATE(e, print_match_save, &e->ip);
	}

	/* print counters for iptables -R */
	if (counters < 0)
		printf("-c %llu %llu ", (unsigned long long)e->counters.pcnt, (unsigned long long)e->counters.bcnt);

	/* Print target name */
	target_name = iptc_get_target(e, h);
	if (target_name && (*target_name != '\0'))
#ifdef IPT_F_GOTO
		printf("-%c %s ", e->ip.flags & IPT_F_GOTO ? 'g' : 'j', target_name);
#else
	printf("-j %s ", target_name);
#endif

	/* Print targinfo part */
	t = ipt_get_target((struct ipt_entry *)e);
	if (t->u.user.name[0]) {
		struct xtables_target *target =
			xtables_find_target(t->u.user.name, XTF_TRY_LOAD);

		if (!target) {
			fprintf(stderr, "Can't find library for target `%s'\n",
					t->u.user.name);
			exit(1);
		}

		if (target->save)
			target->save(&e->ip, t);
		else {
			/* If the target size is greater than ipt_entry_target
			 * there is something to be saved, we just don't know
			 * how to print it */
			if (t->u.target_size !=
					sizeof(struct ipt_entry_target)) {
				fprintf(stderr, "Target `%s' is missing "
						"save function\n",
						t->u.user.name);
				exit(1);
			}
		}
	}
	printf("\n");
}

	static int
list_rules(const ipt_chainlabel chain, int rulenum, int counters,
		struct iptc_handle *handle)
{
	const char *this = NULL;
	int found = 0;

	if (counters)
		counters = -1;		/* iptables -c format */

	/* Dump out chain names first,
	 * thereby preventing dependency conflicts */
	if (!rulenum) for (this = iptc_first_chain(handle);
			this;
			this = iptc_next_chain(handle)) {
		if (chain && strcmp(this, chain) != 0)
			continue;

		if (iptc_builtin(this, handle)) {
			struct ipt_counters count;
			printf("-P %s %s", this, iptc_get_policy(this, &count, handle));
			if (counters)
				printf(" -c %llu %llu", (unsigned long long)count.pcnt, (unsigned long long)count.bcnt);
			printf("\n");
		} else {
			printf("-N %s\n", this);
		}
	}

	for (this = iptc_first_chain(handle);
			this;
			this = iptc_next_chain(handle)) {
		const struct ipt_entry *e;
		int num = 0;

		if (chain && strcmp(this, chain) != 0)
			continue;

		/* Dump out rules */
		e = iptc_first_rule(this, handle);
		while(e) {
			num++;
			if (!rulenum || num == rulenum)
				print_rule(e, handle, this, counters);
			e = iptc_next_rule(e, handle);
		}
		found = 1;
	}

	errno = ENOENT;
	return found;
}

	static struct ipt_entry *
generate_entry(const struct ipt_entry *fw,
		struct xtables_rule_match *matches,
		struct ipt_entry_target *target)
{
	unsigned int size;
	struct xtables_rule_match *matchp;
	struct ipt_entry *e;

	size = sizeof(struct ipt_entry);
	for (matchp = matches; matchp; matchp = matchp->next)
		size += matchp->match->m->u.match_size;

	e = xtables_malloc(size + target->u.target_size);
	*e = *fw;
	e->target_offset = size;
	e->next_offset = size + target->u.target_size;

	size = 0;
	for (matchp = matches; matchp; matchp = matchp->next) {
		memcpy(e->elems + size, matchp->match->m, matchp->match->m->u.match_size);
		size += matchp->match->m->u.match_size;
	}
	memcpy(e->elems + size, target, target->u.target_size);

	return e;
}

static void clear_rule_matches(struct xtables_rule_match **matches)
{
	struct xtables_rule_match *matchp, *tmp;

	for (matchp = *matches; matchp;) {
		tmp = matchp->next;
		if (matchp->match->m) {
			free(matchp->match->m);
			matchp->match->m = NULL;
		}
		if (matchp->match == matchp->match->next) {
			free(matchp->match);
			matchp->match = NULL;
		}
		free(matchp);
		matchp = tmp;
	}

	*matches = NULL;
}

void
get_kernel_version(void) {
	static struct utsname uts;
	int x = 0, y = 0, z = 0;

	if (uname(&uts) == -1) {
		fprintf(stderr, "Unable to retrieve kernel version.\n");
		xtables_free_opts(1);
		exit(1);
	}

	sscanf(uts.release, "%d.%d.%d", &x, &y, &z);
	kernel_version = LINUX_VERSION(x, y, z);
}

int do_command(int argc, char *argv[], char **table, struct iptc_handle **handle)
{
	struct ipt_entry fw, *e = NULL;
	int invert = 0;
	unsigned int nsaddrs = 0, ndaddrs = 0;
	struct in_addr *saddrs = NULL, *daddrs = NULL;

	int c, verbose = 0;
	const char *chain = NULL;
	const char *shostnetworkmask = NULL, *dhostnetworkmask = NULL;
	const char *policy = NULL, *newname = NULL;
	unsigned int rulenum = 0, options = 0, command = 0;
	const char *pcnt = NULL, *bcnt = NULL;
	int ret = 1;
	struct xtables_match *m;
	struct xtables_rule_match *matches = NULL;
	struct xtables_rule_match *matchp;
	struct xtables_target *target = NULL;
	struct xtables_target *t;
	const char *jumpto = "";
	char *protocol = NULL;
	int proto_used = 0;
	unsigned long long cnt;

	memset(&fw, 0, sizeof(fw));

	/* re-set optind to 0 in case do_command gets called
	 * a second time */
	optind = 0;

	/* clear mflags in case do_command gets called a second time
	 * (we clear the global list of all matches for security)*/
	for (m = xtables_matches; m; m = m->next)
		m->mflags = 0;

	for (t = xtables_targets; t; t = t->next) {
		t->tflags = 0;
		t->used = 0;
	}

	/* Suppress error messages: we may add new options if we
	   demand-load a protocol. */
	opterr = 0;

	while ((c = getopt_long(argc, argv,
					"-A:D:R:U:I:L::S::M:F::Z::N:X::E:P:Vh::o:p:s:d:j:i:fbvnt:m:xc:g:",
					opts, NULL)) != -1) {
		switch (c) {
			/*
			 * Command selection
			 */
			case 'A':
				add_command(&command, CMD_APPEND, CMD_NONE,
						invert);
				chain = optarg;
				break;

			case 'D':
				add_command(&command, CMD_DELETE, CMD_NONE,
						invert);
				chain = optarg;
				if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!') {
					rulenum = parse_rulenumber(argv[optind++]);
					command = CMD_DELETE_NUM;
				}
				break;

			case 'R':
				add_command(&command, CMD_REPLACE, CMD_NONE,
						invert);
				chain = optarg;
				if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					rulenum = parse_rulenumber(argv[optind++]);
				else
					xtables_error(PARAMETER_PROBLEM,
							"-%c requires a rule number",
							cmd2char(CMD_REPLACE));
				break;

			case 'I':
				add_command(&command, CMD_INSERT, CMD_NONE,
						invert);
				chain = optarg;
				if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					rulenum = parse_rulenumber(argv[optind++]);
				else rulenum = 1;
				break;

			case 'U':
				add_command(&command, CMD_UPDATE, CMD_NONE,
						invert);
				chain = optarg;
				if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					rulenum = parse_rulenumber(argv[optind++]);
				else rulenum = 1;
				break;


			case 'L':
				add_command(&command, CMD_LIST, CMD_ZERO,
						invert);
				if (optarg) chain = optarg;
				else if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					chain = argv[optind++];
				if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					rulenum = parse_rulenumber(argv[optind++]);
				break;

			case 'S':
				add_command(&command, CMD_LIST_RULES, CMD_ZERO,
						invert);
				if (optarg) chain = optarg;
				else if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					chain = argv[optind++];
				if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					rulenum = parse_rulenumber(argv[optind++]);
				break;

			case 'F':
				add_command(&command, CMD_FLUSH, CMD_NONE,
						invert);
				if (optarg) chain = optarg;
				else if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					chain = argv[optind++];
				break;

			case 'Z':
				add_command(&command, CMD_ZERO, CMD_LIST|CMD_LIST_RULES,
						invert);
				if (optarg) chain = optarg;
				else if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					chain = argv[optind++];
				break;

			case 'N':
				if (optarg && (*optarg == '-' || *optarg == '!'))
					xtables_error(PARAMETER_PROBLEM,
							"chain name not allowed to start "
							"with `%c'\n", *optarg);
				if (xtables_find_target(optarg, XTF_TRY_LOAD))
					xtables_error(PARAMETER_PROBLEM,
							"chain name may not clash "
							"with target name\n");
				add_command(&command, CMD_NEW_CHAIN, CMD_NONE,
						invert);
				chain = optarg;
				break;

			case 'X':
				add_command(&command, CMD_DELETE_CHAIN, CMD_NONE,
						invert);
				if (optarg) chain = optarg;
				else if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					chain = argv[optind++];
				break;

			case 'E':
				add_command(&command, CMD_RENAME_CHAIN, CMD_NONE,
						invert);
				chain = optarg;
				if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					newname = argv[optind++];
				else
					xtables_error(PARAMETER_PROBLEM,
							"-%c requires old-chain-name and "
							"new-chain-name",
							cmd2char(CMD_RENAME_CHAIN));
				break;

			case 'P':
				add_command(&command, CMD_SET_POLICY, CMD_NONE,
						invert);
				chain = optarg;
				if (optind < argc && argv[optind][0] != '-'
						&& argv[optind][0] != '!')
					policy = argv[optind++];
				else
					xtables_error(PARAMETER_PROBLEM,
							"-%c requires a chain and a policy",
							cmd2char(CMD_SET_POLICY));
				break;

			case 'h':
				if (!optarg)
					optarg = argv[optind];

				/* iptables -p icmp -h */
				if (!matches && protocol)
					xtables_find_match(protocol,
							XTF_TRY_LOAD, &matches);

				exit_printhelp(matches);

				/*
				 * Option selection
				 */
			case 'p':
				xtables_check_inverse(optarg, &invert, &optind, argc);
				set_option(&options, OPT_PROTOCOL, &fw.ip.invflags,
						invert);

				/* Canonicalize into lower case */
				for (protocol = argv[optind-1]; *protocol; protocol++)
					*protocol = tolower(*protocol);

				protocol = argv[optind-1];
				fw.ip.proto = xtables_parse_protocol(protocol);

				if (fw.ip.proto == 0
						&& (fw.ip.invflags & IPT_INV_PROTO))
					xtables_error(PARAMETER_PROBLEM,
							"rule would never match protocol");
				break;

			case 's':
				xtables_check_inverse(optarg, &invert, &optind, argc);
				set_option(&options, OPT_SOURCE, &fw.ip.invflags,
						invert);
				shostnetworkmask = argv[optind-1];
				break;

			case 'd':
				xtables_check_inverse(optarg, &invert, &optind, argc);
				set_option(&options, OPT_DESTINATION, &fw.ip.invflags,
						invert);
				dhostnetworkmask = argv[optind-1];
				break;

#ifdef IPT_F_GOTO
			case 'g':
				set_option(&options, OPT_JUMP, &fw.ip.invflags,
						invert);
				fw.ip.flags |= IPT_F_GOTO;
				jumpto = parse_target(optarg);
				break;
#endif

			case 'j':
				set_option(&options, OPT_JUMP, &fw.ip.invflags,
						invert);
				jumpto = parse_target(optarg);
				/* TRY_LOAD (may be chain name) */
				target = xtables_find_target(jumpto, XTF_TRY_LOAD);

				if (target) {
					size_t size;

					size = IPT_ALIGN(sizeof(struct ipt_entry_target))
						+ target->size;

					target->t = xtables_calloc(1, size);
					target->t->u.target_size = size;
					strcpy(target->t->u.user.name, jumpto);
					xtables_set_revision(target->t->u.user.name,
							target->revision);
					if (target->init != NULL)
						target->init(target->t);
					opts = xtables_merge_options(opts,
							target->extra_opts,
							&target->option_offset);
					if (opts == NULL)
						xtables_error(OTHER_PROBLEM,
								"can't alloc memory!");
				}
				break;


			case 'i':
				xtables_check_inverse(optarg, &invert, &optind, argc);
				set_option(&options, OPT_VIANAMEIN, &fw.ip.invflags,
						invert);
				xtables_parse_interface(argv[optind-1],
						fw.ip.iniface,
						fw.ip.iniface_mask);
				break;

			case 'o':
				xtables_check_inverse(optarg, &invert, &optind, argc);
				set_option(&options, OPT_VIANAMEOUT, &fw.ip.invflags,
						invert);
				xtables_parse_interface(argv[optind-1],
						fw.ip.outiface,
						fw.ip.outiface_mask);
				break;

			case 'f':
				set_option(&options, OPT_FRAGMENT, &fw.ip.invflags,
						invert);
				fw.ip.flags |= IPT_F_FRAG;
				break;

			case 'v':
				if (!verbose)
					set_option(&options, OPT_VERBOSE,
							&fw.ip.invflags, invert);
				verbose++;
				break;

			case 'm': {
					  size_t size;

					  if (invert)
						  xtables_error(PARAMETER_PROBLEM,
								  "unexpected ! flag before --match");

					  m = xtables_find_match(optarg, XTF_LOAD_MUST_SUCCEED,
							  &matches);
					  size = IPT_ALIGN(sizeof(struct ipt_entry_match))
						  + m->size;
					  m->m = xtables_calloc(1, size);
					  m->m->u.match_size = size;
					  strcpy(m->m->u.user.name, m->name);
					  xtables_set_revision(m->m->u.user.name, m->revision);
					  if (m->init != NULL)
						  m->init(m->m);
					  if (m != m->next) {
						  /* Merge options for non-cloned matches */
						  opts = xtables_merge_options(opts,
								  m->extra_opts,
								  &m->option_offset);
						  if (opts == NULL)
							  xtables_error(OTHER_PROBLEM,
									  "can't alloc memory!");
					  }
				  }
				  break;

			case 'n':
				  set_option(&options, OPT_NUMERIC, &fw.ip.invflags,
						  invert);
				  break;

			case 't':
				  if (invert)
					  xtables_error(PARAMETER_PROBLEM,
							  "unexpected ! flag before --table");
				  *table = optarg;
				  break;

			case 'x':
				  set_option(&options, OPT_EXPANDED, &fw.ip.invflags,
						  invert);
				  break;

			case 'V':
				  if (invert)
					  printf("Not %s ;-)\n", prog_vers);
				  else
					  printf("%s v%s\n",
							  prog_name, prog_vers);
				  exit(0);

			case '0':
				  set_option(&options, OPT_LINENUMBERS, &fw.ip.invflags,
						  invert);
				  break;

			case 'M':
				  xtables_modprobe_program = optarg;
				  break;

			case 'c':

				  set_option(&options, OPT_COUNTERS, &fw.ip.invflags,
						  invert);
				  pcnt = optarg;
				  bcnt = strchr(pcnt + 1, ',');
				  if (bcnt)
					  bcnt++;
				  if (!bcnt && optind < argc && argv[optind][0] != '-'
						  && argv[optind][0] != '!')
					  bcnt = argv[optind++];
				  if (!bcnt)
					  xtables_error(PARAMETER_PROBLEM,
							  "-%c requires packet and byte counter",
							  opt2char(OPT_COUNTERS));

				  if (sscanf(pcnt, "%llu", &cnt) != 1)
					  xtables_error(PARAMETER_PROBLEM,
							  "-%c packet counter not numeric",
							  opt2char(OPT_COUNTERS));
				  fw.counters.pcnt = cnt;

				  if (sscanf(bcnt, "%llu", &cnt) != 1)
					  xtables_error(PARAMETER_PROBLEM,
							  "-%c byte counter not numeric",
							  opt2char(OPT_COUNTERS));
				  fw.counters.bcnt = cnt;
				  break;


			case 1: /* non option */
				  if (optarg[0] == '!' && optarg[1] == '\0') {
					  if (invert)
						  xtables_error(PARAMETER_PROBLEM,
								  "multiple consecutive ! not"
								  " allowed");
					  invert = TRUE;
					  optarg[0] = '\0';
					  continue;
				  }
				  fprintf(stderr, "Bad argument `%s'\n", optarg);
				  exit_tryhelp(2);

			default:
				  if (!target
						  || !(target->parse(c - target->option_offset,
								  argv, invert,
								  &target->tflags,
								  &fw, &target->t))) {
					  for (matchp = matches; matchp; matchp = matchp->next) {
						  if (matchp->completed)
							  continue;
						  if (matchp->match->parse(c - matchp->match->option_offset,
									  argv, invert,
									  &matchp->match->mflags,
									  &fw,
									  &matchp->match->m))
							  break;
					  }
					  m = matchp ? matchp->match : NULL;

					  /* If you listen carefully, you can
					     actually hear this code suck. */

					  /* some explanations (after four different bugs
					   * in 3 different releases): If we encounter a
					   * parameter, that has not been parsed yet,
					   * it's not an option of an explicitly loaded
					   * match or a target.  However, we support
					   * implicit loading of the protocol match
					   * extension.  '-p tcp' means 'l4 proto 6' and
					   * at the same time 'load tcp protocol match on
					   * demand if we specify --dport'.
					   *
					   * To make this work, we need to make sure:
					   * - the parameter has not been parsed by
					   *   a match (m above)
					   * - a protocol has been specified
					   * - the protocol extension has not been
					   *   loaded yet, or is loaded and unused
					   *   [think of iptables-restore!]
					   * - the protocol extension can be successively
					   *   loaded
					   */
					  if (m == NULL
							  && protocol
							  && (!find_proto(protocol, XTF_DONT_LOAD,
									  options&OPT_NUMERIC, NULL)
								  || (find_proto(protocol, XTF_DONT_LOAD,
										  options&OPT_NUMERIC, NULL)
									  && (proto_used == 0))
							     )
							  && (m = find_proto(protocol, XTF_TRY_LOAD,
									  options&OPT_NUMERIC, &matches))) {
						  /* Try loading protocol */
						  size_t size;

						  proto_used = 1;

						  size = IPT_ALIGN(sizeof(struct ipt_entry_match))
							  + m->size;

						  m->m = xtables_calloc(1, size);
						  m->m->u.match_size = size;
						  strcpy(m->m->u.user.name, m->name);
						  xtables_set_revision(m->m->u.user.name,
								  m->revision);
						  if (m->init != NULL)
							  m->init(m->m);

						  opts = xtables_merge_options(opts,
								  m->extra_opts,
								  &m->option_offset);
						  if (opts == NULL)
							  xtables_error(OTHER_PROBLEM,
									  "can't alloc memory!");

						  optind--;
						  continue;
					  }
					  if (!m) {
						  if (c == '?') {
							  if (optopt) {
								  xtables_error(
										  PARAMETER_PROBLEM,
										  "option `%s' "
										  "requires an "
										  "argument",
										  argv[optind-1]);
							  } else {
								  xtables_error(
										  PARAMETER_PROBLEM,
										  "unknown option "
										  "`%s'",
										  argv[optind-1]);
							  }
						  }
						  xtables_error(PARAMETER_PROBLEM,
								  "Unknown arg `%s'", optarg);
					  }
				  }
		}
		invert = FALSE;
	}

	if (strcmp(*table, "nat") == 0 &&
			((policy != NULL && strcmp(policy, "DROP") == 0) ||
			 (jumpto != NULL && strcmp(jumpto, "DROP") == 0)))
		xtables_error(PARAMETER_PROBLEM,
				"\nThe \"nat\" table is not intended for filtering, "
				"the use of DROP is therefore inhibited.\n\n");

	for (matchp = matches; matchp; matchp = matchp->next)
		if (matchp->match->final_check != NULL)
			matchp->match->final_check(matchp->match->mflags);

	if (target != NULL && target->final_check != NULL)
		target->final_check(target->tflags);

	/* Fix me: must put inverse options checking here --MN */

	if (optind < argc)
		xtables_error(PARAMETER_PROBLEM,
				"unknown arguments found on commandline");
	if (!command)
		xtables_error(PARAMETER_PROBLEM, "no command specified");
	if (invert)
		xtables_error(PARAMETER_PROBLEM,
				"nothing appropriate following !");

	if (command & (CMD_REPLACE | CMD_INSERT | CMD_DELETE | CMD_APPEND)) {
		if (!(options & OPT_DESTINATION))
			dhostnetworkmask = "0.0.0.0/0";
		if (!(options & OPT_SOURCE))
			shostnetworkmask = "0.0.0.0/0";
	}

	if (shostnetworkmask)
		xtables_ipparse_any(shostnetworkmask, &saddrs,
				&fw.ip.smsk, &nsaddrs);

	if (dhostnetworkmask)
		xtables_ipparse_any(dhostnetworkmask, &daddrs,
				&fw.ip.dmsk, &ndaddrs);

	if ((nsaddrs > 1 || ndaddrs > 1) &&
			(fw.ip.invflags & (IPT_INV_SRCIP | IPT_INV_DSTIP)))
		xtables_error(PARAMETER_PROBLEM, "! not allowed with multiple"
				" source or destination IP addresses");

	if (command == CMD_REPLACE && (nsaddrs != 1 || ndaddrs != 1))
		xtables_error(PARAMETER_PROBLEM, "Replacement rule does not "
				"specify a unique address");

	generic_opt_check(command, options);

	if (chain && strlen(chain) > IPT_FUNCTION_MAXNAMELEN)
		xtables_error(PARAMETER_PROBLEM,
				"chain name `%s' too long (must be under %i chars)",
				chain, IPT_FUNCTION_MAXNAMELEN);

	/* only allocate handle if we weren't called with a handle */
	if (!*handle)
		*handle = iptc_init(*table);

	/* try to insmod the module if iptc_init failed */
	if (!*handle && xtables_load_ko(xtables_modprobe_program, false) != -1)
		*handle = iptc_init(*table);

	if (!*handle)
		xtables_error(VERSION_PROBLEM,
				"can't initialize iptables table `%s': %s",
				*table, iptc_strerror(errno));

	if (command == CMD_APPEND
			|| command == CMD_DELETE
			|| command == CMD_INSERT
			|| command == CMD_REPLACE) {
		if (strcmp(chain, "PREROUTING") == 0
				|| strcmp(chain, "INPUT") == 0) {
			/* -o not valid with incoming packets. */
			if (options & OPT_VIANAMEOUT)
				xtables_error(PARAMETER_PROBLEM,
						"Can't use -%c with %s\n",
						opt2char(OPT_VIANAMEOUT),
						chain);
		}

		if (strcmp(chain, "POSTROUTING") == 0
				|| strcmp(chain, "OUTPUT") == 0) {
			/* -i not valid with outgoing packets */
			if (options & OPT_VIANAMEIN)
				xtables_error(PARAMETER_PROBLEM,
						"Can't use -%c with %s\n",
						opt2char(OPT_VIANAMEIN),
						chain);
		}

		if (target && iptc_is_chain(jumpto, *handle)) {
			fprintf(stderr,
					"Warning: using chain %s, not extension\n",
					jumpto);

			if (target->t)
				free(target->t);

			target = NULL;
		}

		/* If they didn't specify a target, or it's a chain
		   name, use standard. */
		if (!target
				&& (strlen(jumpto) == 0
					|| iptc_is_chain(jumpto, *handle))) {
			size_t size;

			target = xtables_find_target(IPT_STANDARD_TARGET,
					XTF_LOAD_MUST_SUCCEED);

			size = sizeof(struct ipt_entry_target)
				+ target->size;
			target->t = xtables_calloc(1, size);
			target->t->u.target_size = size;
			strcpy(target->t->u.user.name, jumpto);
			if (!iptc_is_chain(jumpto, *handle))
				xtables_set_revision(target->t->u.user.name,
						target->revision);
			if (target->init != NULL)
				target->init(target->t);
		}

		if (!target) {
			/* it is no chain, and we can't load a plugin.
			 * We cannot know if the plugin is corrupt, non
			 * existant OR if the user just misspelled a
			 * chain. */
#ifdef IPT_F_GOTO
			if (fw.ip.flags & IPT_F_GOTO)
				xtables_error(PARAMETER_PROBLEM,
						"goto '%s' is not a chain\n", jumpto);
#endif
			xtables_find_target(jumpto, XTF_LOAD_MUST_SUCCEED);
		} else {
			e = generate_entry(&fw, matches, target->t);
			free(target->t);
		}
	}

	printf("SOI: Command %d\n", command);
	printf("Params: nsaddrs %u, ndaddrs %u\n", nsaddrs, ndaddrs);
	switch (command) {
		case CMD_APPEND:
			ret = append_entry(chain, e,
					nsaddrs, saddrs, ndaddrs, daddrs,
					options&OPT_VERBOSE,
					*handle);
			break;
		case CMD_DELETE:
			ret = delete_entry(chain, e,
					nsaddrs, saddrs, ndaddrs, daddrs,
					options&OPT_VERBOSE,
					*handle, matches);
			break;
		case CMD_DELETE_NUM:
			ret = iptc_delete_num_entry(chain, rulenum - 1, *handle);
			break;
		case CMD_REPLACE:
			ret = replace_entry(chain, e, rulenum - 1,
					saddrs, daddrs, options&OPT_VERBOSE,
					*handle);
			break;
		case CMD_INSERT:
			ret = insert_entry(chain, e, rulenum - 1,
					nsaddrs, saddrs, ndaddrs, daddrs,
					options&OPT_VERBOSE,
					*handle);
			break;
		case CMD_UPDATE:
			ret = update_entry(chain, e, rulenum - 1,
					nsaddrs, saddrs, ndaddrs, daddrs,
					options&OPT_VERBOSE,
					*handle);
			break;

		case CMD_FLUSH:
			ret = flush_entries(chain, options&OPT_VERBOSE, *handle);
			break;
		case CMD_ZERO:
			ret = zero_entries(chain, options&OPT_VERBOSE, *handle);
			break;
		case CMD_LIST:
		case CMD_LIST|CMD_ZERO:
			ret = list_entries(chain,
					rulenum,
					options&OPT_VERBOSE,
					options&OPT_NUMERIC,
					options&OPT_EXPANDED,
					options&OPT_LINENUMBERS,
					*handle);
			if (ret && (command & CMD_ZERO))
				ret = zero_entries(chain,
						options&OPT_VERBOSE, *handle);
			break;
		case CMD_LIST_RULES:
		case CMD_LIST_RULES|CMD_ZERO:
			ret = list_rules(chain,
					rulenum,
					options&OPT_VERBOSE,
					*handle);
			if (ret && (command & CMD_ZERO))
				ret = zero_entries(chain,
						options&OPT_VERBOSE, *handle);
			break;
		case CMD_NEW_CHAIN:
			ret = iptc_create_chain(chain, *handle);
			break;
		case CMD_DELETE_CHAIN:
			ret = delete_chain(chain, options&OPT_VERBOSE, *handle);
			break;
		case CMD_RENAME_CHAIN:
			ret = iptc_rename_chain(chain, newname,	*handle);
			break;
		case CMD_SET_POLICY:
			ret = iptc_set_policy(chain, policy, options&OPT_COUNTERS ? &fw.counters : NULL, *handle);
			break;
		default:
			/* We should never reach this... */
			exit_tryhelp(2);
	}

	if (verbose > 1)
		dump_entries(*handle);

	clear_rule_matches(&matches);

	if (e != NULL) {
		free(e);
		e = NULL;
	}

	free(saddrs);
	free(daddrs);
	xtables_free_opts(1);

	return ret;
}
