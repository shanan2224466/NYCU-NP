#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>

using namespace std;

struct Command {
    vector<string> args;
    string         outfile;
    char           pipe_type;
    int            pipe_n;
};

struct NumberedPipe {
    int  countdown;
    int  fd[2];
};

void send_msg(int client_fd, const char *msg) {
    size_t len = strlen(msg);
    size_t total_sent = 0;
    while (total_sent < len) {
        ssize_t n = send(client_fd, msg + total_sent, len - total_sent, 0);
        if (n < 0) {
            perror("send_msg");
            return;
        }
        total_sent += n;
    }
    return;
}

static vector<string> tokenize(const string& line) {
    vector<string> tokens;
    istringstream  ss(line);
    string         tok;
    while (ss >> tok)
        tokens.push_back(tok);
    return tokens;
}

static void safe_pipe(int fd[2]) {
    if (pipe(fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
}

static void wait_pids(const vector<pid_t> &pids) {
    int status;
    for (pid_t p : pids)
        waitpid(p, &status, 0);
}

static int handle_builtin(const vector<string> &args, int client_fd) {
    if (args.empty()) return false;

    if (args[0] == "exit") {
        return -1;

    } else if (args[0] == "setenv") {
        if (args.size() < 3) {
            send_msg(client_fd, "Usage: setenv <var> <value>\n");
            return 1;
        }
        if (setenv(args[1].c_str(), args[2].c_str(), 1) == -1)
            perror("setenv");
        return 1;

    } else if (args[0] == "printenv") {
        if (args.size() < 2) {
            send_msg(client_fd, "Usage: printenv <var>\n");
            return 1;
        }
        const char *val = getenv(args[1].c_str());
        if (val)
            send_msg(client_fd, ((string)val + "\n").c_str());
        return 1;
    }

    return 0;
}

static vector<Command> parse_line(const string& line) {
    vector<string>  tokens = tokenize(line);
    vector<Command> cmds;
    Command         cur;
    cur.pipe_type = ' ';
    cur.pipe_n    = 0;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const string &t = tokens[i];

        if (t == "|") {
            cur.pipe_type = '|';
            cur.pipe_n    = 0;
            cmds.push_back(cur);
            cur = Command();
            cur.pipe_type = ' ';
            cur.pipe_n    = 0;

        } else if (t.size() >= 2 && t[0] == '|' && isdigit((unsigned char)t[1])) {
            cur.pipe_type = '|';
            cur.pipe_n    = stoi(t.substr(1));
            cmds.push_back(cur);
            cur = Command();
            cur.pipe_type = ' ';
            cur.pipe_n    = 0;

        } else if (t.size() >= 2 && t[0] == '!' && isdigit((unsigned char)t[1])) {
            cur.pipe_type = '!';
            cur.pipe_n    = stoi(t.substr(1));
            cmds.push_back(cur);
            cur = Command();
            cur.pipe_type = ' ';
            cur.pipe_n    = 0;

        } else if (t == ">") {
            if (i + 1 < tokens.size()) {
                cur.outfile = tokens[++i];
            }

        } else {
            cur.args.push_back(t);
        }
    }

    if (!cur.args.empty())
        cmds.push_back(cur);

    return cmds;
}

static vector<NumberedPipe> g_npipes;

static void tick_numbered_pipes() {
    for (auto &np : g_npipes)
        np.countdown--;
}

static int find_ready_npipe() {
    for (int i = 0; i < (int)g_npipes.size(); i++)
        if (g_npipes[i].countdown == 0)
            return i;
    return -1;
}

static int get_npipe_write(int n, bool merge_stderr) {
    for (auto &np : g_npipes)
        if (np.countdown == n)
            return np.fd[1];

    NumberedPipe np;
    np.countdown = n;
    safe_pipe(np.fd);
    g_npipes.push_back(np);
    (void)merge_stderr;
    return np.fd[1];
}

static const int BATCH_SIZE = 50;

static void execute_line(vector<Command> &cmds, int client_fd, int server_fd) {
    if (cmds.empty()) return;

    int ordinary_read = -1;

    size_t batch_start = 0;

    while (batch_start < cmds.size()) {
        size_t batch_end = min(batch_start + (size_t)BATCH_SIZE, cmds.size());

        vector<pid_t> batch_pids;

        for (size_t idx = batch_start; idx < batch_end; idx++) {
            Command &cmd = cmds[idx];

            dup2(client_fd, STDIN_FILENO);
            dup2(client_fd, STDOUT_FILENO);
            dup2(client_fd, STDERR_FILENO);

            int in_fd = STDIN_FILENO;

            if (ordinary_read != -1) {
                in_fd = ordinary_read;
                ordinary_read = -1;
            } else {
                int npi = find_ready_npipe();
                if (npi != -1) {
                    close(g_npipes[npi].fd[1]);
                    in_fd = g_npipes[npi].fd[0];
                    g_npipes.erase(g_npipes.begin() + npi);
                }
            }

            int  out_fd    = STDOUT_FILENO;
            int  err_fd    = STDERR_FILENO;
            bool close_out = false;

            if (!cmd.outfile.empty()) {
                out_fd = open(cmd.outfile.c_str(),
                              O_WRONLY | O_CREAT | O_TRUNC,
                              S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                if (out_fd == -1) {
                    perror("open");
                    if (in_fd != STDIN_FILENO) close(in_fd);
                    continue;
                }
                close_out = true;

            } else if (cmd.pipe_type == '|' && cmd.pipe_n == 0) {
                int pfd[2];
                safe_pipe(pfd);
                out_fd        = pfd[1];
                ordinary_read = pfd[0];
                close_out     = true;

            } else if (cmd.pipe_type == '|' || cmd.pipe_type == '!') {
                out_fd = get_npipe_write(cmd.pipe_n, cmd.pipe_type == '!');
                if (cmd.pipe_type == '!')
                    err_fd = out_fd;
                close_out = false;
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                exit(EXIT_FAILURE);

            } else if (pid == 0) {

                if (in_fd != STDIN_FILENO) {
                    dup2(in_fd, STDIN_FILENO);
                    close(in_fd);
                }
                if (out_fd != STDOUT_FILENO)
                    dup2(out_fd, STDOUT_FILENO);
                if (err_fd != STDERR_FILENO)
                    dup2(err_fd, STDERR_FILENO);

                for (auto &np : g_npipes) {
                    close(np.fd[0]);
                    close(np.fd[1]);
                }
                if (ordinary_read != -1)
                    close(ordinary_read);
                if (out_fd != STDOUT_FILENO && out_fd != err_fd)
                    close(out_fd);
                if (err_fd != STDERR_FILENO)
                    close(err_fd);
                close(client_fd);

                vector<char*> argv;
                for (auto &a : cmd.args)
                    argv.push_back(const_cast<char*>(a.c_str()));
                argv.push_back(nullptr);

                if (execvp(argv[0], argv.data()) < 0) {
                    if (errno == ENOENT) {
                        string err = "Unknown command: [" + (string)argv[0] + "].\n";
                        write(STDERR_FILENO, err.c_str(), err.size());
                    }
                }

            } else {
                batch_pids.push_back(pid);

                if (in_fd != STDIN_FILENO)
                    close(in_fd);

                if (close_out) {
                    close(out_fd);
                }

            }
        }

        wait_pids(batch_pids);

        batch_start = batch_end;
    }

    if (ordinary_read != -1)
        close(ordinary_read);
}

int main() {
    setenv("PATH", "bin:.", 1);

    int client_fd, server_fd;
    struct sockaddr_in client_info, server_info;
    socklen_t client_len = sizeof(client_info);
    char inputBuf[15000];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        exit(0);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_info, 0, sizeof(server_info));
    server_info.sin_family = AF_INET;
    server_info.sin_addr.s_addr = INADDR_ANY;
    server_info.sin_port = htons(7001);

    if (bind(server_fd, (struct sockaddr*)&server_info, sizeof(server_info)) < 0) {
        perror("bind");
        exit(0);
    }

    if (listen(server_fd, 1) < 0) {
        perror("listen");
        exit(0);
    }

    while (true) {
        client_fd = accept(server_fd, (struct sockaddr*) &client_info, &client_len);

        send_msg(client_fd, "% ");
        ssize_t n;
        while ((n = recv(client_fd, inputBuf, 15000 - 1, 0)) > 0) {
            inputBuf[n] = '\0';

            string line(inputBuf);

            if (line.empty() || line.find_first_not_of(" \t\r\n") == string::npos)
                continue;

            line.erase(line.find_last_not_of(" \t\r\n") + 1);

            vector<Command> cmds = parse_line(line);
            if (cmds.empty()) continue;

            int stat = handle_builtin(cmds[0].args, client_fd);
            if (stat == 1) {
                tick_numbered_pipes();
                send_msg(client_fd, "% ");
                continue;
            } else if (stat == -1) {
                break;
            }

            execute_line(cmds, client_fd, server_fd);

            tick_numbered_pipes();
            send_msg(client_fd, "% ");
        }
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
