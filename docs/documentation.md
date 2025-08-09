---
layout: default
title: Arc Editor - Documentation
---

# Documentation

[Back to Home](index.html)

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
