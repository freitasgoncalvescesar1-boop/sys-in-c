#ifndef LOW_H
#define LOW_H

#include <stdio.h>
#include <string.h>

#define LOW_COLOR_RESET   "\033[0m"
#define LOW_COLOR_BORDER  "\033[1;35m" // Magenta / Roxo
#define LOW_COLOR_PROJECT "\033[1;36m" // Ciano
#define LOW_COLOR_BIN     "\033[1;32m" // Verde
#define LOW_COLOR_TAG     "\033[1;33m" // Amarelo
#define LOW_COLOR_LABEL   "\033[1;37m" // Branco Negrito
#define LOW_COLOR_LINK    "\033[0;36m" // Ciano Claro
#define LOW_COLOR_MUTED   "\033[0;90m" // Cinza Escuro

static inline void low_print_banner(const char *tool_name) {
    printf("\n%s╭────────────────────────────────────────────────────────────────────────────╮%s\n", LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s│%s  %ssys-in-c%s %s#%s %slow-utils/%-24s%s           %s$~2026-2026~$%s   %s│%s\n",
           LOW_COLOR_BORDER, LOW_COLOR_RESET,
           LOW_COLOR_PROJECT, LOW_COLOR_RESET,
           LOW_COLOR_MUTED, LOW_COLOR_RESET,
           LOW_COLOR_BIN, tool_name, LOW_COLOR_RESET,
           LOW_COLOR_TAG, LOW_COLOR_RESET,
           LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s│%s  Created by Cesar                                                         %s│%s\n",
           LOW_COLOR_BORDER, LOW_COLOR_RESET, LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s├────────────────────────────────────────────────────────────────────────────┤%s\n",
           LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s│%s  For more information about the MIT license, visit:                        %s│%s\n",
           LOW_COLOR_BORDER, LOW_COLOR_RESET, LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s│%s   %s•%s %shttps://github.com/freitasgoncalvescesar1-boop/sys-in-c%s                  %s│%s\n",
           LOW_COLOR_BORDER, LOW_COLOR_RESET, LOW_COLOR_PROJECT, LOW_COLOR_RESET, LOW_COLOR_LINK, LOW_COLOR_RESET, LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s│%s   %s•%s %shttps://github.com/PLGCesar/sys-in-c%s                                   %s│%s\n",
           LOW_COLOR_BORDER, LOW_COLOR_RESET, LOW_COLOR_PROJECT, LOW_COLOR_RESET, LOW_COLOR_LINK, LOW_COLOR_RESET, LOW_COLOR_BORDER, LOW_COLOR_RESET);
    printf("%s╰────────────────────────────────────────────────────────────────────────────╯%s\n\n",
           LOW_COLOR_BORDER, LOW_COLOR_RESET);
}

#endif
