/*
* Programming Assignment 02: ls v1.2.0
* Added: -l (long listing), default column display (down then across)
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

extern int errno;

/* column widths + total blocks for -l, computed in a first pass */
typedef struct {
    int  nlink, user, group, size;
    long blocks;
} widths_t;

void do_ls(const char *dir, int long_flag);

/* ---------- helpers for -l ---------- */

static int num_width(long n)
{
    int w = 1;
    while (n >= 10) { n /= 10; w++; }
    return w;
}

static void user_name(uid_t uid, char *buf, size_t n)
{
    struct passwd *pw = getpwuid(uid);
    if (pw) snprintf(buf, n, "%s", pw->pw_name);
    else    snprintf(buf, n, "%u", (unsigned)uid);
}

static void group_name(gid_t gid, char *buf, size_t n)
{
    struct group *gr = getgrgid(gid);
    if (gr) snprintf(buf, n, "%s", gr->gr_name);
    else    snprintf(buf, n, "%u", (unsigned)gid);
}

/* st_mode -> "drwxr-xr-x" */
static void mode_to_str(mode_t m, char *s)
{
    s[0] = S_ISDIR(m)  ? 'd' : S_ISLNK(m)  ? 'l' : S_ISCHR(m) ? 'c' :
           S_ISBLK(m)  ? 'b' : S_ISFIFO(m) ? 'p' : S_ISSOCK(m) ? 's' : '-';

    s[1] = (m & S_IRUSR) ? 'r' : '-';
    s[2] = (m & S_IWUSR) ? 'w' : '-';
    s[3] = (m & S_ISUID) ? ((m & S_IXUSR) ? 's' : 'S')
                         : ((m & S_IXUSR) ? 'x' : '-');

    s[4] = (m & S_IRGRP) ? 'r' : '-';
    s[5] = (m & S_IWGRP) ? 'w' : '-';
    s[6] = (m & S_ISGID) ? ((m & S_IXGRP) ? 's' : 'S')
                         : ((m & S_IXGRP) ? 'x' : '-');

    s[7] = (m & S_IROTH) ? 'r' : '-';
    s[8] = (m & S_IWOTH) ? 'w' : '-';
    s[9] = (m & S_ISVTX) ? ((m & S_IXOTH) ? 't' : 'T')
                         : ((m & S_IXOTH) ? 'x' : '-');
    s[10] = '\0';
}

/* Pass 1: update max widths and total blocks for one entry */
static void measure(const char *dir, const char *name, widths_t *w)
{
    char path[PATH_MAX], ubuf[64], gbuf[64];
    struct stat st;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (lstat(path, &st) == -1) return;

    user_name(st.st_uid, ubuf, sizeof(ubuf));
    group_name(st.st_gid, gbuf, sizeof(gbuf));

    int a = num_width((long)st.st_nlink);
    int b = (int)strlen(ubuf);
    int c = (int)strlen(gbuf);
    int d = num_width((long)st.st_size);

    if (a > w->nlink) w->nlink = a;
    if (b > w->user)  w->user  = b;
    if (c > w->group) w->group = c;
    if (d > w->size)  w->size  = d;
    w->blocks += st.st_blocks;
}

/* Pass 2: print one line in "ls -l" format */
static void print_long(const char *dir, const char *name, const widths_t *w)
{
    char path[PATH_MAX], perms[11], ubuf[64], gbuf[64];
    struct stat st;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (lstat(path, &st) == -1) {
        perror(path);
        return;
    }

    mode_to_str(st.st_mode, perms);
    user_name(st.st_uid, ubuf, sizeof(ubuf));
    group_name(st.st_gid, gbuf, sizeof(gbuf));

    char *t = ctime(&st.st_mtime) + 4;     /* skip "Tue " */

    printf("%s %*ld %-*s %-*s %*ld %.12s %s",
           perms,
           w->nlink, (long)st.st_nlink,
           w->user,  ubuf,
           w->group, gbuf,
           w->size,  (long)st.st_size,
           t, name);

    if (S_ISLNK(st.st_mode)) {
        char target[PATH_MAX];
        ssize_t len = readlink(path, target, sizeof(target) - 1);
        if (len != -1) {
            target[len] = '\0';
            printf(" -> %s", target);
        }
    }
    putchar('\n');
}

/* ---------- helpers for column display ---------- */

/* Terminal width in characters; falls back to 80 if ioctl fails */
static int term_width(void)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
        return 80;
    return ws.ws_col;
}

/* Default display: "down then across" */
static void print_columns(char **names, int n, int maxlen)
{
    if (n == 0)
        return;

    int colw = maxlen + 2;                 /* longest name + 2 spaces gap */
    int cols = term_width() / colw;        /* how many columns fit */
    if (cols < 1)
        cols = 1;                          /* very long name: at least 1 column */
    int rows = (n + cols - 1) / cols;      /* ceiling division */

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            int i = r + c * rows;          /* the key formula */
            if (i >= n)
                break;
            printf("%-*s", colw, names[i]);
        }
        putchar('\n');
    }
}

/* ---------- main ---------- */

int main(int argc, char *argv[])
{
    int opt;
    int long_flag = 0;

    while ((opt = getopt(argc, argv, "l")) != -1)
    {
        switch (opt)
        {
        case 'l':
            long_flag = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-l] [dir...]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind == argc)
    {
        do_ls(".", long_flag);
    }
    else
    {
        for (int i = optind; i < argc; i++)
        {
            printf("Directory listing of %s : \n", argv[i]);
            do_ls(argv[i], long_flag);
            puts("");
        }
    }
    return 0;
}

void do_ls(const char *dir, int long_flag)
{
    DIR *dp = opendir(dir);
    if (dp == NULL)
    {
        fprintf(stderr, "Cannot open directory : %s\n", dir);
        return;
    }

    /* Step 1: read all names into a dynamic array */
    char **names = NULL;
    int n = 0, cap = 0, maxlen = 0;
    struct dirent *entry;

    errno = 0;
    while ((entry = readdir(dp)) != NULL)
    {
        if (entry->d_name[0] == '.')
            continue;

        if (n == cap)                      /* array full: double its size */
        {
            cap = cap ? cap * 2 : 16;
            char **tmp = realloc(names, cap * sizeof(char *));
            if (tmp == NULL)
            {
                perror("realloc");
                exit(EXIT_FAILURE);
            }
            names = tmp;
        }

        names[n] = strdup(entry->d_name);  /* own copy: d_name is reused by readdir */
        if (names[n] == NULL)
        {
            perror("strdup");
            exit(EXIT_FAILURE);
        }

        int len = (int)strlen(entry->d_name);
        if (len > maxlen)
            maxlen = len;
        n++;
    }
    if (errno != 0)
        perror("readdir failed");
    closedir(dp);

    /* Step 2: display */
    if (long_flag)
    {
        widths_t w = {0, 0, 0, 0, 0};
        for (int i = 0; i < n; i++)
            measure(dir, names[i], &w);
        printf("total %ld\n", w.blocks / 2);
        for (int i = 0; i < n; i++)
            print_long(dir, names[i], &w);
    }
    else
    {
        print_columns(names, n, maxlen);
    }

    /* Step 3: free memory */
    for (int i = 0; i < n; i++)
        free(names[i]);
    free(names);
}
