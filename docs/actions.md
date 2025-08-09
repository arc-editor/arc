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

| Key | Action  |
| --- | ------- |
| `d` | Delete  |
| `c` | Change  |

The "change" action deletes the target text and then enters insert mode, allowing you to replace it.

---

## Text Object Specifiers

Some targets can be combined with a specifier to change what is selected. The specifier comes after the action and before the target (e.g. `d` `i` `w`).

| Key | Description                               |
| --- | ----------------------------------------- |
| `i` | "inner" - selects the text object itself. |
| `a` | "around" - selects the text object and surrounding whitespace. |

---

## Targets (Motions and Text Objects)

Targets are the nouns of the command system. They define the region of text that an action will operate on. They can be either motions or text objects.

### Word Motions

| Key | Description                               |
| --- | ----------------------------------------- |
| `w` | Moves to the beginning of the next word.  |
| `b` | Moves to the beginning of the current word. If already at the beginning of a word, moves to the beginning of the previous word. |
| `e` | Moves to the end of the current word. If already at the end of a word, moves to the end of the next word. |
| `W` | Moves to the beginning of the next WORD (whitespace-separated). |
| `B` | Moves to the beginning of the previous WORD. |
| `E` | Moves to the end of the current WORD.     |

A "word" is a sequence of letters, numbers, and underscores. A "WORD" is a sequence of non-whitespace characters.

**Note:** When used with an action like `d` or `c`, the `w` and `W` motions behave like `e` and `E`. For example, `dw` will delete from the cursor to the end of the current word, just like `de`.

### Word Text Objects

These are used with actions like `d` and `c`, and must be combined with a specifier (`i` or `a`).

| Key | Description                               |
| --- | ----------------------------------------- |
| `iw` | Inner word.                             |
| `aw` | Around a word (includes whitespace).    |
| `iW` | Inner WORD.                             |
| `aW` | Around a WORD.                          |

### Character Find Motions

These motions work on the current line.

| Key     | Description                                        |
| ------- | -------------------------------------------------- |
| `f{char}` | Moves the cursor to the next occurrence of `{char}`. |
| `F{char}` | Moves the cursor to the previous occurrence of `{char}`. |
| `t{char}` | Moves the cursor *until* (just before) the next occurrence of `{char}`. |
| `T{char}` | Moves the cursor *until* (just after) the previous occurrence of `{char}`. |

### Paragraph Motions

| Key | Description                                                               |
| --- | ------------------------------------------------------------------------- |
| `np`| Moves the cursor to the beginning of the next paragraph.                  |
| `pp`| Moves the cursor to the beginning of the previous paragraph.              |

### Special Go-to Motions

The `g` key acts as a prefix for several special motions.

| Key | Description                                |
| --- | ------------------------------------------ |
| `ge`| Go to the end of the document.             |
| `gb`| Go to the beginning of the document.       |

---

## Visual Mode

Visual mode is an alternative way to define a target for an action.

1.  Press `v` to enter visual mode.
2.  Move the cursor to select a range of text.
3.  Press an action key (like `d` or `c`) to perform the action on the selected text.

This is useful for operations on arbitrary or complex text regions that don't neatly fit a predefined motion.
