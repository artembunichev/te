/*
	te -- text editor.
*/


#include<stdlib.h>
#include<stdarg.h>
#include<fcntl.h>
#include<unistd.h>
#include<string.h>
#include<stdio.h>


/* maximum input/output buffer size. */
#define MXBFSZ 4096
/* the number of lines to expand the lines array. */
#define EXLINS 32
/* by how many character do expand the line. */
#define EXLIN 64


/* target file descriptor. */
int fd;

/* input buffer. */
char ibu[MXBFSZ];
/* actually read bytes from input buffer. */
ssize_t arb;

/* the list of lines in the file. */
char** lins;
/* `lins' size. */
int linssz;
/* `lins' actual length. */
int linsl;
/* line's metadata. */
struct linmtdt {
	/* actual length. */
	int l;
	/* total size. */
	int sz;
};
/* list of line metadatas. the index is the same as in `lins'. */
struct linmtdt* linmtdts;


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
	free(lins);
	free(linmtdts);
	exit(0);
}

/* read the target file into memory. */
void
rdf() {
	/* input buffer iterator. */
	int i;
	/* totally read bytes. */
	ssize_t trb;

	trb = 0;
	lins = smalloc(EXLINS * sizeof(char*));
	linssz = EXLINS;
	linmtdts = smalloc(EXLINS * sizeof(struct linmtdt));
	linmtdts[0] = (struct linmtdt){0, 0};
	linsl = 0;

	while ((arb = read(fd, &ibu, MXBFSZ)) > 0) {
		trb += arb;

		for (i = 0; i < arb; ++i) {
			if (linmtdts[linsl].l == linmtdts[linsl].sz) {
				lins[linsl] = srealloc(lins[linsl], linmtdts[linsl].sz += EXLIN);
			}
	
			lins[linsl][linmtdts[linsl].l] = ibu[i];
			linmtdts[linsl].l++;

			if (ibu[i] == '\n') {
				++linsl;
				if (linsl == linssz) {
					lins = srealloc(lins, (linssz += EXLINS) * sizeof(char*));
					linmtdts = srealloc(linmtdts, linssz * sizeof(struct linmtdt));
					linmtdts[linsl] = (struct linmtdt){0, 0};
				}
			}
		}
	}

	dprintf(1, "%zu\n", trb);
}

/* ordinary print. */
void
printp() {
	int i;

	for (i = 0; i < linsl; ++i) {
		write(1, lins[i], linmtdts[i].l);
	}
}

/* print with line numbers. */
void
printn() {
	int i;

	for (i = 0; i < linsl; ++i) {
		dprintf(1, "%-2d  ", i+1);
		write(1, lins[i], linmtdts[i].l);
	}
}

/* print unambiguously. */
void
printl() {
	/* line index. */
	int i;
	/* character index within the line. */
	int j;

	for (i = 0; i < linsl; ++i) {
		for (j = 0; j < linmtdts[i].l; ++j) {
			char* s;
			/* actual length of printed sequence. */
			int l;

			s = smalloc(2);
			l = 1;

			switch (lins[i][j]) {
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
				s = strcpy(s, &lins[i][j]);
			}

			write(1, s, l);
			free(s);
		}
	}
}

/* main loop for reading command input. */
void
cmdloop() {
	while ((arb = read(0, &ibu, MXBFSZ)) > 0) {
		switch (ibu[0]) {
		case 'p':
			printp();
			break;
		case 'n':
			printn();
			break;
		case 'l':
			printl();
			break;
		case 'q':
			quit();
			break;
		}
	}
}

int
main(int argc, char** argv) {
	if (argc == 1) die("specify a file to edit.\n");

	fd = open(argv[1], O_RDWR);
	if (fd == -1) die("can't open file %s.\n", argv[1]);

	rdf();

	cmdloop();

	return 0;
}
