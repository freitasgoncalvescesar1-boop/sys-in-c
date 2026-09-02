#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>

/* Estrutura de Estado Global para Depuração e Telemetria */
typedef struct {
    int32_t secret_life;
    char secret_message[64];
    char *heap_buffer;
    volatile sig_atomic_t is_paused;
    uint32_t iteration_count;
} app_state_t;

static app_state_t g_state;

/* Handler de Sinal para Dump de Diagnóstico sob Demanda (SIGUSR1) */
static void debug_signal_handler(int sig) {
    if (sig == SIGUSR1) {
        printf("\n\033[1;33m[DEBUG SIGNAL (SIGUSR1) RECEBIDO]\033[0m\n");
        printf("  • Iteração Atual : %u\n", g_state.iteration_count);
        printf("  • secret_life    : %d (Endereço: %p)\n", g_state.secret_life, (void*)&g_state.secret_life);
        printf("  • secret_message : \"%s\" (Endereço: %p)\n", g_state.secret_message, (void*)g_state.secret_message);
        printf("  • heap_buffer    : \"%s\" (Endereço: %p)\n\n", g_state.heap_buffer, (void*)g_state.heap_buffer);
        fflush(stdout);
    } else if (sig == SIGUSR2) {
        g_state.is_paused = !g_state.is_paused;
        printf("\n\033[1;35m[DEBUG SIGNAL (SIGUSR2)] Estado alterado para: %s\033[0m\n\n",
               g_state.is_paused ? "PAUSADO" : "RODANDO");
        fflush(stdout);
    }
}

static void print_banner(void) {
    printf("\n========================================================\n");
    printf("[ ALVO DE TESTE DEPURÁVEL - PID: %d ]\n", getpid());
    printf("========================================================\n");
    printf("  • Variavel Inteira (secret_life) : %d  (Stack: %p)\n", g_state.secret_life, (void*)&g_state.secret_life);
    printf("  • Mensagem na Stack              : \"%s\" (Stack: %p)\n", g_state.secret_message, (void*)g_state.secret_message);
    printf("  • Buffer Dinamico                : \"%s\" (Heap:  %p)\n", g_state.heap_buffer, (void*)g_state.heap_buffer);
    printf("========================================================\n");
    printf("Controles de Depuração Disponíveis em Outro Terminal:\n");
    printf("  1. Inspecionar via peekmem : ./peekmem %d -s \"CHAVE_SECRETA\"\n", getpid());
    printf("  2. Solicitar Dump de Estado: kill -USR1 %d\n", getpid());
    printf("  3. Pausar/Continuar Execução: kill -USR2 %d\n", getpid());
    printf("========================================================\n\n");
}

int main(void) {
    /* Configuração de Sinais de Depuração */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = debug_signal_handler;
    sigaction(SIGUSR1, &sa, NULL);
    sigaction(SIGUSR2, &sa, NULL);

    /* Inicialização do Estado */
    g_state.secret_life = 9999;
    strncpy(g_state.secret_message, "CHAVE_SECRETA_DO_SISTEMA_2026", sizeof(g_state.secret_message) - 1);
    g_state.heap_buffer = malloc(128);
    if (!g_state.heap_buffer) {
        perror("malloc");
        return 1;
    }
    strcpy(g_state.heap_buffer, "HEAP_DATA_DINAMICA_SUPER_CONFIDENCIAL");
    g_state.is_paused = 0;
    g_state.iteration_count = 0;

    print_banner();

    while (1) {
        if (!g_state.is_paused) {
            g_state.iteration_count++;
            printf("\r[*] Loop de execução [Tick: %-6u | Vida: %-5d] ",
                   g_state.iteration_count, g_state.secret_life);
            fflush(stdout);
        }
        sleep(1);
    }

    free(g_state.heap_buffer);
    return 0;
}
