#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>

#define SHM_KEY 0x1234
#define SEM_KEY 0x5678
#define DISK_QUEUE_SIZE 100

typedef struct {
    int pid;
    int num_pages;
    int* tracks;
} ProcessDiskInfo;

typedef struct {
    int total_page_frames;
    int page_size;
    int pages_per_process;
    int lookahead_or_X;
    int min_free_pool_size;
    int max_free_pool_size;
    int total_processes;
    int max_track_number;
    int disk_queue_size;
    ProcessDiskInfo* processes;
    int MAX_FRAMES;
    int* frame_table;
    int head;
} SimulationParams;

typedef struct {
    int process_id;
    bool read_request;
    int frame_index;
    int disk_address;
} DiskRequest;

typedef struct {
    DiskRequest queue[DISK_QUEUE_SIZE];
    int front;
    int rear;
    int count;
} DiskQueue;

void freeProcessDiskInfo(ProcessDiskInfo* process) {
    free(process->tracks);
    process->tracks = NULL;
}

void cleanupSimulationParams(SimulationParams* params) {
    for (int i = 0; i < params->total_processes; i++) {
        freeProcessDiskInfo(&params->processes[i]);
    }
    free(params->processes);
    params->processes = NULL;
    free(params->frame_table);
    params->frame_table = NULL;
}

void initializeSimulationParams(SimulationParams* params) {
    params->MAX_FRAMES = 10;
    params->frame_table = (int*) malloc(params->MAX_FRAMES * sizeof(int));
    if (!params->frame_table) {
        perror("Failed to allocate memory for frame table");
        exit(EXIT_FAILURE);
    }
    memset(params->frame_table, -1, params->MAX_FRAMES * sizeof(int));
    params->head = 0;
}

void readInputFile(const char* filename, SimulationParams* params) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }

    char line[256];
    int currentProcessIndex = -1;

    while (fgets(line, sizeof(line), file)) {
        char* pos;
        if ((pos = strchr(line, '\n')) != NULL) *pos = '\0';

        if (line[0] == '#' || line[0] == '\0') continue;

        if (strncmp(line, "k ", 2) == 0) {
            sscanf(line, "k %d", &params->total_processes);
            params->processes = (ProcessDiskInfo*) calloc(params->total_processes, sizeof(ProcessDiskInfo));
            if (!params->processes) {
                perror("Failed to allocate memory for processes");
                fclose(file);
                exit(EXIT_FAILURE);
            }
        } else if (strncmp(line, "pid", 3) == 0) {
            int pid, num_pages;
            sscanf(line, "pid%d %d", &pid, &num_pages);
            currentProcessIndex = pid - 1;
            params->processes[currentProcessIndex].pid = pid;
            params->processes[currentProcessIndex].num_pages = num_pages;
            params->processes[currentProcessIndex].tracks = (int*) malloc(num_pages * sizeof(int));
            if (!params->processes[currentProcessIndex].tracks) {
                perror("Failed to allocate memory for tracks");
                fclose(file);
                exit(EXIT_FAILURE);
            }
        } else if (isdigit(line[0])) {
            int page, track;
            sscanf(line, "%d %d", &page, &track);
            if (currentProcessIndex != -1 && page < params->processes[currentProcessIndex].num_pages) {
                params->processes[currentProcessIndex].tracks[page] = track;
            }
        }
    }

    fclose(file);
    printf("Configuration and process data loaded successfully.\n");
}

void handlePageFault(SimulationParams* params, int pageNumber, DiskQueue* disk_queue) {
    printf("Handling page fault for page number %d...\n", pageNumber);
    int found = 0;
    for (int i = 0; i < params->MAX_FRAMES; i++) {
        if (params->frame_table[i] == pageNumber) {
            printf("Page %d found in frame %d (Page hit)\n", pageNumber, i);
            found = 1;
            break;
        }
    }
    if (!found) {
        printf("Page %d not found in any frame (Page miss). Replacing page in frame %d.\n", pageNumber, params->head);
        params->frame_table[params->head] = pageNumber;
        params->head = (params->head + 1) % params->MAX_FRAMES;

        DiskRequest request;
        request.process_id = getpid();
        request.read_request = true;
        request.frame_index = params->head;
        request.disk_address = pageNumber * params->page_size;

        if (disk_queue->count < DISK_QUEUE_SIZE) {
            disk_queue->rear = (disk_queue->rear + 1) % DISK_QUEUE_SIZE;
            disk_queue->queue[disk_queue->rear] = request;
            disk_queue->count++;
            printf("Enqueued new disk request for process %d, page %d.\n", getpid(), pageNumber);
        } else {
            fprintf(stderr, "Disk queue is full. Cannot enqueue page fault request.\n");
        }
    }
    printf("Page fault handling completed.\n");
}

void handlePageFaultWithTimeout(SimulationParams* params, int pageNumber, DiskQueue* disk_queue) {
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(0, &read_fds);

    int result = select(1, &read_fds, NULL, NULL, &timeout);
    if (result == -1) {
        perror("select failed");
        exit(EXIT_FAILURE);
    } else if (result == 0) {
        printf("Timeout occurred. No disk request received within 5 seconds for page %d.\n", pageNumber);
        return;
    }

    handlePageFault(params, pageNumber, disk_queue);
}

void diskDriverProcess(int sem_id, DiskQueue *disk_queue) {
    printf("Disk driver process started.\n");

    while (true) {
        struct sembuf sb = {0, -1, 0};
        semop(sem_id, &sb, 1);

        if (disk_queue->count > 0) {
            DiskRequest request = disk_queue->queue[disk_queue->front];
            printf("Processing disk request: Process ID %d, Frame Index %d, Disk Address %d\n",
                   request.process_id, request.frame_index, request.disk_address);
            sleep(1);

            sb.sem_op = 1;
            semop(sem_id, &sb, 1);

            disk_queue->front = (disk_queue->front + 1) % DISK_QUEUE_SIZE;
            disk_queue->count--;
            printf("Processed disk request, %d requests remaining.\n", disk_queue->count);
        } else {
            printf("Disk queue is empty. Waiting for requests...\n");
        }

        if (disk_queue->count == 0) {
            printf("No more requests in the disk queue. Exiting...\n");
            break;
        }
    }
}

void childProcess(SimulationParams *params, DiskQueue* disk_queue, int process_id) {
    printf("Child process %d running, ready to handle page faults...\n", process_id);
    bool found = false;

    for (int i = 0; i < params->total_processes; i++) {
        if (params->processes[i].pid == process_id) {
            found = true;
            ProcessDiskInfo process = params->processes[i];
            printf("Found process %d with %d pages.\n", process_id, process.num_pages);
            for (int j = 0; j < process.num_pages; j++) {
                printf("Request to access page %d for process %d on track %d\n", j, process_id, process.tracks[j]);
                handlePageFaultWithTimeout(params, process.tracks[j], disk_queue);
                sleep(1);
            }
            printf("All page requests processed for process %d, child process %d exiting...\n", process_id, getpid());
            exit(0);
        }
    }
    if (!found) {
        fprintf(stderr, "Process ID %d not found in initialization list.\n", process_id);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s input_filename\n", argv[0]);
        return EXIT_FAILURE;
    }
    printf("Starting main program.\n");

    int shm_id = shmget(SHM_KEY, sizeof(SimulationParams), 0666 | IPC_CREAT);
    if (shm_id == -1) {
        perror("shmget failed");
        return EXIT_FAILURE;
    }
    SimulationParams* params = (SimulationParams*) shmat(shm_id, NULL, 0);
    if (params == (void*) -1) {
        perror("shmat failed");
        return EXIT_FAILURE;
    }
    initializeSimulationParams(params);

    int sem_id = semget(SEM_KEY, 1, IPC_CREAT | 0666);
    if (sem_id == -1) {
        perror("semget failed");
        cleanupSimulationParams(params);
        shmdt(params);
        shmctl(shm_id, IPC_RMID, NULL);
        return EXIT_FAILURE;
    }
    semctl(sem_id, 0, SETVAL, 1);

    readInputFile(argv[1], params);  // Fixed to ensure proper initialization

    DiskQueue disk_queue;
    disk_queue.front = 0;
    disk_queue.rear = -1;
    disk_queue.count = 0;

    pid_t pid = fork();
    if (pid == 0) {
        diskDriverProcess(sem_id, &disk_queue);
    } else {
        for (int i = 0; i < params->total_processes; i++) {
            childProcess(params, &disk_queue, params->processes[i].pid);
        }
        int status;
        while (wait(&status) > 0);

        cleanupSimulationParams(params);
        shmdt(params);
        shmctl(shm_id, IPC_RMID, NULL);
        semctl(sem_id, 0, IPC_RMID, NULL);
        printf("Main process completed, cleanup done.\n");
    }

    return EXIT_SUCCESS;
}
