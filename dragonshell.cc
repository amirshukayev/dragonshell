#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <locale>
#include <fcntl.h>
#include <sys/wait.h>
#include <cerrno>

const std::string CD_USAGE_MSG = "dragonshell: expected argument to \"cd\".";
const std::string PWD_USAGE_MSG = "usage: pwd";
const std::string A2PATH_USAGE_MSG = "usage: a2path <path>";

std::vector<std::string> path_vars;
std::vector<int> child_processes;

struct sigaction sa;

std::string trim(const std::string &s)
{
    std::string::const_iterator it = s.begin();
    while (it != s.end() && isspace(*it))
        it++;

    std::string::const_reverse_iterator rit = s.rbegin();
    while (rit.base() != it && isspace(*rit))
        rit++;

    return std::string(it, rit.base());
}

/**
 * @brief Tokenize a string 
 * 
 * @param str - The string to tokenize
 * @param delim - The string containing delimiter character(s)
 * @return std::vector<std::string> - The list of tokenized strings. Can be empty
 */
std::vector<std::string> tokenize(const std::string &str, const char *delim) {
	char* cstr = new char[str.size() + 1];
	std::strcpy(cstr, str.c_str());

	char* tokenized_string = strtok(cstr, delim);

	std::vector<std::string> tokens;
	while (tokenized_string != NULL)
	{
        std::string current_token = std::string(tokenized_string);
		tokens.push_back(current_token);
		tokenized_string = strtok(NULL, delim);
	}
	delete[] cstr;

	return tokens;
}

char *strConvert(std::string str) {
    size_t len = str.length();
    char *buf = new char[len + 1];
    memcpy(buf, str.data(), len);
    buf[len] = '\0';
    return buf;
}

char **strVecConvert(std::vector<std::string> strings) {
    std::vector<char*> cstrings;   
    cstrings.reserve(strings.size() - 1);

    for (unsigned int i = 0; i < strings.size(); i++) {
        cstrings.push_back(&strings[i][0]);
    }
    cstrings.push_back(NULL);

    return cstrings.data();
}

std::vector<std::vector<std::string> > split_tokens_on_token(std::vector<std::string> tokens, std::string token) {
    std::vector<std::string> left_args = std::vector<std::string>();
    std::vector<std::string> right_args = std::vector<std::string>();

    int i = 0;
    while (tokens[i] != token) {
        left_args.push_back(tokens[i]);
        i++;
    }
    i++;
    for (unsigned int j = i; j < tokens.size(); j++) {
        right_args.push_back(tokens[j]);
    }

    std::vector<std::vector<std::string> > res;
    res.push_back(left_args);
    res.push_back(right_args);
    return res;
}

/**
 * using the path variables to see if there's a file with the partial path
 * returns null if there is no file in any path
 */
std::string get_path(std::string partial_path) {
    for (unsigned int i = 0; i < path_vars.size(); i++) {
        std::string path_var = path_vars[i];
        std::string tmp_path = path_var + partial_path;

        if (path_var.length() > 1 && path_var.at(path_var.length()-1) != '/') {
            tmp_path = path_var + "/" + partial_path;
        }
        
        if (access(strConvert(tmp_path), F_OK) == 0) {
            return tmp_path;
        }
    }
    return "";
}

int errorCommand(std::vector<std::string> tokens, std::string error_msg) {
    std::cout << error_msg << std::endl;
    return -1;
}

int cd(std::vector<std::string> tokens) {
    if (tokens.size() != 2) {
        std::cout << CD_USAGE_MSG << std::endl;
    } else {
        int status = chdir(tokens[1].c_str());
        if (status != 0) {
            perror("dragonshell");
        }
    }
    return 0;
}

std::string get_pwd_string() {
    char the_path[2048];

    getcwd(the_path, 2048-1);
    strcat(the_path, "/");

    return std::string(the_path);
}

int pwd(std::vector<std::string> tokens) {
    if (tokens.size() != 1) {
        std::cout << PWD_USAGE_MSG << std::endl;
    } else {
        std::cout << get_pwd_string() << std::endl;
    }
    return 0;
}

int a2path(std::vector<std::string> tokens) {
    using namespace std;
    if (tokens.size() != 2) {
        cout << "dragonshell: " << A2PATH_USAGE_MSG << endl;
        return -1;
    } else {
        string path_string = tokens[1];
        vector<string> path_tokens = tokenize(path_string, ":");
        if (path_tokens.size() < 2) {
            cout << "dragonshell: no paths provided" << endl;
        } else if (trim(path_tokens[0]) != "$PATH") {
            cout << "dragonshell: " << path_tokens[0] << " is not the path" << endl;
        } else {
            for (unsigned int i = 1; i < path_tokens.size(); i++) {
                path_vars.push_back(path_tokens[i]);
            }
        }
        return 0;
    }
}

int print_path() {
    // start at 1 because first element of path_vars is empty string
    for (unsigned int i = 1; i < path_vars.size()-1; i++) {
        std::cout << path_vars[i] << ":";
    }
    std::cout << path_vars[path_vars.size()-1] << std::endl;
    return 0;
}

int run(std::vector<std::string> tokens, int fd[2], bool change_write, bool change_read, bool should_wait) {
    // step 1: find which program to run using path/pwd
    std::string program_path = get_path(tokens[0]);
    if (program_path == "") {
        perror("dragonshell");
    }

    // step 2: run it
    int pid = fork();
    if (pid < 0) {
        return -1;
    } else if (pid == 0) {
        if (change_write) {
            close(fd[0]);
            dup2(fd[1], STDOUT_FILENO);
        }
        if (change_read) {
            close(fd[1]);
            dup2(fd[0], STDIN_FILENO);
        }
        extern char **environ;
        execve(program_path.c_str(), strVecConvert(tokens), environ);
    } else {
        if (change_write) 
            close(fd[1]);
        if (change_read)
            close(fd[0]);
        if (should_wait) {
            int status = 0;
            while(wait(&status) != pid) {}
        } else {
            child_processes.push_back(pid);
            std::cout << "PID " << pid << " is running in the background" << std::endl;
        }
    }
    return 0;
}

int run_background_task(std::vector<std::string> tokens, int fd[2]) {
    return run(tokens, fd, true, false, false);
}

int run_left_pipe(std::vector<std::string> tokens, int fd[2]) {
    return run(tokens, fd, true, false, true);
}

int run_right_pipe(std::vector<std::string> tokens, int fd[2]) {
    return run(tokens, fd, false, true, true);
}

int run_redirect_to_file(std::vector<std::string> tokens, int fd[2], std::string filename) {
    std::string program_path = get_path(tokens[0]);
    if (program_path == "") {
        perror("dragonshell: program not found");
    }

    int pid = fork();
    if (pid < 0) {
        return -1;
    } else if (pid == 0) {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        return execv(program_path.c_str(), strVecConvert(tokens));
    } else {
        close(fd[1]);
        dup(fd[0]);

        int BUFFER_SIZE = 0x10000;
        char buffer[BUFFER_SIZE];

        int file = open((get_pwd_string() + filename).c_str(), O_CREAT | O_WRONLY, 0644);
        if (file < 0) {
            perror("dragonshell");
        }

        while (read(fd[1], buffer, BUFFER_SIZE) != 0) {
            for (int i = 0; i < BUFFER_SIZE; i++) {
                if (*(buffer+i) == 0) break;
                write(file, buffer+i, sizeof(char));
            }
        } 
    }
    return 0;
}

int ds_exit() {
    std::cout << std::endl << "DragonShell: Exiting..." << std::endl;
    for (unsigned int i = 0; i < child_processes.size(); i++) {
        kill(child_processes[i], SIGKILL);
    }
    _exit(0);
}

bool is_background_task(std::vector<std::string> tokens) {
    if (tokens.size() > 0 && tokens[tokens.size()-1] == "&") {
        return true;
    }
    return false;
}

bool is_routable(std::vector<std::string> tokens) {
    for (unsigned int i = 0; i < tokens.size(); i++) {
        if (tokens[i] == ">")
            return true;
    }
    return false;
}

bool is_pipeable(std::vector<std::string> tokens) {
    for (unsigned int i = 0; i < tokens.size(); i++) {
        if (tokens[i] == "|")
            return true;
    }
    return false;
}

int route_with_piping(std::vector<std::string> pipeable) {
    std::vector<std::string> left, right;
    std::vector<std::vector<std::string> > left_and_right = split_tokens_on_token(pipeable, "|");
    left = left_and_right[0];
    right = left_and_right[1];

    // file descriptor, we will use this to read output from first process
    int fd[2];
    pipe(fd);

    if (run_left_pipe(left, fd) < 0 || run_right_pipe(right, fd) < 0) {
        perror("something went wrong running the command");
    }

    return 0;
}

int route_with_redirect(std::vector<std::string> redirectable) {
    std::vector<std::string> left, right;
    std::vector<std::vector<std::string> > left_and_right = split_tokens_on_token(redirectable, ">");

    left = left_and_right[0];
    right = left_and_right[1];

    int fd[2];
    pipe(fd);

    return run_redirect_to_file(left, fd, right[0]);
}

int route_background_task(std::vector<std::string> background_task) {
    background_task.pop_back();

    int fd[2];
    int dev_null = open("/dev/null", O_WRONLY, 0644);

    if (dev_null < 0)
        perror("can't open devnull");
    fd[1] = dev_null;

    return run_background_task(background_task, fd);
}

int route(std::vector<std::string> tokens)  {
    if (tokens.size() < 1)
        std::cout << "Error, no command provided" << std::endl;
	std::string command_name = tokens[0];

    // main routing
    if (tokens.size() == 1 && trim(tokens[0]) == "exit") {
        ds_exit();
        return 0;
    } else if (is_background_task(tokens)) {
        return route_background_task(tokens);
    } else if (is_routable(tokens)) {
        return route_with_redirect(tokens);
    } else if (is_pipeable(tokens)) {
        return route_with_piping(tokens);
    } else if (command_name == "cd") {
        return cd(tokens);
    } else if (command_name == "$PATH") {
        return print_path();
    } else if (command_name == "pwd") {
        return pwd(tokens);
    } else if (command_name == "a2path") {
        return a2path(tokens);
    } else {
        return run(tokens, NULL, false, false, true);
    }
    return 0;
}

void start() {
    while (true) {
		std::cout << "DragonShell > ";
		std::string input;
		if (!std::getline(std::cin, input))
            ds_exit();

        std::vector<std::string> commands = tokenize(input, ";");
        for (std::vector<std::string>::iterator command = commands.begin(); command != commands.end(); command++) {
            trim(*command);
            std::vector<std::string> tokens = tokenize(*command, " ");
            if (route(tokens) < 0) {
                // something went wrong, just continue
            }
        }
	}
}

void signal_callback_handler(int signum) {
    // we don't gotta do nothing (basically SIG_IGN)
}

void init_signals() {
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}

void init_path() {
    path_vars.push_back("");
    path_vars.push_back("/bin/");
    path_vars.push_back("/usr/bin/");
}

int main(int argc, char **argv) {
    std::cout << " ~ DragonShell ~" << std::endl;

    // push starting path variables
    init_path();
    init_signals();
    start();
	return 0;
}