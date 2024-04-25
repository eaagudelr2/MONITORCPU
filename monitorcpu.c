// Incluir directivas de preprocesador necesarias
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>

#define MAX_PROCESSES 1024
#define ALERT_THRESHOLD 90.0
#define MAX_TOP_PROCESSES 20

// Estructura para almacenar información sobre un proceso
typedef struct {
    int pid;
    char name[256];
    char command[256];
    float cpu_usage;
    float time;
} ProcessInfo;

// Prototipos de funciones
void call_alert_script();
double get_cpu_percent();
void *monitor_cpu(void *arg);
int compare_processes(const void *a, const void *b);
int get_process_list(ProcessInfo *processes);

int main() {
    // Crear un hilo para monitorear el uso de CPU
    pthread_t cpu_thread;
    if (pthread_create(&cpu_thread, NULL, monitor_cpu, NULL) != 0) {
        perror("Error creating CPU monitoring thread");
        exit(EXIT_FAILURE);
    }

    // Ciclo infinito para monitorear la lista de procesos
    while (1) {
        ProcessInfo processes[MAX_PROCESSES];
        int process_count = get_process_list(processes);
        if (process_count > 0) {
            printf("\nTop %d Processes by CPU Usage:\n", MAX_TOP_PROCESSES);
            printf("PID   NAME                  CPU Usage   Command                        Time\n");
            printf("----------------------------------------------------------------------------\n");
            for (int i = 0; i < MAX_TOP_PROCESSES && i < process_count; i++) {
                printf("%-5d %-20s %-10.2f%% %-30s %-10.2f\n", processes[i].pid, processes[i].name,
                       processes[i].cpu_usage, processes[i].command, processes[i].time);
            }

            printf("\n");
        }

        sleep(15); // Esperar 15 segundos antes de volver a consultar la lista de procesos
    }

    return 0;
}

// Implementación de funciones

void call_alert_script() {
    system("./alerta.sh");
}

double get_cpu_percent() {
    static double prev_used_cpu_time = 0.0;
    static double prev_total_cpu_time = 0.0;

    FILE* file = fopen("/proc/stat", "r");
    if (file == NULL) {
        perror("Error opening /proc/stat");
        exit(EXIT_FAILURE);
    }

    char line[256];
    fgets(line, sizeof(line), file); // Leer la primera línea que contiene el total de la CPU
    fclose(file);

    // Analizar la línea para obtener los tiempos de CPU
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal, guest, guest_nice;
    sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal, &guest, &guest_nice);

    // Calcular el total de la CPU y el tiempo de uso de la CPU
    unsigned long long total_cpu_time = user + nice + system + idle + iowait + irq + softirq + steal;
    unsigned long long used_cpu_time = total_cpu_time - idle;

    // Calcular el porcentaje de uso de la CPU
    double cpu_percent;
    if (prev_total_cpu_time != 0.0) {
        double total_diff = total_cpu_time - prev_total_cpu_time;
        double used_diff = used_cpu_time - prev_used_cpu_time;
        cpu_percent = (used_diff / total_diff) * 100.0;
    } else {
        cpu_percent = 0.0;
    }

    prev_total_cpu_time = total_cpu_time;
    prev_used_cpu_time = used_cpu_time;

    printf("CPU Usage: %.2f%%\n", cpu_percent);
    return cpu_percent;
}

void *monitor_cpu(void *arg) {
    while (1) {
        double cpu_percent = get_cpu_percent();
        if (cpu_percent > ALERT_THRESHOLD) {
            printf("CPU usage is greater than %.2f%%\n", ALERT_THRESHOLD);
            // Llamar al script de alerta si el uso de la CPU supera el umbral
            call_alert_script();
        }

        sleep(5); // Esperar 5 segundos antes de volver a consultar
    }
    return NULL;
}

int compare_processes(const void *a, const void *b) {
    ProcessInfo *pa = (ProcessInfo *)a;
    ProcessInfo *pb = (ProcessInfo *)b;
    return (pb->cpu_usage - pa->cpu_usage); // Orden descendente
}

int get_process_list(ProcessInfo *processes) {
    DIR *dir = opendir("/proc");
    struct dirent *entry;
    int count = 0;

    if (dir != NULL) {
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_DIR) {
                char path[PATH_MAX];
                snprintf(path, sizeof(path), "/proc/%s/stat", entry->d_name);

                FILE *stat_file = fopen(path, "r");
                if (stat_file != NULL) {
                    int pid;
                    char name[256], command[256];
                    char state;

                    unsigned long utime, stime;
                    fscanf(stat_file, "%d %s %c %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %lu %lu",
                           &pid, name, &state, &utime, &stime);
                    fclose(stat_file);

                    // Calcular el uso de CPU para el proceso actual
                    unsigned long total_time = utime + stime;
                    float seconds = sysconf(_SC_CLK_TCK);
                    float cpu_usage = 100.0 * ((total_time / seconds) / (float)sysconf(_SC_CLK_TCK));

                    // Almacenar información sobre el proceso
                    processes[count].pid = pid;
                    strncpy(processes[count].name, name, sizeof(processes[count].name) - 1);
                    processes[count].name[sizeof(processes[count].name) - 1] = '\0'; // Asegurar terminación nula
                    processes[count].cpu_usage = cpu_usage;

                    // Obtener el comando del proceso
                    snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
                    FILE *cmd_file = fopen(path, "r");
                    if (cmd_file != NULL) {
                        fscanf(cmd_file, "%255s", command); // Lee hasta 255 caracteres (terminado por nulo)
                        fclose(cmd_file);
                        strncpy(processes[count].command, command, sizeof(processes[count].command) - 1);
                        processes[count].command[sizeof(processes[count].command) - 1] = '\0'; // Asegurar terminación nula
                    } else {
                        strcpy(processes[count].command, "N/A"); // Si no se puede obtener el comando, establecer como "N/A"
                    }

                    // Almacenar el tiempo de ejecución del proceso
                    processes[count].time = (float)total_time / sysconf(_SC_CLK_TCK);

                    count++;
                }
            }
        }
        closedir(dir);
    } else {
        perror("Error opening /proc directory");
        return -1;
    }

    // Ordenar los procesos por uso de CPU (orden descendente)
    qsort(processes, count, sizeof(ProcessInfo), compare_processes);

    return count;
}
