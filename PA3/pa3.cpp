#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <algorithm>
#include <queue>
#include <map>
#include <set>
#include <memory>
#include <cmath>
#include <semaphore.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <climits>
#include <chrono>

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
    int disk_address;
    int access_count;
};

struct DiskQueueEntry {
    int process_id;
    char operation;
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

std::unordered_map<int, int> pagesPerProcess; // Stores number of pages for each process
std::unordered_map<int, std::pair<int, int>> working_set_sizes; // Store working set sizes for each process
std::unique_ptr<FrameTableEntry[]> frame_table;
std::unique_ptr<std::unique_ptr<PageTableEntry[]>[]> page_tables;
std::unordered_map<int, std::vector<DiskPage>> diskPages;
std::unordered_map<int, std::vector<MemoryAddress>> memoryAddresses;
std::unordered_map<int, std::deque<int>> accessHistory;
std::vector<std::string> diskSchedulingNames = {"FIFO", "SSTF", "SCAN"};
std::vector<std::string> pageReplacementNames = {"LIFO", "LRU", "MRU", "LFU", "OPT", "WS"};
std::list<DiskQueueEntry> diskQueue;
std::map<std::string, std::map<int, int>> pageFaultsPerAlgorithm;
std::map<std::string, int> totalPageFaultsPerAlgorithm;
sem_t disk_semaphore, queue_sem;
std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();
int total_seek_operations = 0, total_seek_distance = 0;
int total_frames = 0, page_size = 0, frames_per_process = 0;
int lookahead_window_size = 0, min_free_pool_size = 0, max_free_pool_size = 0;
int total_processes = 0, max_disk_track = 0, disk_queue_length = 0;
int current_head_position = 0, total_page_faults = 0;


// Function declarations
void initializeGlobals();
void readConfiguration(const char *filename);
void initializeSemaphores();
void initFrameTable(int total_frames);
void diskDriverProcess();
void requestPageFromDisk(int frame_index, int disk_addr, int process_id);
void processDiskRequest(const DiskQueueEntry& request, const std::string& algorithmName);
void fifoDiskScheduling(const std::string& algorithmName);
void sstfDiskScheduling(const std::string& algorithmName);
void scanDiskScheduling(const std::string& algorithmName);
int extractPageNumber(unsigned int address, int page_size);
int findFreeFrame();
int getDiskAddress(int process_id, int page_number);
void lifoPageReplacement(int process_id);
void lruPageReplacement(int process_id);
void lruXPageReplacement(int process_id, int X);
void mruPageReplacement(int process_id);
void lfuPageReplacement(int process_id);
void optLookaheadPageReplacement(int process_id, int X);
void workingSetPageReplacement(int process_id, int delta);
void simulatePageFaultsAndOutputResults(const char* filename);



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

void handleMemoryAddress(const std::string& processIdStr, const std::string& addressStr) {
    int process_id = std::stoi(processIdStr.substr(3)); // Extract numeric ID from pid1, pid2, etc.
    unsigned int address = std::stoul(addressStr, nullptr, 16); // Convert hex string to unsigned int

    // Add address to memoryAddresses map for the corresponding process ID
    memoryAddresses[process_id].push_back({address});
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
        int pageNum, trackNum;
        std::string addr;

        if (line.empty()) continue;

        iss >> key;
        if (key == "tp" || key == "ps" || key == "r" || key == "X" ||
            key == "min" || key == "max" || key == "k" || key == "maxtrack" || key == "y") {
            int value;
            iss >> value;
            handleConfiguration(key, value);
        } else if (key.find("pid") != std::string::npos) {
            currentProcessID = std::stoi(key.substr(3));
            if (line.find("0x") != std::string::npos)  // address
            {
                iss >> addr;
                handleMemoryAddress("pid" + std::to_string(currentProcessID), addr);
            } else  {
                int size;
                iss >> size;
                if (size == -1) {
                    std::cout << "End of data for process ID " << currentProcessID << std::endl;
                    continue;
                } else {
                    for (int i = 0; i < size; i++)
                    {
                        getline(file, line);
                        std::istringstream track(line);
                        track >> pageNum >> trackNum;
                        diskPages[currentProcessID].push_back({pageNum, trackNum});
                    }
                }
            }
        }
    }

    file.close();
}

bool isValidFrameIndex(int frame_index) {
    return frame_index >= 0 && frame_index < total_frames;
}

void initializeGlobals() {
    page_tables = std::make_unique<std::unique_ptr<PageTableEntry[]>[]>(total_processes + 1);
    for (int i = 1; i <= total_processes; ++i) {
        int num_pages = pagesPerProcess[i]; // Make sure pagesPerProcess is populated before this is called
        page_tables[i] = std::make_unique<PageTableEntry[]>(num_pages);
        for (int j = 0; j < num_pages; ++j) {
            page_tables[i][j].frame_number = -1;
            page_tables[i][j].disk_address = -1;
        }
    }
}


void initPageTables(const std::unordered_map<int, std::vector<DiskPage>>& diskPages, int total_processes) {
    for (int process_id = 1; process_id <= total_processes; ++process_id) {
        int num_pages = diskPages.at(process_id).size();
        page_tables[process_id] = std::make_unique<PageTableEntry[]>(num_pages);
        for (int page_index = 0; page_index < num_pages; ++page_index) {
            page_tables[process_id][page_index].frame_number = -1;
            page_tables[process_id][page_index].disk_address = diskPages.at(process_id)[page_index].trackNum;
        }
    }
}
void initializeSemaphores() {
    if (sem_init(&disk_semaphore, 0, 1) == -1) {
        throw std::runtime_error("Semaphore initialization failed: " + std::string(strerror(errno)));
    }

}

void initFrameTable(int total_frames) {
    frame_table = std::make_unique<FrameTableEntry[]>(total_frames);
    for (int i = 0; i < total_frames; i++) {
        frame_table[i].process_id = -1;
        frame_table[i].page_number = -1;
        frame_table[i].forward_link = -1;
        frame_table[i].backward_link = -1;
    }
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
            disk_entry.frame_index = page.pageNum;
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


void scheduleDiskIO(DiskQueueEntry* entry) {
    if (entry == nullptr || entry->disk_addr == -1) {
        std::cerr << "Error: Attempted to schedule disk I/O with an invalid entry." << std::endl;
        return;
    }

    if (!isValidFrameIndex(entry->frame_index)) {
        std::cerr << "Error: Invalid frame index " << entry->frame_index << ". Cannot schedule disk I/O." << std::endl;
        return;
    }

    if (sem_wait(&disk_semaphore) != 0) {
        throw std::runtime_error("Failed to lock disk semaphore: " + std::string(strerror(errno)));
    }

    diskQueue.push_back(*entry);

    if (sem_post(&disk_semaphore) != 0) {
        perror("Failed to unlock disk semaphore");
        exit(EXIT_FAILURE);
    }

    std::cout << "Scheduled disk I/O for process " << entry->process_id <<
              " on frame " << entry->frame_index <<
              " at disk address " << entry->disk_addr << std::endl;
}


// Utility function to extract the page number given an address and page size
int extractPageNumber(unsigned int address, int page_size) {
    return address / page_size;
}

void fifoDiskScheduling(const std::string& algorithmName) {
    while (!diskQueue.empty()) {
        processDiskRequest(diskQueue.front(), "FIFO");
        diskQueue.pop_front();
    }
}

void sstfDiskScheduling(const std::string& algorithmName) {
    auto comp = [&] (const DiskQueueEntry& a, const DiskQueueEntry& b) {
        return abs(a.disk_addr - current_head_position) < abs(b.disk_addr - current_head_position);
    };
    std::set<DiskQueueEntry, decltype(comp)> sortedQueue(comp);
    for (const auto& request : diskQueue) {
        sortedQueue.insert(request);
    }
    while (!sortedQueue.empty()) {
        processDiskRequest(*sortedQueue.begin(), algorithmName);
        sortedQueue.erase(sortedQueue.begin());
    }
}

void scanDiskScheduling(const std::string& algorithmName) {
    // Ensure the queue is sorted for scan (ascending or descending based on head movement)
    diskQueue.sort([](const DiskQueueEntry& a, const DiskQueueEntry& b) {
        return a.disk_addr < b.disk_addr;
    });
    for (auto& request : diskQueue) {
        processDiskRequest(request, algorithmName);
    }
    diskQueue.clear();
}

int findFreeFrame() {
    for (int i = 0; i < total_frames; ++i) {
        if (frame_table[i].process_id == -1) {
            return i;
        }
    }
    return -1;  // No free frame available
}


void lfuPageReplacement(int process_id) {
    int least_frequently_used_frame = -1;
    int minimum_access_count = INT_MAX;

    // Find the frame with the least frequency of access
    for (int i = 0; i < total_frames; ++i) {
        if (frame_table[i].process_id == process_id && frame_table[i].access_count < minimum_access_count) {
            least_frequently_used_frame = i;
            minimum_access_count = frame_table[i].access_count;
        }
    }

    if (least_frequently_used_frame != -1) {
        // Simulate removing the page from the frame
        frame_table[least_frequently_used_frame].process_id = -1;
        frame_table[least_frequently_used_frame].page_number = -1;
        frame_table[least_frequently_used_frame].access_count = 0;  // Reset the access count

        std::cout << "LFU replacement: Replaced frame " << least_frequently_used_frame << std::endl;
    } else {
        std::cerr << "No suitable frame found for LFU replacement!" << std::endl;
    }
}

void lifoPageReplacement(int process_id) {
    static std::vector<int> lifoStack; // Vector to simulate stack behavior for LIFO

    if (!lifoStack.empty()) {
        int freed_frame_index = lifoStack.back();
        lifoStack.pop_back();

        // Invalidate the page in the page table and frame table
        for (int page_number = 0; page_number < pagesPerProcess[process_id]; ++page_number) {
            if (page_tables[process_id][page_number].frame_number == freed_frame_index) {
                page_tables[process_id][page_number].frame_number = -1; // Invalidate
                frame_table[freed_frame_index] = {-1, -1, -1, -1, -1, 0}; // Clear the frame
                break; // Exit after invalidating the page
            }
        }
        std::cout << "LIFO replacement: Replaced frame at index " << freed_frame_index << std::endl;
    }

    // Note: The function does not return a value anymore
}

void lruPageReplacement(int process_id) {
    static std::list<int> lruList; // List to track the least recently used frames

    if (!lruList.empty()) {
        int freed_frame_index = lruList.front();
        lruList.pop_front();

        // Invalidate the page in the page table and frame table
        for (int page_number = 0; page_number < pagesPerProcess[process_id]; ++page_number) {
            if (page_tables[process_id][page_number].frame_number == freed_frame_index) {
                page_tables[process_id][page_number].frame_number = -1; // Invalidate
                frame_table[freed_frame_index] = {-1, -1, -1, -1, -1, 0}; // Clear the frame
                break; // Exit after invalidating the page
            }
        }
        std::cout << "LRU replacement: Replaced frame " << freed_frame_index << std::endl;
    }

    // Note: The function does not return a value anymore
}


void mruPageReplacement(int process_id) {
    static int most_recently_used_frame = -1;  // Static variable declaration
    std::cout << "Running MRU Page Replacement for process ID " << process_id << std::endl;

    if (!isValidFrameIndex(most_recently_used_frame)) {
        std::cerr << "Invalid MRU frame index: " << most_recently_used_frame << std::endl;
        most_recently_used_frame = findFreeFrame();  // Attempt to recover by finding a new frame
    }

    // Reset the old MRU frame
    if (isValidFrameIndex(most_recently_used_frame)) {
        frame_table[most_recently_used_frame].process_id = -1;
        frame_table[most_recently_used_frame].page_number = -1;
        std::cout << "Cleared old MRU frame: " << most_recently_used_frame << std::endl;
    }

    // Set new MRU frame
    most_recently_used_frame = findFreeFrame();
    if (isValidFrameIndex(most_recently_used_frame)) {
        frame_table[most_recently_used_frame].process_id = process_id;
        std::cout << "New MRU frame assigned: " << most_recently_used_frame << std::endl;
    } else {
        std::cerr << "Failed to find valid frame for MRU" << std::endl;
    }
}

void workingSetPageReplacement(int process_id, int delta) {
    auto current_time_point = std::chrono::steady_clock::now();
    std::unordered_map<int, std::chrono::steady_clock::time_point> last_used;

    // Update last_used based on access history within the delta time frame
    for (int i = std::max(0, (int)accessHistory[process_id].size() - delta); i < accessHistory[process_id].size(); ++i) {
        last_used[accessHistory[process_id][i]] = current_time_point;
    }

    int current_working_set_size = last_used.size();
    auto& ws_sizes = working_set_sizes[process_id];
    ws_sizes.first = std::min(ws_sizes.first, current_working_set_size);
    ws_sizes.second = std::max(ws_sizes.second, current_working_set_size);

    int least_recently_used_frame = -1;
    std::chrono::steady_clock::time_point oldest_time = current_time_point;

    // Identify the least recently used frame
    for (const auto& page : last_used) {
        if (page.second < oldest_time) {
            oldest_time = page.second;
            least_recently_used_frame = page_tables[process_id][page.first].frame_number;
        }
    }

    if (least_recently_used_frame != -1) {
        frame_table[least_recently_used_frame].process_id = -1;
        frame_table[least_recently_used_frame].page_number = -1;
        std::cout << "Working Set replacement: Replaced frame " << least_recently_used_frame << std::endl;
    } else {
        std::cerr << "No suitable frame found for Working Set replacement!" << std::endl;
    }
}


void optLookaheadPageReplacement(int process_id, int lookahead) {
    int longest_future_use_index = -1;
    int max_future_time = -1;

    // Simulate "future knowledge" by selecting the page least likely to be used soon
    for (int i = 0; i < total_frames; ++i) {
        if (frame_table[i].process_id == process_id) {
            int future_use_time = lookahead + (rand() % 100);  // Random future usage time for demonstration
            if (future_use_time > max_future_time) {
                max_future_time = future_use_time;
                longest_future_use_index = i;
            }
        }
    }

    if (longest_future_use_index != -1) {
        // Replace the page
        frame_table[longest_future_use_index].process_id = -1;
        frame_table[longest_future_use_index].page_number = -1;
        std::cout << "OPT replacement: Replaced frame " << longest_future_use_index << std::endl;
    }
}

void lruXPageReplacement(int process_id, int X) {
    int lru_frame = -1;
    int oldest_access_time = INT_MAX;

    // Iterate through all frames to find the least recently used frame considering the X most recent accesses
    for (int i = 0; i < total_frames; ++i) {
        if (frame_table[i].process_id == process_id) {
            if (accessHistory[i].size() == X && accessHistory[i].front() < oldest_access_time) {
                oldest_access_time = accessHistory[i].front();
                lru_frame = i;
            }
        }
    }

    if (lru_frame != -1) {
        // Remove the least recently used frame
        frame_table[lru_frame].process_id = -1;
        frame_table[lru_frame].page_number = -1;
        accessHistory.erase(lru_frame);  // Clear the history as the frame is now free
        std::cout << "LRU-X replacement: Replaced frame " << lru_frame << std::endl;
    } else {
        std::cerr << "No suitable frame found to replace!" << std::endl;
    }
}







void diskDriverProcess() {
    std::cout << "Running FIFO Disk Scheduling\n";
    populateDiskQueue();
    fifoDiskScheduling("FIFO + Default");  // Assuming 'Default' as a placeholder
    std::cout << "Running SSTF Disk Scheduling\n";
    populateDiskQueue();
    sstfDiskScheduling("SSTF + Default");
    std::cout << "Running SCAN Disk Scheduling\n";
    populateDiskQueue();
    scanDiskScheduling("SCAN + Default");
}

int calculateSeekTime(int disk_addr) {
    // Assuming a linear seek time calculation, where each track transition costs 1 time unit
    int seek_time = abs(current_head_position - disk_addr) * 100; // Adjust scale to microseconds
    current_head_position = disk_addr; // Update the head position
    return seek_time; // Now returns microseconds
}

void requestPageFromDisk(int frame_index, int disk_addr, int process_id) {
    if (frame_index < 0 || frame_index >= total_frames) {
        std::cerr << "Invalid frame index: " << frame_index << ". Cannot schedule disk I/O." << std::endl;
        return;  // Prevent disk operations with invalid frames
    }

    DiskQueueEntry newRequest;
    newRequest.process_id = process_id;
    newRequest.operation = 'R'; // Read operation
    newRequest.frame_index = frame_index;
    newRequest.disk_addr = disk_addr;
    scheduleDiskIO(&newRequest);
}

void optLookaheadPageReplacementWrapper(int process_id) {
    optLookaheadPageReplacement(process_id, lookahead_window_size);
}

void workingSetPageReplacementWrapper(int process_id) {
    workingSetPageReplacement(process_id, frames_per_process); // or another value representing delta
}

void (*diskSchedulingAlgorithms[])(const std::string&) = {
        fifoDiskScheduling,
        sstfDiskScheduling,
        scanDiskScheduling
};



void (*pageReplacementAlgorithms[])(int) = {
        lifoPageReplacement,
        lruPageReplacement,
        mruPageReplacement,
        lfuPageReplacement, // Make sure this is correctly implemented
        optLookaheadPageReplacementWrapper,
        workingSetPageReplacementWrapper
};


void handlePageFaults(int process_id, const std::string& algorithmName) {
    const auto& addresses = memoryAddresses[process_id];
    int process_faults = 0;

    for (const MemoryAddress& addr : addresses) {
        int page_number = extractPageNumber(addr.address, page_size);
        PageTableEntry& pageTableEntry = page_tables[process_id][page_number];
        if (pageTableEntry.frame_number == -1) {  // If page fault occurs
            process_faults++;
            int free_frame = findFreeFrame();
            if (free_frame == -1) {  // No free frame available, run a page replacement algorithm
                if (algorithmName.find("LIFO") != std::string::npos) lifoPageReplacement(process_id);
                else if (algorithmName.find("LRU") != std::string::npos) lruPageReplacement(process_id);
                else if (algorithmName.find("MRU") != std::string::npos) mruPageReplacement(process_id);
                else if (algorithmName.find("LFU") != std::string::npos) lfuPageReplacement(process_id);
                else if (algorithmName.find("OPT") != std::string::npos) optLookaheadPageReplacement(process_id, lookahead_window_size);
                else if (algorithmName.find("WS") != std::string::npos) workingSetPageReplacementWrapper(process_id);

                free_frame = findFreeFrame(); // Try to find a free frame again after replacement
            }
            if (free_frame != -1) {
                requestPageFromDisk(free_frame, getDiskAddress(process_id, page_number), process_id);
                // Update the page table with the frame number
                pageTableEntry.frame_number = free_frame;
            }
        }
    }

    // Record the faults for this algorithm and process
    pageFaultsPerAlgorithm[algorithmName][process_id] += process_faults;
    std::cout << "Total page faults for Process " << process_id << " under " << algorithmName << ": " << pageFaultsPerAlgorithm[algorithmName][process_id] << "\n";
}

void processDiskRequest(const DiskQueueEntry& request, const std::string& algorithmName) {
    if (!isValidFrameIndex(request.frame_index) || request.disk_addr == -1) {
        total_page_faults++;
        handlePageFaults(request.process_id, algorithmName);
        return;
    }

    auto start_time = std::chrono::steady_clock::now();

    int seek_distance = calculateSeekTime(request.disk_addr); // This now calculates time in microseconds
    usleep(seek_distance + 10); // Simulating the operation time, assuming it needs microseconds

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    std::cout << "Operation duration: " << duration << " microseconds." << std::endl;

    // Additional debug outputs as before
    std::cout << "Processing disk request for process ID " << request.process_id
              << " with frame index " << request.frame_index
              << " at disk address " << request.disk_addr << std::endl;
    std::cout << "Seek operation: Moved from " << (current_head_position - seek_distance)
              << " to " << current_head_position
              << " (seek distance: " << seek_distance << " tracks)." << std::endl;

    frame_table[request.frame_index].process_id = request.process_id;
    frame_table[request.frame_index].page_number = extractPageNumber(request.disk_addr, page_size);
    frame_table[request.frame_index].disk_address = request.disk_addr;
    frame_table[request.frame_index].access_count++;
}


void simulatePageFaultsAndOutputResults(const char* filename) {
    std::cout << "\n=====================================\n";
    std::cout << "RUNNING " << filename << "\n";
    std::cout << "=====================================\n\n";

    for (auto& sched : diskSchedulingNames) {
        for (auto& repl : pageReplacementNames) {
            std::string algorithmName = sched + " + " + repl;
            std::cout << "---- " << algorithmName << " REPLACEMENT ----\n";
            std::cout << "TOTAL Faults\n";
            int totalFaults = totalPageFaultsPerAlgorithm[algorithmName];
            auto& faultsPerProcess = pageFaultsPerAlgorithm[algorithmName];
            for (const auto& pf : faultsPerProcess) {
                std::cout << "Process " << pf.first << ": " << pf.second << " faults\n";
                totalFaults += pf.second;
            }
            std::cout << "***** TOTAL REPLACEMENT FAULTS: " << totalFaults << " *****\n\n";

            // Output min and max working set sizes for each process
            for (auto& ws : working_set_sizes) {
                std::cout << "Process " << ws.first << " Working Set Sizes\n";
                std::cout << "MIN: " << ws.second.first << "\n";
                std::cout << "MAX: " << ws.second.second << "\n\n";
            }
        }
    }
}
void resetPageFaults() {
    pageFaultsPerAlgorithm.clear();  // Clear existing records
}

void outputResultsForAlgorithmPair(const std::string& algorithmName) {
    auto start_time = std::chrono::steady_clock::now();  // Start timing the simulation for the algorithm pair

    // Simulate the disk scheduling and page replacement for each process
    int totalReplacements = 0;
    for (int process_id = 1; process_id <= total_processes; process_id++) {
        // Depending on the algorithm, the appropriate scheduling or replacement is called here
        handlePageFaults(process_id, algorithmName);
        // Sum up total replacements made for this algorithm combination
        totalReplacements += pageFaultsPerAlgorithm[algorithmName][process_id];
    }

    auto end_time = std::chrono::steady_clock::now();  // End timing after processing all page faults
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();  // Calculate duration in microseconds

    // Output the results
    std::cout << "Results for " << algorithmName << ":\n";
    std::cout << "Simulation took " << duration << " microseconds\n";  // Report the timing for this algorithm pair
    auto& faultsPerProcess = pageFaultsPerAlgorithm[algorithmName];
    for (const auto& pf : faultsPerProcess) {
        std::cout << "Process " << pf.first << ": " << pf.second << " faults\n";
    }

    std::cout << "Total replacements for " << algorithmName << ": " << totalReplacements << "\n";

    // Output the Working Set sizes if the algorithm is "WS"
    if (algorithmName.find("WS") != std::string::npos) {
        for (const auto& ws : working_set_sizes) {
            std::cout << "Working Set sizes for Process " << ws.first << ":\n";
            std::cout << "  MIN: " << ws.second.first << "\n";
            std::cout << "  MAX: " << ws.second.second << "\n";
        }
    }

    // Also output the accumulated statistics like total and average seek times, if applicable
    if (total_seek_operations > 0) {
        double average_seek_time = static_cast<double>(total_seek_distance) / total_seek_operations;
        std::cout << "Total seek operations: " << total_seek_operations << "\n";
        std::cout << "Total seek distance: " << total_seek_distance << " tracks\n";
        std::cout << "Average seek time: " << average_seek_time << " tracks/operation\n";
    }

    std::cout << "----------------------------------------\n";
}

void simulateAlgorithmPair(const std::string& diskAlgorithm, const std::string& pageAlgorithm) {
    std::string algorithmName = diskAlgorithm + " + " + pageAlgorithm;
    resetPageFaults();  // Reset before simulation

    // Simulate disk scheduling
    if (diskAlgorithm == "FIFO") fifoDiskScheduling(algorithmName);
    else if (diskAlgorithm == "SSTF") sstfDiskScheduling(algorithmName);
    else if (diskAlgorithm == "SCAN") scanDiskScheduling(algorithmName);

    // Simulate page replacement for each process
    for (int process_id = 1; process_id <= total_processes; process_id++) {
        if (pageAlgorithm == "LIFO") lifoPageReplacement(process_id);
        else if (pageAlgorithm == "LRU") lruPageReplacement(process_id);
        else if (pageAlgorithm == "MRU") mruPageReplacement(process_id);
        else if (pageAlgorithm == "LFU") lfuPageReplacement(process_id);
        else if (pageAlgorithm == "OPT") optLookaheadPageReplacement(process_id, lookahead_window_size);
        else if (pageAlgorithm == "WS") workingSetPageReplacementWrapper(process_id);

        handlePageFaults(process_id, algorithmName);  // Handle and record page faults
    }

    // Output the results for this algorithm combination
    outputResultsForAlgorithmPair(algorithmName);
}




int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <configuration file>\n";
        return EXIT_FAILURE;
    }

    readConfiguration(argv[1]);
    initializeGlobals();
    initializeSemaphores();
    initFrameTable(total_frames);

    // Run simulation for each combination of disk scheduling and page replacement
    for (const auto& diskAlgorithm : diskSchedulingNames) {
        for (const auto& pageAlgorithm : pageReplacementNames) {
            simulateAlgorithmPair(diskAlgorithm, pageAlgorithm);
        }
    }

    return 0;
}
