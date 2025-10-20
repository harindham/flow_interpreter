Following is the main design of our algorithm:

1. Parser Layer

    * Parses the .flow file and stores each component (node, pipe, concatenate, stderr redirect, file I/O) in corresponding structures inside a global Flow object.

2. Execution Engine

    * The execute_component() function recursively resolves dependencies and executes components.

    * For pipes, it uses Unix pipe() and fork() to connect the output of one component to the input of another. Connects. the output of one component (source) to the input of another (destination). Implemented using the pipe() system call to create a pair of file descriptors.

    * For nodes, it uses execl("/bin/sh", "sh", "-c", command, NULL) to run shell commands. Implemented via exec_node(), which redirects stdin/stdout using dup2() and executes the command using execl()

    * For files, it supports both read and write redirection (depending on whether an input_fd is provided).

    * For concatenate, multiple inputs are merged and passed forward through a single output stream.

    * Also there is Stderr Redirect, Allows redirecting a node’s standard error stream into a pipeline.

3. I/O Handling

    * Uses dup2() to redirect stdin, stdout, and optionally stderr between processes.

    * Handles reading/writing via file descriptors for efficient streaming.