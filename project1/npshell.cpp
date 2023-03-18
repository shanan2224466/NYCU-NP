#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>
#include <set>
#include <errno.h>
#include <iomanip>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

using namespace std;
#define TEST true
#define MAX_LENGTH 100
#define MAX_PROCESS 100
#define MAX_CMD 10

/* index, numbered, type, comm */
struct Command{
    // Command index of the current line.
    size_t index;
    // Numbered pipe (i) 0 means write to the stdout. (ii) '|' has numbered = 1.
    size_t numbered;
    char type;
    string comm;
};
/* index, pid, pipefd[2] */
struct Pipe{
    // Count down the numbered pipe.
    size_t index;
    int pipefd[2];
};

/* Convert a string to a int which ignore all the non-digit characters. */
size_t s2i(string s){
    string result = "";
    for (char c : s)
        if (isdigit(c))
            result += c;
    return (size_t) stoi(result);
}

void set_env(string var, string val, int overwrite){
    const char *name = var.c_str(), *value = val.c_str();
    if (setenv(name, value, overwrite))
        cerr << "** setenv error **" << endl;
}

void get_env(string var){
    const char *name = var.c_str();
    char *p = getenv(name);
    if (p != NULL)
        cout << p << endl;
    else
        cerr << "** getenv error **" << endl;
}

void print_pipelist(const vector<struct Pipe> &pipe_list){
    for (const auto &p : pipe_list)
        cout << " numbered: " << left << setw(25) << p.index << " pipe: " << p.pipefd[0] << ", " << p.pipefd[1] << endl;
}

void print_comlist(const vector<struct Command> &com_list){
    for (const auto &c : com_list)
        cout << c.index << " command: " << left << setw(15) << c.comm << " numbered: " << left << setw(3) << c.numbered << " type: " << c.type << endl;
}

/* Close all tmp_pipe except those pipes that were stored in the pipe_list. */
void close_tmppipe(int tmp_pipe[][2], vector<struct Pipe> &pipe_list){
    for (int i = 0; i < MAX_PROCESS; i++){
        int tmp = tmp_pipe[i][0];
        auto it = find_if(pipe_list.begin(), pipe_list.end(), [tmp, i](const struct Pipe &p){return tmp == p.pipefd[0];});
        /* If the tmp_pipe is not in the pipe_list. */
        if (it == pipe_list.end()){
            close(tmp_pipe[i][0]);
            close(tmp_pipe[i][1]);
        }
    }
}
/* Close the outdated pipe file descriptor. */
void close_pipelist(vector<struct Pipe> &pipe_list){
    for (auto it = pipe_list.begin(); it != pipe_list.end();){
        /* index is an unsigned long int. */
        if ((ssize_t)it->index < 0){
            close(it->pipefd[0]);
            close(it->pipefd[1]);
            it = pipe_list.erase(it);
        }
        else{
            ++it;
        }
    }
}

void wait_pid(const vector<int> &pid_list){
    int status;
    for (const auto &it : pid_list){
        waitpid(it, &status, 0);
    }
}

/* Check if the pipe of the command should be in the pipe_list. */
bool store_pipelist(int count, size_t com_listsize, const struct Command &it, const vector<struct Pipe> pipe_list){
    /* There are some conditions that the pipe should be kept in the list.
       The command attempts to write to the command (i) which is not right behind it. (ii) which is not in the current command line. (iii) which is exceed the MAX_PROCESS capacity. */
    if (it.numbered > 1 || it.index + it.numbered >= com_listsize || (count % MAX_PROCESS) + it.numbered >= MAX_PROCESS){
        /* If there was one previous command already attempted to write to the command that you wanna write to,
           it should not be store in the pipe_list. You just redirect the stdout to the existing pipe writing end.*/
        for (const auto &item : pipe_list){
            if (item.index == it.numbered)
                return false;
        }
        return true;
    }
    return false;
}

/* Extract the commands from the input line, then manage them to a command list. */
vector<struct Command> get_comlist(string &s, vector<struct Command> &com_list){
    string c, cmd = "";
    stringstream ss(s);
    while(ss >> c){
        if (c[0] != '|' && c[0] != '!'){
            cmd += c + ' ';
        }
        else if (c[0] == '|'){
            size_t n = (c.size() == 1 ? 1 : s2i(c));
            /* For the spec 3.5.5 */
            if (com_list.size()){
                if (com_list[com_list.size() - 1].type == '!'){
                    com_list[com_list.size() - 1].numbered++;
                }
            }
            com_list.push_back({com_list.size(), n, '|', cmd});
            cmd = "";
        }
        else if (c[0] == '!'){
            size_t n = (c.size() == 1 ? 1 : s2i(c));
            /* For the spec 3.5.5 */
            if (com_list.size()){
                if (com_list[com_list.size() - 1].type == '|'){
                    com_list[com_list.size() - 1].numbered++;
                }
            }
            com_list.push_back({com_list.size(), n, '!', cmd});
            cmd = "";
        }
    }
    if (cmd != ""){
        com_list.push_back({com_list.size(), 0, ' ', cmd});
    }
    /* Mimic the real shell operation. */
    else if (cmd == "" && c.size() == 1){
        string S;
        cout << "> ";
        getline(cin, S);
        get_comlist(S, com_list);
    }
    return com_list;
}

/* Fork the child process. After some preparation including dup2 and close, we use execvp() to run the child process. */
int run(char **argv, bool error, int in, int out, int tmp_pipe[][2], vector<struct Pipe> &pipe_list){
    pid_t pid;
    if ((pid = fork()) == 0){
        dup2(in, STDIN_FILENO);
        dup2(out, STDOUT_FILENO);
        if (error)
            dup2(out, STDERR_FILENO);
        /* Do not close the in and out file descriptors. They came from tmp_pipe and pipe_list. Moreover, they might be STDOUT or STDIN sometimes. */
        close_tmppipe(tmp_pipe, pipe_list);
        for (auto &it : pipe_list){
            close(it.pipefd[0]);
            close(it.pipefd[1]);
        }
        if (execvp(argv[0], argv) == -1){
            perror("Execvp error");
            cerr << "errno: " << errno << endl;
            exit(1);
        }
    }
    else if (pid < 0){
        perror("Fork error");
        cerr << "errno: " << errno << endl;
        exit(1);
    }
    return pid;
}
/* Process the commands at the current input line. */
void execute(vector<struct Command> &com_list, vector<struct Pipe> &pipe_list){
    int count = 0, tmp_pipe[MAX_PROCESS][2];
    vector<int> pid_list;
    for (auto &it : com_list){
        string filename = "";
        char *argv[MAX_CMD], buffer[MAX_LENGTH];
        strcpy(buffer, it.comm.c_str());
        memset(argv, 0, sizeof(char*) * MAX_CMD);

        argv[0] = strtok(buffer, " \t\n\r");
        for (int i = 1; i < MAX_CMD; i++){
            argv[i] = strtok(NULL, " \t\n\r");
            if (argv[i] == NULL)
                break;
            else if (strcmp(argv[i], ">") == 0){
                argv[i] = NULL;
                filename = strtok(NULL, " \t\n\r");
            }
        }

        /* Close pipes and wait for the child processes when the amount of the forked processes exceed MAX_PROCESS. */
        if (count / MAX_PROCESS > 0 && !(count % MAX_PROCESS)){
            close_tmppipe(tmp_pipe, pipe_list);
            wait_pid(pid_list);
            pid_list.clear();
        }
        
        /* Initialize MAX_PROCESS pipes at the beginning of every MAX_PROCESS rounds. */
        if (count % MAX_PROCESS == 0){
            for (int i = 0; i < MAX_PROCESS; i++){
                if(pipe(tmp_pipe[i]) == -1){
                    perror("Pipe error");
                    cerr << "errno: " << errno << endl;
                    exit(1);
                }
            }
        }
        
        if (store_pipelist(count, com_list.size(), it, pipe_list)){
            struct Pipe tmp = {it.numbered, {tmp_pipe[count % MAX_PROCESS][0], tmp_pipe[count % MAX_PROCESS][1]}};
            pipe_list.push_back(tmp);
        }

        set<string> command_set {"ls", "cat", "number", "removetag", "removetag0", "noop"};
        auto match = command_set.find(argv[0]);
        if (match != command_set.end()){
            /* Initialize the writing end to the current pipe[1], and the reading end to the previous pipe[0]. */
            int in = tmp_pipe[(count + 499) % MAX_PROCESS][0], out = tmp_pipe[count % MAX_PROCESS][1];
            
            bool error = false;
            if (it.type == '!')
                error = true;
            
            /* (i) There was a pipe writes to the current command. (ii) There was a command writes to the same future command. */
            for (size_t i = 0; i < pipe_list.size(); i++){
                if (pipe_list[i].index == 0)
                    in = pipe_list[i].pipefd[0];
                if (pipe_list[i].index == it.numbered)
                    out = pipe_list[i].pipefd[1];
            }
            if (it.numbered == 0)
                out = STDOUT_FILENO;
            if (!filename.empty()){
                /* The permission flag (user, group, others) needs at least S_IRWXU. */
                if ((out = open(filename.c_str(), O_RDWR | O_TRUNC | O_CREAT, S_IRWXU | S_IROTH)) == -1){
                    perror("Error opening file");
                    cout << "errno: " << errno << endl;
                    exit(1);
                }
            }
            int pid = run(argv, error, in, out, tmp_pipe, pipe_list);
            pid_list.push_back(pid);
            if (!filename.empty())
                close(out);
        }
        else{
            cerr << "Unknown command: [" << argv[0] << "]." << endl;
        }
        for (auto &it : pipe_list)
            it.index--;
        count++;
        /* For spec 3.5.8. If the parent close() outside the loop, the later child process will hang forever.
           Since all processes (including parent process) close the write end of a pipe, then the read end
           would receive the EOF. Any process hasn't close it, would cause the child process (such as cat)
           waits for the read end receive any further input. */
        close_pipelist(pipe_list);
    }
    close_tmppipe(tmp_pipe, pipe_list);
    wait_pid(pid_list);
}
/* Preprocess the built-in commands which can be refered to spec 3.3 */
void pre_execute(string &s, vector<struct Pipe> &pipe_list){
    vector<struct Command> com_list;
    get_comlist(s, com_list);
    //print_comlist(com_list);
    
    int i = 0;
    string command[MAX_CMD];
    stringstream ss(com_list[0].comm);
    while(ss >> command[i++]);
    if(command[0] == "exit")
        exit(0);
    else if (command[0] == "setenv"){
        if (!command[1].empty() && !command[2].empty())
            set_env(command[1], command[2], 1);
        else
            cerr << "Syntax: setenv [variable] [value]" << endl;
        /* For spec 3.5.6 */
        for (auto &it : pipe_list)
            it.index--;
    }
    else if (command[0] == "printenv"){
        if (!command[1].empty())
            get_env(command[1]);
        else
            cerr << "Syntax: printenv [variable]" << endl;
        /* For spec 3.5.6 */
        for (auto &it : pipe_list)
            it.index--;
    }
    else{
        execute(com_list, pipe_list);
    }
    return;
}

int main(int argc, char* argv[]){
    vector<struct Pipe> pipe_list;
    set_env("PATH", "bin:.", 1);
    if (TEST)
        freopen("test_case.txt", "r", stdin);
    while(true){
        string s;
        cout << "% ";
        getline(cin, s);
        if (TEST)
            cout << s << endl;
        if (s == "")
            continue;
        pre_execute(s, pipe_list);
    }
    
    return 0;
}