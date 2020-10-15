#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define PROMPT (">")
#define MAX_WORD_LEN (100)
#define MAX_WORDS_NUM (100)
#define MAX_COMMANDS_NUM (100)

/* Every word in the commad can has a white-space attached to it... */
#define MAX_LINE_BUFF_SIZE (MAX_WORDS_NUM * (MAX_WORD_LEN + 1)) + 1

#define WHITE_SPACE (" ")
#define AMPERSAND_CHAR ("&")

#define CD_COMMAND ("cd")
#define CD_HOME_CHAR ("~")
#define CD_PREV_CHAR ("-")

#define EXIT_COMMAND ("exit")
#define JOBS_COMMAND ("jobs")
#define ECHO_COMMAND ("echo")
#define HISTORY_COMMAND ("history")

#define COMMANDS_DIR ("/bin/")

/* Preventing `unused argument`. */
#define UNUSED(...) (void)(__VA_ARGS__)

/* Print error log to stderr.*/
#define LOG_ERROR(fmt, args...) do {       \
        fprintf(stderr, fmt "\n", ##args); \
    } while (0)                            \

/* If the cond isn't true, and write the given message to stderr. */
#define CHECK(cond, fmt, args...)do {                \
    if (!(cond))                                     \
    {                                                \
        LOG_ERROR(fmt, ##args);                      \
    }} while (0)                                     \

/* If the cond isn't true, return -1 and write the given message to stderr. */
#define CHECK_RET(cond, fmt, args...) do {           \
    if (!(cond))                                     \
    {                                                \
        LOG_ERROR(fmt, ##args);                      \
        return -1;                                   \
    }} while (0)                                     \


/* Check the given value isn't NULL. */
#define NOT_NULL(value, message) CHECK_RET((value) != NULL, message)

/**
 * Command struct.
 */
struct Command
{
    pid_t pid;
    char command_line[MAX_LINE_BUFF_SIZE];
} typedef Command;

/**
 * Saving all of the records.
 *
 */
struct CommandsRecord
{
    size_t amount;
    Command commands[MAX_COMMANDS_NUM];
} typedef CommandsRecord;

/* The previous directory. */
char prev_dir[MAX_WORD_LEN] = {};

/**
 * Adding a new command.
 *
 * @param pid The pid of the executor of the command (the child process).
 * @param words The command split by white-spaces.
 * @return int 0 fir success, -1 otherwise.
 */
int add_command(pid_t pid, char **words, CommandsRecord *commands_record)
{
    CHECK_RET(commands_record->amount <= MAX_COMMANDS_NUM,
              "Passed max allowed commands.");

    commands_record->commands[commands_record->amount].pid = pid;

    char *current_command = commands_record->commands[commands_record->amount].command_line;
    memset(current_command, 0, MAX_LINE_BUFF_SIZE);

    for (size_t i = 0; i < MAX_WORDS_NUM && words[i] != NULL; i++)
    {
        /* Copy the current word. */
        CHECK_RET(strncat(current_command, words[i], MAX_WORD_LEN) == current_command,
                          "Failed to copy a word to the command's string.");

        /* Add white-space after the current word. */
        CHECK_RET(strncat(current_command, WHITE_SPACE, sizeof(char)) == current_command,
                          "Failed to copy a white-space to the command's string.");
    }

    /* Delete the last white-space. */
    size_t command_len = strnlen(current_command, MAX_LINE_BUFF_SIZE);
    current_command[command_len - 1] = 0;

    commands_record->amount++;

    return 0;
}

/**
 * Get a line via stdin
 * Note: The given string (line) can be resized and the size will be stored in size.
 * @param line The line from the user.
 * @param size In and Out param, the size of line
 *             and the size of the read line.
 * @return int 0 in success, -1 otherwise.
 */
int read_line(char *line, size_t *size)
{
    NOT_NULL(line, "The Given string is null.");

    /* Read the line from the user. */
    ssize_t line_size  = getline(&line, size, stdin);

    /* Getline function can fail in case of EOF char
       at the start of the line.*/
    if (line_size < 0)
    {
        *size = 0;
        return -1;
    }

    /* Erase the newline ('\n'). */
    if (line_size == 1)
    {
        *size = 0;
    }
    else
    {
        line[line_size - 1] = 0;
        *size = (size_t)line_size;
    }

    return 0;
}

/**
 * Removes specific char from a given string.
 *
 * @param str The string to change.
 * @param garbage The char to remove.
 */
void remove_char(char *str, char garbage) {
    char *src, *dst;

    for (src = dst = str; *src != '\0'; src++)
    {
        *dst = *src;

        if (*dst != garbage)
        {
            dst++;
        }
    }

    *dst = '\0';
}

/**
 * Splits a given string to words by a space delimiter.
 * Note: The function allocates the words (and not the words-array).
 *
 * @param line The given line to split.
 * @param line_size The size of the given line.
 * @param words Out param for the split line (allocated by the caller).
 * @param num_of_words Out parameter for the num of words.
 * @return int 0 for success, -1 otherwise.
 */
int split_line(char* line, size_t line_size, char *words[], size_t *num_of_words)
{
    NOT_NULL(line, "The given string is null.");

    /* Check if the line is empty. */
    if (strnlen(line, MAX_LINE_BUFF_SIZE) == 0)
    {
        *num_of_words = 0;
        return 0;
    }

    size_t words_counter = 0;

    /* Split the first word. */
    char *word = strtok(line, " ");

    /* Go over the given line. */
    while(word != NULL)
    {
        /* Check if the words is too long. */
        CHECK_RET(strnlen(word, MAX_WORD_LEN) <= MAX_WORD_LEN,
                  "Current word (%s) is too big", word);

        /* Allocate memory for the word.*/
        words[words_counter] = (char*)malloc(sizeof(char) * (MAX_WORD_LEN + 1));
        NOT_NULL(words[words_counter], "Failed to allocate memory for a word.");

        /* Copy the word. */
        strncpy(words[words_counter++], word, MAX_WORD_LEN);

        /* Split the next word. */
        word = strtok(NULL, " ");
    }

    *num_of_words = words_counter;
    return 0;
}

/**
 * Run a given function on a new process.
 *
 * @param child_func A function-pointer (gets a command and the commands record) to run on the child process.
 * @param words The command split to words.
 * @param parent_func A function-pointer (gets the child's pid, command and the commands record)
 *                    to run on the parent process.
 * @param commands_record The commands record.
 * @return int 0 for success, -1 for failure.
 */
int run_process(void (*child_func)(char**, CommandsRecord*), char** words,
                int (*parent_func)(pid_t, char**, CommandsRecord*), CommandsRecord* commands_record)
{
    /* Open a new process.*/
    pid_t pid = fork();
    CHECK_RET(pid >= 0, "Error in system call");

    /* This is the child process.*/
    if (pid == 0)
    {
        (*child_func)(words, commands_record);
    }

    /* This is the parent process. */
    else
    {
        CHECK_RET((*parent_func)(pid, words, commands_record) == 0,
                  "Failed to run the function on the parent's process");
    }

    return 0;
}

/**
 * An implement for function of the parent process.
 * This waits until the child dies...
 *
 * @param pid The process id of the child.
 * @param arg In this implementation this argument isn't used.
 * @return int 0 for success, false otherwise.
 */
int wait_parent(pid_t pid, char **words, CommandsRecord *commands_record)
{

    /* Wait until the child process die.
       This sys-call can fail if the child has already died (and it the wanted behavior). */
    int status = -1;
    waitpid(pid, &status, WUNTRACED);

    CHECK_RET(add_command(pid, words, commands_record) == 0, "Failed to add the command.");
    return 0;
}

/**
 * An implement for function of the parent process.
 * This doesn't wait for the child to die...
 *
 * @param pid The process id of the child.
 * @param arg In this implementation this argument isn't used.
 * @return int 0 for success, false otherwise.
 */
int background_parent(pid_t pid, char **words, CommandsRecord *commands_record)
{
    /* Tell the kernel to reap the child process from the parent process
       (to prevent a 'zombie-process').*/
    CHECK_RET(signal(SIGCHLD, SIG_IGN) != SIG_ERR,
              "Error in system call");

    CHECK_RET(add_command(pid, words, commands_record) == 0, "Failed to add the command.");

    return 0;
}

/**
 * An implementation for function of the child process.
 * This function executes a linux command.
 *
 * @param words An Array of the command, split by space.
 * @param commands_record Unused arg.
 */
void child_command_executor(char **words, CommandsRecord *commands_record)
{
    UNUSED(commands_record);

    printf("%d\n", getpid());

    /* Handles an echo command with '"'. */
    if (strncmp(words[0], ECHO_COMMAND, MAX_WORD_LEN) == 0)
    {
        for (size_t i = 0; i < MAX_WORDS_NUM && words[i] != NULL; i++)
        {
            remove_char(words[i], '"');
        }
    }

    /* Add the first word in the command the dir of the binaries (`/bin/`) */
    char temp[MAX_WORD_LEN + 1] = {};
    strncpy(temp, COMMANDS_DIR, sizeof(temp));
    strncat(temp, words[0], sizeof(temp) - 1);

    /* Copy the temp string to the first word in the words array.*/
    memset(words[0], 0, MAX_WORD_LEN + 1);
    strncpy(words[0], temp, MAX_WORD_LEN);

    /* Execute the command. */
    CHECK(execve(words[0], words, NULL), "Error in system call");

    /* In case execve failed, exit the child-process. */
    exit(EXIT_FAILURE);
}

/**
 * Execute the command `jobs`.
 * Prints which job is alive and its pid.
 *
 * @param words Unused arg.
 * @param commands_record The commands-record for reading the previous commands.
 * @return int 0 for success, -1 otherwise.
 */
void execute_jobs(char **words, CommandsRecord *commands_record)
{
    UNUSED(words);

    for (size_t i = 0; i < commands_record->amount; i++)
    {
        pid_t current_pid = commands_record->commands[i].pid;

        /* Get the pid's group, if the request fails the process is dead.*/
        if (getpgid(current_pid) >= 0)
        {
            printf("%d %s\n", current_pid, commands_record->commands[i].command_line);
        }
    }

    /* Exit the child-process. */
    exit(EXIT_SUCCESS);
}

/**
 * Execute the command `history`.
 *
 * @param words Unused arg.
 * @param commands_record The commands record (for reading the previous commands).
 */
void execute_history(char **words, CommandsRecord *commands_record)
{
    UNUSED(words);

    for (size_t i = 0; i < commands_record->amount; i++)
    {
        pid_t current_pid = commands_record->commands[i].pid;

        /* Get the pid's group, if the request fails the process is dead.*/
        int gpid = getpgid(current_pid);

        printf("%d %s %s\n", current_pid, commands_record->commands[i].command_line,
               gpid >= 0 ? "RUNNING" : "DONE");
    }

    /* Print the history command (the parent-process add the command to the commands-record). */
    printf("%d history RUNNING\n", getpid());

    /* Exit the child-process. */
    exit(EXIT_SUCCESS);
}

int execute_cd(char **words, size_t num_of_words, CommandsRecord *commands_record)
{
    /* Print the current pid to the shell. */
    printf("%d\n", getpid());

    /* Add the current command to the commands-record. */
    CHECK_RET(add_command(getpid(), words, commands_record) == 0,
              "Failed to add a command.");

    CHECK_RET(num_of_words <= 2, "Error: Too many arguments");

    /* Get the current working direcotry.
       The function itself allocate the buffer with malloc. */
    char *temp = NULL;
    temp = getcwd(temp, 0);
    NOT_NULL(temp, "Failed to get the current directory.");

    /* Cd to the home directory. */
    if (num_of_words == 1 || strncmp(words[1], CD_HOME_CHAR, sizeof(char)) == 0)
    {
        CHECK_RET(chdir(getenv("HOME")) == 0, "Error in system call");
    }

    /* Cd to the previous direcotry. */
    else if (strncmp(words[1], CD_PREV_CHAR, sizeof(char)) == 0)
    {
        /* If there isn't previous directory. */
        CHECK_RET(strnlen(prev_dir, sizeof(prev_dir)) > 0, "cd: OLDPWD not set");

        /* Cd to the previous direcotry. */
        CHECK_RET(chdir(prev_dir) == 0, "Error in system call");
    }

    else
    {
        /* Check if the requested direcotry exists. */
        struct stat sb;
        if (stat(words[1], &sb) == 0 && S_ISDIR(sb.st_mode))
        {
            CHECK_RET(chdir(words[1]) == 0, "Error in system call");
        }
        else
        {
            LOG_ERROR("Error: No such file or directory");
            free(temp);
            return 0;
        }
    }

    /* Copy the previous direcotry. */
    memset(prev_dir, 0, sizeof(prev_dir));
    strncpy(prev_dir, temp, sizeof(prev_dir));
    free(temp);

    return 0;
}

/**
 * Handle a command.
 * Note: This function can free words from the words-array.
 *
 * @param words An array with the command, split by space.
 * @param num_of_words The number of words in the given command.
 * @return int 0 for success, -1 otherwise.
 */
int handle_command(char **words, size_t num_of_words, CommandsRecord *commads_record)
{
    /* Check if the command is `cd`. */
    if (strncmp(words[0], CD_COMMAND, MAX_WORD_LEN) == 0)
    {
        /* Always exit with success (the error log was printed in the previous function). */
        execute_cd(words, num_of_words, commads_record);
        return 0;
    }

    /* Check if the command is `exit`. */
    if (strncmp(words[0], EXIT_COMMAND, MAX_WORD_LEN) == 0)
    {
        /* Print the current pid and exit. */
        printf("%d\n", getpid());
        exit(EXIT_SUCCESS);
    }

    /* Check if the command is `jobs`. */
    if (strncmp(words[0], JOBS_COMMAND, MAX_WORD_LEN) == 0)
    {
        CHECK_RET(run_process(execute_jobs, words, wait_parent, commads_record) == 0,
                  "Failed to execute the command `jobs`.");
        return 0;
    }

    /* Check if the command is `history`. */
    if (strncmp(words[0], HISTORY_COMMAND, MAX_WORD_LEN) == 0)
    {
        CHECK_RET((run_process(execute_history, words, wait_parent, commads_record)) == 0,
                  "Failed to execute the command `history`.");

        return 0;
    }

    /* Check if the command ends with `&`*/
    if (strncmp(words[num_of_words - 1], AMPERSAND_CHAR, 1) == 0)
    {
        /* Erase the '&' word. */
        free(words[num_of_words - 1]);
        words[num_of_words - 1] = NULL;

        CHECK_RET(run_process(child_command_executor, words, background_parent, commads_record) == 0,
                  "Failed to execute the current command.");
    }
    else
    {
        CHECK_RET(run_process(child_command_executor, words, wait_parent, commads_record) == 0,
                  "Failed to execute the current command.");
    }

    return 0;
}

/**
 * The shell main loop.
 */
void shell_loop()
{
    char line_buff[MAX_LINE_BUFF_SIZE] = {};
    size_t line_size = sizeof(line_buff);

    char *words[MAX_WORDS_NUM];
    size_t num_of_words = 0;

    CommandsRecord commands_record;

    do {
        printf(PROMPT);

        /* Read a line from the user. */
        CHECK(read_line(line_buff, &line_size) == 0, "Failed to get a line from the user");

        if (line_size > 0)
        {
            /* Split the line from the user to words. */
            CHECK(split_line(line_buff, line_size, words, &num_of_words) == 0,
                  "Failed to split the line to words.");

            /* Handle the command from the user. */
            CHECK(handle_command(words, num_of_words, &commands_record) == 0,
                  "Failed to handle the current command.");
        }

        /* Free the allocated words. */
        for (size_t i = 0; i < num_of_words; i++)
        {
            if (words[i] != NULL)
            {
                free(words[i]);
                words[i] = NULL;
            }
        }

        /* Reset the sizers and the buffer.*/
        line_size = sizeof(line_buff);
        num_of_words = 0;
        memset(line_buff, 0, sizeof(line_buff));

    } while (1 == 1);
}

int main()
{
    shell_loop();
    return 0;
}
