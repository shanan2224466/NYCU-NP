#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <regex>
#include <set>
#include <iomanip>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

using namespace std;
#define MAXNUMBERED 500
#define MINUS_ONE 0xffffffffffffffffULL
/* The elements of com_list only increase during the program. */
struct Command{
    // The index of the command in the comm_list. (i) Start from 0 and accumulate during the whole program!
    size_t index;
    // Which pipe in the pipe_list this command occupies. (index) (i) MINUS_ONE means the command does not occupy any of pipe. (ii) ex. pipe_list[com_list[3].pipe].command = 3, pipe_list[com_list[6].pipe].command = 6
    size_t pipe;
    // Write the stdout to which command in the comm_list. (i) 0 means do not write to other process instead write to the stdout. (ii) '|' pipe also has numbered = 1.
    size_t numbered;
    string comm;
};
/* The elements of pipe_list will increase and decrease during the program. */
struct Pipe{
    // The index of the pipe in the pipe_list. (i) Start from 0 and accumulate during the whole program. (ii) While pushing back to the pipe_list, we pop out those useless pipe.
    size_t index;
    // Which command in the comm_list occupy this pipe. (index) (i) MINUS_ONE means the pipe was not occupied by any of command.
    size_t command;
    // The process id of the occupying process. (i) Initialize as -1.
    int pid;
    int pipefd[2];
};

/* Convert a string to a int which ignore all the non-digit characters. */
int s2i(string s){
    string result = "";
    for (char c : s)
        if (isdigit(c))
            result += c;
    return stoi(result);
}

void set_env(string var, string val, int overwrite){
    const char *name = var.c_str(), *value = val.c_str();
    if (setenv(name, value, overwrite))
        cerr << "** setenv error **" << endl;
    return;
}

void get_env(string var){
    const char *name = var.c_str();
    char *p = getenv(name);
    if (p != NULL)
        cout << p << endl;
    else
        cerr << "** getenv error **" << endl;
    return;
}
/* Check whether there was at least one command write stdout to the current command. */
size_t check_previouscommand(int comm_index, const vector<struct Command> &com_list){
    for (const auto &it : com_list)
        if (it.numbered == comm_index)
            return it.pipe;
    return MINUS_ONE;
}
/* Check whether there was at least one command (before you) write stdout to the same future command as yours. If so, return the pipe_index that the command occupies. */
size_t check_pipe2samecommand(size_t comm_index, const vector<struct Command> &com_list){
    for (const auto &it : com_list)
        if (it.index != comm_index && it.numbered == com_list[comm_index].numbered)
            return it.pipe;
    return MINUS_ONE;
}

void exe_cvp(string command, string option, string file){
    if (!option.empty() && !file.empty()){
        char* const argv[] = {(char* const)command.c_str(), (char* const)option.c_str(), (char* const)file.c_str(), nullptr};
        execvp(argv[0], argv);
    }
    else if (!option.empty()){
        char* const argv[] = {(char* const)command.c_str(), (char* const)option.c_str(), nullptr};
        execvp(argv[0], argv);
    }
    else if (!file.empty()){
        char* const argv[] = {(char* const)command.c_str(), (char* const)file.c_str(), nullptr};
        execvp(argv[0], argv);
    }
    else{
        char* const argv[] = {(char* const)command.c_str(), nullptr};
        execvp(argv[0], argv);
    }
    cerr << "** execvp error **" << endl;
    exit(1);
}
/* Fork the child process. After some preparation including dup2 and close, we use execvp() to run the child process. */
int run(string command, string option, string file, size_t comm_index, const vector<struct Command> &com_list, size_t pipe_index, vector<struct Pipe> &pipe_list){
    pid_t pid;
    if ((pid = fork()) == 0){
        /* It means the command writes to the other command. */
        if (pipe_index != MINUS_ONE){
            size_t p = check_pipe2samecommand(comm_index, com_list);
            /* It means the command writes to a command that have already been written to by other previous command. */
            if (p != MINUS_ONE){
                dup2(pipe_list[p].pipefd[1], STDOUT_FILENO);
            }
            /* It means the command writes to a command that have not been written to by other previous command. */
            else{
                dup2(pipe_list[pipe_list.size()].pipefd[1], STDOUT_FILENO);
            }
        }
        size_t p = check_previouscommand(comm_index, com_list);
        if (p != MINUS_ONE){
            dup2(pipe_list[p].pipefd[0], STDIN_FILENO);
        }
        for (auto &it : pipe_list){
            close(it.pipefd[0]);
            close(it.pipefd[1]);
        }
        exe_cvp("./bin/" + command, option, file);
    }
    else
        cerr << "** fork error **" << endl;
    return pid;
}

void execute(int last_index, vector<struct Command> &com_list, vector<struct Pipe> &pipe_list){
    /* For those commands attempt to write to the later command that is locate at the current input line. */
    int tmp_pipe[2];
    for (auto it = com_list.begin() + last_index; it != com_list.end(); ++it){
        int i = 0;
        string command[3];
        stringstream ss(it->comm);
        while(ss >> command[i++]);
        /* Only those command attempt to write stdout to other command will push their pipe to the pipe_list. */
        if (it->numbered != 0){
            if(pipe(tmp_pipe) == -1){
                cerr << "** pipe error **" << endl;
                exit(1);
            }
            struct Pipe tmp = {pipe_list.size(), it->index, -1, {-1, -1}};
            memcpy(tmp.pipefd, tmp_pipe, sizeof(int) * 2);
            pipe_list.push_back(tmp);
        }
        /* To indicate if the command[0] is the following command. */
        set<string> command_set {"ls", "cat", "number", "removetag", "removetag0", "noop"};
        auto match = command_set.find(command[0]);
        if(command[0] == "exit")
            exit(0);
        else if (command[0] == "setenv"){
            if (!command[1].empty() && !command[2].empty())
                set_env(command[1], command[2], 1);
            else
                cerr << "Syntax: setenv [variable] [value]" << endl;
        }
        else if (command[0] == "printenv"){
            if (!command[1].empty())
                get_env(command[1]);
            else
                cerr << "Syntax: printenv [variable]" << endl;
        }
        else if (match != command_set.end()){
            //int pid = run(command[0], command[1], command[2], it->index, com_list, it->numbered != 0 ? pipe_list.size() : MINUS_ONE, pipe_list);
            
        }
        else{//////////// deal with the other commands write-in scenario
            cerr << "Unknown command: [" << command[0] << "]." << endl;
        }
        for (const auto &item : com_list){
            if (item.numbered < it->numbered){
                struct Pipe tmp = pipe_list[item.pipe];
                close(tmp.pipefd[0]);
                close(tmp.pipefd[1]);
                delete_pipe(tmp.index, pipe_list);
            }
            else
                break;
        }
    }
    
    return;
}

int get_command(int last_index, string &s, vector<struct Command> &com_list, vector<struct Pipe> &pipe_list){
    /* Extract the commands to a list from the input string line. */
    regex re("\\|(\\d*)");
    sregex_iterator it(s.begin(), s.end(), re);
    sregex_iterator end;
    if (it == end){
        com_list.push_back({com_list.size(), MINUS_ONE, 0, s});
    }
    else{
        while(it != end){
            smatch match = *it;
            string number = match[1];
            struct Command tmp = {com_list.size(), MINUS_ONE, com_list.size() + (number.empty() ? 1 : s2i(match.str())), match.prefix().str()};
            com_list.push_back(tmp);
            ++it;
            if (it == end && !match.suffix().str().empty()){
                com_list.push_back({com_list.size(), MINUS_ONE, 0, match.suffix().str().erase(0, 1)});
            }
            else if (it == end && match.suffix().str().empty()){
                cerr << "** Syntex: '|' is not allowed to be alone in the end of a line. **" << endl;
                for (int i = 0; i < com_list.size() - last_index; i++){
                    com_list.pop_back();
                }
                return last_index;
            }
        }
    }
    for (const auto& p : com_list)
        cout << p.index << " command: " << left << setw(15) << p.comm << " number: " << left << setw(3) << p.numbered << " pipe: " << p.pipe << endl;
    
    execute(last_index, com_list, pipe_list);
    for (const auto& p : pipe_list)
        cout << p.index << " command: " << left << setw(15) << com_list[p.command].comm << " pid: " << left << setw(3) << p.pid << " pipe: " << p.pipefd[0] << ", " << p.pipefd[1] << endl;
    
    return com_list.size();
}

int main(int argc, char* argv[]){
    int last_index = 0;
    /* pipe_list is for those commands attempt to write to the future command that is not locate at the current input line. */
    vector<struct Pipe> pipe_list;
    vector<struct Command> com_list;
    set_env("PATH", "bin:.", 1);
    while(true){
        string s;
        cout << "% ";
        getline(cin, s);
        /* last_index is the index of how many commands in the com_list we have processed. */
        last_index = get_command(last_index, s, com_list, pipe_list);
    }
    
    return 0;
}