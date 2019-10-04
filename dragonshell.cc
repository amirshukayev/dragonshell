#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <locale>
#include <fcntl.h>

const std::string CD_USAGE_MSG = "DragonShell: expected argument to \"cd\".";
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

    for (int i = 0; i < strings.size(); i++) {
        cstrings.push_back(&strings[i][0]);
    }
    cstrings.push_back(nullptr);

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
    for (int j = i; j < tokens.size(); j++) {
        right_args.push_back(tokens[j]);
    }

    std::vector<std::vector<std::string> > res;
    res.push_back(left_args);
    res.push_back(right_args);
    return res;
}

void pprint_vec(std::vector<std::string> strings) {
    using namespace std;

    cout << "{ ";
    for (int i = 0; i < strings.size(); i++) {
        std::string _str = strings[i];
        cout << _str << ", ";
    }
    cout << " }" << endl;
}

/**
 * using the path variables to see if there is
 * is a file with the partial path
 * returns null if there is no path and also prints the error
 */
std::string get_path(std::string partial_path) {
    for (int i = 0; i < path_vars.size(); i++) {
        std::string path_var = path_vars[i];
        std::string tmp_path = path_var + partial_path;
        if (access(strConvert(tmp_path), F_OK) == 0) {
            return tmp_path;
        }
    }
    return "";
}

/**
 * if the first token of the command is not recognized
 */
int errorCommand(std::vector<std::string> tokens, std::string error_msg) {
    std::cout << error_msg << std::endl;
    return -1;
}

int cd(std::vector<std::string> tokens) {
    if (tokens.size() != 2) {
        return errorCommand(tokens, CD_USAGE_MSG);
    } else {
        int status = chdir(tokens[1].c_str());
        if (status != 0) {
            std::cout << "Could not change directories, path does not exist or no permission" << std::endl;
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
        return errorCommand(tokens, PWD_USAGE_MSG);
    } else {
        std::cout << get_pwd_string() << std::endl;
        return 0;
    }
}

int a2path(std::vector<std::string> tokens) {
    if (tokens.size() != 2) {
        return errorCommand(tokens, A2PATH_USAGE_MSG);
    } else {
        path_vars.push_back(tokens[1]);
        return 0;
    }
}

int run(std::vector<std::string> tokens, int fd[2], bool change_write, bool change_read, bool should_wait) {
    // step 1: find which program to run using path/pwd
    std::string program_path = get_path(tokens[0]);
    if (program_path == "") {
        return errorCommand(tokens, "File does not exist");
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
        int status = execve(program_path.c_str(), strVecConvert(tokens), environ);
    } else {
        if (change_write) 
            close(fd[1]);
        if (change_read)
            close(fd[0]);

        if (should_wait) {
            int status = 0;
            while(wait(&status) != pid) {}
        }
    }
    return 0;
}

int run_redirect_to_file(std::vector<std::string> tokens, int fd[2], std::string filename) {
    std::string program_path = get_path(tokens[0]);
    if (program_path == "") {
        return errorCommand(tokens, 
            "Either no access or file doesn't exist (fix this Amir, say which one)");
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
        int *status = (int *) malloc(sizeof(int));

        int BUFFER_SIZE = 0x10000;
        char buffer[BUFFER_SIZE];

        int file = open((get_pwd_string() + filename).c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (file < 0) {
            std::cout << "open on " << get_pwd_string() + filename << " didn't work, returned: " << strerror(errno) << std::endl;
        }

        while (read(fd[1], buffer, BUFFER_SIZE) != 0) {
            for (int i = 0; i < BUFFER_SIZE; i++) {
                if (*(buffer+i) == 0) break;
                write(file, buffer+i, sizeof(char));
            }
        } 
        
        while (1) {
            int terminated_pid = wait(status);
            if (terminated_pid == pid)
                break;
        }
        free(status);
        return 0;
    }
    return 0;
}

int ds_exit() {
    std::cout << std::endl << "DragonShell: Exiting..." << std::endl;
    for (int i = 0; i < child_processes.size(); i++) {
        kill(child_processes[i], SIGKILL);
    }
    exit(0);
}

bool is_background_task(std::vector<std::string> tokens) {
    if (tokens.size() > 0 && tokens[tokens.size()-1] == "&") {
        return true;
    }
    return false;
}

bool is_routable(std::vector<std::string> tokens) {
    for (int i = 0; i < tokens.size(); i++) {
        if (tokens[i] == ">")
            return true;
    }
    return false;
}

bool is_pipeable(std::vector<std::string> tokens) {
    for (int i = 0; i < tokens.size(); i++) {
        if (tokens[i] == "|")
            return true;
    }
    return false;
}

int route(std::vector<std::string> tokens)  {
	std::string command_name = tokens[0];

    if (command_name == "cd") {
        return cd(tokens);
    } else if (command_name == "pwd") {
        return pwd(tokens);
    } else if (command_name == "a2path") {
        return a2path(tokens);
    } else if (tokens.size() < 1) {
        return errorCommand(tokens, "no command");
    } else {
        std::cout << "huh" << std::endl;
        return run(tokens, nullptr, false, false, true);
    }
    return 0;
}

int route_with_piping(std::vector<std::string> pipeable) {
    std::vector<std::string> left, right;
    std::vector<std::vector<std::string> > left_and_right = split_tokens_on_token(pipeable, "|");
    left = left_and_right[0];
    right = left_and_right[1];

    // file descriptor, we will use this to read output from first process
    int fd[2];
    pipe(fd);

    if (run(left, fd, true, false, true) < 0) {
        perror("something went wrong running the command");
    }
    if (run(right, fd, false, true, true) < 0) {
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
    int devnull = open("/dev/null", O_WRONLY, 0644);
    if (devnull < 0) {
        perror("can't open devnull");
    }
    fd[1] = devnull;

    return run(background_task, fd, true, false, false);
}

void start() {
    while (true) {
		std::string input;
		std::cout << "DragonShell > ";

		if (!std::getline(std::cin, input)) {
            ds_exit();
        }

        std::vector<std::string> commands = tokenize(input, ";");

        for (std::vector<std::string>::iterator command = commands.begin(); command != commands.end(); command++) {
            trim(*command);
            std::vector<std::string> tokens = tokenize(*command, " ");
            
            int status;
            if (is_background_task(tokens)) {
                status = route_background_task(tokens);
            } else if (is_routable(tokens)) {
                status = route_with_redirect(tokens);
            } else if (is_pipeable(tokens)) {
                status = route_with_piping(tokens);
            } else {
                status = route(tokens);
            } if (status < 0) {
                std::cout << "Status code: " << status << " something happened" << std::endl;
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
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTSTP, &sa, nullptr);
}

void init_path() {
    path_vars.push_back("/bin/");
    path_vars.push_back("/usr/bin/");
    path_vars.push_back("");
}

int main(int argc, char **argv) {

    std::cout << " ~ DragonShell ~" << std::endl;

    // push starting path variables
    init_path();
    init_signals();
    start();
	return 0;
}