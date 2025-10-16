#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_NODES 128
#define MAX_PIPES 128
#define MAX_CONCAT 128
#define MAX_PARTS 16

typedef struct Node {
    char name[64];
    char command[256];
} Node;

typedef struct Pipe {
    char name[64];
    char from[64];
    char to[64];
} Pipe;

typedef struct Concat {
    char name[64];
    int parts;
    char partnames[MAX_PARTS][64];
} Concat;

typedef struct StderrRedirect {
    char name[64];
    char from[64];
} StderrRedirect;

typedef struct FileIO {
    char name[64];
    char filename[256];
} FileIO;

typedef struct Flow {
    Node nodes[MAX_NODES];
    Pipe pipes[MAX_PIPES];
    Concat concats[MAX_CONCAT];
    StderrRedirect errors[MAX_NODES];
    FileIO files[MAX_NODES];
    int node_count;
    int pipe_count;
    int concat_count;
    int err_count;
    int file_count;
} Flow;


Node* find_node(Flow *flow, const char *name) {
    for (int i = 0; i < flow->node_count; i++)
        if (strncmp(flow->nodes[i].name, name, sizeof(flow->nodes[i].name)) == 0)
            return &flow->nodes[i];
    return NULL;
}

Pipe* find_pipe(Flow *flow, const char *name) {
    for (int i = 0; i < flow->pipe_count; i++)
        if (strncmp(flow->pipes[i].name, name, sizeof(flow->pipes[i].name)) == 0)
            return &flow->pipes[i];
    return NULL;
}

Concat* find_concat(Flow *flow, const char *name) {
    for (int i = 0; i < flow->concat_count; i++)
        if (strncmp(flow->concats[i].name, name, sizeof(flow->concats[i].name)) == 0)
            return &flow->concats[i];
    return NULL;
}

FileIO* find_file(Flow *flow, const char *name) {
    for (int i = 0; i < flow->file_count; i++)
        if (strncmp(flow->files[i].name, name, sizeof(flow->files[i].name)) == 0)
            return &flow->files[i];
    return NULL;
}

StderrRedirect* find_stderr(Flow *flow, const char *name) {
    for (int i = 0; i < flow->err_count; i++)
        if (strncmp(flow->errors[i].name, name, sizeof(flow->errors[i].name)) == 0)
            return &flow->errors[i];
    return NULL;
}


void parse_flow_file(const char *filename, Flow *flow) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        exit(1);
    }
    char line[512];
    Node *n = NULL;
    Pipe *p = NULL;
    Concat *c = NULL;
    StderrRedirect *e = NULL;
    FileIO *fi = NULL;

    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        if (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[len-1] = '\0';
            len--;
        }

        char *eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = line;
        char *value = eq + 1;

        while (*key == ' ') key++;
        while (*value == ' ') value++;

        char *k_end = key + strlen(key) - 1;
        while (k_end > key && (*k_end == ' ' || *k_end == '\r')) {
            *k_end = 0;
            k_end--;
        }

        char *v_end = value + strlen(value) - 1;
        while (v_end > value && (*v_end == ' ' || *v_end == '\r')) {
            *v_end = 0;
            v_end--;
        }

        if (strncmp(key, "node", 4) == 0) {
            n = &flow->nodes[flow->node_count++];
            strncpy(n->name, value, sizeof(n->name)-1);
            n->name[sizeof(n->name)-1] = '\0';
            continue;
        }
        if (strcmp(key, "command") == 0 && n) {
            strncpy(n->command, value, sizeof(n->command)-1);
            n->command[sizeof(n->command)-1] = '\0';
            n = NULL;
            continue;
        }
        if (strncmp(key, "pipe", 4) == 0) {
            p = &flow->pipes[flow->pipe_count++];
            strncpy(p->name, value, sizeof(p->name)-1);
            p->name[sizeof(p->name)-1] = '\0';
            continue;
        }
        if (strcmp(key, "from") == 0 && p) {
            strncpy(p->from, value, sizeof(p->from)-1);
            p->from[sizeof(p->from)-1] = '\0';
            continue;
        }
        if (strcmp(key, "to") == 0 && p) {
            strncpy(p->to, value, sizeof(p->to)-1);
            p->to[sizeof(p->to)-1] = '\0';
            p = NULL;
            continue;
        }
        if (strncmp(key, "concatenate", 11) == 0) {
            c = &flow->concats[flow->concat_count++];
            strncpy(c->name, value, sizeof(c->name)-1);
            c->name[sizeof(c->name)-1] = '\0';
            c->parts = 0;
            continue;
        }
        if (strcmp(key, "parts") == 0 && c) {
            c->parts = atoi(value);
            continue;
        }
        if (strncmp(key, "part_", 5) == 0 && c) {
            int idx = atoi(key + 5);
            if (idx >= 0 && idx < MAX_PARTS) {
                strncpy(c->partnames[idx], value, sizeof(c->partnames[idx]) - 1);
                c->partnames[idx][sizeof(c->partnames[idx]) - 1] = '\0';
            }
            continue;
        }
        if (strncmp(key, "stderr", 6) == 0) {
            e = &flow->errors[flow->err_count++];
            strncpy(e->name, value, sizeof(e->name)-1);
            e->name[sizeof(e->name)-1] = '\0';
            continue;
        }
        if (strcmp(key, "from") == 0 && e) {
            strncpy(e->from, value, sizeof(e->from)-1);
            e->from[sizeof(e->from)-1] = '\0';
            e = NULL;
            continue;
        }
        if (strncmp(key, "file", 4) == 0) {
            fi = &flow->files[flow->file_count++];
            strncpy(fi->name, value, sizeof(fi->name)-1);
            fi->name[sizeof(fi->name)-1] = '\0';
            continue;
        }
        if (strcmp(key, "name") == 0 && fi) {
            strncpy(fi->filename, value, sizeof(fi->filename)-1);
            fi->filename[sizeof(fi->filename)-1] = '\0';
            fi = NULL;
            continue;
        }
    }
    fclose(f);
}

void exec_node(Node *node, int input_fd, int output_fd, int redirect_stderr) {
    pid_t pid = fork();
    if (pid == 0) {
        if (input_fd != -1) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }
        if (output_fd != -1) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }
        if (redirect_stderr)
            dup2(STDOUT_FILENO, STDERR_FILENO);

        execl("/bin/sh", "sh", "-c", node->command, NULL);
        perror("exec");
        exit(1);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    } else {
        perror("fork");
        exit(1);
    }
}

void copy_fd(int input_fd, int output_fd) {
    char buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = read(input_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_written = write(output_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("write");
            exit(1);
        }
    }

    if (bytes_read < 0) {
        perror("read");
        exit(1);
    }
}

void execute_component(Flow *flow, const char *name, int input_fd, int output_fd) {
    Node *node = find_node(flow, name);
    FileIO *file = find_file(flow, name);
    Concat *concat = find_concat(flow, name);
    StderrRedirect *stderr_redir = find_stderr(flow, name);
    Pipe *p = find_pipe(flow, name);

    if (node) {
        exec_node(node, input_fd, output_fd, 0);
        return;
    }

    if (file) {
        if(input_fd==-1){
            int fd = open(file->filename, O_RDONLY);
            if (fd < 0) { perror("open file"); exit(1); }
            char buf[1024]; ssize_t r;
            while ((r = read(fd, buf, sizeof(buf))) > 0)
                write(output_fd, buf, r);
            close(fd);
            return;
        }
        else{
            int fd = open(file->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror("open"); exit(1); }

            copy_fd(input_fd, fd);

            close(input_fd);
            close(fd);
            return;
        }
    }

    if (concat) {
        for (int i = 0; i < concat->parts; i++) {
            execute_component(flow, concat->partnames[i], input_fd, output_fd);
        }
        return;
    }

    if (stderr_redir) {
        Node *source_node = find_node(flow, stderr_redir->from);
        if (!source_node) {
            fprintf(stderr, "Source node %s for stderr component not found!\n", stderr_redir->from);
            exit(1);
        }
        exec_node(source_node, input_fd, output_fd, 1);
        return;
    }

    if (p) {
        int fds[2];
        if (pipe(fds) == -1) {
            perror("pipe");
            exit(1);
        }
        pid_t pid = fork();
        if (pid == 0) {
            close(fds[0]);
            execute_component(flow, p->from, input_fd, fds[1]);
            close(fds[1]);
            exit(0);
        }
        pid_t pid2 = fork();
        if (pid2 == 0) {
            close(fds[1]);
            execute_component(flow, p->to, fds[0], output_fd);
            close(fds[0]);
            exit(0);
        }
        close(fds[0]);
        close(fds[1]);
        waitpid(pid, NULL, 0);
        waitpid(pid2, NULL, 0);
        return;
    }

    fprintf(stderr, "Component %s not found!\n", name);
    exit(1);
}

void execute_main_pipe(Flow *flow, const char *pipe_name) {
    Pipe *pipe_obj = NULL;
    for (int i = 0; i < flow->pipe_count; i++)
        if (strncmp(flow->pipes[i].name, pipe_name, sizeof(flow->pipes[i].name)) == 0)
            pipe_obj = &flow->pipes[i];

    if (!pipe_obj) {
        fprintf(stderr, "Pipe %s not found\n", pipe_name);
        exit(1);
    }

    int fd[2];
    if (pipe(fd) == -1) {
        perror("pipe");
        exit(1);
    }

    pid_t pid = fork();
    if (pid == 0) {
        close(fd[0]);
        execute_component(flow, pipe_obj->from, -1, fd[1]);
        close(fd[1]);
        exit(0);
    } else if (pid > 0) {
        close(fd[1]);
        execute_component(flow, pipe_obj->to, fd[0], -1);
        close(fd[0]);
        waitpid(pid, NULL, 0);
    } else {
        perror("fork");
        exit(1);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <file.flow> <target>\n", argv[0]);
        exit(1);
    }

    Flow flow = {0};
    parse_flow_file(argv[1], &flow);

    /* Execute the target pipe */
    execute_main_pipe(&flow, argv[2]);
    return 0;
}
