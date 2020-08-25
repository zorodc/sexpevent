#include "../dc_lisp.h"

/* STDLIB */
#include <stdio.h>
#include <string.h>
#include <alloca.h>

/*  UNIX  */
#include <sys/time.h>
#include <sys/resource.h>

static unsigned long get_time(void)
{
    struct timeval t;
    struct timezone tzp;
    gettimeofday(&t, &tzp);
    return (unsigned long) (t.tv_sec*1e6 + t.tv_usec);
}

char getch(void* str) { return **(char**)str; }
void prints(void* _, const char* str) { printf("%s", str); }
void printr(void* _, const char* str, dcl_chunk range)
{
	const long size = range.lst - range.fst;
	char* vla_str   = alloca(size+1);

	vla_str[size] = '\0';
	memcpy(vla_str, str + range.fst, (unsigned long)size);
	printf("%s", vla_str);
}

int main(void)
{
	const char* str = /* A test expr. */
		"(defun (nulp lst) (eq lst nil))"
		"(defun (adde e s) (cons   e s))"
		"(defun (hasp e s)"
		"  (if  (nulp s)          f"
		"   (if (equal (car s) e) t"
		"   (has-p   e (cdr s))))) "
		"(defun (main args)"
		"  (setq a-set nil)"
		"  (adde 120 a-set)"
		"  (adde 130 a-set)"
		"  (adde 140 a-set)"
		"  (if (hasp 13 a-set)"
		"    (adde \"unlucky\" a-set)))";

	struct dcl_evaluator lex = dcl_output_lexer;
	lex.ostream =      0; /* No state needed. */
	lex.outputs = prints;
	lex.outputr = printr;

	dcl_lex_ctx lctx = DCL_CTX_INIT;
	lex.charbuf      = str;
	lex.cur_pos      =   0;
	unsigned long st = get_time();

	do dcl_tokenize (&lctx, (void*) &str, getch,
	                (struct dcl_event_vt*)&lex);
	while (++lex.cur_pos, *str++);

	printf("\nTIME ELAPSED: %ld us\n", get_time() - st);
}
