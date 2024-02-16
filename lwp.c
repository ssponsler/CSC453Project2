#include <stdio.h>

#define MAX_PROCESSES 10
#define TIME_QUANTUM 4

typedef struct {
    int id;
    int arrival_time;
    int burst_time;
    int remaining_time;
} Process;

void rr(Process processes[], int n) {
    int total_burst_time = 0;
    for (int i = 0; i < n; i++) {
        total_burst_time += processes[i].burst_time;
        processes[i].remaining_time = processes[i].burst_time;
    }

    int current_time = 0;
    while (total_burst_time > 0) {
        for (int i = 0; i < n; i++) {
            if (processes[i].remaining_time > 0) {
                int execute_time = processes[i].remaining_time < TIME_QUANTUM ? processes[i].remaining_time : TIME_QUANTUM;
                current_time += execute_time;
                processes[i].remaining_time -= execute_time;
                total_burst_time -= execute_time;
                printf("Process %d executed for %d units at time %d\n", processes[i].id, execute_time, current_time);
            }
        }
    }
}

int main() {
    Process processes[MAX_PROCESSES] = {
        {0, 0, 5, 0},
        {1, 1, 4, 0},
        {2, 2, 2, 0},
        {3, 3, 1, 0}
    };

    int n = sizeof(processes) / sizeof(processes[0]);

    rr(processes, n);

    return 0;
}
