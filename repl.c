#define _GNU_SOURCE
/* repl.c — Neutrino interactive REPL.
 *
 * Lifetime note: the evaluator stores environment names as non-owning slices
 * into the source and closures as raw pointers into the parse arena. So every
 * ACCEPTED entry's (arena, source) pair must outlive the session — a closure
 * defined at the prompt keeps referencing both. We therefore retain each
 * accepted entry and free the lot only at exit. Rejected/incomplete entries
 * are discarded immediately. (Aborted runtime errors leak their in-flight
 * temporaries via longjmp — bounded per error, the one rough edge here.)
 *
 * Completeness: the parser swallows newlines inside (), [], and if..end, so an
 * unterminated construct fails at EOF. Thus "error whose token is EOF" means
 * "incomplete, read another line"; any other error is a real syntax error. */
#include "repl.h"
#include "lexer.h"
#include "parser.h"
#include "arena.h"
#include "ast.h"
#include "value.h"
#include <time.h>
#include "version.h"
#include "eval.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef HAVE_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

/* ------------------------------------------------------------------ */
/* session retention: accepted (arena, source) pairs live until exit   */
/* ------------------------------------------------------------------ */
typedef struct { Arena **arenas; char **srcs; size_t count, cap; } Keep;

static void keep_push(Keep *k, Arena *a, char *src)
{
    if (k->count == k->cap) {
        k->cap = k->cap ? k->cap * 2 : 16;
        k->arenas = realloc(k->arenas, k->cap * sizeof *k->arenas);
        k->srcs   = realloc(k->srcs,   k->cap * sizeof *k->srcs);
        if (!k->arenas || !k->srcs) abort();
    }
    k->arenas[k->count] = a;
    k->srcs[k->count]   = src;
    k->count++;
}

static void keep_free_all(Keep *k)
{
    for (size_t i = 0; i < k->count; i++) { arena_free(k->arenas[i]); free(k->srcs[i]); }
    free(k->arenas); free(k->srcs);
}

/* ------------------------------------------------------------------ */
/* tab completion (identifiers + keywords)                             */
/* ------------------------------------------------------------------ */
static EnvObj *g_globals;        /* live global frame, for name completion */

#ifdef HAVE_READLINE
static char  **g_cands;
static size_t  g_ncands;

static void build_candidates(void)
{
    for (size_t i = 0; i < g_ncands; i++) free(g_cands[i]);
    free(g_cands); g_cands = nullptr; g_ncands = 0;

    static const char *kw[] = { "let", "fn", "if", "then", "else", "end", "for", "while",
                                "do", "in", "break", "continue", "return", "true", "false", "null" };
    size_t nk = sizeof kw / sizeof *kw;
    size_t cap = nk + (g_globals ? g_globals->count : 0);
    g_cands = malloc(cap * sizeof *g_cands);
    if (!g_cands) abort();

    for (size_t i = 0; i < nk; i++) g_cands[g_ncands++] = strdup(kw[i]);
    if (g_globals)
        for (uint32_t i = 0; i < g_globals->count; i++)
            g_cands[g_ncands++] = strndup(g_globals->names[i], g_globals->namelens[i]);
}

static char *name_generator(const char *text, int state)
{
    static size_t idx, len;
    if (state == 0) { build_candidates(); idx = 0; len = strlen(text); }
    while (idx < g_ncands) {
        const char *c = g_cands[idx++];
        if (strncmp(c, text, len) == 0) return strdup(c);
    }
    return nullptr;
}

static char **repl_completion(const char *text, int start, int end)
{
    (void)start; (void)end;
    rl_attempted_completion_over = 1;          /* no filename fallback */
    rl_completion_append_character = '\0';     /* don't append a space (call-friendly) */
    return rl_completion_matches(text, name_generator);
}
#endif /* HAVE_READLINE */

/* ------------------------------------------------------------------ */
/* line input (readline, or a plain fgets fallback)                    */
/* ------------------------------------------------------------------ */
static char *prompt_line(const char *prompt)
{
#ifdef HAVE_READLINE
    return readline(prompt);
#else
    fputs(prompt, stdout); fflush(stdout);
    char *buf = nullptr; size_t cap = 0;       /* getline: no line-length limit */
    ssize_t n = getline(&buf, &cap, stdin);
    if (n < 0) { free(buf); return nullptr; }
    if (n && buf[n-1] == '\n') buf[n-1] = '\0';
    return buf;                                 /* caller frees, same as readline */
#endif
}

#ifdef HAVE_READLINE
/* Add to history without the trailing newline(s) that append_line leaves on
 * the accumulated buffer (the parser wants them; recalled commands do not).
 * add_history copies its argument, so a temporary truncation is safe. */
static void history_add_trimmed(char *s)
{
    size_t n = strlen(s), k = n;
    while (k && (s[k-1] == '\n' || s[k-1] == '\r')) k--;
    if (k == 0) return;
    char saved = s[k];
    s[k] = '\0';
    add_history(s);
    s[k] = saved;
}
#endif

static char *append_line(char *acc, size_t *len, const char *line)
{
    size_t ll = strlen(line);
    acc = realloc(acc, *len + ll + 2);
    if (!acc) abort();
    memcpy(acc + *len, line, ll);
    *len += ll;
    acc[(*len)++] = '\n';
    acc[*len] = '\0';
    return acc;
}

static bool is_blank(const char *s)
{
    for (; *s; s++) if (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* diagnostics                                                         */
/* ------------------------------------------------------------------ */
static void print_diag(const char *src, uint32_t line, uint32_t col, const char *msg)
{
    fprintf(stderr, "  error at %u:%u: %s\n", line, col, msg ? msg : "error");
    const char *p = src;
    for (uint32_t ln = 1; ln < line && *p; p++) if (*p == '\n') ln++;
    const char *eol = p;
    while (*eol && *eol != '\n') eol++;
    fprintf(stderr, "    %.*s\n", (int)(eol - p), p);
    fprintf(stderr, "    %*s^\n", col ? (int)col - 1 : 0, "");
}

/* ------------------------------------------------------------------ */
/* SIGINT: cancel the current (possibly multi-line) entry, don't exit  */
/* ------------------------------------------------------------------ */
static sigjmp_buf g_sigint_jmp;
static void on_sigint(int sig) { (void)sig; siglongjmp(g_sigint_jmp, 1); }

/* ------------------------------------------------------------------ */
/* output paging ('more on' captures an entry's output and pages it)   */
/* ------------------------------------------------------------------ */
static bool g_more;              /* paging enabled? (default off) */

static void page_output(const char *buf, size_t sz)
{
    if (!buf || sz == 0) return;
    size_t lines = 0;
    for (size_t i = 0; i < sz; i++) if (buf[i] == '\n') lines++;
    int rows = 24;
    struct winsize ws;
    if (ioctl(fileno(stdout), TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) rows = ws.ws_row;
    if ((int)lines < rows) { fwrite(buf, 1, sz, stdout); return; }
    const char *pager = getenv("PAGER");
    if (!pager || !*pager) pager = "less";
    FILE *p = popen(pager, "w");
    if (!p) { fwrite(buf, 1, sz, stdout); return; }
    fwrite(buf, 1, sz, p);
    pclose(p);
}

/* If `line` is the command `word` (followed by end or whitespace, not '('),
 * return a pointer to its trimmed argument (empty string if none); else null. */
static const char *match_command(const char *line, const char *word)
{
    while (*line == ' ' || *line == '\t') line++;
    size_t wl = strlen(word);
    if (strncmp(line, word, wl) != 0) return nullptr;
    const char *after = line + wl;
    if (*after != '\0' && *after != ' ' && *after != '\t') return nullptr;  /* e.g. 'format(' */
    while (*after == ' ' || *after == '\t') after++;
    return after;
}

/* ------------------------------------------------------------------ */
static void print_banner(bool color)
{
    if (color) {
        fputs(" \033[38;2;32;139;255m╭─╴\033[0m   \033[38;2;32;139;255m╶─╮\033[0m    \033[38;2;32;139;255m _   _            _        _             \033[0m\n", stdout);
        fputs("   \033[38;2;70;96;140m▪\033[0m \033[38;2;70;96;140m▪\033[0m \033[38;2;70;96;140m▪\033[0m      \033[38;2;32;139;255m| \\ | | ___ _   _| |_ _ __(_)_ __   ___  \033[0m\n", stdout);
        fputs(" \033[38;2;106;91;255m·\033[0m \033[38;2;70;96;140m▪\033[0m \033[38;2;0;194;214m◉\033[0m \033[38;2;70;96;140m▪\033[0m \033[38;2;106;91;255m·\033[0m    \033[38;2;32;139;255m|  \\| |/ _ \\ | | | __| '__| | '_ \\ / _ \\ \033[0m\n", stdout);
        fputs("   \033[38;2;70;96;140m▪\033[0m \033[38;2;70;96;140m▪\033[0m \033[38;2;70;96;140m▪\033[0m      \033[38;2;32;139;255m| |\\  |  __/ |_| | |_| |  | | | | | (_) |\033[0m\n", stdout);
        fputs(" \033[38;2;32;139;255m╰─╴\033[0m   \033[38;2;32;139;255m╶─╯\033[0m    \033[38;2;32;139;255m|_| \\_|\\___|\\__,_|\\__|_|  |_|_| |_|\\___/ \033[0m\n", stdout);
        fputs("              \033[38;2;120;132;150ma small functional array language\033[0m\n", stdout);
        char now[32]; time_t t = time(NULL);
        strftime(now, sizeof now, "%Y-%m-%d %H:%M:%S", localtime(&t));
        printf("              \033[38;2;120;132;150mv%s · built %s · session %s\033[0m\n",
               NEUTRINO_VERSION, NEUTRINO_BUILT, now);
    } else {
        fputs(" ╭─╴   ╶─╮     _   _            _        _             \n", stdout);
        fputs("   ▪ ▪ ▪      | \\ | | ___ _   _| |_ _ __(_)_ __   ___  \n", stdout);
        fputs(" · ▪ ◉ ▪ ·    |  \\| |/ _ \\ | | | __| '__| | '_ \\ / _ \\ \n", stdout);
        fputs("   ▪ ▪ ▪      | |\\  |  __/ |_| | |_| |  | | | | | (_) |\n", stdout);
        fputs(" ╰─╴   ╶─╯    |_| \\_|\\___|\\__,_|\\__|_|  |_|_| |_|\\___/ \n", stdout);
        fputs("              a small functional array language\n", stdout);
        char now[32]; time_t t = time(NULL);
        strftime(now, sizeof now, "%Y-%m-%d %H:%M:%S", localtime(&t));
        printf("              v%s · built %s · session %s\n", NEUTRINO_VERSION, NEUTRINO_BUILT, now);
    }
}

/* ------------------------------------------------------------------ */
int repl_run(void)
{
    Interp I;
    interp_init(&I);
    EnvObj *globals = globals_new();
    g_globals = globals;
    Keep keep = {0};
    value_set_multiline(true);                   /* aligned matrices in the interactive shell */

    char histpath[4096];
    const char *home = getenv("HOME");
    if (home) snprintf(histpath, sizeof histpath, "%s/.neutrino_history", home);
    else      snprintf(histpath, sizeof histpath, ".neutrino_history");

#ifdef HAVE_READLINE
    rl_readline_name = "neutrino";
    rl_attempted_completion_function = repl_completion;
#ifdef HAVE_GNU_READLINE
    rl_catch_signals = 0;                       /* we install our own SIGINT */
#endif
    using_history();
    stifle_history(1000);
    read_history(histpath);
    if (isatty(fileno(stdin))) {
        print_banner(isatty(fileno(stdout)) && !getenv("NO_COLOR"));
        fputs("              type  help  for a tour  \u00b7  Ctrl-D to exit  \u00b7  Ctrl-C cancels\n\n", stdout);
    }
#else
    if (isatty(fileno(stdin))) {
        print_banner(isatty(fileno(stdout)) && !getenv("NO_COLOR"));
        fputs("              type  help  for a tour  \u00b7  Ctrl-D to exit\n\n", stdout);
    }
#endif

    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, nullptr);

    static char  *acc;
    static size_t acclen;
    static bool   cont;
    acc = nullptr; acclen = 0; cont = false;

    for (;;) {
        if (sigsetjmp(g_sigint_jmp, 1)) {       /* landed here on Ctrl-C */
#ifdef HAVE_GNU_READLINE
            rl_free_line_state();
            rl_cleanup_after_signal();
#endif
            fputc('\n', stdout);
            free(acc); acc = nullptr; acclen = 0; cont = false;
        }

        char *line = prompt_line(cont ? "     ...> " : "neutrino> ");
        if (!line) {                            /* EOF / Ctrl-D */
            if (cont) {                         /* cancel partial entry, back to prompt */
                fputc('\n', stdout);
                free(acc); acc = nullptr; acclen = 0; cont = false;
                continue;
            }
            fputc('\n', stdout);
            break;
        }

        if (!cont) {                            /* '!cmd' / 'more' / 'format' commands */
            const char *q = line;
            while (*q == ' ' || *q == '\t') q++;
            const char *arg;
            if (*q == '!') {
                q++;
                if (*q) { fflush(stdout); fflush(stderr); int rc = system(q); (void)rc; }
                else fputs("usage: !<shell command>\n", stderr);
#ifdef HAVE_READLINE
                add_history(line);
#endif
                free(line);
                continue;
            }
            if ((arg = match_command(q, "more"))) {
                if (!strcmp(arg, "on") || !strcmp(arg, "1"))       g_more = true;
                else if (!strcmp(arg, "off") || !strcmp(arg, "0")) g_more = false;
                else if (*arg == '\0') printf("more is %s\n", g_more ? "on" : "off");
                else fputs("usage: more on | off\n", stderr);
#ifdef HAVE_READLINE
                add_history(line);
#endif
                free(line);
                continue;
            }
            if ((arg = match_command(q, "manual"))) {
                /* Page MANUAL.md: look in the current directory, then next to
                 * the binary's directory (set NEUTRINO_MANUAL to override). */
                const char *cand[3]; int nc = 0;
                const char *envp = getenv("NEUTRINO_MANUAL");
                if (envp && *envp) cand[nc++] = envp;
                cand[nc++] = "MANUAL.md";
                cand[nc++] = "/usr/local/share/neutrino/MANUAL.md";
                const char *found = nullptr;
                for (int ci = 0; ci < nc; ci++) {
                    FILE *tf = fopen(cand[ci], "rb");
                    if (tf) { fclose(tf); found = cand[ci]; break; }
                }
                if (!found) {
                    fputs("manual: MANUAL.md not found (run from the repo root, "
                          "or set NEUTRINO_MANUAL=/path/to/MANUAL.md)\n", stderr);
                } else {
                    const char *pager = getenv("PAGER");
                    if (!pager || !*pager) pager = "less";
                    char cmd[1200];
                    snprintf(cmd, sizeof cmd, "%s '%s'", pager, found);
                    if (system(cmd) != 0) {           /* no pager? print it */
                        FILE *mf = fopen(found, "rb");
                        if (mf) {
                            char buf[4096]; size_t got;
                            while ((got = fread(buf, 1, sizeof buf, mf)) > 0)
                                fwrite(buf, 1, got, stdout);
                            fclose(mf);
                        }
                    }
                }
#ifdef HAVE_READLINE
                add_history(line);
#endif
                free(line);
                continue;
            }
            if ((arg = match_command(q, "pretty"))) {
                if (!strcmp(arg, "on") || !strcmp(arg, "1"))       value_set_multiline(true);
                else if (!strcmp(arg, "off") || !strcmp(arg, "0")) value_set_multiline(false);
                else if (*arg == '\0') printf("pretty is %s\n", value_multiline() ? "on" : "off");
                else fputs("usage: pretty on | off\n", stderr);
#ifdef HAVE_READLINE
                add_history(line);
#endif
                free(line);
                continue;
            }
            if ((arg = match_command(q, "format"))) {
                if (*arg == '\0') printf("format: %s\n", value_format_desc());
                else if (!value_format_by_name(arg))
                    fprintf(stderr, "format: unknown mode '%s' (try short, long, short e, long e, default)\n", arg);
#ifdef HAVE_READLINE
                add_history(line);
#endif
                free(line);
                continue;
            }
        }

        acc = append_line(acc, &acclen, line);
        free(line);

        if (is_blank(acc)) { free(acc); acc = nullptr; acclen = 0; cont = false; continue; }

        Arena *a = arena_new();
        Parser p;
        parser_init(&p, acc, a);
        AstNode *prog = parser_parse(&p);

        if (p.had_error) {
            if (p.err_tok.kind == TOK_EOF) {    /* unterminated -> read another line */
                arena_free(a);
                cont = true;
                continue;
            }
            print_diag(acc, p.err_tok.line, p.err_tok.col, p.err_msg);
            arena_free(a);
#ifdef HAVE_READLINE
            history_add_trimmed(acc);
#endif
            free(acc); acc = nullptr; acclen = 0; cont = false;
            continue;
        }

#ifdef HAVE_READLINE
        history_add_trimmed(acc);
#endif
        keep_push(&keep, a, acc);               /* arena + source now owned by the session */

        char  *cap = nullptr; size_t capsz = 0; FILE *ms = nullptr;
        bool   paging = g_more && isatty(fileno(stdout));
        if (paging) {
            ms = open_memstream(&cap, &capsz);
            if (ms) value_set_out(ms); else paging = false;
        }

        Value r = vm_eval_program(&I, prog, globals, /*echo=*/true);
        if (I.had_error)
            print_diag(acc, I.cur_line, I.cur_col, I.err);
        value_release(r);

        if (paging) {
            value_set_out(nullptr);
            fclose(ms);
            page_output(cap, capsz);
            free(cap);
        }

        acc = nullptr; acclen = 0; cont = false; /* ownership transferred to keep */
    }

#ifdef HAVE_READLINE
    write_history(histpath);
    for (size_t i = 0; i < g_ncands; i++) free(g_cands[i]);
    free(g_cands);
#endif
    free(acc);                                   /* any unfinished entry */
    env_clear(globals);                          /* break global closure/env cycles */
    env_release(globals);
    vm_session_end();                            /* free retained chunks before arenas */
    keep_free_all(&keep);                        /* closures are gone; safe to free arenas/srcs */
    return 0;
}
