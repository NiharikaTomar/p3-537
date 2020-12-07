#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

// global vars
#define BUFFER_SIZE 540
#define MAX 10000  // max jobs at any point in time
char line[BUFFER_SIZE];  // get command line
char* num_argv[512];  // user command in an array
char prompt[] = "mysh> ";  // command line prompt
int myexit = 0;  // exit counter
int tmpout;  // file desc for dup
int tmperr;  // file desc for dup
int curr_out;
int curr_err;
int redirectVal = 0;  // redirection  counter
char* bufferTokens[512];

// JOB STRUCT
int job_id = 0;  // next job ID to allocate
struct job_t {  // The job struct
  pid_t pid;  // job PID
  int jid;  // job ID [0, 1, 2, ...]
  int status;  // checks if job is fg=0 or bg=1
  char line[BUFFER_SIZE];  // command line
} jobs[MAX];  // The job list

// func dec
void alljobs(struct job_t *jobs, int argc);
void myWait(struct job_t *jobs, int argc);

// driver
int main(int argc, char *argv[]) {
  // Check if number of command-line arguments is correct.
  if (argc > 2 || argc < 1) {
    fprintf(stderr, "Usage: mysh [batchFile]\n");
    fflush(stderr);
    exit(1);
  }
  // Open the file and check if it opened successfully
  FILE *fp = NULL;  // file pointer
  if (argc == 2) {
    fp = fopen(argv[1], "r");
    if (fp == NULL) {
      fprintf(stderr, "Error: Cannot open file %s\n", argv[1]);
      fflush(stderr);
      exit(1);
    }
  } else {
    fp = stdin;
  }
  int numArgs;  // arg count
  // if in interactive mode
  if (argc == 1) {
    fprintf(stdout, "%s", prompt);  // print shell prompt
    fflush(stdout);
  }
  // loop to take in user input
  while (fgets(line, BUFFER_SIZE, fp)) {
    // replace new line with end of string
    int length = strlen(line);
    if (line[length -1] == '\n') {
      line[length-1] = '\0';
    }

    // if in batch mode
    if (argc == 2) {
      write(STDOUT_FILENO, line, strlen(line));
      write(STDOUT_FILENO, "\n", 1);
    }

    // redirection
    if (strchr(line, '>') != NULL) {
      char rightOutput[512];  // array for right side of >
      // Splitting input string into tokens to remove whitespaces
      char *token2;
      token2 = strtok(line, ">");
      token2 = strtok(NULL, ">");
      // exit var for redirection
      int exitKey = 0;
      int f = 0;  // indicates fail
      if (token2 != NULL) {
        strcpy(rightOutput, token2);
      } else {
        int len = strlen(line);
        line[len-1] = '>';
        exitKey = 1;
      }
      if (exitKey == 0) {
        char *buffer;
        buffer = strtok(rightOutput, " ");
        while (buffer != NULL) {
          bufferTokens[f] = buffer;
          buffer = strtok(NULL, " ");
          f++;
        }
        if (f != 1) {
          int len = strlen(line);
          line[len-1]= '>';
        }
      }
      if (f == 1) {
        // new contents
        tmpout = open(bufferTokens[0], O_RDWR|O_CREAT|O_TRUNC, 0666);
        tmperr = open(bufferTokens[0], O_RDWR|O_CREAT|O_TRUNC, 0666);

        // old contents
        curr_out = dup(fileno(stdout));
        curr_err = dup(fileno(stderr));

        dup2(tmpout, fileno(stdout));
        dup2(tmperr, fileno(stderr));
        redirectVal = 1;
      }
    }
    // if user inputs > 512 chars, ignore line
    if (strlen(line) > 512) {
      continue;
    }
    // checks for empty line
    if (strlen(line) == 0 || line[0] == '\n' || line[0] == '\0') {
      if (argc == 1) {
        fprintf(stdout, "%s", prompt);  // print shell prompt
        fflush(stdout);
      }
      continue;
    }
    // Splitting input string into tokens to remove whitespaces
    char *token;
    token = strtok(line, " \n\t");
    int i = 0;
    while (token != NULL) {
      num_argv[i] = token;
      token = strtok(NULL, " ");
      i++;
    }
    num_argv[i] = NULL;  // setting last value to NULL for execvp
    numArgs = i;  // getting arg count
    if (numArgs == 0) {  // if no args
      continue;
    }
    // checking if stdin is bg job
    if (strcmp(num_argv[numArgs-1], "&") == 0) {
      if (numArgs != 1) {
        jobs[job_id].status = 1;
        num_argv[numArgs - 1] = NULL;
        numArgs--;
      } else {
        continue;
      }
    }
    // check for builtin commands
    if (strcmp(num_argv[0], "exit") == 0 && numArgs == 1) {
      myexit = 1;
      break;
    } else if (strcmp(line, "jobs") == 0) {
      alljobs(jobs, argc);
      continue;
    } else if (strcmp(num_argv[0], "wait") == 0) {
      myWait(jobs, argc);
      continue;
    }
    // updating changes to job struct
    jobs[job_id].jid = job_id;
    int index = 0;
    for (int row = 0; row < numArgs; row++) {
      for (int col = 0; col < strlen(num_argv[row]); col++) {
        jobs[job_id].line[index]= num_argv[row][col];
        index++;
      }
      if (row < numArgs - 1) {  // remove space after end
        jobs[job_id].line[index] = ' ';
        index++;
      }
    }
    pid_t pid;  // holds process ID
    pid = fork();  // fork child
    jobs[job_id].pid = pid;
    int fork_status;

    if (pid == 0) {  // Child
      execvp(num_argv[0], num_argv);  // execute the command
      fprintf(stderr, "%s: Command not found\n", num_argv[0]);
      fflush(stderr);
      exit(1);
    } else if (pid > 0) {  // Parent
      if (jobs[job_id].status != 1) {  // if the job is not bg
        waitpid(pid, &fork_status, 0);
      }
    } else {
      perror("Fork failed");
      exit(1);
    }
    job_id++;  // increment #jobs
    if (argc == 1) {
      fprintf(stdout, "%s", prompt);  // print shell prompt
      fflush(stdout);
    }
    if (redirectVal == 1) {
      // flushing and closing new file desc vals
      fflush(stdout);
      close(tmpout);
      fflush(stderr);
      close(tmperr);
      dup2(curr_out, fileno(stdout));
      dup2(curr_err, fileno(stderr));
      // closing old i.e current file desc vals
      close(curr_out);
      close(curr_err);
      redirectVal = 0;  // resetting counter
    }
  }

  if (argc == 2) {
    myexit = 1;
  }
  if (myexit == 0 && argc != 2) {
    write(STDOUT_FILENO, "\n", 1);
  }

  // Closing file
  if (fclose(fp) != 0) {
    // printing error message
    fprintf(stderr, "Error while closing the file\n");
    fflush(stderr);
    exit(1);
  }
  return 0;
}

// func to list all current jobs running
void alljobs(struct job_t *jobs, int argc) {
  int i;
  int fork_status;
  for (i = 0; i < job_id; i++) {
    if (jobs[i].status == 1) {
      if (waitpid(jobs[i].pid, &fork_status, WNOHANG) == 0) {
        fprintf(stdout, "%d : %s\n", jobs[i].jid, jobs[i].line);
        fflush(stdout);
      }
    }
  }
  if (argc == 1) {
    fprintf(stdout, "%s", prompt);  // print shell prompt
    fflush(stdout);
  }
}

// func to calculate the wait time with pid
void myWait(struct job_t *jobs, int argc) {
  int fork_status;
  int isdigit = 0;
  int arg = atoi(num_argv[1]);
  if (jobs[arg].status == 1) {
    for (int i=0; i < strlen(num_argv[1]); i++) {
      if (!isdigit(num_argv[1][i])) {
        isdigit = 1;
      }
    }
    if (isdigit == 0) {
      waitpid(jobs[arg].pid, &fork_status, 0);
      fprintf(stdout, "JID %d terminated\n", arg);
      fflush(stdout);
      if (argc == 1) {
        fprintf(stdout, "%s", prompt);  // print shell prompt
        fflush(stdout);
      }
    } else {
      fprintf(stderr, "Invalid JID %d\n", arg);
      fflush(stderr);
    }
  } else {
    fprintf(stderr, "Invalid JID %d\n", arg);
    fflush(stderr);
  }
}
