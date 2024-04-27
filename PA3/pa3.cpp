#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <iomanip>
#include <list>
#include <algorithm>



// Data Structures
struct PageTableEntry {
    int frame_number;
    int disk_address;
};

struct FrameTableEntry {
    int process_id;
    int page_number;
    int forward_link;
    int backward_link;
};

struct DiskQueueEntry {
    int process_id;
    char operation;  // 'R' for read, 'W' for write
    int frame_index;
    int disk_addr;
};

struct DiskPage {
    int pageNum;
    int trackNum;
};

struct MemoryAddress {
    unsigned int address;
};

struct ProcessDiskInfo {
    int process_id;
    int total_pages;
    std::vector<DiskPage> pages;
};

std::unordered_map<int, std::vector<DiskPage>> diskPages;
std::unordered_map<int, std::vector<MemoryAddress>> memoryAddresses;

// Global variables
FrameTableEntry *frame_table;
PageTableEntry **page_tables;
int total_frames = 0;
int page_size = 0;
int frames_per_process = 0;
int lookahead_window_size = 0;
int min_free_pool_size = 0;
int max_free_pool_size = 0;
int total_processes = 0;
int max_disk_track = 0;
int disk_queue_length = 0;
sem_t disk_semaphore;
int total_page_faults = 0;
std::list<DiskQueueEntry> diskQueue;
int current_head_position = 0;  // Track the head position for SSTF and SCAN
sem_t queue_sem;

void initializeGlobals() {
    total_frames = 100;  // default values if not set by configuration
    page_size = 4096;
    frames_per_process = 5;
    lookahead_window_size = 3;
    min_free_pool_size = 10;
    max_free_pool_size = 20;
    total_processes = 3;
    max_disk_track = 500;
    disk_queue_length = 10;
}

// Function declarations
void readConfiguration(const char *filename);
void initializeSemaphores();
void initFrameTable(int total_frames);
void diskDriverProcess();
void pageFaultHandler(int process_id, unsigned int page_number);
void requestPageFromDisk(int frame_index, int disk_addr, int process_id);
void scheduleDiskIO(DiskQueueEntry *entry);
void startDiskOperation(char op, int memory_addr, int disk_addr);
void pageReplacementProcess();
void replacePage(int replacement_algorithm, int process_id);
void processDiskRequest(const DiskQueueEntry& request);
void fifoDiskScheduling();
void sstfDiskScheduling();
void scanDiskScheduling();
int extractPageNumber(unsigned int address, int page_size);
int findFreeFrame();
int getDiskAddress(int process_id, int page_number);

void handleConfiguration(const std::string& key, int value) {
    // Match the key with the corresponding global variable
    if (key == "tp") {
        total_frames = value;
        std::cout << "Total frames set to: " << total_frames << std::endl;
    } else if (key == "ps") {
        page_size = value;
        std::cout << "Page size set to: " << page_size << " bytes" << std::endl;
    } else if (key == "r") {
        frames_per_process = value;
        std::cout << "Frames per process set to: " << frames_per_process << std::endl;
    } else if (key == "X") {
        lookahead_window_size = value;
        std::cout << "Lookahead window size set to: " << lookahead_window_size << std::endl;
    } else if (key == "min") {
        min_free_pool_size = value;
        std::cout << "Min free pool size set to: " << min_free_pool_size << std::endl;
    } else if (key == "max") {
        max_free_pool_size = value;
        std::cout << "Max free pool size set to: " << max_free_pool_size << std::endl;
    } else if (key == "k") {
        total_processes = value;
        std::cout << "Total processes set to: " << total_processes << std::endl;
    } else if (key == "maxtrack") {
        max_disk_track = value;
        std::cout << "Maximum disk track set to: " << max_disk_track << std::endl;
    } else if (key == "y") {
        disk_queue_length = value;
        std::cout << "Disk queue length set to: " << disk_queue_length << std::endl;
    } else {
        std::cerr << "Unknown configuration key: " << key << std::endl;
    }
    std::cout << "Configuration: " << key << " = " << value << std::endl;
}

void readConfiguration(const char *filename) {
    std::ifstream file(filename);
    std::string line;
    int currentProcessID = 0;

    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        exit(EXIT_FAILURE);
    }

    while (getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        if (line.empty()) continue;

        iss >> key;
        if (key == "tp") {
            int value;
            iss >> value;
            handleConfiguration("tp", value);
        } else if (key == "ps") {
            int value;
            iss >> value;
            handleConfiguration("ps", value);
        } else if (key == "r") {
            int value;
            iss >> value;
            handleConfiguration("r", value);
        } else if (key == "X") {
            int value;
            iss >> value;
            handleConfiguration("X", value);
        } else if (key == "min") {
            int value;
            iss >> value;
            handleConfiguration("min", value);
        } else if (key == "max") {
            int value;
            iss >> value;
            handleConfiguration("max", value);
        } else if (key == "k") {
            int value;
            iss >> value;
            handleConfiguration("k", value);
        } else if (key == "maxtrack") {
            int value;
            iss >> value;
            handleConfiguration("maxtrack", value);
        } else if (key == "y") {
            int value;
            iss >> value;
            handleConfiguration("y", value);
        } else if (key.find("pid") != std::string::npos) {
            currentProcessID = std::stoi(key.substr(3));
        } else if (isdigit(key[0])) {
            int pageNum, trackNum;
            iss >> pageNum >> trackNum;
            diskPages[currentProcessID].push_back({pageNum, trackNum});
        }
    }

    file.close();
}

void populateDiskQueue() {
    for (const auto& entry : diskPages) {
        int process_id = entry.first;
        const std::vector<DiskPage>& pages = entry.second;
        for (const auto& page : pages) {
            DiskQueueEntry disk_entry;
            disk_entry.process_id = process_id;
            disk_entry.operation = 'R'; // Assuming read operation for simulation
            disk_entry.disk_addr = page.trackNum; // Using trackNum as disk address
            diskQueue.push_back(disk_entry);
        }
    }
}

int getDiskAddress(int process_id, int page_number) {
    // Check if the process ID exists in the diskPages map
    auto it = diskPages.find(process_id);
    if (it != diskPages.end()) {
        // Check if the page number is within bounds
        if (page_number >= 0 && page_number < it->second.size()) {
            // Return the disk address corresponding to the page number
            return it->second[page_number].trackNum;
        }
    }
    // If process ID or page number is invalid, return -1
    return -1;
}

void processDiskRequest(const DiskQueueEntry& request) {
    // Simulate disk I/O operation
    if (request.operation == 'R') {
        // Perform read operation from disk
        std::cout << "Reading from disk address: " << request.disk_addr << " for process: " << request.process_id << std::endl;
    } else if (request.operation == 'W') {
        // Perform write operation to disk
        std::cout << "Writing to disk address: " << request.disk_addr << " for process: " << request.process_id << std::endl;
    } else {
        std::cerr << "Invalid disk operation: " << request.operation << std::endl;
    }
    // Simulate processing time
    usleep(1000); // Sleep for 1 millisecond
}

void fifoDiskScheduling() {
    while (!diskQueue.empty()) {
        processDiskRequest(diskQueue.front());
        diskQueue.pop_front();
    }
}

void sstfDiskScheduling() {
    while (!diskQueue.empty()) {
        auto closest = std::min_element(diskQueue.begin(), diskQueue.end(),
                                        [](const DiskQueueEntry& a, const DiskQueueEntry& b) {
                                            return abs(a.disk_addr - current_head_position) < abs(b.disk_addr - current_head_position);
                                        });
        processDiskRequest(*closest);
        diskQueue.erase(closest);
    }
}

void scanDiskScheduling() {
    // Ensure the queue is sorted for scan (ascending or descending based on head movement)
    diskQueue.sort([](const DiskQueueEntry& a, const DiskQueueEntry& b) { return a.disk_addr < b.disk_addr; });
    for (auto& request : diskQueue) {
        processDiskRequest(request);
    }
    diskQueue.clear();
}
int extractPageNumber(unsigned int address, int page_size) {
    return (address / page_size);
}
int findFreeFrame() {
    for (int i = 0; i < total_frames; ++i) {
        if (frame_table[i].process_id == -1) {
            return i;
        }
    }
    return -1;  // No free frame available
}

void requestPageFromDisk(int frame_index, int disk_addr, int process_id) {
    // Simulate the request for a page from disk
    std::cout << "Requesting page from disk for process " << process_id << " at disk address " << disk_addr << " to frame " << frame_index << std::endl;
    // Additional code to handle actual disk I/O would go here in a real system
}

int handlePageFaults() {
    int total_page_faults = 0; // Reset page fault count for each iteration

    // Iterate over the memory addresses
    for (const auto& processEntry : memoryAddresses) {
        int process_id = processEntry.first;
        const std::vector<MemoryAddress>& addresses = processEntry.second;

        for (const MemoryAddress& addr : addresses) {
            int page_number = extractPageNumber(addr.address, page_size);

            // Check if the page is present in the process's page table
            PageTableEntry* pageTableEntry = &page_tables[process_id][page_number];
            if (pageTableEntry->frame_number == -1) {
                // Page fault
                ++total_page_faults;

                // Find a free frame
                int free_frame = findFreeFrame();

                if (free_frame == -1) {
                    // No free frame available, invoke page replacement process
                    sem_wait(&queue_sem);
                    DiskQueueEntry request;
                    request.process_id = process_id;
                    request.operation = 'W';
                    request.frame_index = -1;
                    request.disk_addr = pageTableEntry->disk_address;
                    diskQueue.push_back(request);
                    sem_post(&queue_sem);
                    sem_post(&disk_semaphore);
                } else {
                    // Update the page table entry
                    pageTableEntry->frame_number = free_frame;
                    frame_table[free_frame].process_id = process_id;
                    frame_table[free_frame].page_number = page_number;

                    // Schedule disk read operation
                    requestPageFromDisk(free_frame, pageTableEntry->disk_address, process_id);
                }
            }
            // Page is present, update the replacement algorithm data structures
            // ...
        }
    }

    return total_page_faults;
}


void diskDriverProcess() {
    std::cout << "Running FIFO Disk Scheduling\n";
    populateDiskQueue();
    fifoDiskScheduling();

    std::cout << "Running SSTF Disk Scheduling\n";
    populateDiskQueue();
    sstfDiskScheduling();

    std::cout << "Running SCAN Disk Scheduling\n";
    populateDiskQueue();
    scanDiskScheduling();
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <input file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    initializeGlobals();  // Initialize default values
    readConfiguration(argv[1]);  // Update values based on configuration file
    initializeSemaphores();
    initFrameTable(total_frames);  // Initialize frame table with total_frames

    // Start disk operations in a separate process if necessary
    pid_t pid = fork();
    if (pid == 0) {
        diskDriverProcess();  // Executes all disk scheduling algorithms in sequence
        exit(0);
    } else if (pid > 0) {
        wait(NULL);  // Wait for the disk operations to complete
    } else {
        perror("Failed to fork");
        exit(EXIT_FAILURE);
    }

    printf("Simulation completed with %d frames of %d bytes each.\n", total_frames, page_size);
    return 0;
}

void initializeSemaphores() {
    if (sem_init(&disk_semaphore, 0, 1) == -1) {
        perror("Semaphore initialization failed");
        exit(EXIT_FAILURE);
    }
}

void initFrameTable(int total_frames) {
    frame_table = (FrameTableEntry*)malloc(total_frames * sizeof(FrameTableEntry));
    if (!frame_table) {
        perror("Failed to allocate memory for frame table");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < total_frames; ++i) {
        frame_table[i].process_id = -1;
        frame_table[i].page_number = -1;
        frame_table[i].forward_link = -1;
        frame_table[i].backward_link = -1;
    }
}
