#include "mpc.h" 

// Define a macro to control errors(error handling)
#define LASSERT(args, cond, fmt, ...) \
				if (!(cond)) { \
					lval* err = lval_err(fmt, ##__VA_ARGS__); lval_del(args); return err; }

#define LASSERT_TYPE(func, args, index, expect)	\
				LASSERT(args, args->cell[index]->type == expect, \
						"Function '%s' passed incorrect type for argument %d. Got %s, Expected %s.", \
						func, index, ltype_name(args->cell[index]->type), ltype_name(expect))

#define LASSERT_NUM(func, args, num) \
				LASSERT(args, args->count == num, \
						"Function '%s' passed incorrect number of arguments. Got %d, Expected %d.", \
						func, args->count, num)

#define LASSERT_NOT_EMPTY(func, args, index) \
				LASSERT(args, args->cell[index]->count != 0, \
						"Function '%s' passed {} for argument %d.", func, index);


// If we are compiling on Windows
#if defined _WIN64 || _WIN32

static char buffer[2048];

// Fake readline function
char* readline(char* prompt)
{
	fputs(prompt, stdout);
	char* temp = fgets(buffer, 2048, stdin);
	char* cpy = malloc(strlen(buffer) + 1);
	strcpy_s(cpy, (strlen(buffer) + 1), buffer);
	temp = NULL;
	cpy[strlen(cpy) - 1] = '\0';
	return cpy;
}

// Fake add_history function
void add_history(char* unused) {}

// Otherwise include these
#else
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define strcpy_s(a, b, c) (strcpy((a), (c)))
#include <editline/readline.h>
#include <editline/history.h>
#endif

// Parsers
mpc_parser_t* Number;
mpc_parser_t* Operator;
mpc_parser_t* String;
mpc_parser_t* Comment;
mpc_parser_t* Sexpr;
mpc_parser_t* Qexpr;
mpc_parser_t* Expr;
mpc_parser_t* Lispi;

// Forward Declaration(Prototypes)
struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;


// Create enumerations of possible lval struct types
enum { LVAL_ERR, LVAL_NUM, LVAL_OPR, LVAL_STR,
	LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR };

typedef lval*(*lbuiltin)(lenv*, lval*);


// Declare a new struct LVAL
struct lval {
	int type;
	long num;

	// Error and Symbol(operator) types have some string data
	char* err;
	char* opr;
	char* str;

	// Function
	lbuiltin builtin;
	lenv* env;
	lval* formals;
	lval* body;

	// Count and Pointer to a list of "lval*"
	int count;
	lval** cell;
};


// Declare a new struct lenv
struct lenv {
	lenv* par;
	int count;
	char** oprs;
	lval** vals;
};

// Prototypes necessary
void lval_print(lval* v);
lval* lval_add(lval* v, lval* x);
lval* builtin_op(lenv* e, lval* a, char* op);
lval* lval_pop(lval* v, int i);
lval* lval_take(lval* v, int i);
lval* lval_eval(lenv* e, lval* v);
lval* lval_join(lval* x, lval* y);
void lval_del(lval* v);
lval* lval_err(char* fmt, ...);
lval* lval_copy(lval* v);
char* ltype_name(int t);
lval* builtin_def(lenv* e, lval* a);
lval* lval_call(lenv* e, lval* f, lval* a);
lval* builtin_var(lenv* e, lval* a, char* func);
lval* builtin_load(lenv* e, lval* a);
lval* builtin_error(lenv* e, lval* a);
lval* builtin_print(lenv* e, lval* a);


// Creates a new lenv
lenv* lenv_new(void)
{
	lenv* e = malloc(sizeof(lenv));
	e->par = NULL;
	e->count = 0;
	e->oprs = NULL;
	e->vals = NULL;
	return e;
}

// Delete a lenv
void lenv_del(lenv* e)
{
	for (int i = 0; i < e->count; ++i)
	{
		free(e->oprs[i]);
		lval_del(e->vals[i]);
	}

	free(e->oprs);
	free(e->vals);
	free(e);
}

// Find the correct environment 
lval* lenv_get(lenv* e, lval* k)
{
	// iterate over all the items in the environment
	for (int i = 0; i < e->count; ++i)
	{
		// Check if the input string matches any operator string
		if (strcmp(e->oprs[i], k->opr) == 0)
		{
			// Return a copy of the value
			return lval_copy(e->vals[i]);
		}
	}

	// If no operator check in parent otherwise error
	if (e->par)
	{
		return lenv_get(e->par, k);
	}
	else
	{
		// If no symbol found
		return lval_err("Unbound operator '%s'!", k->opr);
	}
	
}

// Puts a new variable provided by the user at the old position
void lenv_put(lenv* e, lval* k, lval* v)
{
	// Iterate over all items in the environment
	for (int i = 0; i < e->count; ++i)
	{
		// If a variable is found delete that
		if (strcmp(e->oprs[i], k->opr) == 0)
		{
			lval_del(e->vals[i]);
			e->vals[i] = lval_copy(v);
			return;
		}
	}

	// If no existing  entry were found allocate space for new entry
	e->count++;
	e->vals = realloc(e->vals, sizeof(lval*) * e->count);
	e->oprs = realloc(e->oprs, sizeof(char*) * e->count);

	// Copy the contents of lval and operator strings to a new location.
	e->vals[e->count - 1] = lval_copy(v);
	e->oprs[e->count - 1] = malloc(strlen(k->opr) + 1);
	strcpy_s(e->oprs[e->count - 1], (strlen(k->opr) + 1), k->opr);
}

// Function for copying environments
lenv* lenv_copy(lenv* e)
{
	lenv* n = malloc(sizeof(lenv));
	n->par = e->par;
	n->count = e->count;
	n->oprs = malloc(sizeof(char*) * n->count);
	n->vals = malloc(sizeof(lval*) * n->count);
	for (int i = 0; i < e->count; ++i)
	{
		n->oprs[i] = malloc(strlen(e->oprs[i]) + 1);
		strcpy_s(n->oprs[i], (strlen(e->oprs[i])+ 1), e->oprs[i]);
		n->vals[i] = lval_copy(e->vals[i]);
	}

	return n;
}

// Declare a variable in the outermost(global) context
void lenv_def(lenv* e, lval* k, lval* v)
{
	// Iterate till e has no parent
	while (e->par)
	{
		e = e->par;
	}
	lenv_put(e, k, v);
}



// Declare a new number type lval
lval* lval_num(long x)
{
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_NUM;
	v->num = x;
	return v;
}

// Create a new error type lval
lval* lval_err(char* fmt, ...)
{
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_ERR;

	// Create a va list and initialize it
	va_list va;
	va_start(va, fmt);

	// Allocate 512 bytes of space
	v->err = malloc(512);
	
	// printf the error string with a maximum of 511 bytes
	vsnprintf(v->err, 511, fmt, va);
	

	// Reallocate to number of bytes actually used
	v->err = realloc(v->err, strlen(v->err) + 1);

	// Clean up our va list
	va_end(va);	

	return v;
}


// Construct a pointer to a new operator lval
lval* lval_opr(char* s)
{
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_OPR;
	v->opr = malloc(strlen(s) + 1);
	strcpy_s(v->opr, (strlen(s) + 1), s);
	return v;
}

// Construct a pointer to chars(string)
lval* lval_str(char* s)
{
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_STR;
	v->str = malloc(strlen(s) + 1);
	strcpy_s(v->str, (strlen(s) + 1), s);
	return v;
}


// A pointer to a new empty Sexpr lavl
lval* lval_sexpr(void)
{
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_SEXPR;
	v->count = 0;
	v->cell = NULL;
	return v;
}

// A pointer to a new empty Sexpr lavl
lval* lval_qexpr(void)
{
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_QEXPR;
	v->count = 0;
	v->cell = NULL;
	return v;
}

// Create new function
lval* lval_builtin(lbuiltin func)
{
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_FUN;
	v->builtin = func;
	return v;
}



// Function to delete(free) lval* to avoid memory leaks
void lval_del(lval* v)
{
	switch (v->type)
	{
	case LVAL_NUM:
		break;								// Do nothing for numbers
	case LVAL_FUN:
		if (!v->builtin)
		{
			lenv_del(v->env);
			lval_del(v->formals);
			lval_del(v->body);
		}
		break;
	// For error and operator type free them
	case LVAL_ERR:
		free(v->err);
		break;
	case LVAL_OPR:
		free(v->opr);
		break;
	case LVAL_STR:
		free(v->str);
		break;
	// For Sexpr or Qexpr delete(free) all the elements inside
	case LVAL_QEXPR:
	case LVAL_SEXPR:
		for (int i = 0; i < v->count; ++i)
		{
			lval_del(v->cell[i]);			// Free every pointer in the cell
		}
		// Also free the cell containing the pointers
		free(v->cell);
		break;
	}

	// Free the lvalue struct itself
	free(v);
}

// Copy lvals
lval* lval_copy(lval* v)
{
	lval* x = malloc(sizeof(lval));
	x->type = v->type;

	// Copy functions and numbers directly
	switch (v->type)
	{
	case LVAL_FUN:
		if (v->builtin)
		{
			x->builtin = v->builtin;
		}
		else
		{
			x->builtin = NULL;
			x->env = lenv_copy(v->env);
			x->formals = lval_copy(v->formals);
			x->body = lval_copy(v->body);
		}
		break;
	case LVAL_NUM:
		x->num = v->num;
		break;

	// Copy strings using malloc and strcpy
	case LVAL_ERR:
		x->err = malloc(strlen(v->err) + 1);
		strcpy_s(x->err, (strlen(v->err) + 1), v->err);
		break;
	case LVAL_OPR:
		x->opr = malloc(strlen(v->opr) + 1);
		strcpy_s(x->opr, (strlen(v->opr) + 1), v->opr);
		break;
	case LVAL_STR:
		x->str = malloc(strlen(v->str) + 1);
		strcpy_s(x->str, (strlen(v->str) + 1), v->str);
		break;
	case LVAL_SEXPR:
	case LVAL_QEXPR:
		x->count = v->count;
		x->cell = malloc(sizeof(lval*) * x->count);
		for (int i = 0; i < x->count; ++i)
		{
			x->cell[i] = lval_copy(v->cell[i]);
		}
		break;
	}

	return x;
}


// add add two lval* increment the count realloc the cell, point the cell
lval* lval_add(lval* v, lval* x)
{
	v->count++;
	v->cell = realloc(v->cell, sizeof(lval*) * v->count);
	v->cell[v->count - 1] = x;
	return v;
}


// lval* pops out the value at index i
lval* lval_pop(lval* v, int i)
{
	// Find the element at i
	lval* x = v->cell[i];

	// Shift memory after the element at i over the top
	memmove(&v->cell[i],
		&v->cell[i + 1], sizeof(lval*) * (v->count - i - 1));

	// Decrease the count of the items in the list
	v->count--;

	// Reallocate the memory used
	v->cell = realloc(v->cell, sizeof(lval*) * (v->count));
	return x;
}

// deletes(takes) the element and deletes the rest of the list
lval* lval_take(lval* v, int i)
{
	lval* x = lval_pop(v, i);
	lval_del(v);
	return x;
}

// A constructor for user defined lval functions
lval* lval_lambda(lval* formals, lval* body)
{
	lval* v = malloc(sizeof(lval));
	v->type = LVAL_FUN;

	v->builtin = NULL;
	v->env = lenv_new();

	v->formals = formals;
	v->body = body;
	return v;
}

// Add a builtin for our lambda function
lval* builtin_lambda(lenv* e, lval* a)
{
	// Check parameters to make sure only Q-Expressions
	LASSERT_NUM("\\", a, 2);
	LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
	LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

	// Check first Q-Expression contains only operators
	for (int i = 0; i < a->cell[0]->count; ++i)
	{
		LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_OPR),
			"Cannot define non-operator! Got %s, Expected %s.", 
			ltype_name(a->cell[0]->cell[i]->type), ltype_name(LVAL_OPR));
	}

	// Pop the first two arguments and pass them to lval_lambda
	lval* formals = lval_pop(a, 0);
	lval* body = lval_pop(a, 0);
	lval_del(a);

	return lval_lambda(formals, body);
}

// Print out the Sub expressions line by line
void lval_expr_print(lval* v, char open, char close)
{
	putchar(open);
	for (int i = 0; i < v->count; ++i)
	{

		// Print value contained within
		lval_print(v->cell[i]);

		// Avoid printing trailing space if last element
		if (i != (v->count - 1))
		{
			putchar(' ');
		}
	}

	putchar(close);
}

// Print a string unescaped
void lval_print_str(lval* v)
{
	// Make a copy of the string
	char* escaped = malloc(strlen(v->str) + 1);
	strcpy_s(escaped, (strlen(v->str) + 1), v->str);
	
	// Pass it through the escape function
	escaped = mpcf_escape(escaped);

	// Print it between ""
	printf("\"%s\"", escaped);
	
	// Free the copied string
	free(escaped);
}


// Print an "lval"
void lval_print(lval* v)
{
	switch (v->type)
	{
	case LVAL_FUN:
		if (v->builtin)
		{
			printf("<builtin>");
		}
		else
		{
			printf("\\ ");
			lval_print(v->formals);
			putchar(' ');
			lval_print(v->body);
			putchar(')');
		}
		break;
	case LVAL_NUM:
		printf("%li", v->num);
		break;
	case LVAL_ERR:
		printf("Error: %s", v->err);
		break;
	case LVAL_OPR:
		printf("%s", v->opr);
		break;
	case LVAL_STR:
		lval_print_str(v);
		break;
	case LVAL_SEXPR:
		lval_expr_print(v, '(', ')');
		break;
	case LVAL_QEXPR:
		lval_expr_print(v, '{', '}');
		break;
	}
}


// Print an "lval" followed by a newline character.
void lval_println(lval* v)
{
	lval_print(v);
	putchar('\n');
}

// Check if two values for equality
int lval_eq(lval* x, lval* y)
{
	// Different type are always equal
	if (x->type != y->type)
	{
		return 0;
	}
	
	// Compare based upton type
	switch (x->type)
	{
	case LVAL_NUM:
		return (x->num == y->num);
	
	// Compare string values
	case LVAL_ERR:
		return (strcmp(x->err, y->err) == 0);
	case LVAL_OPR:
		return (strcmp(x->opr, y->opr) == 0);
	case LVAL_STR:
		return (strcmp(x->str, y->str) == 0);

	// If builtin compare, otherwise compare formals and body
	case LVAL_FUN:
		if (x->builtin || y->builtin)
		{
			return x->builtin == y->builtin;
		}
		else
		{
			return lval_eq(x->formals, y->formals) && lval_eq(x->body, y->body);
		}

	// If list compare every individual element
	case LVAL_QEXPR:
	case LVAL_SEXPR:
		if (x->count != y->count)
		{
			return 0;
		}
		for (int i = 0; i < x->count; ++i)
		{
			// If any element not equal then whole list is not equal
			if (!lval_eq(x->cell[i], y->cell[i]))
			{
				return 0;
			}
		}
		
		// Otherwise must be equal
		return 1;
		break;	
	}

	return 0;
}

// Generate builtins for logical operators like and, or and not!
lval* builtin_logop(lenv* e, lval* a, char* op)
{
	if ((strcmp(op, "not") == 0) && (a->count == 2))
	{
		LASSERT_NUM(op, a, 1);
	}
	if ((strcmp(op, "not") != 0) && (a->count != 2))
	{
		LASSERT_NUM(op, a, 2);
	}


	int r = 0;
	if (strcmp(op, "and") == 0)
	{
		r = (a->cell[0]->num && a->cell[1]->num);
	}
	if (strcmp(op, "or") == 0)
	{
		r = (a->cell[0]->num || a->cell[1]->num);
	}
	if (strcmp(op, "not") == 0)
	{
		r = !(a->cell[0]->num);
	}

	lval_del(a);
	return lval_num(r);
}


// Generate builtins for comparisons like == and !=
lval* builtin_cmp(lenv* e, lval* a, char* op)
{
	LASSERT_NUM(op, a, 2);
	int r = 0;
	if (strcmp(op, "==") == 0)
	{
		r = lval_eq(a->cell[0], a->cell[1]);
	}
	if (strcmp(op, "!=") == 0)
	{
		r = !lval_eq(a->cell[0], a->cell[1]);
	}

	lval_del(a);
	return lval_num(r);
}

// Generate builtins for oders such as >, <, >= and <=
lval* builtin_ord(lenv* e, lval* a, char* op)
{
	LASSERT_NUM(op, a, 2);
	LASSERT_TYPE(op, a, 0, LVAL_NUM);
	LASSERT_TYPE(op, a, 1, LVAL_NUM);

	int r = 0;
	if (strcmp(op, ">") == 0)
	{
		r = (a->cell[0]->num > a->cell[1]->num);
	}
	if (strcmp(op, "<") == 0)
	{
		r = (a->cell[0]->num < a->cell[1]->num);
	}
	if (strcmp(op, ">=") == 0)
	{
		r = (a->cell[0]->num >= a->cell[1]->num);
	}
	if (strcmp(op, "<=") == 0)
	{
		r = (a->cell[0]->num <= a->cell[1]->num);
	}

	lval_del(a);
	return lval_num(r);
}

// Generate a builtin for if
lval* builtin_if(lenv* e, lval* a)
{
	LASSERT_NUM("if", a, 3);
	LASSERT_TYPE("if", a, 0, LVAL_NUM);
	LASSERT_TYPE("if", a, 1, LVAL_QEXPR);
	LASSERT_TYPE("if", a, 2, LVAL_QEXPR);

	// Mark both Expressions evaluable
	lval* x;
	a->cell[1]->type = LVAL_SEXPR;
	a->cell[2]->type = LVAL_SEXPR;

	if (a->cell[0]->num)
	{
		// If condition is true evaluate first expression
		x = lval_eval(e, lval_pop(a, 1));
	}
	else
	{
		// Otherwise evaluate second expression
		x = lval_eval(e, lval_pop(a, 2));
	}

	// Delete the argument list and return
	lval_del(a);
	return x;
}


// Use operator string to see which operator to perform
lval* builtin_op(lenv* e, lval* a, char* op)
{
	// Ensure all arguments are numbers
	for (int i = 0; i < a->count; ++i)
	{
		LASSERT_TYPE(op, a, i, LVAL_NUM);
	}

	// Pop the first element
	lval* x = lval_pop(a, 0);

	// If no arguments and subtract then perform unary negation
	if ((strcmp(op, "-") == 0) && a->count == 0)
	{
		x->num = -x->num;
	}

	// While there are still elements remaining
	while (a->count > 0)
	{
		// Pop the next element
		lval* y = lval_pop(a, 0);


		if (strcmp(op, "+") == 0)
		{
			x->num += y->num;
		}
		if (strcmp(op, "-") == 0)
		{
			x->num -= y->num;
		}
		if (strcmp(op, "*") == 0)
		{
			x->num *= y->num;
		}
		if (strcmp(op, "/") == 0)
		{
			if (y->num == 0)
			{
				lval_del(x);
				lval_del(y);
				x = lval_err("Division By Zero!");
				break;
			}
			x->num /= y->num;
		}
		if (strcmp(op, "%") == 0)
		{
			x->num %= y->num;
		}
		if (strcmp(op, "^") == 0)
		{
			x->num = (long)pow(x->num, y->num);
		}
		if (strcmp(op, "min") == 0)
		{
			x->num = min(x->num, y->num);
		}
		if (strcmp(op, "max") == 0)
		{
			x->num = max(x->num, y->num);
		}

		lval_del(y);
	}

	// Delete the input expression and return the result
	lval_del(a);
	return x;
}

// builtin mathematical functions
lval* builtin_add(lenv* e, lval* a)
{
	return builtin_op(e, a, "+");
}

lval* builtin_sub(lenv* e, lval* a)
{
	return builtin_op(e, a, "-");
}

lval* builtin_mul(lenv* e, lval* a)
{
	return builtin_op(e, a, "*");
}

lval* builtin_div(lenv* e, lval* a)
{
	return builtin_op(e, a, "/");
}

lval* builtin_mod(lenv* e, lval* a)
{
	return builtin_op(e, a, "%");
}

lval* builtin_pow(lenv* e, lval* a)
{
	return builtin_op(e, a, "^");
}

lval* builtin_min(lenv* e, lval* a)
{
	return builtin_op(e, a, "min");
}

lval* builtin_max(lenv* e, lval* a)
{
	return builtin_op(e, a, "max");
}

lval* builtin_def(lenv* e, lval* a)
{
	return builtin_var(e, a, "def");
}

lval* builtin_put(lenv* e, lval* a)
{
	return builtin_var(e, a, "=");
}

lval* builtin_gt(lenv* e, lval* a)
{
	return builtin_ord(e, a, ">");
}

lval* builtin_lt(lenv* e, lval* a)
{
	return builtin_ord(e, a, "<");
}

lval* builtin_ge(lenv* e, lval* a)
{
	return builtin_ord(e, a, ">=");
}

lval* builtin_le(lenv* e, lval* a)
{
	return builtin_ord(e, a, "<=");
}

lval* builtin_eq(lenv* e, lval* a)
{
	return builtin_cmp(e, a, "==");
}

lval* builtin_ne(lenv* e, lval* a)
{
	return builtin_cmp(e, a, "!=");
}

lval* builtin_and(lenv* e, lval* a)
{
	return builtin_logop(e, a, "and");
}

lval* builtin_or(lenv* e, lval* a)
{
	return builtin_logop(e, a, "or");
}

lval* builtin_not(lenv* e, lval* a)
{
	return builtin_logop(e, a, "not");
}


// Return a string of the type expected
char* ltype_name(int t)
{
	switch (t)
	{
	case LVAL_FUN:
		return "Function";
	case LVAL_NUM:
		return "Number";
	case LVAL_ERR:
		return "Error";
	case LVAL_OPR:
		return "Operator";
	case LVAL_STR:
		return "String";
	case LVAL_SEXPR:
		return "S-Expression";
	case LVAL_QEXPR:
		return "Q-Expression";
	default:
		return "Unknown";
	}
}

// return an lval* of head from an Qexpr
lval* builtin_head(lenv* e, lval* a)
{
	// Check possible error conditions
	LASSERT_NUM("head", a, 1);

	LASSERT_TYPE("head", a, 0, LVAL_QEXPR);

	LASSERT_NOT_EMPTY("head", a, 0);

	// Otherwise take first argument
	lval* v = lval_take(a, 0);

	// Delete all elements that are not head and return
	while (v->count > 1)
	{
		lval_del(lval_pop(v, 1));
	}

	return v;
}

// return an lval* of head from an Qexpr
lval* builtin_tail(lenv* e, lval* a)
{
	// Check possible error conditions
	LASSERT_NUM("tail", a, 1);

	LASSERT_TYPE("tail", a, 0, LVAL_QEXPR);

	LASSERT_NOT_EMPTY("tail", a, 0);

	// Otherwise take first argument
	lval* v = lval_take(a, 0);

	// Delete first element and return
	lval_del(lval_pop(v, 0));

	return v;
}

// Converts S-Expression into Q-Expression
lval* builtin_list(lenv* e, lval* a)
{
	a->type = LVAL_QEXPR;
	return a;
}

// Converts Q-Expression into S-Expression and evaluates using lval_eval()
lval* builtin_eval(lenv* e, lval* a)
{
	LASSERT_NUM("eval", a, 1);

	LASSERT_TYPE("eval", a, 0, LVAL_QEXPR);


	lval* x = lval_take(a, 0);
	x->type = LVAL_SEXPR;
	return lval_eval(e, x);
}

// Joins Q-Expressions and returns a Q-Expression
lval* builtin_join(lenv* e, lval* a)
{
	for (int i = 0; i < a->count; ++i)
	{
		LASSERT_TYPE("join", a, i, LVAL_QEXPR);
	}

	lval* x = lval_pop(a, 0);

	while (a->count)
	{
		lval* y = lval_pop(a, 0);
		x = lval_join(x, y);
	}

	lval_del(a);
	return x;
}

// Joins 2 Q-Exressions to one Q-Expression
lval* lval_join(lval* x, lval* y)
{
	// For each cell in 'y' add it to 'x'
	for (int i = 0; i < y->count; ++i)
	{
		x = lval_add(x, y->cell[i]);
	}

	// Delete the empty 'y' and return 'x'
	free(y->cell);
	free(y);
	return x;
}

// Evaluate a Sexpr and return a lval*
lval* lval_eval_sexpr(lenv* e, lval* v)
{

	// Evaluate children
	for (int i = 0; i < v->count; ++i)
	{
		v->cell[i] = lval_eval(e, v->cell[i]);
	}

	// Error checking
	for (int i = 0; i < v->count; ++i)
	{
		if (v->cell[i]->type == LVAL_ERR)
		{
			return lval_take(v, i);
		}
	}

	// Empty Expression
	if (v->count == 0)
	{
		return v;
	}

	// Single Expression
	if (v->count == 1)
	{
		return lval_take(v, 0);
	}

	// Ensure first element is symbol
	lval* f = lval_pop(v, 0);
	if (f->type != LVAL_FUN)
	{
		lval* err = lval_err("S-Expression starts with incorrect type! Got %s, Expected %s.", 
			ltype_name(f->type), ltype_name(LVAL_FUN));
		lval_del(f);
		lval_del(v);
		return err;
	}

	// Call builtin with operator
	lval* result = lval_call(e, f, v);
	lval_del(f);
	return result;
}

// Add a new function for every operator/function
void lenv_add_builtin(lenv* e, char* name, lbuiltin func)
{
	lval* k = lval_opr(name);
	lval* v = lval_builtin(func);
	lenv_put(e, k, v);
	lval_del(k);
	lval_del(v);
}

// Add the new builtins
void lenv_add_builtins(lenv* e)
{
	// List functions
	lenv_add_builtin(e, "list", builtin_list);
	lenv_add_builtin(e, "head", builtin_head);
	lenv_add_builtin(e, "tail", builtin_tail);
	lenv_add_builtin(e, "eval", builtin_eval);
	lenv_add_builtin(e, "join", builtin_join);

	// Mathematical functions
	lenv_add_builtin(e, "+", builtin_add);
	lenv_add_builtin(e, "-", builtin_sub);
	lenv_add_builtin(e, "*", builtin_mul);
	lenv_add_builtin(e, "/", builtin_div);
	lenv_add_builtin(e, "%", builtin_mod);
	lenv_add_builtin(e, "^", builtin_pow);
	lenv_add_builtin(e, "min", builtin_min);
	lenv_add_builtin(e, "max", builtin_max);

	// Variable functions
	lenv_add_builtin(e, "\\", builtin_lambda);
	lenv_add_builtin(e, "def", builtin_def);
	lenv_add_builtin(e, "=", builtin_put);

	// Comparison functions
	lenv_add_builtin(e, "if", builtin_if);
	lenv_add_builtin(e, "==", builtin_eq);
	lenv_add_builtin(e, "!=", builtin_ne);
	lenv_add_builtin(e, ">", builtin_gt);
	lenv_add_builtin(e, "<", builtin_lt);
	lenv_add_builtin(e, ">=", builtin_ge);
	lenv_add_builtin(e, "<=", builtin_le);

	// Logical Operators
	lenv_add_builtin(e, "and", builtin_and);
	lenv_add_builtin(e, "or", builtin_or);
	lenv_add_builtin(e, "not", builtin_not);

	// String Functions
	lenv_add_builtin(e, "load", builtin_load);
	lenv_add_builtin(e, "error", builtin_error);
	lenv_add_builtin(e, "print", builtin_print);

}


// Define functions(builtin)
lval* builtin_var(lenv* e, lval* a, char* func)
{
	LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

	// First argument is operator list
	lval* oprs = a->cell[0];

	// Ensure all elements of the first list are sybmols
	for (int i = 0; i < oprs->count; ++i)
	{
		LASSERT(a, (oprs->cell[i]->type == LVAL_OPR), 
			"Function 'def' cannot define non-operator! Got %s, Expected %s", 
			ltype_name(oprs->cell[i]->type), ltype_name(LVAL_OPR));
	}

	// Check correct number of symbols and values
	LASSERT(a, (oprs->count == a->count - 1), 
		"Function 'def' passed too many arguments for operators! Got %d, Expected %d.", 
		oprs->count, a->count - 1);
		
	// Assign copies of values to operators
	for (int i = 0; i < oprs->count; ++i)
	{
		if (strcmp(func, "def") == 0)
		{
			lenv_def(e, oprs->cell[i], a->cell[i + 1]);
		}

		if (strcmp(func, "=") == 0)
		{
			lenv_put(e, oprs->cell[i], a->cell[i + 1]);
		}
	}

	lval_del(a);
	return lval_sexpr();
}

// Print the lines provided from a file
lval* builtin_print(lenv* e, lval* a)
{
	// Print each argument followed by a space
	for (int i = 0; i < a->count; ++i)
	{
		lval_print(a->cell[i]);
		putchar(' ');
	}
	
	// Print a newline and delete arguments
	putchar('\n');
	lval_del(a);	

	return lval_sexpr();
}

// Print an error in a string provided by the user
lval* builtin_error(lenv* e, lval* a)
{
	LASSERT_NUM("error", a, 1);
	LASSERT_TYPE("error", a, 0, LVAL_STR);

	// Construct error from first argument
	lval* err = lval_err(a->cell[0]->str);

	// Delete arguments and return
	lval_del(a);
	return err;
}

// Call user defined functions
lval* lval_call(lenv* e, lval* f, lval* a)
{
	// If builtin then simply call that function
	if (f->builtin)
	{
		return f->builtin(e, a);
	}

	// Save the argument counts
	int given = a->count;
	int total = f->formals->count;

	// While there are arguments to be processed
	while (a->count)
	{
		// If we ran out of formal arguments to bind
		if (f->formals->count == 0)
		{
			lval_del(a); 
			return lval_err("Function passed too many arguments! Got %d, Expected %d.", given, total);
		}
		
		// Pop the first operator from the formals
		lval* opr = lval_pop(f->formals, 0);

		// Special case to deal with '&'
		if (strcmp(opr->opr, "&") == 0)
		{
			// Ensure '&' is followed by another operator
			if (f->formals->count != 1)
			{
				lval_del(a);
				return lval_err("Function formal invalid! Operator '&' not followed by a single operator.");
			}

			// Next formal should be bound to remaining arguments
			lval* nopr = lval_pop(f->formals, 0);
			lenv_put(f->env, nopr, builtin_list(e, a));
			lval_del(opr);
			lval_del(nopr);
			break;
		}

		// Pop the next argument from the list
		lval* val = lval_pop(a, 0);

		// Bind a copy into the function's environment
		lenv_put(f->env, opr, val);

		// Delete operator and value
		lval_del(opr);
		lval_del(val);

	}

	// Argument list is now bound so clean it
	lval_del(a);

	// If '&' remain in formal list bind to empty list
	if (f->formals->count > 0 && strcmp(f->formals->cell[0]->opr, "&") == 0)
	{
		// Check to ensure that '&' is not passed invalidly
		if (f->formals->count != 2)
		{
			return lval_err("Function format invalid! Symbol '&' no followed by a single symbol.");
		}

		// Pop and delete '&' symbol
		lval_del(lval_pop(f->formals, 0));

		// Pop next operator and create empty list
		lval* opr = lval_pop(f->formals, 0);
		lval* val = lval_qexpr();

		// Bind to environment and delete
		lenv_put(f->env, opr, val);
		lval_del(opr);
		lval_del(val);
	}

	// If all formals have been bound evaluate
	if (f->formals->count == 0)
	{
		// Set environment parent to evaluation environment
		f->env->par = e;

		// Evaluate and return
		return builtin_eval(f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
	}
	else
	{
		// Otherwise return partially evaluated function
		return lval_copy(f);
	}

}

// lval* evaluator
lval* lval_eval(lenv* e, lval* v)
{
	if (v->type == LVAL_OPR)
	{
		lval* x = lenv_get(e, v);
		lval_del(v);
		return x;
	}

	if (v->type == LVAL_SEXPR)
	{
		return lval_eval_sexpr(e, v);
	}
	
	return v;
}

// Reads a string and returns it unescaped
lval* lval_read_str(mpc_ast_t* t)
{
	// Cut off the final quote characters
	t->contents[strlen(t->contents) - 1] = '\0';
	
	// Copy the string missing out the first quote character
	char* unescaped = malloc(strlen(t->contents + 1) + 1);
	strcpy_s(unescaped, (strlen(t->contents + 1) + 1), (t->contents + 1));
	
	// Pass through the unescape function
	unescaped = mpcf_unescape(unescaped);
	
	// Construct a new lval using the string
	lval* str = lval_str(unescaped);
	
	// Free the string and retuen
	free(unescaped);
	return str;
}


// read a number from the AST output to lval*
lval* lval_read_num(mpc_ast_t* t)
{
	errno = 0;
	long x = strtol(t->contents, NULL, 10);
	return errno != ERANGE ? lval_num(x) : lval_err("Invalid number.");
}

// read from the AST output to lval*
lval* lval_read(mpc_ast_t* t)
{
	// If operators or numbers return conversion to that type
	if (strstr(t->tag, "number"))
	{
		return lval_read_num(t);
	}
	if (strstr(t->tag, "string"))
	{
		return lval_read_str(t);
	}
	if (strstr(t->tag, "operator"))
	{
		return lval_opr(t->contents);
	}

	// If root(>) or sexpr then create empty list
	lval* x = NULL;
	if (strcmp(t->tag, ">") == 0)
	{
		x = lval_sexpr();
	}
	if (strstr(t->tag, "sexpr"))
	{
		x = lval_sexpr();
	}
	if (strstr(t->tag, "qexpr"))
	{
		x = lval_qexpr();
	}

	// Fill in this list with any valid expression contained within
	for (int i = 0; i < t->children_num; ++i)
	{
		if (strcmp(t->children[i]->contents, "(") == 0)
		{
			continue;
		}
		if (strcmp(t->children[i]->contents, ")") == 0)
		{
			continue;
		}
		if (strcmp(t->children[i]->contents, "{") == 0)
		{
			continue;
		}
		if (strcmp(t->children[i]->contents, "}") == 0)
		{
			continue;
		}
		if (strcmp(t->children[i]->tag, "regex") == 0)
		{
			continue;
		}
		if (strstr(t->children[i]->tag, "comment"))
		{
			continue;
		}
		x = lval_add(x, lval_read(t->children[i]));

	}

	return x;
}

// Loads a file through a string provided
lval* builtin_load(lenv* e, lval* a)
{
	LASSERT_NUM("load", a, 1);
	LASSERT_TYPE("load", a, 0, LVAL_STR);

	// Parse a file given by string
	mpc_result_t r;
	if (mpc_parse_contents(a->cell[0]->str, Lispi, &r))
	{
		// Read contents
		lval* expr = lval_read(r.output);
		mpc_ast_delete(r.output);

		// Evaluate each expression
		while (expr->count)
		{
			lval* x = lval_eval(e, lval_pop(expr, 0));
			
			// If evaluation leads to error print it
			if (x->type == LVAL_ERR)
			{
				lval_println(x);
			}
			lval_del(x);
		}

		// Delete expressions and arguments
		lval_del(expr);
		lval_del(a);

		// Return empty list
		return lval_sexpr();
	}
	else
	{
		// Get parse error as String
		char* err_msg = mpc_err_string(r.error);
		mpc_err_delete(r.error);

		// Create a new message using it
		lval* err = lval_err("Could not load Library %s", err_msg);

		// Clean up and return error
		free(err_msg);
		lval_del(a);
		return err;
	}
}



int main(int argc, char* argv[])
{
	// Parsers
	Number	 = mpc_new("number");
	Operator = mpc_new("operator");
	String	 = mpc_new("string");
	Comment  = mpc_new("comment");
	Sexpr    = mpc_new("sexpr");
	Qexpr    = mpc_new("qexpr");
	Expr     = mpc_new("expr");
	Lispi    = mpc_new("lispi");

	
	// Define the above parsers
	mpca_lang(MPC_LANG_DEFAULT,
		"																			\
			number	:	/-?[0-9]+/ ;												\
			operator:	/[a-zA-Z0-9_+\\-*\\/\\\\=<>!%^&]+/ ;						\
			string	:	/\"(\\\\.|[^\"])*\"/ ;										\
			comment	:	/;[^\\r\\n]*/ ;												\
			sexpr	:	'(' <expr>* ')' ;											\
			qexpr	:	'{' <expr>* '}' ;											\
			expr	:	<number> | <operator> | <string>							\
						| <comment>	| <sexpr> | <qexpr> ;							\
			lispi	:	/^/ <expr>* /$/ ;											\
		",
		Number, Operator, String, Comment, Sexpr, Qexpr, Expr, Lispi);

	// Create a new environment
	lenv* e = lenv_new();
	lenv_add_builtins(e);

	// Interactive Prompt
	if (argc == 1)
	{
		// Print Welcome message
		puts("Welcome to Lispi 0.0.1.0");
		puts("Press Ctrl+C to exit!");


		// Never ending loop
		while (1)
		{
			// Output the prompt and get input
			char* input = readline("Lispi> ");

			// Add input to history
			add_history(input);

			// Attempt to parse the input
			mpc_result_t r;
			if (mpc_parse("<stdin>", input, Lispi, &r)) {

				lval* x = lval_eval(e, lval_read(r.output));
				lval_println(x);
				lval_del(x);

				mpc_ast_delete(r.output);
			}
			else
			{
				// Otherwise print the error
				mpc_err_print(r.error);
				mpc_err_delete(r.error);
			}

			// Free the input buffer
			input = NULL;
			free(input);
		}
	}

	// Supplied with list of files
	if(argc >= 2)
	{
		// Loop over each supplied filename
		for (int i = 1; i < argc; ++i)
		{
			// Argument list with a single argument, the filename
			lval* args = lval_add(lval_sexpr(), lval_str(argv[i]));

			// Pass to builtin load and get the result
			lval* x = builtin_load(e, args);

			// If the result is an error be sure to print it
			if (x->type == LVAL_ERR)
			{
				lval_println(x);
			}
			lval_del(x);

		}
	}

	// Delete the environment
	lenv_del(e);

	// Undefine and delete our parsers
	mpc_cleanup(8,
		Number, Operator, String, Comment,
		Sexpr, Qexpr, Expr, Lispi);
	return 0;
}