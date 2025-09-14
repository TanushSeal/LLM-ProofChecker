// proof_checker.c
// Build as shared library:
//  gcc -std=c11 -O2 -Wall -fPIC -c proof_checker.c -o proof_checker.o
//  gcc -shared -o libproofchecker.so proof_checker.o
//
// Optional standalone build:
//  gcc -std=c11 -O2 -Wall -DBUILD_STANDALONE -o proof_checker proof_checker.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* Simple AST for WFFs in prefix notation.
   Nodes:
     kind 'A' - atomic variable (single uppercase letter)
     kind 'N' - negation (unary), child in left
     kind 'C' - implication (binary), left and right children
*/
typedef struct Node {
    char kind;
    char atom;            // valid if kind == 'A'
    struct Node *left;
    struct Node *right;
} Node;

/* Proof line representation */
typedef struct {
    int line_no;
    char *formula_str;    // original string (whitespace removed)
    Node *formula_ast;    // parsed AST (NULL until parsed)
    char *just;           // justification string
} ProofLine;

/* Dynamic storage for proof lines */
#define INITIAL_CAP 256
static ProofLine *proof = NULL;
static int proof_capacity = 0;
static int proof_count = 0;

/* Utility: allocate or expand proof array */
static void ensure_proof_capacity(void) {
    if (!proof) {
        proof_capacity = INITIAL_CAP;
        proof = (ProofLine*)calloc(proof_capacity, sizeof(ProofLine));
        if (!proof) { perror("calloc"); exit(EXIT_FAILURE); }
    } else if (proof_count >= proof_capacity) {
        proof_capacity *= 2;
        proof = (ProofLine*)realloc(proof, proof_capacity * sizeof(ProofLine));
        if (!proof) { perror("realloc"); exit(EXIT_FAILURE); }
    }
}

/* ---------------- Simple dynamic string buffer for captured output ---------------- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} StrBuf;

static int sb_init(StrBuf *s) {
    s->cap = 4096;
    s->len = 0;
    s->buf = (char*)malloc(s->cap);
    if (!s->buf) return 0;
    s->buf[0] = '\0';
    return 1;
}

static void sb_free(StrBuf *s) {
    if (!s) return;
    free(s->buf);
    s->buf = NULL;
    s->len = s->cap = 0;
}

static int sb_grow_to(StrBuf *s, size_t need) {
    if (need <= s->cap) return 1;
    size_t newcap = s->cap ? s->cap : 1024;
    while (newcap < need) newcap *= 2;
    char *nb = (char*)realloc(s->buf, newcap);
    if (!nb) return 0;
    s->buf = nb;
    s->cap = newcap;
    return 1;
}

static int sb_appendf(StrBuf *s, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) return 0;
    size_t want = (size_t)needed + 1 + s->len;
    if (!sb_grow_to(s, want)) return 0;
    va_start(ap, fmt);
    vsnprintf(s->buf + s->len, s->cap - s->len, fmt, ap);
    va_end(ap);
    s->len += (size_t)needed;
    return 1;
}

/* Global output buffer pointer used by internal functions */
static StrBuf g_sb;
static StrBuf *g_out = NULL;

/* Helper to append messages to global output buffer */
static void out_append(const char *fmt, ...) {
    if (!g_out) return;
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (needed < 0) return;
    size_t want = (size_t)needed + 1 + g_out->len;
    if (!sb_grow_to(g_out, want)) return;
    va_start(ap, fmt);
    vsnprintf(g_out->buf + g_out->len, g_out->cap - g_out->len, fmt, ap);
    va_end(ap);
    g_out->len += (size_t)needed;
}

/* ---------------- Forward declarations (parsing etc.) ---------------- */
static Node *parse_node(const char *s, int *idx);
static int is_wff_str(const char *s);
static void free_tree(Node *n);
static Node *clone_tree(const Node *n);
static int equal_tree(const Node *a, const Node *b);
static void skip_ws_str(const char *s, int *idx);

/* ---------------- Parsing functions ---------------- */

/* skip whitespace in a string, advancing idx */
static void skip_ws_str(const char *s, int *idx) {
    int n = (int)strlen(s);
    while (*idx < n && isspace((unsigned char)s[*idx])) (*idx)++;
}

/* Parse a WFF starting at s[idx] (no leading whitespace assumed).
   Returns pointer to Node on success and updates idx to position after the WFF.
   Returns NULL on failure; idx may be left at some position.
*/
static Node *parse_node(const char *s, int *idx) {
    skip_ws_str(s, idx);
    int n = (int)strlen(s);
    if (*idx >= n) return NULL;
    char tok = s[*idx];

    if (isupper((unsigned char)tok)) {
        Node *node = (Node*)malloc(sizeof(Node));
        if (!node) { perror("malloc"); exit(EXIT_FAILURE); }
        node->kind = 'A';
        node->atom = tok;
        node->left = node->right = NULL;
        (*idx)++;
        return node;
    }
    if (tok == 'n') {
        (*idx)++;
        Node *child = parse_node(s, idx);
        if (!child) return NULL;
        Node *node = (Node*)malloc(sizeof(Node));
        if (!node) { perror("malloc"); exit(EXIT_FAILURE); }
        node->kind = 'N';
        node->atom = 0;
        node->left = child;
        node->right = NULL;
        return node;
    }
    if (tok == 'c') {
        (*idx)++;
        Node *left = parse_node(s, idx);
        if (!left) return NULL;
        Node *right = parse_node(s, idx);
        if (!right) { free_tree(left); return NULL; }
        Node *node = (Node*)malloc(sizeof(Node));
        if (!node) { perror("malloc"); exit(EXIT_FAILURE); }
        node->kind = 'C';
        node->atom = 0;
        node->left = left;
        node->right = right;
        return node;
    }
    return NULL;
}

/* Check whether string s is a WFF (entire string, no trailing garbage).
   Returns 1 if yes, 0 otherwise.
*/
static int is_wff_str(const char *s) {
    int idx = 0;
    skip_ws_str(s, &idx);
    Node *n = parse_node(s, &idx);
    if (!n) return 0;
    skip_ws_str(s, &idx);
    int ok = (idx == (int)strlen(s));
    free_tree(n);
    return ok;
}

/* Free AST */
static void free_tree(Node *n) {
    if (!n) return;
    free_tree(n->left);
    free_tree(n->right);
    free(n);
}

/* Clone AST */
static Node *clone_tree(const Node *n) {
    if (!n) return NULL;
    Node *c = (Node*)malloc(sizeof(Node));
    if (!c) { perror("malloc"); exit(EXIT_FAILURE); }
    c->kind = n->kind;
    c->atom = n->atom;
    c->left = clone_tree(n->left);
    c->right = clone_tree(n->right);
    return c;
}

/* Structural equality of ASTs */
static int equal_tree(const Node *a, const Node *b) {
    if (a == NULL && b == NULL) return 1;
    if (a == NULL || b == NULL) return 0;
    if (a->kind != b->kind) return 0;
    if (a->kind == 'A') return a->atom == b->atom;
    if (a->kind == 'N') return equal_tree(a->left, b->left);
    if (a->kind == 'C') return equal_tree(a->left, b->left) && equal_tree(a->right, b->right);
    return 0;
}

/* ---------------- Pattern matching (axiom instance check) ---------------- */

/* Bindings for pattern variables: map 'A'..'Z' -> Node* (NULL if unbound) */
typedef struct {
    Node *map[26];
} Bindings;

/* Initialize bindings to all NULL */
static void bindings_init(Bindings *b) {
    for (int i = 0; i < 26; ++i) b->map[i] = NULL;
}
static void bindings_clear(Bindings *b) {
    for (int i = 0; i < 26; ++i) b->map[i] = NULL;
}

/* Match pattern `p` against formula `f` with bindings `b`. */
static int match_pattern_rec(Node *p, Node *f, Bindings *b) {
    if (!p || !f) return 0;
    if (p->kind == 'A') {
        char var = p->atom;
        int idx = var - 'A';
        if (idx < 0 || idx >= 26) return 0; // safety
        if (b->map[idx] == NULL) {
            b->map[idx] = f; // bind pattern var to this formula subtree (store pointer to f)
            return 1;
        } else {
            return equal_tree(b->map[idx], f);
        }
    } else if (p->kind == 'N') {
        if (f->kind != 'N') return 0;
        return match_pattern_rec(p->left, f->left, b);
    } else if (p->kind == 'C') {
        if (f->kind != 'C') return 0;
        return match_pattern_rec(p->left, f->left, b) && match_pattern_rec(p->right, f->right, b);
    }
    return 0;
}

/* Top-level: check if formula AST `f` is an instance of axiom whose pattern is given by string pat_str. */
static int is_instance_of_axiom_pattern(const char *pat_str, Node *f) {
    int idx = 0;
    Node *pattern = parse_node(pat_str, &idx);
    if (!pattern) return 0;
    skip_ws_str(pat_str, &idx);
    if (idx != (int)strlen(pat_str)) {
        free_tree(pattern);
        return 0;
    }
    Bindings b;
    bindings_init(&b);
    int ok = match_pattern_rec(pattern, f, &b);
    free_tree(pattern);
    bindings_clear(&b);
    return ok;
}

/* ---------------- Modus Ponens checking ---------------- */
static int check_modus_ponens(Node *cur, int i, int j) {
    if (i < 1 || j < 1 || i > proof_count || j > proof_count) return 0;
    Node *Ai = proof[i-1].formula_ast;
    Node *Aj = proof[j-1].formula_ast;
    if (!Ai || !Aj) return 0;

    /* Case 1: Ai is A, Aj is c A B, cur equals B */
    if (Aj->kind == 'C' && equal_tree(Ai, Aj->left) && equal_tree(cur, Aj->right)) return 1;
    /* Case 2: Aj is A, Ai is c A B */
    if (Ai->kind == 'C' && equal_tree(Aj, Ai->left) && equal_tree(cur, Ai->right)) return 1;
    return 0;
}

/* ---------------- Substitution checking ---------------- */

static Node *apply_subst(const Node *p, char var, const Node *replacement) {
    if (!p) return NULL;
    if (p->kind == 'A') {
        if (p->atom == var) {
            return clone_tree(replacement);
        } else {
            Node *n = (Node*)malloc(sizeof(Node));
            if (!n) { perror("malloc"); exit(EXIT_FAILURE); }
            n->kind = 'A';
            n->atom = p->atom;
            n->left = n->right = NULL;
            return n;
        }
    } else if (p->kind == 'N') {
        Node *child = apply_subst(p->left, var, replacement);
        Node *n = (Node*)malloc(sizeof(Node));
        if (!n) { perror("malloc"); exit(EXIT_FAILURE); }
        n->kind = 'N';
        n->atom = 0;
        n->left = child;
        n->right = NULL;
        return n;
    } else { // 'C'
        Node *L = apply_subst(p->left, var, replacement);
        Node *R = apply_subst(p->right, var, replacement);
        Node *n = (Node*)malloc(sizeof(Node));
        if (!n) { perror("malloc"); exit(EXIT_FAILURE); }
        n->kind = 'C';
        n->atom = 0;
        n->left = L;
        n->right = R;
        return n;
    }
}

static int check_substitution(Node *current, const char *just) {
    /* parse just: find uppercase var and '=' and rhs */
    const char *p = just;
    while (*p && !isupper((unsigned char)*p)) p++;
    if (!*p) return 0;
    char var = *p;
    const char *eq = strchr(just, '=');
    if (!eq) return 0;
    const char *rhs = eq + 1;
    while (*rhs && isspace((unsigned char)*rhs)) rhs++;
    if (!*rhs) return 0;
    if (!is_wff_str(rhs)) return 0;
    int idx = 0;
    Node *replacement = parse_node(rhs, &idx);
    if (!replacement) return 0;
    skip_ws_str(rhs, &idx);
    if (idx != (int)strlen(rhs)) { free_tree(replacement); return 0; }

    for (int k = 0; k < proof_count; ++k) {
        Node *src = proof[k].formula_ast;
        if (!src) continue;
        Node *subs = apply_subst(src, var, replacement);
        int ok = equal_tree(subs, current);
        free_tree(subs);
        if (ok) { free_tree(replacement); return 1; }
    }
    free_tree(replacement);
    return 0;
}

/* ---------------- Top-level axiom patterns ---------------- */
static const char *AX1_PAT = "cPcQP";
static const char *AX2_PAT = "ccScPQccSPcSQ";
static const char *AX3_PAT = "ccnPnQcQP";

static int is_instance_AX1(Node *f) { return is_instance_of_axiom_pattern(AX1_PAT, f); }
static int is_instance_AX2(Node *f) { return is_instance_of_axiom_pattern(AX2_PAT, f); }
static int is_instance_AX3(Node *f) { return is_instance_of_axiom_pattern(AX3_PAT, f); }

/* ---------------- Input parsing and checking driver ---------------- */

/* Trim leading/trailing whitespace from a C string (in place) */
static void trim_inplace(char *s) {
    if (!s) return;
    int n = (int)strlen(s);
    int i = 0;
    while (i < n && isspace((unsigned char)s[i])) i++;
    if (i > 0) memmove(s, s + i, n - i + 1);
    n = (int)strlen(s);
    while (n > 0 && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}

/* Remove all whitespace characters from a string, in place */
static void clean_inplace(char *s) {
    char *p = s, *q = s;
    while (*p) {
        if (!isspace((unsigned char)*p)) {
            *q++ = *p;
        }
        p++;
    }
    *q = '\0';
}

/* Read proof from a FILE* (lines). Returns 0 on success, negative on error. */
static int read_proof_from_file(FILE *fp) {
    char buf[4096];
    int expected_line = 1;
    proof_count = 0; // reset
    while (fgets(buf, sizeof buf, fp)) {
        char *nl = strchr(buf, '\n');
        if (nl) *nl = '\0';
        char *p = buf;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;

        int lineno = 0;
        int consumed = 0;
        if (sscanf(p, "%d%n", &lineno, &consumed) != 1) {
            out_append("Bad input line (missing line number): %s\n", buf);
            return -1;
        }
        p += consumed;
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) { out_append("Missing formula on line %d\n", lineno); return -2; }

        char formula_token[1024];
        int fi = 0;
        while (*p && !isspace((unsigned char)*p)) {
            if (fi < (int)sizeof(formula_token)-1) formula_token[fi++] = *p;
            p++;
        }
        formula_token[fi] = '\0';
        while (*p && isspace((unsigned char)*p)) p++;
        char *just_str = strdup(p);
        if (!just_str) { out_append("Memory error\n"); return -3; }
        trim_inplace(just_str);

        if (lineno != expected_line) {
            out_append("Line numbers must be consecutive starting at 1 (expected %d but got %d)\n", expected_line, lineno);
            free(just_str);
            return -4;
        }
        expected_line++;

        ensure_proof_capacity();
        proof[proof_count].line_no = lineno;
        proof[proof_count].formula_str = strdup(formula_token);
        proof[proof_count].formula_ast = NULL;
        proof[proof_count].just = just_str;
        proof_count++;
    }
    return 0;
}

/* Parse all formulas into ASTs and validate syntactic WFF */
static int parse_all_formulas(void) {
    for (int i = 0; i < proof_count; ++i) {
        clean_inplace(proof[i].formula_str);
        const char *fs = proof[i].formula_str;
        if (!is_wff_str(fs)) {
            out_append("Line %d: formula is not a WFF: \"%s\"\n", proof[i].line_no, fs);
            return 0;
        }
        int idx = 0;
        proof[i].formula_ast = parse_node(fs, &idx);
        skip_ws_str(fs, &idx);
        if (idx != (int)strlen(fs) || proof[i].formula_ast == NULL) {
            out_append("Internal parse error at line %d\n", proof[i].line_no);
            return 0;
        }
    }
    return 1;
}

/* Check each line's justification and append status into output buffer. Returns 1 if all ok, 0 otherwise. */
static int check_proof(void) {
    int all_ok = 1;
    for (int i = 0; i < proof_count; ++i) {
        ProofLine *pl = &proof[i];
        int ok = 0;
        if (pl->just == NULL) {
            ok = 0;
        } else if (strcasecmp(pl->just, "Premise") == 0) {
            ok = 1;
        } else if (strcasecmp(pl->just, "AX1") == 0) {
            ok = is_instance_AX1(pl->formula_ast);
        } else if (strcasecmp(pl->just, "AX2") == 0) {
            ok = is_instance_AX2(pl->formula_ast);
        } else if (strcasecmp(pl->just, "AX3") == 0) {
            ok = is_instance_AX3(pl->formula_ast);
        } else if (strncasecmp(pl->just, "MP", 2) == 0) {
            int a = -1, b = -1;
            const char *s = pl->just + 2;
            if (sscanf(s, " %d %d", &a, &b) < 2) {
                out_append("Line %d: bad MP justification format: \"%s\"\n", pl->line_no, pl->just);
                ok = 0;
            } else {
                ok = check_modus_ponens(pl->formula_ast, a, b);
            }
        } else if (strncasecmp(pl->just, "Substitution", 12) == 0) {
            ok = check_substitution(pl->formula_ast, pl->just);
        } else {
            out_append("Line %d: unknown justification: \"%s\"\n", pl->line_no, pl->just);
            ok = 0;
        }

        if (ok) {
            out_append("Line %d: OK: %s    [%s]\n", pl->line_no, pl->formula_str, pl->just);
        } else {
            out_append("Line %d: INVALID: %s    [%s]\n", pl->line_no, pl->formula_str, pl->just);
            all_ok = 0;
        }
    }
    return all_ok;
}

/* Cleanup proof memory */
static void cleanup_proof(void) {
    for (int i = 0; i < proof_count; ++i) {
        free(proof[i].formula_str);
        free(proof[i].just);
        free_tree(proof[i].formula_ast);
    }
    free(proof);
    proof = NULL;
    proof_capacity = proof_count = 0;
}

/* ---------------- Public API: verify_proof and free_output ---------------- */

/*
  verify_proof:
    - input: proof text (lines separated by '\n')
    - output: pointer to malloc'd string with checker messages (set *output)
    - return: 0 (proof valid), 1 (proof invalid), negative for parse/other errors.
*/
int verify_proof(const char *input, char **output) {
    if (!output) return -100;
    *output = NULL;
    if (!input) return -101;

    if (!sb_init(&g_sb)) return -102;
    g_out = &g_sb;

    // read input via fmemopen
    size_t len = strlen(input);
    // fmemopen expects non-const buffer; cast is OK because we won't modify content
    FILE *fp = fmemopen((void*)input, len, "r");
    if (!fp) {
        out_append("Internal error: fmemopen failed\n");
        char *ret = strdup(g_out->buf);
        *output = ret;
        sb_free(&g_sb);
        g_out = NULL;
        return -103;
    }

    int rc = read_proof_from_file(fp);
    fclose(fp);
    if (rc != 0) {
        // error messages were appended by read_proof_from_file
        char *ret = strdup(g_out->buf);
        *output = ret;
        sb_free(&g_sb);
        g_out = NULL;
        cleanup_proof();
        return -200 + rc; // map to negative code
    }

    if (proof_count == 0) {
        out_append("No proof lines read.\n");
        char *ret = strdup(g_out->buf);
        *output = ret;
        sb_free(&g_sb);
        g_out = NULL;
        cleanup_proof();
        return -201;
    }

    if (!parse_all_formulas()) {
        // parse_all_formulas appended error to outbuf
        char *ret = strdup(g_out->buf);
        *output = ret;
        sb_free(&g_sb);
        g_out = NULL;
        cleanup_proof();
        return -202;
    }

    int ok = check_proof(); // appends per-line output
    // prepare return string (caller will free)
    char *ret = strdup(g_out->buf ? g_out->buf : "");
    *output = ret;

    sb_free(&g_sb);
    g_out = NULL;
    cleanup_proof();
    return ok ? 0 : 1;
}

void free_output(char *p) {
    if (p) free(p);
}

/* Optional standalone program for direct testing
   Compile with -DBUILD_STANDALONE to include main() in the object.
*/
#ifdef BUILD_STANDALONE
int main(void) {
    // read entire stdin into a string
    size_t cap = 8192, len = 0;
    char *buf = malloc(cap);
    if (!buf) { perror("malloc"); return 2; }
    int c;
    while ((c = getchar()) != EOF) {
        if (len + 2 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { perror("realloc"); free(buf); return 2; }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';

    char *out = NULL;
    int rc = verify_proof(buf, &out);
    if (out) {
        printf("%s", out);
        free_output(out);
    }
    free(buf);
    return rc;
}
#endif

