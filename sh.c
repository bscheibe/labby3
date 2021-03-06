/*
 * Sample Project 2
 *
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <pwd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "sh.h"
#include <glob.h>
#include <sys/time.h>
#include "watchmail.h"
#include <sys/stat.h>
#include <pthread.h>
#include <fcntl.h>
#include "watchuser.h"
#include <utmpx.h>

pid_t cpid;

/* function that starts the shell
 * argc: argument count
 * argv: the array of arguments given
 * envp: the array of ponters to environment variables
 */

 pthread_mutex_t lock;
 struct Node_User *watchuser = NULL;
 void *watchuser_thread(void *arg){
     struct utmpx *up;
     //setutxent();

     while(1){
         setutxent();
         while((up = getutxent())){
             if ( up->ut_type == USER_PROCESS )
             {
                 pthread_mutex_lock(&lock);
                 struct Node_User* user = userFind(watchuser, up->ut_user);
                 if(user != NULL){
                     if(user->loggedin == 0){
                         printf("\n%s has logged on %s from %s\n", up->ut_user, up->ut_line, up ->ut_host);
                         user->loggedin = 1;
                     }
                 }
                 pthread_mutex_unlock(&lock);

             }
         }
         sleep(1);
     }
 }

 int sh( int argc, char **argv, char **envp)
{
    //Setting up variables needed for the shell to function
    char * prompt = calloc(PROMPTMAX, sizeof(char));
    char * prefix = "";
    char *commandline = calloc(MAX_CANON, sizeof(char));
    char *command, *arg, *commandpath, *p, *pwd, *owd;
    char **args = calloc(MAXARGS, sizeof(char*));
    int uid, i, status, argsct, watchingusersnum = 0, go = 1;
    struct passwd *password_entry;
    struct Node_Mail *watchmail = NULL;
    char *homedir;

    struct pathelement *pathlist;
    int count;
    int background;
    char *token;
    struct prev_cmd* head = NULL;
    struct alias* alist = NULL;
    int space;
    int valid;
    int nocobler = 0;
    uid = getuid();
    pthread_t watchuser_threadid;
    password_entry = getpwuid(uid);         /* get passwd info */
    homedir = password_entry->pw_dir; /* Home directory to start
                                             out with*/

    // store the current working directory
    if ( (pwd = getcwd(NULL, PATH_MAX+1)) == NULL )
    {
        perror("getcwd");
        exit(2);
    }
    owd = calloc(strlen(pwd) + 1, sizeof(char));
    memcpy(owd, pwd, strlen(pwd));

    // Set up the initial prompt
    prompt[0] = ' ';
    prompt[1] = '[';
    prompt[2] = '\0';

    strcat(prompt, pwd);
    prompt[strlen(prompt)+3] = '\0';
    prompt[strlen(prompt)] = ']';
    prompt[strlen(prompt)] = '>';
    prompt[strlen(prompt)] = ' ';

    /* Put PATH into a linked list */
    pathlist = get_path();

    while ( go )
    {
        /* print your prompt */
        valid = 1;
        background = 0;
        waitpid(-1, &status, WNOHANG);
        printf("%s%s", prefix, prompt);

        // Read in command line
        if(fgets(commandline, MAX_CANON, stdin) == NULL) {
            commandline[0] = '\n';
            commandline[1] = '\0';
            valid = 0;
            printf("\n");
        }
        int space = 1;

        // Remove newline character from end of input
        if (strlen(commandline) > 1) {
            commandline[strlen(commandline) - 1] = '\0';
        }
        else {
            valid = 0;
        }

        // Check command for special cases
        if (commandline[strlen(commandline)-1] == '&') {
            commandline[strlen(commandline)-1] = '\0';
            background = 1;
        }// Check for & here!
        for(i = 0; i < strlen(commandline); i++) {
            if(commandline[i] != ' ') {
                space = 0;
            }
        }
        if (space) {
            commandline[strlen(commandline)-1] = '\n';
            valid = 0;
        }

        // Parse the command line to separate the arguments
        count = 1;
        args[0] = strtok(commandline, " ");
        while((arg=strtok(NULL, " ")) != NULL) {
            args[count] = arg;
            count++;
        }
        args[count] = NULL;
        argsct = count;

        // Check if command is an alias
        struct alias* curr = alist;
        int found = 0;
        int done = 0;
        if(argsct == 1) {
            while(curr != NULL && !done) {
                found = 0;
                if(strcmp(curr->name, args[0]) == 0) {
                    strcpy(commandline, curr->cmd);
                    found = 1;
                    count = 1;
                    args[0] = strtok(commandline, " ");
                    while((arg=strtok(NULL, " ")) != NULL) {
                        args[count] = arg;
                        count++;
                    }
                    args[count] = NULL;
                    argsct = count;
                    if(curr->used == 1) {
                        args[0] = "\n\0";
                        printf("Alias Loop\n");
                        done = 1;
                    }
                    curr->used = 1;
                }
                curr = curr->next;
                if(found) {
                    curr = alist;
                }
            }
        }

        // Reset (used) aspect of each alias struct
        curr = alist;
        while(curr!=NULL) {
            curr->used = 0;
            curr=curr->next;
        }

        // Check for each built in command
        command = args[0];
        if(strcmp(command, "exit") == 0) {
            // Exit the shell
            printf("Executing built-in exit\n");
            exit(0);
        }
        else if(strcmp(command, "unset") == 0) {
            if (!strcmp(args[1], "nocobler")) {
                nocobler = 0;
            }
        }
        else if(strcmp(command, "set") == 0) {
            if (!strcmp(args[1], "nocobler")) {
                nocobler = 1;
            }
        }
        else if(strcmp(command, "watchmail") == 0) {
            printf("Watching mail\n");

            if(argsct == 2) {
                struct stat buffer;
                int exist = stat(args[1],&buffer);
                if(exist == 0) {
                    pthread_t thread_id;

                    char* filepath = (char *)malloc(strlen(args[1]));
                    strcpy(filepath, args[1]);
                    pthread_create(&thread_id, NULL, watchmail_thread, (void *)filepath);
                    watchmail = mailAdd(watchmail, filepath, thread_id);
                } else {
                    printf("%s does not exist\n", args[1]);
                }

            } else if(argsct == 3) {
                if(strcmp(args[2], "off") == 0) {
                    watchmail = mailRemoveNode(watchmail,args[1]);
                } else {
                    printf("Incorrect input for third argument\n");
                }
            } else {
                printf("Incorrect amount of arguments\n");
            }
        }

        else if(strcmp(command, "watchuser") == 0) { // WatchUser
                if(argsct == 2) {
                        printf("Watching user %s\n", args[1]);
                        char* user = (char *)malloc(strlen(args[1]));
                        strcpy(user, args[1]);
                        pthread_mutex_lock(&lock);
                        watchuser = userAdd(watchuser, user);
                        pthread_mutex_unlock(&lock);
=
                        if(watchingusersnum == 0) {
                                pthread_create(&watchuser_threadid, NULL, watchuser_thread, NULL);
                                watchingusersnum = 1;
                        }
                }else{
                        printf("Wrong amount of arguments\n");
                }


        }
        else if(strcmp(command, "which") == 0) {
            // Finds first alias or file in path directory that
            // matches the command
            printf("Executing built-in which\n");
            if(argsct == 1) {
                fprintf(stderr, "which: Too few arguments.\n");
            }
            else {
                // Checks for wildcard arguments
                glob_t globber;
                int i;
                for(i = 1; i < argsct; i++) {
                    int globreturn = glob(args[i], 0, NULL, &globber);
                    if(globreturn == GLOB_NOSPACE) {
                        printf("glob: Runnning out of memory.\n");
                    }
                    else if(globreturn == GLOB_ABORTED) {
                        printf("glob: Read error.\n");
                    }
                    else {
                        if(globber.gl_pathv != NULL) {
                            which(globber.gl_pathv[0], pathlist, alist);
                        }
                        else {
                            which(args[i], pathlist, alist);
                        }
                    }
                }
            }
        }
        else {

            // If the command is not an alias or built in function, find it
            // in the path
            int found = 0;
            int status;
            char* toexec = malloc(MAX_CANON*sizeof(char));
            if(access(args[0], X_OK) == 0) {
                found = 1;
                strcpy(toexec, args[0]);
            }
            else {
                struct pathelement* curr = pathlist;
                char *tmp = malloc(MAX_CANON*sizeof(char));
                while(curr!=NULL & !found) {
                    snprintf(tmp,MAX_CANON,"%s/%s", curr->element, args[0]);
                    if(access(tmp, X_OK)==0) {
                        toexec = tmp;
                        found = 1;
                    }
                    curr=curr->next;
                }
            }
            // If the command if found in the path, execute it as a child process
            if(found) {
                int a = 0;
                printf("Executing %s\n", toexec);
                // Create a child process
                cpid = fork();
                struct itimerval timer;
                if(cpid == 0) {
                    int fid;
                    int found = 0;
                    while (args[a] != NULL) {
                        // Child process executes command
                        if (!strcmp(args[a], ">")) {
                            if (nocobler && !access(args[a+1], F_OK)) {
                                printf("%s: File exists.\n", args[a+1]);
                                a = -1;
                                break;
                            }
                            fid = open(args[a+1], O_WRONLY|O_CREAT|O_TRUNC);
                            close(1);
                            dup(fid);
                            found = 1;
                            break;
                        }// Redirect stdout to file.
                        if (!strcmp(args[a], ">&")) {
                            if (nocobler && !access(args[a+1], F_OK)) {
                                printf("%s: File exists.\n", args[a+1]);
                                a = -1;
                                break;
                            }
                            fid = open(args[a+1], O_WRONLY|O_CREAT|O_TRUNC);
                            close(1);
                            close(2);
                            dup(fid);
                            dup(fid);
                            found = 1;
                            break;
                        }// Redirect stdout and stderr to file.
                        if (!strcmp(args[a], ">>")) {
                            if (nocobler && access(args[a+1], F_OK)) {
                                printf("%s: File does not exist.\n", args[a+1]);
                                a = -1;
                                break;
                            }
                            fid = open(args[a+1], O_WRONLY|O_CREAT|O_APPEND);
                            close(1);
                            dup(fid);
                            found = 1;
                            break;
                        }// Redirect stdout to file, appending instead.
                        if (!strcmp(args[a], ">>&")) {
                            if (nocobler && access(args[a+1], F_OK)) {
                                printf("%s: File does not exist.\n", args[a+1]);
                                a = -1;
                                break;
                            }
                            fid = open(args[a+1], O_WRONLY|O_CREAT|O_APPEND);
                            close(1);
                            close(2);
                            dup(fid);
                            dup(fid);
                            found = 1;
                            break;
                        }// Redirect stdout and stder to file and append.
                        if (!strcmp(args[a], "<")) {
                            fid = open(args[a+1], O_RDONLY, O_CREAT);
                            close(0);
                            dup(fid);
                            found = 1;
                            break;
                        }// Redirect stdin of process to file.
                        a++;
                    }
                    if (found) {
                        args[a] = NULL;
                        args[a+1] = NULL;
                    }// Clean up the args that were used for file redirection.
                    if (a != -1) {
                        execve(toexec, args, envp);
                    } else {
                        exit(0);
                    }// Check if our nocobler caused an issue. Exit if we aren't calling exec.
                }

                else if(cpid == -1) {
                    perror(NULL);
                }
                else {
                    // Parent process (shell) places continues onward.
                    if (background) {
                        continue;
                    }
                    // Parent process (shell) times child process
                    if(argc > 1) {
                        timer.it_value.tv_sec = atoi(argv[1]);
                        timer.it_interval.tv_sec = 0;
                        if(setitimer(ITIMER_REAL, &timer, NULL)==-1) {
                            perror(NULL);
                        }
                    }

                    // Parent process (shell) waits for child process to terminate
                    waitpid(pid, &status, 0);

                    // Disable timer after child process terminates
                    if(argc > 1) {
                        timer.it_value.tv_sec = 0;
                        if(setitimer(ITIMER_REAL, &timer, NULL)==-1) {
                            perror(NULL);
                        }
                    }

                    // Return non-normal exit status from child
                    if(WIFEXITED(status)) {
                        if(WEXITSTATUS(status) != 0) {
                            printf("child exited with status: %d\n", WEXITSTATUS(status));
                        }
                    }
                }
            }

            // Give error if command not found
            else if (valid) {
                fprintf(stderr, "%s: Command not found.\n", args[0]);
            }
        }
    }
    return 0;
} /* sh() */

/* function call for 'which' command
 * command: the command that you're checking
 * pathlist: the path list data structure
 * alist: the alias data structure
 */
char *which(char *command, struct pathelement *pathlist, struct alias *alist )
{
    // Set up path linked list variables
    struct pathelement *curr = pathlist;
    char *path = malloc(MAX_CANON*(sizeof(char)));
    int found = 0;


    // Search aliases for command
    struct alias* curra = alist;
    while(curra != NULL) {
        if(strcmp(curra->name, command)==0) {
            printf("%s:\t aliased to %s\n", curra->name, curra->cmd);
            found = 1;
        }
        curra=curra->next;
    }

    // Search path for command
    while(curr != NULL && !found) {
        strcpy(path, curr->element);
        path[strlen(path)+1] = '\0';
        path[strlen(path)] = '/';
        strcat(path, command);
        if(access(path, F_OK) == 0) {
            printf("%s\n", path);
            found = 1;
        }
        curr = curr->next;
    }

    // Print error if command not found
    if (!found) {
        fprintf(stderr, "%s: command not found\n", command);
        return NULL;
    }

} /* which() */

void *watchmail_thread(void *arg) {

    char* filepath = (char*)arg;
    struct stat stat_path;

    stat(filepath, &stat_path);
    long old_size = (long)stat_path.st_size;

    time_t curtime;
    while(1) {
        time(&curtime);

        stat(filepath, &stat_path);
        if((long)stat_path.st_size != old_size) {
            printf("\a\nBEEP You've Got Mail in %s at time %s\n", filepath, ctime(&curtime));
            fflush(stdout);
            old_size = (long)stat_path.st_size;
        }
        sleep(1);

    }


}
