#!/usr/bin/env python

import argparse
import os

PREAMBLE = """// Generated automatically from {0}. Do not edit.
static const char* {1}Source =
{2};
"""


def blu_to_c_string(input_path, source_lines, module):
    blu_source = ""

    for line in source_lines:
        line = line.replace('"', '\\"')
        line = line.replace('\n', '\\n"')

        if blu_source:
            blu_source += '\n'

        blu_source += '"' + line

    return PREAMBLE.format(input_path, module, blu_source)


def main():
    parser = argparse.ArgumentParser(
        description='Convert a blu library to a C string literal.')
    parser.add_argument('input', help='The source .blu file')
    parser.add_argument('output', help='Output .c file')

    args = parser.parse_args()

    module = os.path.splitext(os.path.basename(args.input))[0]

    with open(args.input, 'r') as f:
        source_lines = f.readlines()

    c_source = blu_to_c_string(args.input, source_lines, module)

    with open(args.output, 'w') as f:
        f.write(c_source)


main()
