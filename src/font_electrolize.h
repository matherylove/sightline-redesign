// font_electrolize.h — auto-generated at build time by the CI workflow
// using: xxd -i Electrolize-Regular.ttf > src/font_electrolize.h
// (with name mangling to electrolize_regular_ttf / electrolize_regular_ttf_len)
//
// Do NOT hand-edit this file. It will be overwritten on every build.
// If you need to regenerate locally:
//   xxd -i Electrolize-Regular.ttf | sed \
//     -e 's/unsigned char .*_ttf/const unsigned char electrolize_regular_ttf/g' \
//     -e 's/unsigned int .*_ttf_len/const unsigned int electrolize_regular_ttf_len/g' \
//     > src/font_electrolize.h
//
// Placeholder — the array below is empty so the code compiles without
// the real font present. The HAS_ELECTROLIZE_EMBED guard in main.cpp
// checks electrolize_regular_ttf_len > 0 before using it.

#pragma once
static const unsigned char electrolize_regular_ttf[]  = { 0 };
static const unsigned int  electrolize_regular_ttf_len = 0;
