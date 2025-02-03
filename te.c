/*
	te -- text editor.
*/


#include<stdlib.h>
#include<stdarg.h>
#include<fcntl.h>
#include<unistd.h>
#include<string.h>
#include<stdio.h>
#include<errno.h>


/* maximum input/output buffer size. */
#define MXBFSZ 4096
/* the number of lines to expand the lines array. */
#define EXLNS 32
/* by how many character do expand the line. */
#define EXLIN 64

#define FIRST() if (!first) return 1;
#define LAST() if (*(ibup+1) != '\n') return 1;
#define FANDLAST() FIRST(); LAST();
#define DFLTADDR() {\
	if (!addrn) addrs[addrn++] = caddr;\
	if (addrn == 1) addrs[addrn++] = caddr;\
}


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
	/* the value of a line. */
	char* str;
	/* its actual length. */
	size_t l;
	/* the total size of it. */
	size_t sz;
};
/* list of line nodes. */
struct ln* lns;
/* actual number of lines in the buffer. */
size_t lnsl;
/* size of `lns'. */
size_t lnssz;

/* current line address. */
size_t caddr;

/* specified (input) addresses. */
size_t addrs[2];
/* current number of addresses specified. */
int addrn;

/* if current examined character is first. */
char first;


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

/* quit the editor. */
void
quit() {
	exit(0);
}

/* read the target file into memory. */
void
rdf() {
	/* input buffer iterator. */
	int i;
	/* totally read bytes. */
	ssize_t trb;
	/* last line. */
	struct ln* lln;

	trb = 0;
	lns = smalloc(EXLNS * sizeof(struct ln));
	lns[0] = (struct ln){0, 0, 0};
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
			if (lns[lnsl].l == lns[lnsl].sz) {
				lns[lnsl].str = srealloc(lns[lnsl].str, lns[lnsl].sz += EXLIN);
			}
	
			lns[lnsl].str[lns[lnsl].l] = ibu[i];
			lns[lnsl].l++;

			if (ibu[i] == '\n') {
				++lnsl;
				if (lnsl == lnssz) {
					lns = srealloc(lns, (lnssz += EXLNS) * sizeof(struct ln));
					lns[lnsl] = (struct ln){0, 0, 0};
				}
			}
		}
	}
	if (arb == -1) die("error reading %s.\n", fpth);

	/*
		append newline to the end of file if it doesn't exist.
	*/
	/* this means that a file doesn't have a newline in the end. */
	if (!lnsl) lnsl = 1;
	lln = &(lns[lnsl-1]);
	if (lln->str[lln->l-1] != '\n') {
		if (lns[lnsl-1].l + 1 == lln->sz) {
			lln->str = srealloc(lln->str, (lln->sz += 1));
		}
		lln->str[lln->l] = '\n';
		lln->l++;
		/* since the added newline in the buffer, think it was read too. */
		trb++;

		dprintf(2, "newline appended.\n");
	}

	caddr = lnsl;

	dprintf(1, "%zu\n", trb);

	close(fd);
}

/* ordinary print. */
void
printp() {
	size_t i;
	for (i = addrs[0]-1; i < addrs[1]; ++i) {
		write(1, lns[i].str, lns[i].l);
	}
}

/* print with line numbers. */
void
printn() {
	size_t i;

	for (i = addrs[0]-1; i < addrs[1]; ++i) {
		dprintf(1, "%-2zu  ", i+1);
		write(1, lns[i].str, lns[i].l);
	}
}

/* print unambiguously. */
void
printl() {
	/* line index. */
	size_t i;
	/* character index within the line. */
	size_t j;

	for (i = addrs[0]-1; i < addrs[1]; ++i) {
		for (j = 0; j < lns[i].l; ++j) {
			char* s;
			/* actual length of printed sequence. */
			int l;

			s = smalloc(2);
			l = 1;

			switch (lns[i].str[j]) {
			case '\n':
				strcpy(s, "$\n");
				l = 2;
				break;
			case '\t':
				strcpy(s, "\\t");
				l = 2;
				break;
			case '\\':
				strcpy(s, "\\\\");
				l = 2;
				break;
			default:
				s = strcpy(s, &(lns[i].str[j]));
			}

			write(1, s, l);
			free(s);
		}
	}
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
		tsb += lns[i].l;
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
		awb = write(fd, lns[i].str, lns[i].l);
		if (awb == -1) die("error writing to %s.\n", fpth);
		twb += awb;
	}

	dprintf(1, "%zu\n", twb);

	close(fd);
}

/* parse input and return number >0 if error occurs. */
int
parsecmd() {
	addrs[0] = addrs[1] = addrn = 0;
	first = 1;

	for (;; first = 0) {
		switch(*ibup) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			caddr = addrs[addrn++] = strtol(ibup, &ibup, 10);
			break;
		case ',': {
			long nxaddr;

			if (addrn > 1) return 1;

			*ibup++;
			nxaddr = strtol(ibup, &ibup, 10);

			if (!addrn) addrs[addrn++] = caddr;
			caddr = addrs[addrn++] = errno ? lnsl : nxaddr;
			break;
		}
		case 'f':
			FANDLAST();
			pfpth();
			return 0;
		case 'b':
			FANDLAST();
			pbyt();
			return 0;
		case '=':
			FANDLAST();
			plastnum();
			return 0;
		case 'p':
			LAST();
			DFLTADDR();
			printp();
			return 0;
		case 'n':
			LAST();
			DFLTADDR();
			printn();
			return 0;
		case 'l':
			LAST();
			DFLTADDR();
			printl();
			return 0;
		case 'w':
			FANDLAST();
			wrf();
			return 0;
		case 'q':
			FANDLAST();
			quit();
			return 0;
		case '\n':
			addrs[0] = addrs[1] = caddr;
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
		if (arb == -1) die("can not read from stdin.\n");

		ibup = &ibu[0];
		if (parsecmd()) {
			dprintf(1, "?\n");
		}
	}
	if (arb == -1) die("error reading stdin.\n");
}

int
main(int argc, char** argv) {
	if (argc == 1) die("specify a file to edit.\n");

	fpth = argv[1];

	rdf();

	cmdloop();

	return 0;
}
