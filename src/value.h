/*
 * value.h — Runtime value type used during expression evaluation.
 *
 * Role in compiler pipeline: "Data representation" layer shared between the
 * executor and expression evaluator.  Every cell in a result row is a Value.
 * Supports INT (long long), FLOAT (double), STRING, BOOL, NULL.
 */
#ifndef VALUE_H
#define VALUE_H

typedef enum {
    VAL_NULL = 0,
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING,
    VAL_BOOL
} ValType;

typedef struct {
    ValType type;
    union {
        long long  ival;
        double     fval;
        char      *sval;   /* heap-allocated, owned by the Value */
        int        bval;   /* 0 = false, 1 = true */
    };
} Value;

/* Constructors */
Value val_null(void);
Value val_int(long long i);
Value val_float(double f);
Value val_string(const char *s);   /* copies s */
Value val_bool(int b);

/* Ownership / lifecycle */
Value val_copy(Value v);           /* deep copy */
void  val_free(Value v);           /* release string memory if any */

/* Comparison: returns <0, 0, >0; NULL-aware (NULL != anything, returns -2 for unknown) */
int   val_cmp(Value a, Value b);

/* Arithmetic: returns VAL_NULL on type error */
Value val_add(Value a, Value b);
Value val_sub(Value a, Value b);
Value val_mul(Value a, Value b);
Value val_div(Value a, Value b);
Value val_neg(Value a);

/* Predicates */
int val_is_null(Value v);
int val_is_true(Value v);          /* for WHERE/ON: non-null non-zero non-empty */
int val_equal(Value a, Value b);   /* NULL-safe equality */

/* Printing */
void  val_print(Value v);
char *val_to_string(Value v);      /* caller frees the returned string */

/* Type promotion: INT -> FLOAT when one side is FLOAT */
Value val_coerce_float(Value v);

#endif /* VALUE_H */
