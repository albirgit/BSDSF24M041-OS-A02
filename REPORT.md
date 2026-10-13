# REPORT.md: Operating Systems Programming Assignment 02 (Re-engineering `ls`)

## Feature 2: ls-v1.1.0 (Long Listing Format, `-l`)

### Q1. What is the crucial difference between `stat()` and `lstat()`? When is it more appropriate to use `lstat()` in `ls`?

Both system calls fill a `struct stat` with a file's metadata (type, permissions, owner, size, timestamps, link count). The difference is how they treat **symbolic links**:

- `stat(path, &st)` **follows** the symlink and returns information about the file the link points to (the target).
- `lstat(path, &st)` does **not** follow the link. It returns information about the symlink itself (its own type, size and timestamps).

For a regular file or directory, both give the same result.

In `ls`, `lstat()` is the right choice, especially for `-l`, because:

1. `ls -l` must show the link itself: file type `l` in the first character and the `name -> target` display. With `stat()` the link would look like its target (for example a regular file or directory) and this information would be lost.
2. A **dangling symlink** (pointing to a file that no longer exists) makes `stat()` fail with `ENOENT`, while `lstat()` still succeeds, so the entry can still be listed.
3. In recursive listing (`-R`), `lstat()` prevents following a link that points back to a parent directory, which would cause infinite recursion.

### Q2. How can bitwise operators and macros extract the file type and permissions from `st_mode`?

`st_mode` is a single integer in which different bit groups mean different things: the upper bits encode the **file type**, the lower 12 bits encode the **permissions** (including the special bits setuid, setgid and sticky). To read a specific piece of information, I use a **mask** with the bitwise AND operator `&`. ANDing with a mask keeps only the bits I ask about and clears the rest.

**File type:** `st_mode & S_IFMT` isolates the type bits, which can be compared with constants such as `S_IFDIR` (directory), `S_IFREG` (regular file) or `S_IFLNK` (symlink):

```c
if ((st.st_mode & S_IFMT) == S_IFDIR)   /* it is a directory */
```

The macros `S_ISDIR(m)`, `S_ISREG(m)`, `S_ISLNK(m)`, `S_ISCHR(m)` and so on do exactly this test and are easier to read. I use them to choose the first character (`d`, `-`, `l`, `c`, `b`, `p`, `s`).

**Permissions:** each permission is one bit, so a single AND tells whether it is set:

```c
if (st.st_mode & S_IRUSR)   /* owner can read */
```

The constants are `S_IRUSR/S_IWUSR/S_IXUSR` (owner), `S_IRGRP/S_IWGRP/S_IXGRP` (group) and `S_IROTH/S_IWOTH/S_IXOTH` (others). If the result is non-zero the bit is set, so I print `r`, `w` or `x`, otherwise `-`.

**Special bits:** `S_ISUID` and `S_ISGID` replace the `x` slot of owner and group with `s` (or `S` if the execute bit is not set underneath), and `S_ISVTX` (sticky bit) replaces the `x` slot of others with `t` (or `T`).

---

## Feature 3: ls-v1.2.0 (Column Display, down then across)

### Q1. Explain the logic for printing items in a "down then across" format. Why is a single loop through the filenames insufficient?

In "down then across" layout, the sorted list fills the **first column from top to bottom**, then continues at the top of the second column, and so on. The steps are:

1. Read all filenames into an array and note the length of the longest one (`maxlen`).
2. Get the terminal width and set the column width `colw = maxlen + 2` (two spaces of gap).
3. Number of columns: `cols = terminal_width / colw` (at least 1).
4. Number of rows: `rows = ceil(n / cols) = (n + cols - 1) / cols`.
5. Print row by row. In row `r`, column `c` shows the item at index `i = r + c * rows`. Each printed value is padded to `colw`.

For example with 10 files and 3 columns, `rows = 4`. Row 0 prints items 0, 4, 8; row 1 prints 1, 5, 9; row 2 prints 2, 6; and row 3 prints 3, 7.

A single loop over the array prints items in array order, which naturally goes **across** a row (0, 1, 2, ...). A terminal prints line by line, so the first line must contain items that are `rows` positions apart in the array (0, 4, 8). That cannot be produced by one sequential pass. I need the total count, the number of columns and rows in advance, and a nested loop with the index formula above.

### Q2. What is the purpose of `ioctl` here? What are the limitations of using only a fixed 80-column width?

`ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` asks the kernel/terminal driver for the current window size and fills `struct winsize` (`ws_col` = columns, `ws_row` = rows). The program uses `ws_col` to calculate how many columns fit on one line. If `ioctl` fails (for example, output is not a terminal) or returns 0, the program falls back to 80.

If only a fixed 80-column width were used:

- On a **wider** terminal, a lot of horizontal space would be wasted, with fewer columns than could fit.
- On a **narrower** terminal (or a small split window), lines would exceed the width and the terminal would wrap them, breaking the column alignment and making the output messy.
- The program would not adapt when the user resizes the window.

---

## Feature 4: ls-v1.3.0 (Horizontal Display, `-x`)

### Q1. Compare the implementation complexity of "down then across" and "across" printing. Which needs more pre-calculation and why?

The **vertical ("down then across")** layout needs more pre-calculation. Before printing anything it must compute the number of columns, then the number of rows (with ceiling division), and then use the index formula `i = r + c * rows`. It also has to handle the last column being shorter when `n` is not a multiple of `rows` (the `i >= n` check). The layout depends on the total number of files.

The **horizontal ("across")** layout is simpler. It just loops through the array once in order, prints each name padded to `colw`, and keeps a running horizontal position `pos`. When `pos + colw` would exceed the terminal width, it prints a newline and resets `pos` to 0. It needs only the terminal width and the column width, with no row count and no index arithmetic.

### Q2. Describe the strategy used to manage the display modes (`-l`, `-x`, default). How does the program decide which function to call?

I used an **enum**, `display_mode_t`, with three values: `MODE_DEFAULT`, `MODE_LONG` and `MODE_HORIZONTAL`. It is better than separate integer flags because only one display mode can be active at a time.

In `main()`, the `getopt()` loop sets the mode: `-l` sets `MODE_LONG`, and `-x` sets `MODE_HORIZONTAL` (but `-l` takes priority over `-x`, like the real `ls`). The mode is then passed to `do_ls()`.

After `do_ls()` has read and sorted all filenames into the array, it checks the mode:

```c
if (mode == MODE_LONG)            -> measure() + print_long() for each file
else if (mode == MODE_HORIZONTAL) -> print_horizontal()
else                              -> print_columns()   /* down then across */
```

Reading and sorting are shared by all modes, so only the final display function differs.

---

## Feature 5: ls-v1.4.0 (Alphabetical Sort)

### Q1. Why must all directory entries be read into memory before sorting? What are the drawbacks for directories with millions of files?

`readdir()` returns entries one at a time in an arbitrary, file-system-dependent order (not alphabetical). To know which name comes first, the program needs to compare **all** names with each other, so no entry can be printed until every entry has been read. Sorting therefore needs the whole list in memory.

Drawbacks for very large directories:

- **High memory usage:** every filename (and the array of pointers) is stored in RAM, which can be hundreds of megabytes for millions of files.
- **Delayed output:** nothing appears until the whole directory has been read and sorted, so the user waits with no feedback.
- **Sorting cost:** `qsort` takes roughly O(n log n) time.
- A very large directory could exhaust memory, whereas an unsorted listing could stream entries directly with constant memory.

### Q2. Explain the purpose and signature of the comparison function required by `qsort()`. Why must it take `const void *` arguments?

`qsort(base, count, size, compare)` is a generic sorting function. It does not know the type of the elements, so the programmer supplies a **comparison function** that tells it how to order two elements. The signature is:

```c
int cmp(const void *a, const void *b);
```

It must return a negative number if the first element should come before the second, zero if they are equal, and a positive number if the first should come after.

`qsort` can sort arrays of any type (ints, structs, strings), so it cannot declare typed parameters. It uses `const void *`, the generic pointer type, and the `const` promises that the comparison will not modify the elements. Inside the function I cast the pointers back to the real type.

In my program the array elements are `char *`, and `qsort` passes the **address of each element**, which is a `char **`. So the function is:

```c
static int cmp_names(const void *a, const void *b)
{
    const char *s1 = *(const char **)a;
    const char *s2 = *(const char **)b;
    return strcmp(s1, s2);
}
```

`strcmp` returns a negative, zero or positive value, exactly what `qsort` needs.

---

## Feature 6: ls-v1.5.0 (Colorized Output)

### Q1. How do ANSI escape codes produce color in a Linux terminal? Show the sequence for green text.

ANSI escape codes are special character sequences that the terminal interprets as **formatting commands** instead of printing them. A sequence starts with the ESC character (octal `\033`), followed by `[`, one or more numbers separated by `;`, and a final letter `m` (which means "set graphics mode"). For example, `0` means reset/normal style and `32` means green foreground. Everything printed after the sequence is shown in that style until another sequence changes it. `\033[0m` resets to the default.

Green text:

```c
printf("\033[0;32m%s\033[0m", "hello");
```

`\033[0;32m` turns green on, `hello` is printed in green, and `\033[0m` resets so the rest of the output is not green. In my code these are `#define` macros (`C_GREEN`, `C_RESET`, etc.).

The invisible escape characters still count as characters for `printf("%-*s")`, so I pad names manually with `colw - strlen(name)` spaces to keep columns aligned. Colors are enabled only when `isatty()` says the output is a terminal.

### Q2. Which bits in `st_mode` must be checked to decide if a file is executable by owner, group, or others?

Three permission bits, each tested with a bitwise AND:

- `st_mode & S_IXUSR` (octal `0100`): executable by the **owner**
- `st_mode & S_IXGRP` (octal `0010`): executable by the **group**
- `st_mode & S_IXOTH` (octal `0001`): executable by **others**

If the result is non-zero, that class has execute permission. My code checks all three at once:

```c
if (S_ISREG(m) && (m & (S_IXUSR | S_IXGRP | S_IXOTH)))   /* green */
```

I also check `S_ISREG` so that directories (which also have execute bits for traversal) are not colored as executables. Directories are checked earlier and printed in blue.

---

## Feature 7: ls-v1.6.0 (Recursive Listing, `-R`)

### Q1. What is a "base case" in a recursive function? What is the base case in the recursive `ls`?

A **base case** is the condition under which a recursive function stops calling itself and simply returns. Without it, the recursion would continue forever (until the stack overflows).

In my recursive `ls`, `do_ls()` lists a directory and then calls itself for every entry that is a subdirectory. The base case is a **directory that contains no subdirectories**: the loop over its entries finds no entry for which `S_ISDIR()` is true, so no further recursive call is made and the function returns to its caller. Skipping `.` and `..` (which would point back to the same or parent directory) and using `lstat()` so symlinks are not followed also guarantee that the recursion always terminates.

### Q2. Why is it essential to construct a full path before the recursive call? What would happen if `do_ls("subdir")` were called inside `do_ls("parent_dir")`?

`readdir()` returns only the **name** of each entry (for example `subdir`), not its path. The program's current working directory never changes while it runs. So `opendir("subdir")` and `lstat("subdir")` would look for `subdir` relative to the directory the program was started from, not inside `parent_dir`.

If I called `do_ls("subdir")` directly:

- `opendir("subdir")` would fail (or open the wrong directory) because `subdir` does not exist in the current working directory, and the recursion would not descend correctly.
- If a directory with the same name happened to exist in the starting directory, the program would list the wrong directory.

Building the full path, `parent_dir/subdir` (using `snprintf(path, ..., "%s/%s", dir, name)`), gives a path that is correct from the current working directory at every depth, so `opendir()` and `lstat()` always reach the intended directory. The path also becomes the header printed for that directory (for example `src/utils:`).
