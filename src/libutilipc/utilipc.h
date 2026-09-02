#ifndef UTILIPC_H
#define UTILIPC_H

#include <time.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define UTILIPC_SHM_NAME     "/utils_ipc_shm"
#define UTILIPC_MAX_MSG      256
#define UTILIPC_HISTORY_SIZE 16
#define UTILIPC_MAX_PROCS    16
#define UTILIPC_MEM_CHUNKS   32

// Tipos de Estado dos Blocos de Memória
#define MEM_SLOT_FREE      0  // Bloco Livre (░)
#define MEM_SLOT_KERNEL    1  // Bloco Kernel / Shared (■)
#define MEM_SLOT_ACTIVE    2  // Bloco Ativo em Uso (■)
#define MEM_SLOT_PINNED    3  // Bloco Travado / Lock (▣)
#define MEM_SLOT_CACHE     4  // Bloco em Cache (▲)

// Registro de Comunicação Inter-Processos
typedef struct {
    char sender_tool[32];
    char target_tool[32];
    pid_t sender_pid;
    char message[UTILIPC_MAX_MSG];
    time_t timestamp;
} utilipc_comm_entry_t;

// Registro de Histórico de Ações
typedef struct {
    char tool[32];
    char action[UTILIPC_MAX_MSG];
    time_t timestamp;
} utilipc_log_entry_t;

// Processos Ativos Rastreados
typedef struct {
    pid_t pid;
    char tool[32];
    time_t start_time;
    int is_active;
} utilipc_proc_entry_t;

// Estrutura Completa de Telemetria e SHM
typedef struct {
    double ram_used_mb;
    double ram_total_mb;
    double cpu_load1;
    char last_action[UTILIPC_MAX_MSG];
    char last_writer[32];
    pid_t last_caller_pid;
    time_t last_updated;
    unsigned int total_ipc_calls;

    // Matriz de 32 Blocos de Memória
    uint8_t mem_slots[UTILIPC_MEM_CHUNKS];
    uint8_t mem_slots_prev[UTILIPC_MEM_CHUNKS];
    time_t last_slot_update;

    // Histórico de Ações
    utilipc_log_entry_t history[UTILIPC_HISTORY_SIZE];
    unsigned int history_head;
    unsigned int history_count;

    // Comunicação Inter-Processos
    utilipc_comm_entry_t comm_log[UTILIPC_HISTORY_SIZE];
    unsigned int comm_head;
    unsigned int comm_count;

    // Processos Ativos
    utilipc_proc_entry_t active_procs[UTILIPC_MAX_PROCS];
    unsigned int active_proc_count;
} utilipc_data_t;

// Funções Principais da .SO
int utilipc_init(void);
void utilipc_close(void);

int utilipc_log(const char *tool, const char *action);
int utilipc_send_msg(const char *sender, const char *target, const char *msg);
int utilipc_write_status(double ram_used, double ram_total, double load1, const char *action);
int utilipc_read_status(utilipc_data_t *out_data);

int utilipc_register_process(const char *tool_name);
int utilipc_unregister_process(void);
void utilipc_touch_mem_chunk(int chunk_idx, uint8_t state);

#endif
