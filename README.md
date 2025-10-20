# flow_interpreter
Introduction to Operating Systems Homework 2

## Compiling this program
```bash
gcc -Wall -Wextra -o flow flow.c
```

## Usage
```bash
./flow [.flow file] [target]
```

## Testing
All test cases are located in the `testcases/` folder along with their supporting files (input data files, etc.). The test suite includes:
- Valid flow examples (no cycles)
- Invalid flow examples (cyclic dependencies)
- Error handling tests
- File I/O tests
- Complex nested pipeline tests

To run tests:
```bash
./flow testcases/[test_file.flow] [target_component]
```


## Design and Implementation

### 1. Parser Layer
Parses the .flow file and stores each component (node, pipe, concatenate, stderr redirect, file I/O) in corresponding structures inside a global Flow object. The parser handles:
- Key=value format with whitespace trimming
- Multiple component types with proper state management
- Error handling for malformed input

### 2. Cycle Detection (Pre-Execution Validation)
**Critical Feature**: Before executing any processes, the program validates the entire flow graph for cyclic dependencies using depth-first search (DFS) with visited tracking. This prevents:
- Infinite recursion during execution
- Partial execution of cyclic flows
- Stack overflow errors

The `validate_flow()` function checks all pipes, concatenates, and stderr redirects recursively, building a call stack to detect when a component references itself directly or indirectly.

### 3. Execution Engine
The `execute_component()` function recursively resolves dependencies and executes components:

- **Pipes**: Uses Unix `pipe()` and `fork()` to connect the output of one component to the input of another. Two child processes run in parallel - one for source, one for destination.

- **Nodes**: Implemented via `exec_node()`, which redirects stdin/stdout using `dup2()` and executes commands using `execl("/bin/sh", "sh", "-c", command, NULL)` to ensure shell compatibility.

- **Files**: Supports both read and write operations. When `input_fd == -1`, opens file for reading. Otherwise, opens with `O_WRONLY | O_CREAT | O_TRUNC` for writing.

- **Concatenate**: Multiple components are executed sequentially with the same input/output file descriptors, merging their outputs into a single stream.

- **Stderr Redirect**: Redirects a node's standard error stream to stdout using `dup2(STDOUT_FILENO, STDERR_FILENO)`, allowing error messages to be piped as regular data.

### 4. I/O Handling
- Uses `dup2()` to redirect stdin, stdout, and optionally stderr between processes
- Proper file descriptor cleanup in parent and child processes
- Efficient streaming via file descriptors with 4KB buffer for file operations
