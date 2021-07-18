#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define SV_IMPLEMENTATION
#include "./sv.h"

#define UNREACHABLE(message)                         \
    do {                                             \
        fprintf(stderr, "%s:%d: UNREACHABLE: %s\n",  \
                __FILE__, __LINE__, message);        \
        exit(69);                                    \
    } while(0)

typedef struct Expr Expr;
typedef size_t Expr_Index;

typedef enum {
    EXPR_KIND_NUMBER = 0,
    EXPR_KIND_CELL,
    EXPR_KIND_BOP,
    EXPR_KIND_UOP,
} Expr_Kind;

typedef enum {
    BOP_KIND_PLUS = 0,
    BOP_KIND_MINUS,
    BOP_KIND_MULT,
    BOP_KIND_DIV,
    COUNT_BOP_KINDS,
} Bop_Kind;

typedef struct {
    Bop_Kind kind;
    String_View token;
    size_t precedence;
} Bop_Def;

typedef enum {
    BOP_PRECEDENCE0 = 0,
    BOP_PRECEDENCE1,
    COUNT_BOP_PRECEDENCE
} Bop_Precedence;

static_assert(COUNT_BOP_KINDS == 4, "The amount of Binary Operators has changed. Please adjust the definition table accordingly");
static const Bop_Def bop_defs[COUNT_BOP_KINDS] = {
    [BOP_KIND_PLUS] = {
        .kind = BOP_KIND_PLUS,
        .token = SV_STATIC("+"),
        .precedence = BOP_PRECEDENCE0,
    },
    [BOP_KIND_MINUS] = {
        .kind = BOP_KIND_MINUS,
        .token = SV_STATIC("-"),
        .precedence = BOP_PRECEDENCE0,
    },
    [BOP_KIND_MULT] = {
        .kind = BOP_KIND_MULT,
        .token = SV_STATIC("*"),
        .precedence = BOP_PRECEDENCE1,
    },
    [BOP_KIND_DIV] = {
        .kind = BOP_KIND_DIV,
        .token = SV_STATIC("/"),
        .precedence = BOP_PRECEDENCE1,
    },
};

const Bop_Def *bop_def_by_token(String_View token)
{
    for (Bop_Kind kind = 0; kind < COUNT_BOP_KINDS; ++kind) {
        if (sv_eq(bop_defs[kind].token, token)) {
            return &bop_defs[kind];
        }
    }

    return NULL;
}

typedef struct {
    Bop_Kind kind;
    Expr_Index lhs;
    Expr_Index rhs;
} Expr_Bop;

typedef enum {
    UOP_KIND_MINUS
} Uop_Kind;

typedef struct {
    Uop_Kind kind;
    Expr_Index param;
} Expr_Uop;

typedef struct {
    size_t row;
    size_t col;
} Cell_Index;

typedef union {
    double number;
    Cell_Index cell;
    Expr_Bop bop;
    Expr_Uop uop;
} Expr_As;

struct Expr {
    Expr_Kind kind;
    Expr_As as;
    const char *file_path;
    size_t file_row;
    size_t file_col;
};

typedef struct {
    size_t count;
    size_t capacity;
    Expr *items;
} Expr_Buffer;

Expr_Index expr_buffer_alloc(Expr_Buffer *eb)
{
    if (eb->count >= eb->capacity) {
        if (eb->capacity == 0) {
            assert(eb->items == NULL);
            eb->capacity = 128;
        } else {
            eb->capacity *= 2;
        }

        eb->items = realloc(eb->items, sizeof(Expr) * eb->capacity);
    }

    memset(&eb->items[eb->count], 0, sizeof(Expr));

    return eb->count++;
}

Expr *expr_buffer_at(Expr_Buffer *eb, Expr_Index index)
{
    assert(index < eb->count);
    return &eb->items[index];
}

typedef enum {
    DIR_LEFT = 0,
    DIR_RIGHT,
    DIR_UP,
    DIR_DOWN,
} Dir;

typedef enum {
    CELL_KIND_TEXT = 0,
    CELL_KIND_NUMBER,
    CELL_KIND_EXPR,
    CELL_KIND_CLONE,
} Cell_Kind;

const char *cell_kind_as_cstr(Cell_Kind kind)
{
    switch (kind) {
    case CELL_KIND_TEXT:
        return "TEXT";
    case CELL_KIND_NUMBER:
        return "NUMBER";
    case CELL_KIND_EXPR:
        return "EXPR";
    case CELL_KIND_CLONE:
        return "CLONE";
    default:
        UNREACHABLE("unknown Cell Kind");
    }
}

typedef enum {
    UNEVALUATED = 0,
    INPROGRESS,
    EVALUATED,
} Eval_Status;

typedef struct {
    Expr_Index index;
    double value;
} Cell_Expr;

typedef union {
    String_View text;
    double number;
    Cell_Expr expr;
    Dir clone;
} Cell_As;

typedef struct {
    Cell_Kind kind;
    Cell_As as;
    Eval_Status status;

    size_t file_row;
    size_t file_col;
} Cell;

typedef struct {
    Cell *cells;
    size_t rows;
    size_t cols;
    const char *file_path;
} Table;

bool is_name(char c)
{
    return isalnum(c) || c == '_';
}

typedef struct {
    String_View text;
    const char *file_path;
    size_t file_row;
    size_t file_col;
} Token;

typedef struct {
    String_View source;
    const char *file_path;
    size_t file_row;
    const char *line_start;
} Lexer;

size_t lexer_file_col(const Lexer *lexer)
{
    return lexer->source.data - lexer->line_start + 1;
}

void lexer_print_loc(const Lexer *lexer, FILE *stream)
{
    fprintf(stream, "%s:%zu:%zu: ",
            lexer->file_path,
            lexer->file_row,
            lexer_file_col(lexer));
}

Token lexer_peek_token(Lexer *lexer)
{
    lexer->source = sv_trim(lexer->source);

    Token token;
    memset(&token, 0, sizeof(token));
    token.file_path = lexer->file_path;
    token.file_row = lexer->file_row;
    token.file_col = lexer_file_col(lexer);

    if (lexer->source.count == 0) {
        return token;
    }

    if (*lexer->source.data == '+' ||
            *lexer->source.data == '-' ||
            *lexer->source.data == '*' ||
            *lexer->source.data == '/' ||
            *lexer->source.data == '(' ||
            *lexer->source.data == ')') {
        token.text = (String_View) {
            .count = 1,
            .data = lexer->source.data
        };
        return token;
    }

    if (is_name(*lexer->source.data)) {
        token.text = sv_take_left_while(lexer->source, is_name);
        return token;
    }

    lexer_print_loc(lexer, stderr);
    fprintf(stderr, "ERROR: unknown token starts with `%c`\n", *lexer->source.data);
    exit(1);
}

Token lexer_next_token(Lexer *lexer)
{
    Token token = lexer_peek_token(lexer);
    sv_chop_left(&lexer->source, token.text.count);
    return token;
}

void lexer_expect_no_tokens(Lexer *lexer)
{
    Token token = lexer_next_token(lexer);
    if (token.text.data != NULL) {
        fprintf(stderr, "%s:%zu:%zu: ERROR: unexpected token `"SV_Fmt"`\n",
                token.file_path,
                token.file_row,
                token.file_col,
                SV_Arg(token.text));
        exit(1);
    }
}

typedef struct {
    size_t capacity;
    char *cstr;
} Tmp_Cstr;

char *tmp_cstr_fill(Tmp_Cstr *tc, const char *data, size_t data_size)
{
    if (data_size + 1 >= tc->capacity) {
        tc->capacity = data_size + 1;
        tc->cstr = realloc(tc->cstr, tc->capacity);
    }

    memcpy(tc->cstr, data, data_size);
    tc->cstr[data_size] = '\0';
    return tc->cstr;
}

bool sv_strtod(String_View sv, Tmp_Cstr *tc, double *out)
{
    char *ptr = tmp_cstr_fill(tc, sv.data, sv.count);
    char *endptr = NULL;
    double result = strtod(ptr, &endptr);
    if (out) *out = result;
    return endptr != ptr && *endptr == '\0';
}

bool sv_strtol(String_View sv, Tmp_Cstr *tc, long int *out)
{
    char *ptr = tmp_cstr_fill(tc, sv.data, sv.count);
    char *endptr = NULL;
    long int result = strtol(ptr, &endptr, 10);
    if (out) *out = result;
    return endptr != ptr && *endptr == '\0';
}

Expr_Index parse_expr(Lexer *lexer, Tmp_Cstr *tc, Expr_Buffer *eb);

Expr_Index parse_primary_expr(Lexer *lexer, Tmp_Cstr *tc, Expr_Buffer *eb)
{
    Token token = lexer_next_token(lexer);

    if (token.text.count == 0) {
        lexer_print_loc(lexer, stderr);
        fprintf(stderr, "ERROR: expected primary expression token, but got end of input\n");
        exit(1);
    }

    double number = 0.0;
    if (sv_strtod(token.text, tc, &number)) {
        Expr_Index expr_index = expr_buffer_alloc(eb);
        Expr *expr = expr_buffer_at(eb, expr_index);
        expr->kind = EXPR_KIND_NUMBER;
        expr->as.number = number;
        expr->file_path = token.file_path;
        expr->file_row  = token.file_row;
        expr->file_col  = token.file_col;
        return expr_index;
    } else if (sv_eq(token.text, SV("("))) {
        Expr_Index expr_index = parse_expr(lexer, tc, eb);
        token = lexer_next_token(lexer);
        if (!sv_eq(token.text, SV(")"))) {
            fprintf(stderr, "%s:%zu:%zu: Expected token `)` but got `"SV_Fmt"`\n", token.file_path, token.file_row, token.file_col, SV_Arg(token.text));
            exit(1);
        }
        return expr_index;
    } else if (sv_eq(token.text, SV("-"))) {
        Expr_Index param_index = parse_expr(lexer, tc, eb);
        Expr_Index expr_index = expr_buffer_alloc(eb);
        {
            Expr *expr = expr_buffer_at(eb, expr_index);
            expr->kind = EXPR_KIND_UOP;
            expr->as.uop.kind = UOP_KIND_MINUS;
            expr->as.uop.param = param_index;
            expr->file_path = token.file_path;
            expr->file_row  = token.file_row;
            expr->file_col  = token.file_col;
        }
        return expr_index;
    } else {
        Expr_Index expr_index = expr_buffer_alloc(eb);
        Expr *expr = expr_buffer_at(eb, expr_index);
        expr->file_path = token.file_path;
        expr->file_row  = token.file_row;
        expr->file_col  = token.file_col;
        expr->kind = EXPR_KIND_CELL;

        if (!isupper(*token.text.data)) {
            lexer_print_loc(lexer, stderr);
            fprintf(stderr, "ERROR: cell reference must start with capital letter\n");
            exit(1);
        }

        expr->as.cell.col = *token.text.data - 'A';

        sv_chop_left(&token.text, 1);

        long int row = 0;
        if (!sv_strtol(token.text, tc, &row)) {
            lexer_print_loc(lexer, stderr);
            fprintf(stderr, "ERROR: cell reference must have an integer as the row number\n");
            exit(1);
        }

        expr->as.cell.row = (size_t) row;
        return expr_index;
    }
}

Expr_Index parse_bop_expr(Lexer *lexer, Tmp_Cstr *tc, Expr_Buffer *eb, size_t precedence)
{
    if (precedence >= COUNT_BOP_PRECEDENCE) {
        return parse_primary_expr(lexer, tc, eb);
    }

    Expr_Index lhs_index = parse_bop_expr(lexer, tc, eb, precedence + 1);

    Token token = lexer_peek_token(lexer);
    const Bop_Def *def = bop_def_by_token(token.text);

    if (def != NULL && def->precedence == precedence) {
        token = lexer_next_token(lexer);
        Expr_Index rhs_index = parse_bop_expr(lexer, tc, eb, precedence);

        Expr_Index expr_index = expr_buffer_alloc(eb);
        {
            Expr *expr = expr_buffer_at(eb, expr_index);
            expr->kind = EXPR_KIND_BOP;
            expr->as.bop.kind = def->kind;
            expr->as.bop.lhs = lhs_index;
            expr->as.bop.rhs = rhs_index;
            expr->file_path = token.file_path;
            expr->file_row = token.file_row;
            expr->file_col = token.file_col;
        }

        return expr_index;
    }

    return lhs_index;
}

Cell *table_cell_at(Table *table, Cell_Index index)
{
    assert(index.row < table->rows);
    assert(index.col < table->cols);
    return &table->cells[index.row * table->cols + index.col];
}

void dump_table(FILE *stream, Table *table)
{
    for (size_t row = 0; row < table->rows; ++row) {
        for (size_t col = 0; col < table->cols; ++col) {
            Cell_Index cell_index = {
                .row = row,
                .col = col,
            };
            Cell *cell = table_cell_at(table, cell_index);

            fprintf(stream, "%s:%zu:%zu: %s\n", table->file_path, cell->file_row, cell->file_col, cell_kind_as_cstr(cell->kind));
        }
    }
}

void dump_expr(FILE *stream, Expr_Buffer *eb, Expr_Index expr_index, int level)
{
    fprintf(stream, "%*s", level * 2, "");

    Expr *expr = expr_buffer_at(eb, expr_index);

    switch (expr->kind) {
    case EXPR_KIND_NUMBER:
        fprintf(stream, "NUMBER: %lf\n", expr->as.number);
        break;

    case EXPR_KIND_CELL:
        fprintf(stream, "CELL(%zu, %zu)\n", expr->as.cell.row, expr->as.cell.col);
        break;

    case EXPR_KIND_UOP:
        switch (expr->as.uop.kind) {
        case UOP_KIND_MINUS:
            fprintf(stream, "UOP(MINUS):\n");
            break;
        default:
            UNREACHABLE("unknown Unary Operator Kind");
        }

        dump_expr(stream, eb, expr->as.uop.param, level + 1);
        break;

    case EXPR_KIND_BOP:
        switch (expr->as.bop.kind) {
        case BOP_KIND_PLUS:
            fprintf(stream, "BOP(PLUS):\n");
            break;

        case BOP_KIND_MINUS:
            fprintf(stream, "BOP(MINUS):\n");
            break;

        case BOP_KIND_MULT:
            fprintf(stream, "BOP(MULT):\n");
            break;

        case BOP_KIND_DIV:
            fprintf(stream, "BOP(DIV):\n");
            break;

        case COUNT_BOP_KINDS:
        default:
            UNREACHABLE("unknown Binary Operator Kind");
        }

        dump_expr(stream, eb, expr->as.bop.lhs, level + 1);
        dump_expr(stream, eb, expr->as.bop.rhs, level + 1);
        break;

    default:
        UNREACHABLE("unknown Expression Kind");
    }
}

Expr_Index parse_expr(Lexer *lexer, Tmp_Cstr *tc, Expr_Buffer *eb)
{
    return parse_bop_expr(lexer, tc, eb, BOP_PRECEDENCE0);
}

void usage(FILE *stream)
{
    fprintf(stream, "Usage: ./minicel <input.csv>\n");
}

char *slurp_file(const char *file_path, size_t *size)
{
    char *buffer = NULL;

    FILE *f = fopen(file_path, "rb");
    if (f == NULL) {
        goto error;
    }

    if (fseek(f, 0, SEEK_END) < 0) {
        goto error;
    }

    long m = ftell(f);
    if (m < 0) {
        goto error;
    }

    buffer = malloc(sizeof(char) * m);
    if (buffer == NULL) {
        goto error;
    }

    if (fseek(f, 0, SEEK_SET) < 0) {
        goto error;
    }

    size_t n = fread(buffer, 1, m, f);
    assert(n == (size_t) m);

    if (ferror(f)) {
        goto error;
    }

    if (size) {
        *size = n;
    }

    fclose(f);

    return buffer;

error:
    if (f) {
        fclose(f);
    }

    if (buffer) {
        free(buffer);
    }

    return NULL;
}

void parse_table_from_content(Table *table, Expr_Buffer *eb, Tmp_Cstr *tc, String_View content)
{
    for (size_t row = 0; row < table->rows; ++row) {
        String_View line = sv_chop_by_delim(&content, '\n');
        const char *const line_start = line.data;
        for (size_t col = 0; col < table->cols; ++col) {
            String_View cell_value = sv_trim(sv_chop_by_delim(&line, '|'));
            Cell_Index cell_index = {
                .row = row,
                .col = col,
            };
            Cell *cell = table_cell_at(table, cell_index);
            cell->file_row = row + 1;
            cell->file_col = cell_value.data - line_start + 1;

            if (sv_starts_with(cell_value, SV("="))) {
                sv_chop_left(&cell_value, 1);
                cell->kind = CELL_KIND_EXPR;
                Lexer lexer = {
                    .source = cell_value,
                    .file_path = table->file_path,
                    .file_row = cell->file_row,
                    .line_start = line_start,
                };
                cell->as.expr.index = parse_expr(&lexer, tc, eb);
                lexer_expect_no_tokens(&lexer);
            } else if (sv_starts_with(cell_value, SV(":"))) {
                sv_chop_left(&cell_value, 1);
                cell->kind = CELL_KIND_CLONE;
                if (sv_eq(cell_value, SV("<"))) {
                    cell->as.clone = DIR_LEFT;
                } else if (sv_eq(cell_value, SV(">"))) {
                    cell->as.clone = DIR_RIGHT;
                } else if (sv_eq(cell_value, SV("^"))) {
                    cell->as.clone = DIR_UP;
                } else if (sv_eq(cell_value, SV("v"))) {
                    cell->as.clone = DIR_DOWN;
                } else {
                    fprintf(stderr, "%s:%zu:%zu: ERROR: "SV_Fmt" is not a correct direction to clone a cell from\n", table->file_path, cell->file_row, cell->file_col, SV_Arg(cell_value));
                    exit(1);
                }
            } else {
                if (sv_strtod(cell_value, tc, &cell->as.number)) {
                    cell->kind = CELL_KIND_NUMBER;
                } else {
                    cell->kind = CELL_KIND_TEXT;
                    cell->as.text = cell_value;
                }
            }
        }
    }
}

void estimate_table_size(String_View content, size_t *out_rows, size_t *out_cols)
{
    size_t rows = 0;
    size_t cols = 0;
    for (; content.count > 0; ++rows) {
        String_View line = sv_chop_by_delim(&content, '\n');
        size_t col = 0;
        for (; line.count > 0; ++col) {
            sv_chop_by_delim(&line, '|');
        }

        if (cols < col) {
            cols = col;
        }
    }

    if (out_rows) {
        *out_rows = rows;
    }

    if (out_cols) {
        *out_cols = cols;
    }
}

void table_eval_cell(Table *table, Expr_Buffer *eb, Cell_Index cell_index);

double table_eval_expr(Table *table, Expr_Buffer *eb, Expr_Index expr_index)
{
    Expr *expr = expr_buffer_at(eb, expr_index);

    switch (expr->kind) {
    case EXPR_KIND_NUMBER:
        return expr->as.number;

    case EXPR_KIND_CELL: {
        table_eval_cell(table, eb, expr->as.cell);

        Cell *target_cell = table_cell_at(table, expr->as.cell);
        switch (target_cell->kind) {
        case CELL_KIND_NUMBER:
            return target_cell->as.number;
        case CELL_KIND_TEXT: {
            fprintf(stderr, "%s:%zu:%zu: ERROR: text cells may not participate in math expressions\n", expr->file_path, expr->file_row, expr->file_col);
            fprintf(stderr, "%s:%zu:%zu: NOTE: the text cell is located here\n",
                    table->file_path, target_cell->file_row, target_cell->file_col);
            exit(1);
        }

        case CELL_KIND_EXPR:
            return target_cell->as.expr.value;

        case CELL_KIND_CLONE:
            UNREACHABLE("cell should never be a clone after the evaluation");
        }
    }
    break;

    case EXPR_KIND_BOP: {
        double lhs = table_eval_expr(table, eb, expr->as.bop.lhs);
        double rhs = table_eval_expr(table, eb, expr->as.bop.rhs);

        switch (expr->as.bop.kind) {
        case BOP_KIND_PLUS:
            return lhs + rhs;
        case BOP_KIND_MINUS:
            return lhs - rhs;
        case BOP_KIND_MULT:
            return lhs * rhs;
        case BOP_KIND_DIV:
            return lhs / rhs;
        case COUNT_BOP_KINDS:
        default:
            UNREACHABLE("unknown Binary Operator Kind");
        }
    }
    break;

    case EXPR_KIND_UOP: {
        double param = table_eval_expr(table, eb, expr->as.uop.param);

        switch (expr->as.uop.kind) {
        case UOP_KIND_MINUS:
            return -param;
        default:
            UNREACHABLE("unknown Unary Operator Kind");
        }
    }
    break;
    }
    return 0;
}

Dir opposite_dir(Dir dir)
{
    switch (dir) {
    case DIR_LEFT:
        return DIR_RIGHT;
    case DIR_RIGHT:
        return DIR_LEFT;
    case DIR_UP:
        return DIR_DOWN;
    case DIR_DOWN:
        return DIR_UP;
    default:
        UNREACHABLE("unknown direction");
    }
}

Cell_Index nbor_in_dir(Cell_Index index, Dir dir)
{
    switch (dir) {
    case DIR_LEFT:
        index.col -= 1;
        break;
    case DIR_RIGHT:
        index.col += 1;
        break;
    case DIR_UP:
        index.row -= 1;
        break;
    case DIR_DOWN:
        index.row += 1;
        break;
    default:
        UNREACHABLE("unknown direction");
    }

    return index;
}

Expr_Index move_expr_in_dir(Table *table, Cell_Index cell_index, Expr_Buffer *eb, Expr_Index root, Dir dir)
{
    Cell *cell = table_cell_at(table, cell_index);

    switch (expr_buffer_at(eb, root)->kind) {
    case EXPR_KIND_NUMBER:
        return root;

    case EXPR_KIND_CELL: {
        Expr_Index new_index = expr_buffer_alloc(eb);
        {
            Expr *new_expr = expr_buffer_at(eb, new_index);
            new_expr->kind = EXPR_KIND_CELL;
            new_expr->as.cell = nbor_in_dir(expr_buffer_at(eb, root)->as.cell, dir);
            new_expr->file_path = table->file_path;
            new_expr->file_row = cell->file_row;
            new_expr->file_col = cell->file_col;
        }

        return new_index;
    }
    break;

    case EXPR_KIND_BOP: {
        Expr_Bop bop = {0};

        {
            Expr *root_expr = expr_buffer_at(eb, root);
            bop = root_expr->as.bop;
        }

        bop.lhs = move_expr_in_dir(table, cell_index, eb, bop.lhs, dir);
        bop.rhs = move_expr_in_dir(table, cell_index, eb, bop.rhs, dir);

        Expr_Index new_index = expr_buffer_alloc(eb);
        {
            Expr *new_expr = expr_buffer_at(eb, new_index);
            new_expr->kind = EXPR_KIND_BOP;
            new_expr->as.bop = bop;
            new_expr->file_path = table->file_path;
            new_expr->file_row = cell->file_row;
            new_expr->file_col = cell->file_col;
        }

        return new_index;
    }
    break;

    case EXPR_KIND_UOP: {
        Expr_Uop uop = {0};
        {
            Expr *root_expr = expr_buffer_at(eb, root);
            uop = root_expr->as.uop;
        }

        uop.param = move_expr_in_dir(table, cell_index, eb, uop.param, dir);

        Expr_Index new_index = expr_buffer_alloc(eb);
        {
            Expr *new_expr = expr_buffer_at(eb, new_index);
            new_expr->kind = EXPR_KIND_UOP;
            new_expr->as.uop = uop;
            new_expr->file_path = table->file_path;
            new_expr->file_row = cell->file_row;
            new_expr->file_col = cell->file_col;
        }

        return new_index;
    }
    break;

    default:
        UNREACHABLE("Unknown Expression Kind");
    }
}

void table_eval_cell(Table *table, Expr_Buffer *eb, Cell_Index cell_index)
{
    Cell *cell = table_cell_at(table, cell_index);

    switch (cell->kind) {
    case CELL_KIND_TEXT:
    case CELL_KIND_NUMBER:
        cell->status = EVALUATED;
        break;
    case CELL_KIND_EXPR: {
        if (cell->status == INPROGRESS) {
            fprintf(stderr, "%s:%zu:%zu: ERROR: circular dependency is detected!\n", table->file_path, cell->file_row, cell->file_col);
            exit(1);
        }

        if (cell->status == UNEVALUATED) {
            cell->status = INPROGRESS;
            cell->as.expr.value = table_eval_expr(table, eb, cell->as.expr.index);
            cell->status = EVALUATED;
        }
    }
    break;

    case CELL_KIND_CLONE: {
        if (cell->status == INPROGRESS) {
            fprintf(stderr, "%s:%zu:%zu: ERROR: circular dependency is detected!\n", table->file_path, cell->file_row, cell->file_col);
            exit(1);
        }

        if (cell->status == UNEVALUATED) {
            cell->status = INPROGRESS;

            Dir dir = cell->as.clone;
            Cell_Index nbor_index = nbor_in_dir(cell_index, dir);
            if (nbor_index.row >= table->rows || nbor_index.col >= table->cols) {
                fprintf(stderr, "%s:%zu:%zu: ERROR: trying to clone a cell outside of the table\n", table->file_path, cell->file_row, cell->file_col);
                exit(1);
            }

            table_eval_cell(table, eb, nbor_index);

            Cell *nbor = table_cell_at(table, nbor_index);
            cell->kind = nbor->kind;
            cell->as = nbor->as;

            if (cell->kind == CELL_KIND_EXPR) {
                cell->as.expr.index = move_expr_in_dir(table, cell_index, eb, cell->as.expr.index, opposite_dir(dir));
                cell->as.expr.value = table_eval_expr(table, eb, cell->as.expr.index);
            }

            cell->status = EVALUATED;
        } else {
            UNREACHABLE("evaluated clones are an absurd. When a clone cell is evaluated it becomes its neighbor kind");
        }
    }
    break;

    default:
        UNREACHABLE("unknown Cell Kind");
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(stderr);
        fprintf(stderr, "ERROR: input file is not provided\n");
        exit(1);
    }

    const char *input_file_path = argv[1];

    size_t content_size = 0;
    char *content = slurp_file(input_file_path, &content_size);
    if (content == NULL) {
        fprintf(stderr, "ERROR: could not read file %s: %s\n",
                input_file_path, strerror(errno));
        exit(1);
    }

    String_View input = {
        .count = content_size,
        .data = content,
    };

    Expr_Buffer eb = {0};
    Table table = {
        .file_path = input_file_path,
    };
    Tmp_Cstr tc = {0};

    estimate_table_size(input, &table.rows, &table.cols);
    table.cells = malloc(sizeof(*table.cells) * table.rows * table.cols);
    memset(table.cells, 0, sizeof(*table.cells) * table.rows * table.cols);
    parse_table_from_content(&table, &eb, &tc, input);

    // Evaluate each cell
    for (size_t row = 0; row < table.rows; ++row) {
        for (size_t col = 0; col < table.cols; ++col) {
            Cell_Index cell_index = {
                .row = row,
                .col = col,
            };
            table_eval_cell(&table, &eb, cell_index);
        }
    }

    // Estimate column widths
    size_t *col_widths = malloc(sizeof(size_t) * table.cols);
    {
        for (size_t col = 0; col < table.cols; ++col) {
            col_widths[col] = 0;
            for (size_t row = 0; row < table.rows; ++row) {
                Cell_Index cell_index = {
                    .row = row,
                    .col = col,
                };

                Cell *cell = table_cell_at(&table, cell_index);
                size_t width = 0;
                switch (cell->kind) {
                case CELL_KIND_TEXT:
                    width = cell->as.text.count;
                    break;

                case CELL_KIND_NUMBER: {
                    int n = snprintf(NULL, 0, "%lf", cell->as.number);
                    assert(n >= 0);
                    width = (size_t) n;
                }
                break;

                case CELL_KIND_EXPR: {
                    int n = snprintf(NULL, 0, "%lf", cell->as.expr.value);
                    assert(n >= 0);
                    width = (size_t) n;
                }
                break;

                case CELL_KIND_CLONE:
                    UNREACHABLE("cell should never be a clone after the evaluation");
                }

                if (col_widths[col] < width) {
                    col_widths[col] = width;
                }
            }
        }
    }

    // Render the table
    for (size_t row = 0; row < table.rows; ++row) {
        for (size_t col = 0; col < table.cols; ++col) {
            Cell_Index cell_index = {
                .row = row,
                .col = col,
            };

            Cell *cell = table_cell_at(&table, cell_index);
            int n = 0;
            switch (cell->kind) {
            case CELL_KIND_TEXT:
                n = printf(SV_Fmt, SV_Arg(cell->as.text));
                break;

            case CELL_KIND_NUMBER:
                n = printf("%lf", cell->as.number);
                break;

            case CELL_KIND_EXPR:
                n = printf("%lf", cell->as.expr.value);
                break;

            case CELL_KIND_CLONE:
                UNREACHABLE("cell should never be a clone after the evaluation");
            }
            assert(0 <= n);
            assert((size_t) n <= col_widths[col]);
            printf("%*s", (int) (col_widths[col] - n), "");

            if (col < table.cols - 1) {
                printf("|");
            }
        }
        printf("\n");
    }

    free(col_widths);
    free(content);
    free(table.cells);
    free(eb.items);
    free(tc.cstr);

    return 0;
}

