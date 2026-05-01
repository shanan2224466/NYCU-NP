#include <stdio.h>
#include <stdlib.h>
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
#include <sys/select.h>
#include <arpa/inet.h>
#include <sstream>
#include <map>

using namespace std;

struct UserPipe {
    int from_id;
    int to_id;
    int fd[2];
};

struct NumberedPipe {
    int  countdown;
    int  fd[2];
};

struct Command {
    vector<string> args;
    string         outfile;
    char           pipe_type;
    int            pipe_n;
};

struct LineContext {
    vector<Command> cmds;
    int            user_pipe_to = 0;
    int            user_pipe_from = 0;
};

struct Client {
    int    fd;
    size_t id;
    string name = "(no name)";
    int    port;
    string ip;
    unordered_map<string , string> env = {{"PATH", "bin:."}};
    vector<NumberedPipe> g_npipes;
};
 
static vector<struct UserPipe> g_user_pipes;

static void safe_pipe(int fd[2]) {
    if (pipe(fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
}

void create_user_pipe(int from_id, int to_id) {
    UserPipe up;
    up.from_id = from_id;
    up.to_id = to_id;
    safe_pipe(up.fd);
    g_user_pipes.push_back(up);
}

void remove_user_pipe(int from_id, int to_id) {
    for (size_t i = 0; i < g_user_pipes.size(); i++) {
        if (g_user_pipes[i].from_id == from_id && g_user_pipes[i].to_id == to_id) {
            close(g_user_pipes[i].fd[0]);
            close(g_user_pipes[i].fd[1]);
            g_user_pipes.erase(g_user_pipes.begin() + i);
            break;
        }
    }
}

int getpipeto(int from_id, int to_id) {
    for (size_t i = 0; i < g_user_pipes.size(); i++) {
        if (g_user_pipes[i].from_id == from_id && g_user_pipes[i].to_id == to_id) {
            return i;
        }
    }
    return -1;
}

static vector<Client> clients;

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

void broadcast_msg(const char *msg) {
    for (const auto &c : clients) {
        send_msg(c.fd, msg);
    }
}

string get_env(int client_fd, const string &var) {
    for (const auto &c : clients) {
        if (c.fd == client_fd) {
            auto it = c.env.find(var);
            if (it != c.env.end())
                return it->second;
            else
                return "";
        }
    }
    return "";
}

int add_client(int client_fd, const char *ip, int port) {
    if (clients.size() >= 30) {
        send_msg(client_fd, "Server is full. Try again later.\n");
        return -1;
    }

    Client c;
    c.fd = client_fd;
    c.port = port;
    c.ip = ip;

    size_t pos;
    for (pos = 0; pos < clients.size(); pos++){
        if (clients[pos].id != pos + 1) {
            break;
        }
    }
    c.id = pos + 1;
    clients.insert(clients.begin() + pos, c);

    send_msg(client_fd, "\n"
    "****************************************\n"
    "** Welcome to the information server. **\n"
    "****************************************\n");
    
    string login = "*** User " + clients[pos].name + " entered from " + clients[pos].ip + ":" + to_string(clients[pos].port) + ". ***\n" + "% ";
    broadcast_msg(login.c_str());
    return 0;
}

void remove_client(int client_fd) {
    for (size_t i = 0; i < clients.size(); i++) {
        if (clients[i].fd == client_fd) {
            close(clients[i].fd);
            clients.erase(clients.begin() + i);
            string logout = "*** User " + clients[i].name + " left. ***\n";
            broadcast_msg(logout.c_str());
            break;
        }
    }
}

void who(int client_fd) {
    string msg = "<ID> <nickname> <IP:port> <indicate me>\n";
    for (const auto &c : clients) {
        msg += to_string(c.id) + " " + c.name + " " + c.ip + ":" + to_string(c.port);
        if (c.fd == client_fd)
            msg += " <-me";
        msg += "\n";
    }
    send_msg(client_fd, msg.c_str());
}

void yell(int client_fd, const vector<string> &args) {
    string msg;
    for (vector<string>::const_iterator it = args.begin() + 1; it != args.end(); it++) {
        msg += *it + " ";
    }
    for (auto &c : clients) {
        if (c.fd == client_fd) {
            msg = "*** " + c.name + " yelled ***: " + msg + "\n";
            break;
        }
    }
    broadcast_msg(msg.c_str());
}

void tell(int client_fd, const vector<string> &args) {
    Client *sender;
    for (auto &c : clients) {
        if (c.fd == client_fd) {
            sender = &c;
            break;
        }
    }
    size_t target = stoi(args[1]);
    for (auto &c : clients) {
        if (c.id == target) {
            string msg = "*** " + sender->name + " told you ***: ";
            for (vector<string>::const_iterator it = args.begin() + 2; it != args.end(); it++) {
                msg += *it + " ";
            }
            msg += "\n";
            send_msg(c.fd, msg.c_str());
            return;
        }
    }

    string msg = "*** Error: user #" + to_string(target) + " does not exist yet. ***\n";
    send_msg(client_fd, msg.c_str());
}

void change_name(int client_fd, const string &new_name) {
    Client *client;
    for (auto& c : clients) {
        if (c.name == new_name) {
            string msg = "*** User '" + new_name + "' already exists. ***\n";
            send_msg(client_fd, msg.c_str());
            return;
        }
        if (c.fd == client_fd) {
            client = &c;
        }
    }

    string msg = "*** User from " + client->ip + ":" + to_string(client->port) + " is named '" + new_name + "'. ***\n";
    broadcast_msg(msg.c_str());
    client->name = new_name;
}

int setup_server_socket() {
    int server_fd;
    struct sockaddr_in server_info;

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

    return server_fd;
}

int accept_client(int server_fd) {
    int client_fd;
    struct sockaddr_in client_info;
    socklen_t client_len = sizeof(client_info);

    client_fd = accept(server_fd, (struct sockaddr*) &client_info, &client_len);

    if (client_fd < 0) {
        perror("accept");
        exit(0);
    }

    if (add_client(client_fd, inet_ntoa(client_info.sin_addr), ntohs(client_info.sin_port)) < 0) {
        close(client_fd);
        return -1;
    }

    return client_fd;
}

static vector<string> tokenize(const string& line) {
    vector<string> tokens;
    istringstream  ss(line);
    string         tok;
    while (ss >> tok)
        tokens.push_back(tok);
    return tokens;
}

static int handle_builtin(const vector<string> &args, int client_fd) {
    if (args.empty()) return false;

    if (args[0] == "exit") {
        remove_client(client_fd);
        return -1;

    } else if (args[0] == "setenv") {
        if (args.size() < 3) {
            send_msg(client_fd, "Usage: setenv <var> <value>\n");
            return 1;
        }
        for (auto &c : clients) {
            if (c.fd == client_fd) {
                c.env[args[1]] = args[2];
                break;
            }
        }
        return 1;

    } else if (args[0] == "printenv") {
        if (args.size() < 2) {
            send_msg(client_fd, "Usage: printenv <var>\n");
            return 1;
        }
        send_msg(client_fd, get_env(client_fd, args[1]).c_str());
        return 1;

    } else if (args[0] == "who") {
        who(client_fd);
        return 1;

    } else if (args[0] == "tell") {
        if (args.size() < 3) {
            send_msg(client_fd, "Usage: tell <user id> <message>\n");
            return 1;
        }
        tell(client_fd, args);
        return 1;

    } else if (args[0] == "yell") {
        if (args.size() < 2) {
            send_msg(client_fd, "Usage: yell <message>\n");
            return 1;
        }
        yell(client_fd, args);
        return 1;

    } else if (args[0] == "name") {
        if (args.size() < 2) {
            send_msg(client_fd, "Usage: name <new_name>\n");
            return 1;
        }
        change_name(client_fd, args[1]);
        return 1;
    }

    return 0;
}

static LineContext parse_line(const string& line) {
    LineContext ctx;
    vector<string>  tokens = tokenize(line);
    Command         cur;
    cur.pipe_type = ' ';
    cur.pipe_n    = 0;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const string &t = tokens[i];

        if (t == "|") {
            cur.pipe_type = '|';
            cur.pipe_n    = 0;
            ctx.cmds.push_back(cur);
            cur = Command();
            cur.pipe_type = ' ';
            cur.pipe_n    = 0;

        } else if (t == "!") {
            cur.pipe_type = '!';
            cur.pipe_n    = 0;
            ctx.cmds.push_back(cur);
            cur = Command();
            cur.pipe_type = ' ';
            cur.pipe_n    = 0;

        } else if (t.size() >= 2 && t[0] == '>' && isdigit((unsigned char)t[1])) {
            ctx.user_pipe_to = stoi(t.substr(1));

        } else if (t.size() >= 2 && t[0] == '<' && isdigit((unsigned char)t[1])) {
            ctx.user_pipe_from = stoi(t.substr(1));

        } else if (t.size() >= 2 && t[0] == '|' && isdigit((unsigned char)t[1])) {
            cur.pipe_type = '|';
            cur.pipe_n    = stoi(t.substr(1));
            ctx.cmds.push_back(cur);
            cur = Command();
            cur.pipe_type = ' ';
            cur.pipe_n    = 0;

        } else if (t.size() >= 2 && t[0] == '!' && isdigit((unsigned char)t[1])) {
            cur.pipe_type = '!';
            cur.pipe_n    = stoi(t.substr(1));
            ctx.cmds.push_back(cur);
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
        ctx.cmds.push_back(cur);

    return ctx;
}

static void wait_pids(const vector<pid_t> &pids) {
    int status;
    for (pid_t p : pids)
        waitpid(p, &status, 0);
}

int get_client_index(int client_fd) {
    for (size_t i = 0; i < clients.size(); i++) {
        if (clients[i].fd == client_fd) {
            return i;
        }
    }
    return -1;
}

static void tick_numbered_pipes(int client_fd) {
    int idx = get_client_index(client_fd);
    for (auto &np : clients[idx].g_npipes)
        np.countdown--;
}

static int find_ready_npipe(int client_fd) {
    int idx = get_client_index(client_fd);
    for (size_t i = 0; i < clients[idx].g_npipes.size(); i++)
        if (clients[idx].g_npipes[i].countdown == 0)
            return i;
    return -1;
}

static int get_npipe_write(int n, int client_fd) {
    int idx = get_client_index(client_fd);
    for (auto &np : clients[idx].g_npipes)
        if (np.countdown == n)
            return np.fd[1];

    NumberedPipe np;
    np.countdown = n;
    safe_pipe(np.fd);
    clients[idx].g_npipes.push_back(np);
    return np.fd[1];
}

static void command_exist(const LineContext &ctx, int client_fd) {
    const string &cmd = ctx.cmds[0].args[0];
    if (cmd == "setenv" || cmd == "printenv" || cmd == "exit" || cmd == "who" || cmd == "tell" || cmd == "yell" || cmd == "name") {
        return;
    }

    int idx = get_client_index(client_fd);
    stringstream ss(clients[idx].env["PATH"]);
    string dir;
    while (getline(ss, dir, ':')) {
        dir = dir + "/" + cmd;
        if (access(dir.c_str(), X_OK) == 0) {
            return;
        }
    }
    string err = "Unknown command: [" + cmd + "].\n";
    send_msg(client_fd, err.c_str());
    return;
}

int print_userpipe_msg(LineContext &ctx, int client_fd, string &line) {
    int idx = get_client_index(client_fd);

    if (getpipeto(ctx.user_pipe_from, idx + 1) == -1 && ctx.user_pipe_from != 0) {
        if (ctx.user_pipe_from > (int)clients.size()) {
            string msg = "*** Error: user #" + to_string(ctx.user_pipe_from) + " does not exist yet. ***\n";
            send_msg(client_fd, msg.c_str());
        }
        else {
            string msg = "*** Error: the pipe #" + to_string(ctx.user_pipe_from) + "->#" + to_string(idx + 1) + " already exist yet. ***\n";
            send_msg(client_fd, msg.c_str());
            command_exist(ctx, client_fd);
            return -1;
        }
    }
    if (getpipeto(idx + 1, ctx.user_pipe_to) < 0 && ctx.user_pipe_to != 0) {
        if (ctx.user_pipe_to > (int)clients.size()) {
            string msg = "*** Error: user #" + to_string(ctx.user_pipe_to) + " does not exist yet. ***\n";
            send_msg(client_fd, msg.c_str());
            command_exist(ctx, client_fd);
            return -1;
        }
    }
    else if (getpipeto(idx + 1, ctx.user_pipe_to) != -1 && ctx.user_pipe_to != 0) {
        string msg = "*** Error: the pipe #" + to_string(idx + 1) + "->#" + to_string(ctx.user_pipe_to) + " already exists. ***\n";
        send_msg(client_fd, msg.c_str());
        command_exist(ctx, client_fd);
        return -1;
    }

    if (ctx.user_pipe_from != 0) {
        int pipe_idx = getpipeto(ctx.user_pipe_from, idx + 1);
        if (pipe_idx != -1) {
            string msg = "*** " + clients[idx].name + " (#" + to_string(idx + 1) + ") just received from " + clients[ctx.user_pipe_from - 1].name + " (#" + to_string(ctx.user_pipe_from) + ") by '" + line + "' ***\n";
            int from_fd = clients[ctx.user_pipe_from - 1].fd;
            send_msg(from_fd, msg.c_str());
            send_msg(client_fd, msg.c_str());
        }
    }
    if (ctx.user_pipe_to != 0) {
        string msg = "*** " + clients[idx].name + " (#" + to_string(idx + 1) + ") just piped '" + line + "' to " + clients[ctx.user_pipe_to - 1].name + " (#" + to_string(ctx.user_pipe_to) + ") ***\n";
        int to_fd = clients[ctx.user_pipe_to - 1].fd;
        send_msg(to_fd, msg.c_str());
        send_msg(client_fd, msg.c_str());
    }
    return 0;
}

static void execute_line(LineContext &ctx, int client_fd, int server_fd) {
    if (ctx.cmds.empty()) return;

    int ordinary_read = -1;
    static const int BATCH_SIZE = 50;

    size_t batch_start = 0;
    int idx = get_client_index(client_fd);
    setenv("PATH", clients[idx].env["PATH"].c_str(), 1);

    while (batch_start < ctx.cmds.size()) {
        size_t batch_end = min(batch_start + (size_t)BATCH_SIZE, ctx.cmds.size());

        vector<pid_t> batch_pids;

        for (size_t idx = batch_start; idx < batch_end; idx++) {
            Command &cmd = ctx.cmds[idx];

            int in_fd = STDIN_FILENO;

            if (ordinary_read != -1) {
                in_fd = ordinary_read;
                ordinary_read = -1;

            } else {
                int npi = find_ready_npipe(client_fd);
                if (npi != -1) {
                    int id = get_client_index(client_fd);
                    close(clients[id].g_npipes[npi].fd[1]);
                    in_fd = clients[id].g_npipes[npi].fd[0];
                    clients[id].g_npipes.erase(clients[id].g_npipes.begin() + npi);
                }
            }

            int incoming_write_fd = -1;
            if (ctx.user_pipe_from != 0 && idx == 0) {
                int pipe_idx = getpipeto(ctx.user_pipe_from, get_client_index(client_fd) + 1);
                if (pipe_idx != -1) {
                    incoming_write_fd = g_user_pipes[pipe_idx].fd[1];
                    in_fd = g_user_pipes[pipe_idx].fd[0];
                    g_user_pipes.erase(g_user_pipes.begin() + pipe_idx);
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
                out_fd = get_npipe_write(cmd.pipe_n, client_fd);
                if (cmd.pipe_type == '!')
                    err_fd = out_fd;
                close_out = false;
            }

            if (ctx.user_pipe_to != 0 && idx == ctx.cmds.size() - 1) {
                int idx = get_client_index(client_fd);
                create_user_pipe(clients[idx].id, ctx.user_pipe_to);
                out_fd = g_user_pipes.back().fd[1];
                close_out = false;
            }

            if (incoming_write_fd != -1) {
                close(incoming_write_fd);
            }

            pid_t pid = fork();
            if (pid < 0) {
                perror("fork");
                exit(EXIT_FAILURE);

            } else if (pid == 0) {

                dup2(client_fd, STDIN_FILENO);
                dup2(client_fd, STDOUT_FILENO);
                dup2(client_fd, STDERR_FILENO);

                if (in_fd != STDIN_FILENO) {
                    dup2(in_fd, STDIN_FILENO);
                    close(in_fd);
                }
                if (out_fd != STDOUT_FILENO)
                    dup2(out_fd, STDOUT_FILENO);
                if (err_fd != STDERR_FILENO)
                    dup2(err_fd, STDERR_FILENO);

                for (auto &c : clients) {
                    for (auto &np : c.g_npipes) {
                        close(np.fd[0]);
                        close(np.fd[1]);
                    }
                }
                for (auto &up : g_user_pipes) {
                    close(up.fd[0]);
                    close(up.fd[1]);
                }

                if (ordinary_read != -1)
                    close(ordinary_read);
                if (out_fd != STDOUT_FILENO && out_fd != err_fd)
                    close(out_fd);
                if (err_fd != STDERR_FILENO)
                    close(err_fd);

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

                if (in_fd != STDIN_FILENO) {
                    close(in_fd);
                }

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
    int server_fd = setup_server_socket();

    fd_set current_fd_set, ready_fd_set;
    FD_ZERO(&current_fd_set);
    FD_SET(server_fd, &current_fd_set);
    int max_fd = server_fd;

    while (true) {
        ready_fd_set = current_fd_set;

        if (select(max_fd + 1, &ready_fd_set, NULL, NULL, NULL) < 0) {
            perror("select");
            exit(0);
        }

        for (int fd = 0; fd <= max_fd; ++fd) {
            if (FD_ISSET(fd, &ready_fd_set)) {
                if (fd == server_fd) {
                    int client_fd = accept_client(server_fd);
                    if (client_fd < 0) {
                        continue;
                    }

                    FD_SET(client_fd, &current_fd_set);
                    if (client_fd > max_fd) {
                        max_fd = client_fd;
                    }
                }
                else {
                    char inputBuf[15000];
                    ssize_t n;
                    if ((n = recv(fd, inputBuf, sizeof(inputBuf) - 1, 0)) > 0) {
                        inputBuf[n] = '\0';
                        
                        string line(inputBuf);

                        if (line.empty() || line.find_first_not_of(" \t\r\n") == string::npos)
                            continue;

                        line.erase(line.find_last_not_of(" \t\r\n") + 1);

                        LineContext ctx;
                        ctx = parse_line(line);
                        if (ctx.cmds.empty()) continue;

                        int stat = handle_builtin(ctx.cmds[0].args, fd);
                        if (stat == -1) {
                            FD_CLR(fd, &current_fd_set);
                            continue;
                        }

                        if (stat == 0 && print_userpipe_msg(ctx, fd, line) == 0) {
                            execute_line(ctx, fd, server_fd);
                        }
                        tick_numbered_pipes(fd);
                    }
                    send_msg(fd, "% ");
                }
            }
        }
    }

    close(server_fd);
    return 0;
}
