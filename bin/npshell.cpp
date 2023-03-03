#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <regex>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

using namespace std;

struct command
{
    string comm;
    int numbered;
};

/*Convert a string to a int which ignore all the non-digit characters.*/
int s2i(string s){
    string result = "";
    for (char c : s){
        if (isdigit(c))
            result += c;
    }
    return stoi(result);
}

void set_env(string var, string val, int overwrite){
    const char *name = var.c_str(), *value = val.c_str();
    if (setenv(name, value, overwrite))
        cerr << "** setenv error **" << endl;
}

void get_env(string var){
    const char *name = var.c_str();
    char *p = getenv(name);
    if ( p != NULL)
        cout << p << endl;
    else
        cout << "** getenv error **" << endl;
    return;
}

void exe_cvp(string option, string file){
    if (!option.empty() && !file.empty()){
        const char *argv[] = {option, file, nullptr};
    }
    else if (!option.empty()){
        const char *argv[] = {option, nullptr};
    }
    else if (!file.empty()){
        const char *argv[] = {file, nullptr};
    }
    else{
        const char *argv[] = {option, nullptr};
        execvp("./bin/ls", nullptr);
    }
    execvp("./bin/ls", argv);
    cerr << "** execvp error **" << endl;
    return;
}

void ls(string option, string file){
    pid_t pid;
    return;
    if ((pid = fork()) == 0)
    {
        exe_cvp(option, file);
    }
    else if (pid > 0)
    {
        int status;
        waitpid(pid, &status, 0);
    }
    else
        cerr << "** fork error **" << endl;
    return;
}

int get_command(string &s, vector<struct command> &com_list){
    /*Extract the commands to a list from the input string line.*/
    regex re("\\|(\\d*)");
    sregex_iterator it(s.begin(), s.end(), re);
    sregex_iterator end;
    smatch match;
    if (it == end){
        com_list.push_back({s, 0});
    }
    else{
        while(it != end){
            match = *it;
            string number = match[1];
            struct command tmp = {match.prefix().str(), number.empty() ? 0 : s2i(match.str())};
            com_list.push_back(tmp);
            ++it;
            if (it == end && !match.suffix().str().empty()){
                com_list.push_back({match.suffix().str().erase(0, 1), 0});
            }
        }
    }
    /* for (const auto& p : com_list)
        cout << "command: " << p.comm << " number: " << p.numbered << endl; */
    
    for (auto it = com_list.begin(); it != com_list.end(); ++it){
        int i = 0;
        string command[10];
        stringstream ss(it->comm);
        while(ss >> command[i++]);
        for (i = 0; !command[i].empty(); i++){
            if(command[0] == "exit")
                exit(0);
            else if (command[0] == "setenv"){
                if (!command[1].empty() && !command[2].empty())
                    set_env(command[1], command[2], 1);
                else
                    cout << "Syntax: setenv [variable] [value]" << endl;
                break;
            }
            else if (command[0] == "printenv"){
                if (!command[1].empty())
                    get_env(command[1]);
                else
                    cout << "Syntax: printenv [variable]" << endl;
                break;
            }
            else if (command[0] == "ls"){
                ls(command[1], command[2]);
                break;
            }
            else{
                cerr << "Unknown command: [" << command[0] << "]." << endl;
            }
        }
    }
    return com_list.size();
}

int main(int argc, char* argv[]){
    
    set_env("PATH", "bin:.", 1);
    while(true){
        string s;
        vector<struct command> com_list;
        cout << "% ";
        getline(cin, s);
        string c;
        get_command(s, com_list);
    }
    
    return 0;
}