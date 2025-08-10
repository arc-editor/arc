---
layout: default
title: Arc Editor - Actions and Motions
---

# Actions and Motions

[Back to Documentation](documentation.html)

Arc's normal mode operates on an "action and motion" system, inspired by Vim. Commands follow a `[count][action][target]` structure, allowing for efficient and composable text editing.

-   **`[count]`**: An optional number to repeat the command. For example, `2e` moves the cursor to the end of the second word from the cursor.
-   **`[action]`**: The operation to perform, like `d` for delete.
-   **`[target]`**: The text object to act upon, like `w` for word or `p` for paragraph.

If an action is omitted, the command simply moves the cursor. For example, `w` moves the cursor to the beginning of the next word.

---

## Actions

Actions are the verbs of the command system. They define what to do with the text specified by the target.

| Key | Action | Description |
| --- | --- | --- |
| `d` | Delete | Deletes the text specified by the target. |
| `c` | Change | Deletes the text and enters insert mode. |
| `g` | Go to | A prefix for various jump commands. |
| `z` | Scroll | A prefix for scrolling commands. |

The "change" action deletes the target text and then enters insert mode, allowing you to replace it.

---

## Movement

### Basic Movement

| Key | Description |
| --- | --- |
| `k` | Moves the cursor up. |
| `j` | Moves the cursor down. |
| `h` | Moves the cursor left. |
| `l` | Moves the cursor right. |

### Word Motions

A "word" is a sequence of letters, numbers, and underscores. A "WORD" is a sequence of non-whitespace characters.

| Key | Description |
| --- | --- |
| `w` | Moves to the beginning of the next word. |
| `b` | Moves to the beginning of the previous word. |
| `e` | Moves to the end of the next word. |
| `W` | Moves to the beginning of the next WORD (whitespace-separated). |
| `B` | Moves to the beginning of the previous WORD. |
| `E` | Moves to the end of the next WORD. |

### Character Find Motions

These motions work on the current line.

| Key | Description |
| --- | --- |
| `f{char}` | Moves the cursor to the next occurrence of `{char}`. |
| `F{char}` | Moves the cursor to the previous occurrence of `{char}`. |
| `t{char}` | Moves the cursor *until* (just before) the next occurrence of `{char}`. |
| `T{char}` | Moves the cursor *until* (just after) the previous occurrence of `{char}`. |

### Scrolling

| Key | Description |
| --- | --- |
| `^d` | Scrolls the view down by half a screen. |
| `^u` | Scrolls the view up by half a screen. |
| `zt` | Scrolls the view to put the cursor at the top. |
| `zz` | Scrolls the view to put the cursor in the center. |
| `zb` | Scrolls the view to put the cursor at the bottom. |

---

## Text Objects

Text objects are used with actions like `d` (delete) and `c` (change), and must be combined with a specifier (`i` for "inner" or `a` for "around"). For example, `diw` will delete the inner word under the cursor.

### Specifiers

| Key | Description |
| --- | --- |
| `i` | "inner" - selects the text object itself. |
| `a` | "around" - selects the text object and surrounding whitespace. |

### Word Text Objects

| Key | Description |
| --- | --- |
| `iw` | Inner word. |
| `aw` | Around a word (includes whitespace). |
| `iW` | Inner WORD. |
| `aW` | Around a WORD. |

---

## Search

| Key | Description |
| --- | --- |
| `/` | Searches forward for the entered text. |
| `?` | Searches backward for the entered text. |
| `n` | Moves to the next search result. |
| `p` | Moves to the previous search result. |

### Line Motions

The `g` key acts as a prefix for several special motions.

| Key | Description |
| --- | --- |
| `gh`| Go to the beginning of the line. |
| `gl`| Go to the end of the line. |

---

## Editing

### Entering Insert Mode

| Key | Description |
| --- | --- |
| `i` | Enters insert mode at the cursor. |
| `a` | Enters insert mode after the cursor. |
| `o` | Inserts a new line below the current one and enters insert mode. |
| `O` | Inserts a new line above the current one and enters insert mode. |

### Other Editing Commands

| Key | Description |
| --- | --- |
| `x` | Deletes the character under the cursor. |
| `.` | Repeats the last command. |
| `u` | Undoes the last change. |
| `U` | Redoes the last change. |

---

## Visual Mode

Visual mode is used to select text before performing an action. This is useful for operations on arbitrary or complex text regions that don't neatly fit a predefined motion.

| Key | Description |
| --- | --- |
| `v` | Enters character-wise visual mode. |
| `V` | Enters line-wise visual mode. |

Once in visual mode, move the cursor to select text, then press an action key (like `d` or `c`) to perform the action on the selection.

---

## File and Buffer Management

These commands are accessed by pressing the `space` bar, followed by another key.

| Key | Description |
| --- | --- |
| `f` | Shows the file picker to quickly open files. |
| `/` | Shows the search picker to search for text in the current project. |
| `b` | Shows the buffer picker to switch between open buffers. |
| `w` | Writes the current buffer to disk. |
| `W` | Writes the current buffer to disk, even if it's not modified. |
| `c` | Closes the current buffer if it's not modified. |
| `C` | Closes the current buffer, discarding any changes. |
| `q` | Quits the editor if there are no modified buffers. |
| `Q` | Quits the editor, discarding any changes in all buffers. |
