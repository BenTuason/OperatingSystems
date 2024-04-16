#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <algorithm> // For std::max
#include <vector>
#include <iostream>
#include <map>
#define MAX_RESOURCE_TYPES 10
#define MAX_INSTANCES 50
#define MAX_PROCESSES 10
#define MAX_INSTANCE_NAME_LENGTH 100
#define MAX_INSTRUCTIONS 100
#define MAX_STRING_LENGTH 100
#define MAX_LINE_LENGTH 256


typedef enum { EDF, LLF } SchedulerType;

typedef struct {
    char type[MAX_STRING_LENGTH]; // Resource type
    char instances[MAX_INSTANCES][MAX_STRING_LENGTH]; // Instances names
    int instance_count;
} ResourceType;

typedef struct {
    int original_deadline;
    int deadline; // Adjusted for relative deadline tracking
    int computation_time;
    char instructions[MAX_INSTRUCTIONS][MAX_STRING_LENGTH];
    int instruction_count;
    char masterString[MAX_STRING_LENGTH * MAX_INSTANCES]; // Adjusted size
    int deadline_misses; // Tracks how many times the process missed its deadline
} Process;

typedef struct {
    int pid; // Process ID for identification
    int deadline;
    int remainingTime; // Updated computation time
    int laxity; // For LLF scheduling
} ProcessState;

ProcessState processStates[MAX_PROCESSES];
SchedulerType currentScheduler = EDF;
int available[MAX_RESOURCE_TYPES];
int allocation[MAX_PROCESSES][MAX_RESOURCE_TYPES];
int maxDemand[MAX_PROCESSES][MAX_RESOURCE_TYPES];
Process processes[MAX_PROCESSES];
ResourceType resourceTypes[MAX_RESOURCE_TYPES];
int resourceTypeCount = 0, resourceCount, processCount;
int need[MAX_PROCESSES][MAX_RESOURCE_TYPES];
sem_t resourceAccess;
sem_t scheduleAccess;
void executeProcessInstructions(int processIndex);
// Function Prototypes
void parseOperationFile(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open operation file");
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE_LENGTH];
    fscanf(file, "%d %d", &resourceCount, &processCount);

    // Read available resources
    for (int i = 0; i < resourceCount; i++) {
        fscanf(file, "%d", &available[i]);
    }

    // Read max demand for each process
    for (int i = 0; i < processCount; i++) {
        for (int j = 0; j < resourceCount; j++) {
            fscanf(file, "%d", &maxDemand[i][j]);
        }
    }

    // Skip to the next line
    fgets(line, MAX_LINE_LENGTH, file);

    int currentProcess = -1;
    while (fgets(line, MAX_LINE_LENGTH, file)) {
        // Remove newline character
        line[strcspn(line, "\n")] = 0;

        // Check for process identifier
        if (strncmp(line, "process_", 8) == 0) {
            currentProcess++;
            sscanf(line, "process_%*d: %d %d", &processes[currentProcess].deadline, &processes[currentProcess].computation_time);
            processes[currentProcess].instruction_count = 0;
            processes[currentProcess].masterString[0] = '\0';
        } else if (currentProcess != -1) {
            // Assuming instructions don't span multiple lines
            strcpy(processes[currentProcess].instructions[processes[currentProcess].instruction_count], line);
            processes[currentProcess].instruction_count++;
        }
    }

    // Initialize the need matrix here, outside and after the while loop
    for (int i = 0; i < processCount; i++) {
        for (int j = 0; j < resourceCount; j++) {
            need[i][j] = maxDemand[i][j] - allocation[i][j]; // Initially, allocation is zero
        }
    }

    fclose(file);
}

void parseWordFile(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open word file");
        exit(EXIT_FAILURE);
    }

    char line[256];
    while (fgets(line, sizeof(line), file)) {
        // Remove newline character
        line[strcspn(line, "\n")] = 0;

        char* token = strtok(line, ":"); // Split line at the first ":"
        if (token != NULL) {
            strncpy(resourceTypes[resourceTypeCount].type, token, MAX_INSTANCE_NAME_LENGTH);
            token = strtok(NULL, ""); // This time, get the rest of the line after ":"
            if (token != NULL && token[0] == ' ') {
                // Skip leading spaces, if any, after ":"
                token++;
            }
            if (token != NULL) {
                // Now split the remaining part by ","
                char* instance = strtok(token, ",");
                int instanceIndex = 0;
                while (instance != NULL) {
                    // Trim leading spaces from each instance, if necessary
                    while(*instance == ' ') instance++;
                    strncpy(resourceTypes[resourceTypeCount].instances[instanceIndex], instance, MAX_INSTANCE_NAME_LENGTH);
                    instanceIndex++;
                    instance = strtok(NULL, ",");
                }
                resourceTypes[resourceTypeCount].instance_count = instanceIndex;
            }
            resourceTypeCount++;
        }
    }

    fclose(file);
}

void updateProcessStates() {
    for (int i = 0; i < processCount; i++) {
        // Assuming processes[i].deadline and processes[i].computation_time are available
        processStates[i].pid = i;
        processStates[i].deadline = processes[i].deadline; // Update based on your logic
        processStates[i].remainingTime = processes[i].computation_time; // Update based on your logic
        processStates[i].laxity = processStates[i].deadline - processStates[i].remainingTime; // For LLF
    }
}

int compareEDFLJF(const void* a, const void* b) {
    ProcessState* p1 = (ProcessState*)a;
    ProcessState* p2 = (ProcessState*)b;

    if (p1->deadline != p2->deadline) return p1->deadline - p2->deadline;
    return p2->remainingTime - p1->remainingTime; // Longer jobs first for tie
}

// Compare function for sorting by LLF then by SJF for tie-breaking
int compareLLFSJF(const void* a, const void* b) {
    ProcessState* p1 = (ProcessState*)a;
    ProcessState* p2 = (ProcessState*)b;

    if (p1->laxity != p2->laxity) return p1->laxity - p2->laxity;
    return p1->remainingTime - p2->remainingTime; // Shorter jobs first for tie
}

// Function to decide the next process based on currentScheduler
int getNextProcess() {
    if (currentScheduler == 0) { // EDF
        qsort(processStates, processCount, sizeof(ProcessState), compareEDFLJF);
    } else { // LLF
        qsort(processStates, processCount, sizeof(ProcessState), compareLLFSJF);
    }

    // Return the first process that can run (not completed)
    for (int i = 0; i < processCount; i++) {
        if (processStates[i].remainingTime > 0) return processStates[i].pid;
    }
    return -1; // No process can run
}


int findResourceTypeIndex(const char* resourceName) {
    for (int i = 0; i < resourceTypeCount; i++) {
        if (strcmp(resourceTypes[i].type, resourceName) == 0) {
            return i;
        }
    }
    return -1; // Not found
}

void updateMasterString(int processIndex, int resourceIndex, int amountUsed) {
    Process* proc = &processes[processIndex];

    if (strlen(proc->masterString) > 0) strcat(proc->masterString, ", ");

    // Assuming each `use_resources` consumes the first instance(s) available for simplicity
    for (int i = 0; i < amountUsed && i < resourceTypes[resourceIndex].instance_count; i++) {
        strcat(proc->masterString, resourceTypes[resourceIndex].instances[i]);
        if (i < amountUsed - 1) strcat(proc->masterString, ", ");
    }
}

void updateMasterStringAfterRelease(int processIndex) {
    Process* proc = &processes[processIndex];
    strcpy(proc->masterString, ""); // Reset the master string

    for (int j = 0; j < resourceTypeCount; j++) {
        if (allocation[processIndex][j] > 0) {
            if (strlen(proc->masterString) > 0) strcat(proc->masterString, ", ");
            strcat(proc->masterString, resourceTypes[j].type);
            for (int k = 0; k < allocation[processIndex][j] && k < resourceTypes[j].instance_count; k++) {
                strcat(proc->masterString, ": ");
                strcat(proc->masterString, resourceTypes[j].instances[k]);
            }
        }
    }
}
void updateMasterStringBasedOnAllocation(Process* proc) {
    // Clears and rebuilds the master string based on current allocations
    std::map<std::string, std::vector<std::string>> allocations;
    for (int i = 0; i < resourceTypeCount; i++) {
        for (int j = 0; j < allocation[proc - processes][i]; j++) {
            allocations[resourceTypes[i].type].push_back(resourceTypes[i].instances[j]);
        }
    }

    strcpy(proc->masterString, "");
    for (auto &alloc : allocations) {
        if (proc->masterString[0]) {
            strcat(proc->masterString, ", ");
        }
        strcat(proc->masterString, alloc.first.c_str());
        strcat(proc->masterString, ": ");
        for (size_t i = 0; i < alloc.second.size(); i++) {
            if (i > 0) {
                strcat(proc->masterString, ", ");
            }
            strcat(proc->masterString, alloc.second[i].c_str());
        }
    }
}

int canFinishWithAvailable(int processIndex, int work[], int finish[]) {
    for (int i = 0; i < resourceCount; i++) {
        if (maxDemand[processIndex][i] - allocation[processIndex][i] > work[i]) {
            return 0;
        }
    }
    return 1;
}

// The main safety check according to the Banker's Algorithm

int isStateSafe() {
    int work[MAX_RESOURCE_TYPES];
    int finish[MAX_PROCESSES] = {0};
    memcpy(work, available, sizeof(available));

    int found, safe = 1;
    do {
        found = 0;
        for (int i = 0; i < processCount; i++) {
            if (!finish[i]) {
                int j;
                for (j = 0; j < resourceCount; j++)
                    if (need[i][j] > work[j])
                        break;

                if (j == resourceCount) { // If all needs of process i are met
                    for (int k = 0; k < resourceCount; k++)
                        work[k] += allocation[i][k];
                    finish[i] = 1;
                    found = 1;
                }
            }
        }
    } while (found);

    for (int i = 0; i < processCount; i++)
        if (!finish[i])
            safe = 0;

    return safe;
}

// Adjusted isRequestSafe function to include validation and use the isStateSafe function
int isRequestSafe(int processIndex, int requestedResources[]) {
    sem_wait(&resourceAccess); // Lock the critical section

    // Validate request does not exceed the process's current needs
    int isRequestExceeds = 0;
    for (int i = 0; i < resourceCount; i++) {
        if (requestedResources[i] > need[processIndex][i] || requestedResources[i] > available[i]) {
            printf("Process %d: Request exceeds the process's needs or available resources.\n", processIndex + 1);
            isRequestExceeds = 1;
            break; // Exit the loop as no need to check further
        }
    }

    if (isRequestExceeds) {
        sem_post(&resourceAccess); // Unlock the critical section before returning
        return 0; // Indicate the request cannot be granted
    }

    // Temporarily allocate requested resources for the safety check
    for (int i = 0; i < resourceCount; i++) {
        available[i] -= requestedResources[i];
        allocation[processIndex][i] += requestedResources[i];
        need[processIndex][i] -= requestedResources[i];
    }

    // Perform the safety check using the Banker's Algorithm
    if (!isStateSafe()) {
        // Rollback if not safe
        for (int i = 0; i < resourceCount; i++) {
            available[i] += requestedResources[i];
            allocation[processIndex][i] -= requestedResources[i];
            need[processIndex][i] += requestedResources[i];
        }
        printf("Process %d: Request denied. Would lead to unsafe state.\n", processIndex + 1);
        sem_post(&resourceAccess); // Unlock the critical section before returning
        return 0; // Indicate the request cannot be granted
    }

    // If the code reaches here, it means the request was safe and has been successfully granted
    printf("Process %d: Request granted.\n", processIndex + 1);
    sem_post(&resourceAccess); // Unlock the critical section before returning
    return 1; // Indicate the request has been successfully granted
}
void scheduleNextProcess() {
    sem_wait(&scheduleAccess); // Lock scheduling control

    // Announce which scheduler is being used
    printf("Using Scheduler: %s\n", currentScheduler == EDF ? "EDF" : "LLF");

    // Ensure process states are updated to reflect current scheduling needs
    updateProcessStates();

    if (currentScheduler == EDF) {
        qsort(processStates, processCount, sizeof(ProcessState), compareEDFLJF);
    } else {
        qsort(processStates, processCount, sizeof(ProcessState), compareLLFSJF);
    }

    bool foundProcessToSchedule = false;
    for (int i = 0; i < processCount; i++) {
        if (processStates[i].remainingTime > 0) { // Process has remaining time
            if (!foundProcessToSchedule) {
                foundProcessToSchedule = true;
                printf("Process %d is scheduled next.\n", processStates[i].pid + 1);
                executeProcessInstructions(processStates[i].pid);
            } else {
                // Check for tie situation
                if (currentScheduler == EDF && processStates[i].deadline == processStates[i - 1].deadline ||
                    currentScheduler == LLF && processStates[i].laxity == processStates[i - 1].laxity) {
                    printf("Tie detected. Using tie-breaker logic.\n");
                    // For EDF, prefer the process with the longer remaining time (LJF). For LLF, prefer the process with the shorter remaining time (SJF).
                    int tieBreakerIndex = (currentScheduler == EDF) ? ((processStates[i].remainingTime > processStates[i - 1].remainingTime) ? i : i - 1) :
                                          ((processStates[i].remainingTime < processStates[i - 1].remainingTime) ? i : i - 1);
                    printf("Process %d selected by tie-breaker.\n", processStates[tieBreakerIndex].pid + 1);
                    executeProcessInstructions(processStates[tieBreakerIndex].pid);
                    break; // Break after selecting the process in tie situation
                }
            }
        }
    }

    if (!foundProcessToSchedule) {
        printf("No process scheduled next.\n");
    }

    sem_post(&scheduleAccess); // Unlock scheduling control
}

void executeProcessInstructions(int processIndex) {
    Process *proc = &processes[processIndex];
    int realTimePassed = 0; // Tracks the real time passed to adjust deadlines accurately.

    for (int i = 0; i < proc->instruction_count; i++) {
        char *instruction = proc->instructions[i];
        int execTime = 1;
        if (strncmp(instruction, "calculate", 9) == 0) {
            sscanf(instruction, "calculate(%d)", &execTime);
            proc->computation_time = std::max(0, proc->computation_time - execTime);
            if (proc->deadline - execTime < 0) {
                printf("Process %d will miss its deadline due to executing: %s\n", processIndex + 1, instruction);
                proc->deadline_misses++;
            }else
                printf("Process %d will not miss its deadline.", processIndex + 1);
        }
        else if (strncmp(instruction, "request", 7) == 0) {
            int requestedResources[MAX_RESOURCE_TYPES] = {0};
            sscanf(instruction, "request(%d, %d, %d)", &requestedResources[0], &requestedResources[1], &requestedResources[2]);

            if (isRequestSafe(processIndex, requestedResources)) {
                // Request is safe; update available and allocation
                for (int j = 0; j < resourceCount; j++) {
                    available[j] -= requestedResources[j];
                    allocation[processIndex][j] += requestedResources[j];
                }
                printf("Process %d: Request granted.\n", processIndex + 1);
            } else {
                printf("Process %d: Request denied.\n", processIndex + 1);
            }
            execTime = 1;
        } else if (strncmp(instruction, "use_resources", 13) == 0) {
            int resourceTypeIndex, amountUsed;
            sscanf(instruction, "use_resources(%d,%d)", &resourceTypeIndex, &amountUsed);
            resourceTypeIndex--; // Adjust index to match your 0-based array indexing

            if(resourceTypeIndex >= 0 && resourceTypeIndex < resourceTypeCount) {
                // Ensure the process has the resources to use
                if (amountUsed <= allocation[processIndex][resourceTypeIndex]) {
                    // Simulate resource usage by adjusting allocation
                    allocation[processIndex][resourceTypeIndex] -= amountUsed;
                    // Update the master string to reflect the current resource state
                    updateMasterStringBasedOnAllocation(proc);
                    printf("Process %d -- Master string after using resources: %s\n", processIndex + 1, proc->masterString);
                } else {
                    printf("Process %d: Attempt to use more resources than allocated.\n", processIndex + 1);
                }
            } else {
                printf("Process %d: Resource type index %d out of bounds.\n", processIndex + 1, resourceTypeIndex + 1);
            }

        } else if (strncmp(instruction, "release", 7) == 0) {
            int releaseResources[MAX_RESOURCE_TYPES] = {0};
            sscanf(instruction, "release(%d, %d, %d)",
                   &releaseResources[0], &releaseResources[1], &releaseResources[2]);

            for (int j = 0; j < resourceCount; j++) {
                if (releaseResources[j] > 0 && releaseResources[j] <= allocation[processIndex][j]) {
                    allocation[processIndex][j] -= releaseResources[j];
                    available[j] += releaseResources[j];
                }
            }
            // Update the master string after release
            updateMasterStringBasedOnAllocation(proc);
            printf("Process %d: Resources released. Master string updated: %s\n", processIndex + 1, proc->masterString);
            execTime = 1;
        } else if (strcmp(instruction, "print_resources_used") == 0) {
            printf("Process %d master string: %s\n", processIndex + 1, proc->masterString);
            execTime = 1;
        }
        if (strcmp(instruction, "print_resources_used") != 0) {
            proc->computation_time = std::max(0, proc->computation_time - execTime);
        }

        // Directly adjust deadline after processing each instruction, simplified without realTimePassed.
        proc->deadline = std::max(0, proc->deadline - execTime);
        // Check for deadline misses after each instruction.
        if (proc->deadline_misses > 0) {
            printf("Process %d missed its deadline %d times.\n", processIndex + 1, proc->deadline_misses);
        } else {
            printf("Process %d did not miss its deadline.\n", processIndex + 1);
        }
    }
}





int main(int argc, char *argv[]) {
    sem_init(&resourceAccess, 0, 1);
    sem_init(&scheduleAccess, 0, 1);
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <operation_file> <word_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Parse the operation file
    parseOperationFile(argv[1]);
    // Parse the word file
    parseWordFile(argv[2]);

    pid_t pid;
    for (int i = 0; i < processCount; i++) {
        pid = fork();
        if (pid == 0) { // Child prsocess
            // Execute instructions for process i
            executeProcessInstructions(i);
            exit(0); // Child process exits after executing its instructions
        } else if (pid < 0) {
            perror("fork failed");
            exit(EXIT_FAILURE);
        }
    }

    // Wait for all child processes to complete
    while (wait(NULL) > 0);
    sem_destroy(&resourceAccess);
    sem_destroy(&scheduleAccess);
    return EXIT_SUCCESS;

}