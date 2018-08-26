#!/usr/bin/fish

# -i inplace edit
clang-format -sort-includes -style=webkit -i $argv
clang-tidy $argv -fix -checks="readability-braces-around-statements"
make
# make clean > /dev/null

