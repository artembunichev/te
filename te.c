/*
	te -- text editor.
*/


#include<stdlib.h>
#include<stdarg.h>
#include<fcntl.h>
#include<unistd.h>
#include<signal.h>
#include<sys/ioctl.h>
#include<string.h>
#include<stdio.h>
#include<errno.h>


/* maximum input/output buffer size. */
#define MXBFSZ 4096
/* the number of lines to expand the lines array. */
#define EXLNS 32
/* by how many character do expand the line. */
#define EXLIN 64

/* get the difference between two numbers. */
#define DIFF(A, B) ((A) > (B) ? ((A) - (B)) : ((B) - (A)))

/* check a single address for validity (`0' means valid). */
#define CKADDR(A) ((A) < 0 || (A) > lnsl)
/* save current address value as previous and set a new one. */
#define SCADDR(X) do {pcaddr = caddr; caddr = (X);} while (0)
/* like `SCADDR' but check new value for validity first. */
#define SSCADDR(X) if (!CKADDR(X)) SCADDR(X);
/* restore current address value to previous one. */
#define RCADDR() caddr = pcaddr;
/* free line structure. */
#define FREELN(L) do {free(L->str); free(L);} while (0)

/*
	The following macros are supposed to be called within `cmdloop'.
*/
/* the currently parsed character must be a first one in the stream. */
#define FIRST() if (!first) return 1;
/* currently examined character must be last (next is "\n"). */
#define LAST() if (*(ibup+1) != '\n') return 1;
#define SINGLE() FIRST(); LAST();
/* use default addresses if they were not specified manually. */
#define DFLTADDR() {\
	if (!addrn) addrs[addrn++] = caddr;\
	if (addrn == 1) addrs[addrn++] = addrs[0];\
}
/*
	check an address range for validity and restore current
	address if range is invalid.
*/
#define CKADDRS(Z, O) if (ckaddrs(Z, O)) { RCADDR(); return 1; }
/* extract third address (which is a "destination address") and validate it. */
#define DADDR(D) {\
	if (*++ibup != '\n') {\
		/* we expect only a single destination address. */\
		if (getnxaddr() || *ibup != '\n') return 1;\
	}\
	if (addrn == 2) addrs[addrn++] = (D);\
	if (CKADDR(addrs[2])) return 1;\
}
/* check line mark for validity. */
#define CKMARK() if (*ibup < 'a' || *ibup > 'z') return 1;


/* target file descriptor. */
int fd;
/* target file path. */
char* fpth;

/* input buffer. */
char ibu[MXBFSZ];
/* input buffer pointer. */
char* ibup;
/* actually read bytes from input buffer. */
ssize_t arb;

/* a line node. */
struct ln {
	/* the value of a line without a newline character in the end. */
	char* str;
	/* its actual length. */
	size_t l;
	/* the total size of it. */
	size_t sz;
	/* line's mark, by which it may be refered to. */
	char mark;
};
/* list of pointers to line nodes. */
struct ln** lns;
/* actual number of lines in the buffer. */
size_t lnsl;
/* size of `lns'. */
size_t lnssz;

/* current line address. */
size_t caddr;
/* previous valid current address. */
size_t pcaddr;

/* specified (input) addresses. */
size_t addrs[3];
/* current number of addresses specified. */
int addrn;

/* if current examined character is first. */
char first;

/* if text buffer is somehow modified. */
char dirty;

/* terminal window size, if the output is a terminal. */
struct winsize wsz;
/* number of terminal rows (default is set in `initz'. */
unsigned short row;
struct sigaction sa;


/* die and print the error message with program's name prefix. */
void
die(char* err, ...) {
	va_list ap;
	va_start(ap, err);
	dprintf(2, "[te]: ");
	vdprintf(2, err, ap);
	va_end(ap);
	exit(1);
}

/* safe malloc. */
void*
smalloc(size_t sz) {
	void* ret;

	ret = malloc(sz);
	if (!ret) die("can not allocate %zu bytes.\n", sz);

	return ret;
}

/* safe realloc. */
void*
srealloc(void* p, size_t sz) {
	void* ret;

	ret = realloc(p, sz);
	if (!ret) die("can not reallocate %zu bytes.\n", sz);

	return ret;
}

/* safe calloc. */
void*
scalloc(size_t n, size_t s) {
	void* ret;

	ret = calloc(n, s);
	if (!ret) die("can not alloc %zu objects %zu bytes each.\n", n, s);

	return ret;
}

/* plain quit without any warnings. */
void
quit() {
	exit(0);
}

/* safely quit the editor. */
int
squit() {
	if (dirty) return 1;
	quit();
	return 0;
}

/* initialize `ln' structs after reallocating `lns'. `f' - start index. */
void
initlnmem(int f) {
	int i;

	for (i = 0; i < EXLNS; ++i) {
		lns[f+i] = scalloc(1, sizeof(struct ln));
	}
}

/* read the target file into memory. */
void
rdf() {
	/* input buffer iterator. */
	int i;
	/* totally read bytes. */
	ssize_t trb;

	trb = 0;
	lns = smalloc(EXLNS * sizeof(struct ln*));
	initlnmem(lnsl);
	lnssz = EXLNS;
	lnsl = 0;

	/*
		open file for both reading and writing to prevent an attempt
		to open file we don't have a permisson to write to.
		we won't make use of actual writing for this time.
	*/
	fd = open(fpth, O_RDWR);
	if (fd == -1) die("can't open file %s.\n", fpth);

	while ((arb = read(fd, &ibu, MXBFSZ)) > 0) {
		trb += arb;

		for (i = 0; i < arb; ++i) {
			if (ibu[i] == '\n') {
				++lnsl;
				if (lnsl == lnssz) {
					lns = srealloc(lns, (lnssz += EXLNS) * sizeof(struct ln*));
					initlnmem(lnsl);
				}
				continue;
			}

			if (lns[lnsl]->l == lns[lnsl]->sz) {
				lns[lnsl]->str = srealloc(lns[lnsl]->str, lns[lnsl]->sz += EXLIN);
			}
	
			lns[lnsl]->str[lns[lnsl]->l] = ibu[i];
			lns[lnsl]->l++;

		}
	}
	if (arb == -1) die("error reading %s.\n", fpth);

	/*
		"append" newline to the end of file if it doesn't exist.
		Actually, we don't _append_ the line itself, 'cause we
		store lines without "\n" character in the end. What we
		do is just inform that if we attempt to write the
		file back, a new line will appear.
	*/
	if (!lnsl) {
		lnsl++;
		trb++;
		/* 'cause we "made" a change. */
		dirty = 1;
		dprintf(2, "newline appended.\n");
	}
	else dirty = 0;

	/* initializing addresses, that's why not `SCADDR'. */
	pcaddr = caddr = lnsl;

	dprintf(1, "%zu\n", trb);

	close(fd);
}

/* ordinary print. */
void
printp() {
	size_t i;

	for (i = addrs[0]-1; i < addrs[1]; ++i) {
		write(1, lns[i]->str, lns[i]->l);
		write(1, "\n", 1);
	}

	SCADDR(addrs[1]);
}

/* print with line numbers. */
void
printn() {
	size_t i;

	for (i = addrs[0]-1; i < addrs[1]; ++i) {
		dprintf(1, "%-2zu  ", i+1);
		write(1, lns[i]->str, lns[i]->l);
		write(1, "\n", 1);
	}

	SCADDR(addrs[1]);
}

/* print unambiguously. */
void
printl() {
	/* line index. */
	size_t i;
	/* character index within the line. */
	size_t j;

	for (i = addrs[0]-1; i < addrs[1]; ++i) {
		for (j = 0; j < lns[i]->l; ++j) {
			char* s;
			/* actual length of printed sequence. */
			int l;

			s = smalloc(2);
			l = 1;

			switch (lns[i]->str[j]) {
			case '\t':
				strcpy(s, "\\t");
				l = 2;
				break;
			case '\\':
				strcpy(s, "\\\\");
				l = 2;
				break;
			default:
				s = strcpy(s, &(lns[i]->str[j]));
			}

			write(1, s, l);
			free(s);
		}
		write(1, "$\n", 2);
	}

	SCADDR(addrs[1]);
}

/*
	Set "z" mode facilities.
	In this mode lines are printed so that they fit the screen.
	In order to achieve this, we need to determine the terminal
	size and update it when it changes its sizes.
	If the output does not go to a terminal, use default values instead.
*/
void
setz() {
	if (!isatty(1)) return;

	if (ioctl(1, TIOCGWINSZ, &wsz) != -1) {
		row = wsz.ws_row;
	}
}

/* initialize "z" facilities. */
void
initz() {
	/* default value. */
	row = 32;

	setz();

	sa.sa_handler = &setz;
	sigaction(SIGWINCH, &sa, NULL);
}

/* activate "z" mode, in which lines will fit the screen. */
void
zmode() {
	/* end address (potentially with buffer overflow). */
	int e;

	addrs[0] = addrs[1];
	e = addrs[0] + row - 2;
	addrs[1] = e > lnsl ? lnsl : e;
}

/* print target file path. */
void
pfpth() {
	dprintf(1, "%s\n", fpth);
}

/* print current number of bytes in buffer. */
void
pbyt() {
	/* line index. */
	size_t i;
	/* totally stored bytes. */
	size_t tsb;

	tsb = 0;

	for (i = 0; i < lnsl; ++i) {
		tsb += lns[i]->l;
	}

	dprintf(1, "%zu\n", tsb);
}

/* print number of a last line in the buffer. */
void
plastnum() {
	dprintf(1, "%zu\n", lnsl);
}

/* write text buffer to the target file. */
void
wrf() {
	/* actually written bytes. */
	ssize_t awb;
	/* totally written bytes. */
	ssize_t twb;
	/* line index. */
	int i;

	twb = 0;

	/*
		Provide `O_TRUNC' in order to overwrite current contents.

		And in case file was deleted during editing session, we
	 	pass `O_CREAT' to create the file back.
	*/
	fd = open(fpth, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd == -1) die("can not open %s for writing.\n", fpth);

	for (i = 0; i < lnsl; ++i) {
		awb = write(fd, lns[i]->str, lns[i]->l);
		/* append a newline for each written line. */
		if (awb == -1 || write(fd, "\n", 1) == -1)
			die("error writing to %s.\n", fpth);
		twb += awb+1;
	}

	dirty = 0;

	dprintf(1, "%zu\n", twb);

	close(fd);
}

/* delete line range. */
void
delln() {
	int i;
	int diff;

	diff = addrs[1] - addrs[0] + 1;

	/* free memory occupied by lines we're about to delete. */
	for (i = addrs[0]-1; i < addrs[1]; ++i) FREELN(lns[i]);

	/* move bottom lines to top. */
	memcpy(&lns[addrs[0]-1], &lns[addrs[1]], (lnsl-addrs[1]) * sizeof(struct ln*));

	/* shrink lines array. */
	lnsl -= diff;
	lns = srealloc(lns, (lnssz -= diff) * sizeof(struct ln*));

	/*
		If there is a line after deleted block, we set it as current.
		Otherwise, we make the line above the deleted region current;
		actually, in this case, it will be the last line in the buffer.
	*/
	SCADDR(addrs[0] > lnsl ? lnsl : addrs[0]);

	dirty = 1;
}

/*
	move line(s).
	This function is called only when the movment will
	not be redundant (all the checks are made in `cmdlool').
	That's why it does set `dirty' flag unconditionally.
*/
void
mvln() {
	/* the size of target range. */
	int diff;
	/* size of target range in bytes. */
	int diffb;
	/* temorary place for swapping things. */
	struct ln** tmp;

	diff = addrs[1] - addrs[0] + 1;
	diffb = diff * sizeof(struct ln*);
	tmp = smalloc(diffb);

	/* stash target range. */
	memcpy(tmp, &lns[addrs[0]-1], diffb);

	/* moving to the bottom. */
	if (addrs[2] > addrs[1]) {
		memcpy(&lns[addrs[0]-1], &lns[addrs[1]],
		       (addrs[2]-addrs[1]) * sizeof(struct ln*));
		memcpy(&lns[addrs[2]-diff], tmp, diffb);
	}
	/* moving to the top. */
	else {
		memmove(&lns[addrs[2]+diff], &lns[addrs[2]],
		        (addrs[0]-1-addrs[2]) * sizeof(struct ln*));
		memcpy(&lns[addrs[2]], tmp, diffb);
	}

	SCADDR(addrs[2] > addrs[1] ? addrs[2] : addrs[2] + diff);
	dirty = 1;

	free(tmp);
}

/* routine for replicating a line to another address. */
void
dorep(int i, int leap) {
	/* replicated line. */
	struct ln* rln;

	rln = smalloc(sizeof(struct ln));
	rln->str = smalloc(lns[i]->l);
	memcpy(rln->str, lns[i]->str, lns[i]->l);
	rln->l = lns[i]->l;
	rln->sz = rln->l;
	rln->mark = 0;

	lns[i+leap] = rln;
}

/*
	replicate (copy) lines.
	Assume invalid addresses are filtered out.
*/
void
repln() {
	/* number of lines to move to bottom. */
	int rest;
	int diff;
	int difflb;
	int i;

	rest = lnsl - addrs[2];
	diff = addrs[1] - addrs[0] + 1;
	difflb = diff * sizeof(struct ln*);
	if (lnsl + difflb > lnssz) {
		lns = srealloc(lns, (lnssz += diff) * sizeof(struct ln*));
	}
	lnsl += diff;

	memmove(&lns[addrs[2]]+diff, &lns[addrs[2]], rest * sizeof(struct ln*));

	if (addrs[2] < addrs[0] || addrs[2] >= addrs[1]) {
		int leap;
		/* index of first line to be replicated. */
		int sr;

		sr = addrs[2] >= addrs[1] ? addrs[0] - 1 : addrs[0] - 1 + diff;
		if (addrs[2] >= addrs[1]) {
			leap = addrs[2] - addrs[0] + 1;
		}
		else {
			leap = -(diff + addrs[0]-addrs[2] - 1);
		}

		for (i = sr; i < sr+diff; ++i) {
			dorep(i, leap);
		}
	}
	else {
		int ffw;
		int fbw;
		int tbw;
		int leapfw;
		int leapbw;

		ffw = addrs[0] - 1;
		fbw = addrs[1]+addrs[2]-addrs[0]+1;
		tbw = fbw + addrs[1] - addrs[2];
		leapfw = addrs[2]-addrs[0]+1;
		leapbw = addrs[2]-addrs[1];

		for (i = ffw; i < addrs[2]; ++i) {
			dorep(i, leapfw);
		}

		for (i = fbw; i < tbw; ++i) {
			dorep(i, leapbw);
		}
	}

	SCADDR(addrs[2] + diff);
	dirty = 1;
}

/* join lines. */
void
jln() {
	struct ln* from;
	struct ln* to;
	/* the length of `to' after join. */
	int jl;

	from = lns[addrs[1]-1];
	to = lns[addrs[0]-1];
	jl = to->l + from->l;

	/* check if `to' has enough space. */
	if (jl > to->sz) {
		to->str = srealloc(to->str, to->sz += (jl - to->sz));
	}

	memcpy(&to->str[to->l], from->str, from->l);
	to->l = jl;
	to->mark = 0;
	FREELN(from);

	memcpy(&lns[addrs[1]-1], &lns[addrs[1]], (lnsl-addrs[1]) * sizeof(struct ln*));

	--lnsl;
	SCADDR(addrs[0] < addrs[1] ? addrs[0] : addrs[0] - 1);
	dirty = 1;
}

/* append a line. */
void
apnd() {
	/*
		current state:
		`0' - ordinary character.
		`1' - previous was "\n".
		`2' - previous was a sequence of "\n.".
	*/
	char st;
	int i;
	/* a single read line. */
	char* ln;
	size_t lnl;
	size_t lnsz;

	st = 1;
	ln = NULL;
	lnsz = lnl = 0;
	pcaddr = caddr;
	caddr = addrs[1];

	while ((arb = read(0, &ibu, MXBFSZ)) > 0) {
		i = 0;

		while (i < arb) {
			switch (ibu[i]) {
			case '\n':
				if (st == 2) {
					free(ln);
					return;
				}
				else {
					st = 1;
					if (lnsl+1 > lnssz) {
						lns = srealloc(lns, (lnssz += EXLNS) * sizeof(struct ln*));
					}
					memmove(&lns[caddr+1], &lns[caddr], (lnsl - caddr) * sizeof(struct ln*));
					lnsl++;

					lns[caddr] = scalloc(1, sizeof(struct ln));
					lns[caddr]->str = smalloc(lnl);
					memcpy(lns[caddr]->str, ln, lnl);
					lns[caddr]->sz = lns[caddr]->l = lnl;
					lns[caddr]->mark = 0;

					/*
						here we do *not* zero the `lnsz', because
						this memory has already been allocated anyway,
						so we can continue making use of it.
					*/
					lnl = 0;

					caddr++;
					dirty = 1;
				}
				break;
			case '.':
				if (st == 1) {
					st = 2;
					break;
				}
				/* FALLTHROUGH. */
			default:
				st = 0;
				if (lnl+1 > lnsz) {
					ln = srealloc(ln, lnsz += EXLIN);
				}
				ln[lnl++] = ibu[i];
			}
			++i;
		}
	}
}

/* mark line. */
void
markln() {
	int i;

	/*
		reassign mark.
		i.e. if another line is already marked with this
		mark, remove it from it and mark a requested line.
	*/
	for (i = 0; i < lnsl; ++i) {
		if (lns[i]->mark == *ibup) {
			lns[i]->mark = 0;
			break;
		}
	}

	/*
		we assume that `ibup' points to actual mark that
		has been validated.
	*/
	lns[caddr-1]->mark = *ibup;
}

/* get line address for line marked by character under `ibup'. */
size_t
getmarkaddr() {
	int i;

	for (i = 0; i < lnsl; ++i) {
		/* we're expecting a valid mark value here. */
		if (lns[i]->mark == *ibup) return i+1;
	}

	return -1;
}

/* get single address from input (next one). */
int
getnxaddr() {
	switch (*ibup) {
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		addrs[addrn++] = strtol(ibup, &ibup, 10);
		break;
	case '.':
	case '$':
		addrs[addrn++] = *ibup++ == '.' ? caddr : lnsl;
		break;
	case '\'': {
		/* mark address. */
		size_t maddr;

		ibup++;
		CKMARK();
		maddr = getmarkaddr();

		if (maddr == -1) return 1;
		addrs[addrn++] = maddr;

		ibup++;
		break;
	}
	}

	return 0;
}

/* get address range. */
int
getrng() {
	/* previous input buffer pointer. */
	char* pibup;
	/*
		if it is a comma-delimited range.
		`0' - no.
		`1' - yes.
		`2' - yes and comma is first character.
	*/
	int com;

	pibup = ibup;
	com = 0;

	while (addrn < 2) {
		if (getnxaddr()) return 1;
		if (ibup != pibup) first = 0;
		switch (*ibup) {
		case ',':
			com = 1;
			if (first) {
				com = 2;
				addrs[addrn++] = 1;
			}
			ibup++;
			break;
		default:
			if (com == 2 && addrn == 1) addrs[addrn++] = lnsl;
			return 0;
		}
	}

	return 0;
}

/*
	check addresses for validity.

	`zer' - if address `0' is allowed.
	`ord' - if addresses should be ordered.
*/
int
ckaddrs(char zer, char ord) {
	if (!zer && (!addrs[0] || !addrs[1])) return 1;
	if (ord && (addrs[1] < addrs[0])) return 1;
	if (CKADDR(addrs[0]) || CKADDR(addrs[1])) return 1;
	return 0;
}

/* parse input and return number >0 if error occurs. */
int
parsecmd() {
	addrs[0] = addrs[1] = addrs[2] = addrn = 0;
	first = 1;

	if (getrng()) return 1;

	for (;; first = 0) {
		switch(*ibup) {
		case 'f':
			SINGLE();
			pfpth();
			return 0;
		case 'b':
			SINGLE();
			pbyt();
			return 0;
		case '=':
			SINGLE();
			plastnum();
			return 0;
		case 'p':
			LAST();
			DFLTADDR();
			CKADDRS(0, 1);
			printp();
			return 0;
		case 'n':
			LAST();
			DFLTADDR();
			CKADDRS(0, 1);
			printn();
			return 0;
		case 'l':
			LAST();
			DFLTADDR();
			CKADDRS(0, 1);
			printl();
			return 0;
		case 'z':
			DFLTADDR();
			CKADDRS(0, 1);
			zmode();
			switch(*++ibup) {
			case 'n':
				printn();
				break;
			case 'l':
				printl();
				break;
			case 'p':
			/* FALLTHROUGH. */
			case '\n':
				printp();
				break;
			default:
				return 1;
			}
			SSCADDR(caddr+1);
			return 0;
		case 'd':
			LAST();
			DFLTADDR();
			CKADDRS(0, 1);
			delln();
			return 0;
		case 'k':
			DFLTADDR();
			CKADDRS(0, 1);
			SCADDR(addrs[1]);
			ibup++;
			CKMARK();
			markln();
			return 0;
		case 'm':
			DFLTADDR();
			CKADDRS(0, 1);
			DADDR(addrs[1] == lnsl ? lnsl : addrs[1]+1);
			/* we can not move the range within itself. */
			if (addrs[2] >= addrs[0] && addrs[2] < addrs[1]) return 1;
			/*
				redundant cases, which will result in the same
				line position within the buffer. So it's not
				necessary to perform an actual move.
			*/
			if (addrs[1] == addrs[2]) return 1;
			if (addrs[2] == addrs[0]-1) return 1;
			mvln();
			return 0;
		case 'r':
			DFLTADDR();
			CKADDRS(0, 1);
			DADDR(addrs[1]);
			repln();
			return 0;
		case 'j':
			LAST();
			if (!addrn) addrs[addrn++] = caddr;
			if (addrn == 1) addrs[addrn++] = addrs[0] + 1;
			CKADDRS(0, 0);
			if (addrs[0] == addrs[1]) return 1;
			jln();
			return 0;
		case 'a':
			LAST();
			DFLTADDR();
			CKADDRS(1, 1);
			apnd();
			return 0;
		case 'i':
			LAST();
			DFLTADDR();
			CKADDRS(0, 1);
			/* inserting at X is like appending at X-1. */
			addrs[1]--;
			apnd();
			return 0;
		case 'c':
			LAST();
			DFLTADDR();
			CKADDRS(0, 1);
			/*
				changing the range is like first deleting the
				range and then appending to the previous line.
			*/
			delln();
			addrs[1] = addrs[0] - 1;
			apnd();
			return 0;
		case 'w':
			FIRST();
			switch(*++ibup) {
			case 'q':
				wrf();
				if (squit()) return 1;
				break;
			case '\n':
				wrf();
				return 0;
			}
			return 1;
		case 'q':
			SINGLE();
			if (squit()) return 1;
			return 0;
		case 'Q':
			SINGLE();
			quit();
			return 0;
		case '\n':
			if (first) SCADDR(caddr+1);

			if (!addrn) addrs[addrn++] = caddr;
			if (addrn == 1) addrs[addrn] = addrs[0];
			CKADDRS(0, 1);
			SCADDR(addrs[1]);
			printp();
			return 0;
		default:
			return 1;
		}
	}
}

/* main loop for reading input command. */
void
cmdloop() {
	while (1) {
		arb = read(0, &ibu, MXBFSZ);
		if (!arb) quit();
		if (arb == -1) {
			if (errno == EINTR) continue;
			else die("can not read from stdin.\n");
		}

		ibup = &ibu[0];
		if (parsecmd()) {
			dprintf(2, "?\n");
		}
	}
}

int
main(int argc, char** argv) {
	if (argc == 1) die("specify a file to edit.\n");

	fpth = argv[1];

	initz();

	rdf();

	cmdloop();

	return 0;
}
