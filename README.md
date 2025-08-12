# Arc

A fast, lightweight, and modal text editor designed for speed, efficiency, and a seamless out-of-the-box experience.

## Philosophy

Arc is born from a love of Vim's powerful keybindings and the simplicity of "batteries-included" editors like Helix.

*   **Vim-inspired, simplified:** Arc uses a simple `action -> target` modal editing model. More efficient than Vim's motions without the steep learning curve.
*   **No plugins needed:** Core functionality like a fuzzy finder, Git integration, and themes are (soon-to-be) built-in. No more managing a complex ecosystem of plugins.
*   **Fast and Lightweight:** Written in C, Arc is designed for minimal memory usage and maximum performance.

## Key Features

*   **Modal Editing:** Simple and intuitive `action -> target` keybindings.
*   **Tree-sitter Integration:** Fast and accurate syntax highlighting.
*   **Built-in Git Tools:** Basic integration is planned, starting with displaying the current branch and signs in the gutter.
*   **Fuzzy Finder:** Quickly find and open files.
*   **Theming:** Customize the look and feel of the editor.
*   **LSP Support:** Language Server Protocol support is planned for future development.

## Project Status

**Pre-Alpha:** Arc is currently in the early stages of development. It is usable for some development tasks, but may be unstable and lacks key features like LSP support.

## Installation

To install Arc, you will need `clang` and `make`.

```bash
# Clone the repository
git clone https://github.com/arc-editor/arc.git
cd arc

# Initialize submodules
git submodule update --init --recursive

# Compile the source code
make

# Install the binary
sudo make install
```

## Configuration

Arc is configured using a simple TOML file. The goal is to provide a powerful configuration system that remains easy to understand and manage. (More documentation to come as the configuration system is finalized).

## Contributing

Contributions are welcome! As a pre-alpha project, there are many opportunities to help shape the future of Arc. Feel free to open an issue or submit a pull request.
