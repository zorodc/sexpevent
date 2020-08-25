#ifndef DCLISP_H
#define DCLISP_H
/*
 * A lisp lexer and interpreter-API  modelled as a mealy machine.
 * It presents its APIs through a SAX-like event-based model.
 * Each pass through the parse switch reads only one character.
 * Thus, it is fully preemtible, and capable of being interruped at any time.
 *
 * As a consequence of the homoiconicity property of lisp/conscells,
 *  this library can be used either for lisp evaluation or simple data parsing.
 *
 * So, this library is nice if one seeks the preemptible parser of a
 *  familiar, lispy, text-based format.
 *
 * This was originally meant to be a lisp implementation, but atm only a lexer.
 */

/* Use 'restrict' in the appropriate fashion, if possible. */
#if     __STDC_VERSION__ >= 199901L
#define RESTRICT   restrict
#elif   __GNUC__
#define RESTRICT __restrict__
#else
#define RESTRICT /* NOTHING */
#endif

#ifndef DCL_EOF
#define DCL_EOF '\0' /* Assume by default that the null-byte signals the end. */
#endif /* DCL_EOF */

#define DCFN static /* For the sake of simplicity, all functions are 'static' */

#define dcl_pop(stk)        (--(stk)->len, (stk)->set >>= 2)
#define dcl_top(stk)        ((stk)->set&4)
#define dcl_has(stk, state) ((stk)->len && (dcl_top(stk) == (state)))
#define dcl_push(stk, st)   ((stk)->set <<= 2, ++(stk)->len, (stk)->set |= (st))
struct dcl_stack
{
	unsigned long set;
	unsigned      len;
};

enum dcl_eval_state { DCL_INTERP = 0, DCL_QUASIQ, DCL_QUOTED };
enum dcl_lexr_state { DCL_NORMAL = 0, DCL_INSTRL = 0x80      };
typedef unsigned long dcl_size;

struct dcl_quote_info
{
	struct dcl_stack    modes_stack;
	enum dcl_eval_state quote_kinds;
	dcl_size            paren_depth;
	dcl_size            depth_table[3]; /* Highest depth seen per eval_state. */
};

#define DCL_CTX_INIT {0, 0, 0, 0, 0, {0, 0}}
typedef struct
{
	dcl_size syml; /* String or symbol. */
	dcl_size numl;
	unsigned opar;
	unsigned cpar;
	unsigned parsestate;
	struct dcl_stack modes; // TODO: Move elsewhere?
//	struct dcl_quote_info quoteinfo;
} dcl_lex_ctx;
// TODO: Rather than passing depths, pass quote info pointer.

struct dcl_event_vt; /* forward-decl: the dcl_event vtable */
typedef char (*dcl_event_getch)(void*);
typedef void (*dcl_event_error     )(struct dcl_event_vt*, const char*);
typedef void (*dcl_event_start_cons)(struct dcl_event_vt*, dcl_size depth);
typedef void (*dcl_event_endof_cons)(struct dcl_event_vt*, dcl_size depth);
typedef void (*dcl_event_chunk_strl)(struct dcl_event_vt*, dcl_size len);
typedef void (*dcl_event_endof_strl)(struct dcl_event_vt*, dcl_size len);
typedef void (*dcl_event_endof_numl)(struct dcl_event_vt*, dcl_size len);
typedef void (*dcl_event_identifier)(struct dcl_event_vt*, dcl_size len);

struct dcl_event_vt
{
	dcl_event_error      error;
	dcl_event_start_cons start_cons;
	dcl_event_endof_cons endof_cons;
	dcl_event_chunk_strl chunk_strl;
	dcl_event_endof_strl endof_strl;
	dcl_event_endof_numl endof_numl;
	dcl_event_identifier identifier;
};

DCFN void dcl_tokenize(dcl_lex_ctx* RESTRICT ctx, void* RESTRICT istrm,
                       dcl_event_getch getc, struct dcl_event_vt* RESTRICT cli)
{
	#define SEPERATOR()                                                   \
	do {                                                                  \
	         if (ctx->syml) cli->identifier(cli, ctx->numl + ctx->syml);  \
	    else if (ctx->numl) cli->endof_numl(cli, ctx->numl);              \
	    ctx->syml = ctx->numl = 0;                                        \
	} while (0)

	switch ((unsigned)getc(istrm) | ctx->parsestate) {
	case '0': case '1': case '2':
	case '3': case '4': case '5':
	case '6': case '7': case '8':
	case '9': ++ctx->numl; break;

	/* Note: Client code determines the meaning of the \. */
	case '\\'|DCL_INSTRL:
		cli->chunk_strl(cli, ctx->syml);
		ctx->syml = 0;
		break;

	case  '"'|DCL_NORMAL:
		if (ctx->syml | ctx->numl) cli->error(cli, "illegal '\"'");
		ctx->parsestate = DCL_INSTRL;
		break;

	case  '"'|DCL_INSTRL:
		cli->endof_strl(cli, ctx->syml);
		ctx->syml = 0;
		ctx->parsestate = DCL_NORMAL;
		break;

	case '(':
		SEPERATOR();
		cli->start_cons(cli, ++ctx->opar - ctx->cpar);
		break;

	case ')':
		SEPERATOR();
		if (ctx->cpar >= ctx->opar) cli->error(cli, "extraneous ')'");
		else {
			cli->endof_cons(cli, ctx->opar - ctx->cpar++);
			dcl_pop(&ctx->modes);
		} break;

	case '\'': dcl_push(&ctx->modes, DCL_QUOTED); break;
	case  '`': dcl_push(&ctx->modes, DCL_QUASIQ); break;
	case  ',':
		if (ctx->syml | ctx->numl) cli->error(cli, "unexpected ','");
		if (dcl_has (&ctx->modes, DCL_QUASIQ))
			dcl_push(&ctx->modes, DCL_INTERP);
		else cli->error(cli, "unquote char ',' found in non-quasiquoted form");
		break;

	case '\n':
	case '\t':
	case  ' ': SEPERATOR(); break;

	case DCL_EOF|DCL_NORMAL: SEPERATOR(); cli->error(cli, "END"); break;
	case DCL_EOF|DCL_INSTRL: SEPERATOR(); cli->error(cli, "EOF"); break;
	default: ++ctx->syml;
	}

	#undef SEPERATOR
}

/*
 * This is an interpreter (API) that would use the above state machine.
 * It is given functions that produce pointers and chunks from a length.
 *
 * Two functions are employed to signal a function/macro/form call.
 * One function signals a call determined at compile time, the other one not.
 *
 * A backend may do whatever it likes with this information.
 * Most notably, it may compile function calls into another form.
 */
struct dcl_eval_vt;
typedef struct { unsigned long fst, lst; } dcl_chunk;
typedef void (*dcl_eval_vfnc)(struct dcl_eval_vt*, unsigned long depth);
typedef void (*dcl_eval_cfnc)(struct dcl_eval_vt*, unsigned long depth);
struct dcl_eval_vt
{
	struct
	dcl_event_vt  base_vt;
	dcl_eval_vfnc rt_call; /* The function called is determined at runtime. */
	dcl_eval_cfnc ct_call; /* The function called is known at compile-time. */
};

/*
 * Debug lexer.
 */
DCFN void dcl_eval__error     (struct dcl_event_vt*, const char* msg);
DCFN void dcl_eval__start_cons(struct dcl_event_vt*, dcl_size depth);
DCFN void dcl_eval__endof_cons(struct dcl_event_vt*, dcl_size depth);
DCFN void dcl_eval__chunk_strl(struct dcl_event_vt*, dcl_size len);
DCFN void dcl_eval__endof_strl(struct dcl_event_vt*, dcl_size len);
DCFN void dcl_eval__endof_numl(struct dcl_event_vt*, dcl_size len);
DCFN void dcl_eval__identifier(struct dcl_event_vt*, dcl_size len);

typedef void (*dcl_output_w_range)(void* stream, const char*, dcl_chunk range);
typedef void (*dcl_output_cstring)(void* stream, const char*);
const static struct dcl_evaluator
{
	struct dcl_eval_vt base_vt;
	void*              ostream;
	dcl_output_w_range outputr;
	dcl_output_cstring outputs;

	const char*         charbuf;
	dcl_size            cur_pos;
	unsigned          saw_oparen : 1;
	unsigned          saw_string : 1;
} dcl_output_lexer = {{{dcl_eval__error, /* TODO: Rename? */
                        dcl_eval__start_cons,
                        dcl_eval__endof_cons,
                        dcl_eval__chunk_strl,
                        dcl_eval__endof_strl,
                        dcl_eval__endof_numl,
                        dcl_eval__identifier}, 0, 0}, 0, 0, 0, 0, 0, 0, 0};

/**
 * Print a syntactical element which is not a paren.
 *
 * A series of strings are provided, along with an offset which is added to
 * the first if the last element seen was an open paren.
 * Resets the 'seen' states.
 */
DCFN void dcl_eval__print_tok(struct dcl_evaluator* evl,
                              const char* tag, int off, dcl_size mid_len)
{
	dcl_chunk range;
	range.fst = evl->cur_pos - mid_len;
	range.lst = evl->cur_pos;

	if (evl->saw_oparen) tag += off;

	evl->saw_string = 0; /* FALSE */
	evl->saw_oparen = 0; /* FALSE */
	evl->outputs(evl->ostream, tag);
	evl->outputr(evl->ostream, evl->charbuf, range);
}

DCFN void dcl_eval__error(struct dcl_event_vt* ev, const char* msg)
{
	struct dcl_evaluator* evl = (struct dcl_evaluator*)ev;
	evl->outputs(evl->ostream, msg);
}

DCFN void dcl_eval__start_cons(struct dcl_event_vt* ev, dcl_size depth)
{
	struct dcl_evaluator* evl = (struct dcl_evaluator*)ev;
	if (depth > 0 && !evl->saw_oparen)
	     evl->outputs(evl->ostream, " (");
	else evl->outputs(evl->ostream,  "(");

	evl->saw_oparen = 1; /* TRUE */
}

DCFN void dcl_eval__endof_cons(struct dcl_event_vt* ev, dcl_size depth)
{
	struct dcl_evaluator* evl = (struct dcl_evaluator*)ev;
	evl->saw_oparen = 0; /* FALSE */
	evl->outputs(evl->ostream, ")");
}

DCFN void dcl_eval__chunk_strl(struct dcl_event_vt* ev, dcl_size len)
{
	struct dcl_evaluator* evl = (struct dcl_evaluator*) ev;
	const char* tag = (evl->saw_string) ? "\0" : " STR:\"";
	dcl_eval__print_tok(evl, tag, 1, len);

	evl->saw_string = 1;
}

DCFN void dcl_eval__endof_strl(struct dcl_event_vt* ev, dcl_size len)
{
	struct dcl_evaluator* evl = (struct dcl_evaluator*)ev;

	if (!evl->saw_string) dcl_eval__chunk_strl(ev, len);
	else                  dcl_eval__print_tok(evl, "\0", 0, len);
	evl->outputs(evl, "\"");
}

DCFN void dcl_eval__endof_numl(struct dcl_event_vt* ev, dcl_size len) {
    dcl_eval__print_tok((struct dcl_evaluator*)ev, " NUM:", 1, len);  }

DCFN void dcl_eval__identifier(struct dcl_event_vt* ev, dcl_size len) {
    dcl_eval__print_tok((struct dcl_evaluator*)ev, " ID:", 1, len);   }

#undef DCFN
#endif /* DCLISP_H */
