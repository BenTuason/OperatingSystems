#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <unistd.h>
#include <sys/wait.h>
#include <stdexcept>

using namespace std;

// Function to split a string into tokens
vector<string> split(string str, char delimiter) {
    vector<string> tokens;
    size_t pos;
    string token;
    while ((pos = str.find(delimiter)) != string::npos) {
        token = str.substr(0, pos);
        tokens.push_back(token);
        str.erase(0, pos + 1);
    }
    
    while (str.back() == '\n' || str.back() == '\r' || str.back() == ';' || str.back() == '.') 
        str.pop_back();
    tokens.push_back(str);
    return tokens;
}

// Function to evaluate arithmetic expressions
int evaluate(const char op, int a, int b = 0) {
    if (op == '+') return a + b;
    if (op == '-') return a - b;
    if (op == '*') return a * b;
    if (op == '/') return b != 0 ? a / b : throw runtime_error("Division by zero");
    return a; // For direct assignment
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: " << argv[0] << " instructions.txt input.txt" << endl;
        return EXIT_FAILURE;
    }

    ifstream instructionsFile(argv[1]);
    ifstream inputFile(argv[2]);

    if (!instructionsFile.is_open() || !inputFile.is_open()) {
        cerr << "Error opening files." << endl;
        return EXIT_FAILURE;
    }

    map<string, int> variables;

    // Parse input and internal variables
    vector<string> inputVars;
    vector<string> internalVars;
    string line;
    vector<vector<string>> ops;

    while (getline(instructionsFile, line)) {
        if (line.find("input_var") != string::npos) {
            inputVars = split(line.substr(line.find(' ') + 1), ',');
            for (const auto& v : inputVars) variables[v] = 0;
        } else if (line.find("internal_var") != string::npos) {
            internalVars = split(line.substr(line.find(' ') + 1), ',');
            for (const auto& v : internalVars) variables[v] = 0;
        } else if (line.find("write") != string::npos) {
            break;
        } else {
            vector<string> op = split(line, ' ');
            ops.push_back(op);
        }
    }

    string line0;
    // Assign values to input variables
    getline(inputFile, line0);
    vector<string> inputValues = split(line0, ',');
    for (size_t i = 0; i < inputVars.size() && i < inputValues.size(); ++i) {
        variables[inputVars[i]] = stoi(inputValues[i]);
    }

    // Process instructions
    int pipes[ops.size()][2];
    for (size_t i = 0; i < ops.size(); ++i) {
        if (pipe(pipes[i]) == -1) {
            cerr << "Pipe creation failed." << endl;
            return EXIT_FAILURE;
        }
    }
    int internal_pipes[internalVars.size()][2];
    for (size_t i = 0; i < internalVars.size(); ++i) {
        if (pipe(internal_pipes[i]) == -1) {
            cerr << "Pipe creation failed." << endl;
            return EXIT_FAILURE;
        }
    }

    for (size_t i = 0; i < internalVars.size(); i++) {
        pid_t pid = fork();
        if (pid == -1) {
            cerr << "Fork failed." << endl;
            return EXIT_FAILURE;
        } else if (pid == 0) { // Child process
            int value, result;
            char c[2];
            for (size_t j = 0; j < ops.size(); ++j) {
                if (ops[j].size() == 3) { // direct assignment
                    if (ops[j][2] == internalVars[i]) {
                        close(pipes[j][1]);
                        read(pipes[j][0], &value, sizeof(int));
                        close(pipes[j][0]);
                        result = value;
                    }
                } else {
                    if (ops[j][3] == internalVars[i]) {
                        close(pipes[j][1]);
                        read(pipes[j][0], &value, sizeof(int));
                        read(pipes[j][0], &c, 2);
                        close(pipes[j][0]);
                        result = evaluate(c[0], result, value);
                    }
                }
            }
            close(internal_pipes[i][0]);
            write(internal_pipes[i][1], &result, sizeof(int));
            close(internal_pipes[i][1]);
            exit(EXIT_SUCCESS);
        } else { // Parent process
            for (size_t j = 0; j < ops.size(); ++j) {
                if (ops[j].size() == 3) { // direct assignment
                    if (ops[j][2] == internalVars[i]) {
                        close(pipes[j][0]);
                        write(pipes[j][1], &variables[ops[j][0]], sizeof(int));
                        close(pipes[j][1]);
                    }
                } else {
                    if (ops[j][3] == internalVars[i]) {
                        close(pipes[j][0]);
                        write(pipes[j][1], &variables[ops[j][1]], sizeof(int));
                        write(pipes[j][1], ops[j][0].c_str(), 2);
                        close(pipes[j][1]);
                    }
                }
            }
            wait(nullptr); // Wait for child to terminate
            int value;
            close(internal_pipes[i][1]);
            read(internal_pipes[i][0], &value, sizeof(int));
            close(internal_pipes[i][0]);
            variables[internalVars[i]] = value;
        }
    }

    // Output variables as specified in the write instruction
    vector<string> write_var = split(line.substr(line.find('(') + 1), ',');
    write_var.back().pop_back(); // Remove the closing parenthesis
    for (string var : write_var) {
        if (var[0] == ' ')
            var.erase(0, 1);
        cout << var << " = " << variables[var] << endl;
    }

    return EXIT_SUCCESS;
}
