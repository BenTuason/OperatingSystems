#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

struct DiskTrackInfo {
    int page;
    int track;
};

struct ProcessInfo {
    int pid;
    int size;
    std::vector<DiskTrackInfo> trackInfo;
    std::vector<std::string> addressPairs; // Added member to store pair addresses
};

struct Frame {
    int processId = -1; // Process ID that owns this frame.
    int pageNumber = -1; // Page number within the process' space.
    bool occupied = false; // Is the frame currently used?
};

struct InputParams {
    int tp, ps, r, X, min, max, k, maxtrack, y;
    std::vector<ProcessInfo> processes;
};

std::vector<Frame> frameTable;
std::vector<int> freeFrameList;

void parseInput(const std::string& filename, InputParams& params) {
    std::ifstream file(filename);
    if (!file) {
        std::cerr << "Error opening input file\n";
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines or comments
        if (line.empty() || line[0] == '/' || line[0] == '*') continue;

        std::istringstream iss(line);
        std::string token;
        iss >> token;

        // Handle global parameters
        if (token == "tp" || token == "ps" || token == "r" || token == "X" ||
            token == "min" || token == "max" || token == "k" || token == "maxtrack" || token == "y") {
            int value;
            if (iss >> value) {
                if (token == "tp") params.tp = value;
                else if (token == "ps") params.ps = value;
                else if (token == "r") params.r = value;
                else if (token == "X") params.X = value;
                else if (token == "min") params.min = value;
                else if (token == "max") params.max = value;
                else if (token == "k") params.k = value;
                else if (token == "maxtrack") params.maxtrack = value;
                else if (token == "y") params.y = value;
            }
        } else if (token.find("pid") != std::string::npos) {
            // Parse process information
            int pid = std::stoi(token.substr(3));
            int size;
            if (iss >> size) {
                ProcessInfo process{pid, size};

                // Now read the disk track info for each process
                for (int j = 0; j < size; ++j) {
                    if (!std::getline(file, line)) break;
                    std::istringstream trackStream(line);
                    int page, track;
                    trackStream >> page >> track;
                    process.trackInfo.push_back({page, track});
                }

                // Store the process ID and address pairs
                std::string pidToken;
                std::string addressToken;
                while (iss >> pidToken >> addressToken) {
                    if (pidToken.find("_") != std::string::npos) {
                        // Process ID and address pair
                        process.addressPairs.push_back(addressToken);
                    }
                }

                params.processes.push_back(process);
            }
        }
    }
    file.close();
}


void displayProcessInfo(const std::vector<ProcessInfo>& processes) {
    for (const auto& process : processes) {
        std::cout << "Process ID: " << process.pid << ", Total Pages: " << process.size << std::endl;
        for (size_t i = 0; i < process.trackInfo.size(); ++i) {
            const auto& track = process.trackInfo[i];
            std::cout << "    Page: " << track.page << ", Track: " << track.track;
            if (i < process.addressPairs.size()) {
                std::cout << ", Pair Address: " << process.addressPairs[i];
            }
            std::cout << std::endl;
        }
        // Display process and its pair address
        if (!process.addressPairs.empty()) {
            std::cout << "    Process Pair Address: " << process.pid << " -> " << process.addressPairs.front() << std::endl;
        }
    }
}

void displayInputParams(const InputParams& params) {
    std::cout << "Total Page Frames: " << params.tp << std::endl
              << "Page Size: " << params.ps << std::endl
              << "Page Frames Per Process: " << params.r << std::endl
              << "Lookahead Window Size: " << params.X << std::endl
              << "Min Free Pool Size: " << params.min << std::endl
              << "Max Free Pool Size: " << params.max << std::endl
              << "Number of Processes: " << params.k << std::endl
              << "Largest-numbered Track: " << params.maxtrack << std::endl
              << "Number of I/O Requests in Queue: " << params.y << std::endl;

    displayProcessInfo(params.processes);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>" << std::endl;
        return 1;
    }

    std::string inputFilename = argv[1];
    InputParams params;
    parseInput(inputFilename, params);
    displayInputParams(params);

    return 0;
}
