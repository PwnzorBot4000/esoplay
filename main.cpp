/* esoplay - Execution environment for esoteric languages
 * Copyright (C) 2025 Thanasis Papoutsidakis
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <argparse/argparse.hpp>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

using namespace std;

const int FPS = 10;
const string termination_command = ">>ESOPLAY.TERMINATE<<";

int main(int argc, char** argv) {
    argparse::ArgumentParser program("esoplay", "1.0.0");
    program.add_description("Execution environment for esoteric languages");
    program.add_epilog("Copyright (C) 2025 Thanasis Papoutsidakis\n"
                       "This program comes with ABSOLUTELY NO WARRANTY.\n"
                       "This is free software, and you are welcome to redistribute it\n"
                       "under certain conditions; see the GNU General Public License for details.\n");
    program
        .add_argument("interpreter")
        .help("interpreter to call to execute the file");
    program
        .add_argument("file")
        .help("file to play");

    try {
        program.parse_args(argc, argv);
    }
    catch (const exception& err) {
        cerr << program;
        return 1;
    }

    string interpreter = program.get<string>("interpreter");
    string file = program.get<string>("file");

    // Create pipes for stdin and stdout that will be used by the interpreter
    int pipefd_in[2];
    int pipefd_out[2];
    pipe(pipefd_in);
    pipe(pipefd_out);
    const int stdin_pipe_read = pipefd_in[0];
    const int stdin_pipe_write = pipefd_in[1];
    const int stdout_pipe_read = pipefd_out[0];
    const int stdout_pipe_write = pipefd_out[1];

    // Fork and exec the interpreter
    int pid = fork();
    if (pid == 0) { // Child process
        // Close unused pipe ends
        close(stdin_pipe_write);
        close(stdout_pipe_read);
        // Redirect stdin and stdout to the pipes
        dup2(stdin_pipe_read, STDIN_FILENO);
        dup2(stdout_pipe_write, STDOUT_FILENO);
        // Close the pipes after duplicating
        close(stdin_pipe_read);
        close(stdout_pipe_write);

        pid = fork();
        if (pid == 0) { // Interpreter process
            execlp(interpreter.c_str(), interpreter.c_str(), file.c_str(), (char*) nullptr);
            exit(EXIT_FAILURE);
        }

        write(STDOUT_FILENO, termination_command.c_str(), termination_command.size());
        exit(EXIT_SUCCESS);
    }

    // Close unused pipe ends
    close(stdin_pipe_read);
    close(stdout_pipe_write);

    // Main loop
    const auto start_of_loop = chrono::duration_cast<chrono::milliseconds>(
        chrono::system_clock::now().time_since_epoch()
    );
    bool running = true;
    while (running) {
        // Wait for key from stdin with a timeout
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 1000000 / FPS;
        int ret = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &timeout);
        if (ret == -1) {
            perror("select");
            continue;
        }

        string key;
        if (ret > 0) {
            // Key pressed (else timeout)
            cin >> key;
        }

        const auto current_time = chrono::duration_cast<chrono::milliseconds>(
            chrono::system_clock::now().time_since_epoch()
        );
        const auto elapsed_time = current_time - start_of_loop;

        stringstream input_ss;
        input_ss << "K=" << key << "T=" << elapsed_time.count() << endl;

        // Write to child stdin
        const string input = input_ss.str();
        int bytes_written = write(stdin_pipe_write, input.c_str(), input.size());
        // Note: Here, if the child process exits, we may get a SIGPIPE.
        // We should detect when the child process exits and exit gracefully.
        if (bytes_written == -1) {
            perror("write");
            break;
        }

        // Read from child stdout
        stringstream output_ss;
        char buffer[1024];
        while (true) {
            int bytes_read = read(stdout_pipe_read, buffer, sizeof(buffer));
            if (bytes_read == -1) {
                perror("read");
                break;
            }
            if (bytes_read == 0) {
                break;
            }
            output_ss.write(buffer, bytes_read);
        }
        string output = output_ss.str();

        const auto termination_pos = output.find(termination_command);
        if (termination_pos != string::npos) {
            running = false;
            output.erase(output.find(termination_command), termination_command.size());
        }

        cout << output;
    }

    // Wait for the interpreter to finish
    wait(nullptr);

    // Close the pipes
    close(stdin_pipe_write);
    close(stdout_pipe_read);

    return 0;
}