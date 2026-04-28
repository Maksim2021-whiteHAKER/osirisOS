// x86_64/core/shell.h
#ifndef SHELL_H
#define SHELL_H

void shell_main(void);
int start_with(const char* str, const char* prefix);
int find_and_complete(void);

#endif