#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

#define CMDLINE_MAX 512
#define ARG_MAX 16
#define PIPE_MAX 16
#define CDCHECK 1
#define PWD 2
#define REDIRECT 3
#define BACKGROUND 4
#define SKIP 10
int bg_in_process = 0;
pid_t *bg_process_pids;
char bg_cmd[CMDLINE_MAX];
int bg_count =0;

//struct definition
struct cmd_string {
        char *argv[ARG_MAX + 1];
        int argc;
        char raw[CMDLINE_MAX];
};

//parse command
void parse_command(char *input, struct cmd_string *cmd, int type) { //pass in pointers to the struct and input
        strcpy(cmd->raw, input); //store original command before tokenizing
        char *x;
        x = (char*)malloc(strlen(input)+1);
        strcpy(x,input);
        //printf("x is: %s\n",x);
        int k = 0; //start at beginning of command
        int stop = 0;
        char *arg = strtok(x, " "); //break up by whitespace
        while (arg != NULL) { //until eof
            cmd->argv[k++] = arg; //continue to move down the line
            //printf("arg: %s\n",arg);
            arg = strtok(NULL, " ");
            if(type == CDCHECK){//for CD
                stop++;
                if(stop == 2){
                    cmd->argv[stop] = NULL;
                }
            }
            if(type == PWD){//for pwd
                stop++;
                if(stop ==1){
                    cmd->argv[stop] = NULL;
                }
            }
            if(type == REDIRECT){
                if(strchr(arg,'>') != NULL){
                        arg = strtok(arg,">");
                        cmd->argv[k++]=arg;
                        break;
                }
                if(strchr(arg,'<')!=NULL){
                        arg = strtok(arg,"<");
                        cmd->argv[k++]=arg;
                        break;
                }
            }
            if(k == ARG_MAX+1){
                fprintf(stderr,"Error: too many process arguments\n");
                exit(SKIP);
            }
        }
        cmd->argc=k;
        cmd->argv[k] = NULL;
}
//redirection
void redirect(struct cmd_string *cmd){
        if(strchr(cmd->raw, '>')!=NULL){
                char* output_filename= strtok(cmd->raw, ">");
                output_filename = strtok(NULL, ">");
                output_filename = strtok(output_filename," ");
                if(output_filename == NULL){
                        fprintf(stderr,"Error: no output file\n");
                        exit(SKIP);
                }
                int fd = open(output_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
                dup2(fd,STDOUT_FILENO);
                dup2(fd,STDERR_FILENO);
                close(fd);
                execvp(cmd->argv[0], cmd->argv);
                fprintf(stderr,"Error: command not found\n");
                exit(SKIP);
        }
        else{
                char* output_filename = strtok(cmd->raw, "<");
                output_filename = strtok(NULL, "<");
                output_filename = strtok(output_filename," ");
                if(output_filename == NULL){
                        fprintf(stderr,"Error: no input file\n");
                        exit(SKIP);
                }
                int fd = open(output_filename, O_RDONLY, 0644);
                if(fd == -1){
                        fprintf(stderr,"Error: cannot open input file\n");
                        exit(SKIP);
                }
                dup2(fd,STDIN_FILENO);
                close(fd);
                execvp(cmd->argv[0], cmd->argv);
                fprintf(stderr,"Error: command not found\n");
                exit(SKIP);
        }
}

struct cmd_string* split_cmd(char *cmd, int *count){
    struct cmd_string* commands = (struct cmd_string*)malloc(sizeof(struct cmd_string));
    char x[CMDLINE_MAX];
    char* parts[16];
    char n[CMDLINE_MAX];
    strncpy(x,cmd,CMDLINE_MAX);
    char *part = strtok(x,"|");
    int k = 0;
    while(part != NULL){
        parts[k] = (char*)malloc(strlen(part)+1);
        strcpy(parts[k],part);
        part = strtok(NULL,"|");
        if(part != NULL){
            int i = 0;
            while(part[i] == ' ') i++;
            int j = 0;
            while(part[i] != '\0' && j < CMDLINE_MAX - 1){
                    n[j] = part[i];
                    j++;
                    i++;
            }
            n[j] = '\0';
            part = n;
        }
        k++;
    }
    *count = k;
    for(int i = 0; i < *count; i++){
        commands = (struct cmd_string*)realloc(commands,(i+1)*sizeof(struct cmd_string));
        parse_command(parts[i],&commands[i],0);
    }
    return commands;
}
//MULTIPIPE FUNCTION
int* multipipe(char *command,int* count){
        //you know implicitly that commands are piped
        struct cmd_string* commands = split_cmd(command,count);
        int *status = (int*)malloc(*count*sizeof(int));
        int *pids = (int*)malloc(*count*sizeof(int));
        int **pipes = (int**)malloc((*count-1)*sizeof(int*));
        //create pipes
        for(int i = 0; i < *count-1; i++){
                pipes[i] = (int*)malloc(2*sizeof(int));
                pipe(pipes[i]);
        }
        //run cmds
        int i = 0;
        while(i != *count){
            if(i==0){
                //START only change output on first command
                if(!(pids[i]=fork())){
                        // Close all unneeded FDs
                        for (int j = 0; j < *count - 1; j++) {
                                int uses_read_end = 0;
                                if (i > 0 && j == i - 1) {
                                        uses_read_end = 1;
                                }

                                int uses_write_end = 0;
                                if (i < *count - 1 && j == i) {
                                        uses_write_end = 1;
                                }

                                if (uses_read_end == 0) {
                                        close(pipes[j][0]);  // I don't need the read end of this pipe
                                }
                                if (uses_write_end == 0) {
                                        close(pipes[j][1]);  // I don't need the write end of this pipe
                                }
                        }
                        if(strchr(commands[i].raw, '<') != NULL){
                                struct cmd_string last;
                                parse_command(commands[i].raw,&last,REDIRECT);
                                dup2(pipes[i][1],STDOUT_FILENO);
                                close(pipes[i][1]);
                                redirect(&last);
                        }
                        else{
                                dup2(pipes[i][1],STDOUT_FILENO);
                                close(pipes[i][1]);
                                execvp(commands[i].argv[0],commands[i].argv);
                                perror("fail");
                                exit(1);
                        }
                }
                else{
                        //close pipes in parent
                        close(pipes[i][1]);
                        if (i > 0) close(pipes[i-1][0]);
                }
            }
            else if(i+1 == *count){
                //END change output back to terminal read from previous pipe on last command
                if(!(pids[i]=fork())){
                        // Close all unneeded FDs
                        for (int j = 0; j < *count - 1; j++) {
                                int uses_read_end = 0;
                                if (i > 0 && j == i - 1) {
                                        uses_read_end = 1;
                                }

                                int uses_write_end = 0;
                                if (i < *count - 1 && j == i) {
                                        uses_write_end = 1;
                                }

                                if (uses_read_end == 0) {
                                        close(pipes[j][0]);  // I don't need the read end of this pipe
                                }
                                if (uses_write_end == 0) {
                                        close(pipes[j][1]);  // I don't need the write end of this pipe
                                }
                        }
                        if(strchr(commands[i].raw, '>') != NULL || strchr(commands[i].raw, '<') != NULL){
                                if(strchr(commands[i].raw, '<') != NULL){
                                        fprintf(stderr,"Error: mislocated input redirection\n");
                                        exit(SKIP);        
                                }
                                struct cmd_string last;
                                parse_command(commands[i].raw,&last,REDIRECT);
                                dup2(pipes[i-1][0],STDIN_FILENO);
                                close(pipes[i-1][0]);
                                redirect(&last);
                        }
                        else{
                                dup2(pipes[i-1][0],STDIN_FILENO);
                                close(pipes[i-1][0]);
                                execvp(commands[i].argv[0],commands[i].argv);
                                perror("fail");
                                exit(1);
                        }
                }
                else{
                        //close pipes in parent
                        close(pipes[i-1][0]);
                }
            }
            else{
                //MIDDLE
                if(!(pids[i]=fork())){

                        
                        // Close all unneeded FDs
                        for (int j = 0; j < *count - 1; j++) {
                                int uses_read_end = 0;
                                if (i > 0 && j == i - 1) {
                                        uses_read_end = 1;
                                }

                                int uses_write_end = 0;
                                if (i < *count - 1 && j == i) {
                                        uses_write_end = 1;
                                }

                                if (uses_read_end == 0) {
                                        close(pipes[j][0]);  // I don't need the read end of this pipe
                                }
                                if (uses_write_end == 0) {
                                        close(pipes[j][1]);  // I don't need the write end of this pipe
                                }
                        }




                        dup2(pipes[i][1],STDOUT_FILENO);
                        dup2(pipes[i-1][0],STDIN_FILENO);
                        close(pipes[i][1]);
                        close(pipes[i-1][0]);
                        execvp(commands[i].argv[0],commands[i].argv);
                        perror("fail");
                        exit(1);
                }
                else{
                        //close pipes in parent
                        close(pipes[i][1]);
                        close(pipes[i-1][0]);
                }
            }
            i++;
        }
        for(int i = 0; i < *count-1;i++){
                free(pipes[i]);
        }
        for(int k = 0; k < *count; k++){
            //collect status
            //printf("pidk: %d\n",pids[k]);
            waitpid(pids[k],&status[k],0);
        }
        free(pids);
        free(pipes);
        return status;
}

int check_background(char* cmd){
        char* bg = strchr(cmd, '&');
        if(!bg) return 0;
        if(*(bg+1) != '\0'){
                fprintf(stderr, "Error: mislocated background sign\n");
                return(-1);
        }
	return 1;
}


void monitor_bg(){
	//fprintf(stderr, "[debug] monitor_bg: waiting on pid = %d\n", bg_process_pids);
        //return if already running stops exits
        if(!bg_in_process) return;
	int status;
	pid_t retval = waitpid(bg_process_pids[bg_count-1], &status, WNOHANG);
	//fprintf(stderr, "[debug] waitpid returned = %d\n", retval);
	if (retval == -1) {
        	perror("[monitor_bg] waitpid error");  
        	return;
    	}

    	if (retval == 0) {
        	return; // child still running
    	}
	if(retval == bg_process_pids[bg_count-1]){
                if(bg_count>1){
                        int status1[bg_count];
                        for(int k = 0; k < bg_count; k++){
                                waitpid(bg_process_pids[k],&status1[k],0);
                        }
                        fprintf(stderr, "+ completed '%s' ", bg_cmd);
                        for(int i = 0; i <bg_count; i++){
                                fprintf(stderr,"[%d]", status1[i]);
                        }
                        fprintf(stderr, "\n");
                }
                else{
                        fprintf(stderr, "+ completed '%s&' [%d]\n", bg_cmd, WEXITSTATUS(status));
                        bg_in_process = 0;
                        bg_process_pids = NULL;
                        memset(bg_cmd, 0, sizeof(bg_cmd));
                }
	}
}

void store_bg(pid_t *bg_pids, char* cmd,int count){
	strncpy(bg_cmd, cmd, CMDLINE_MAX);
    	bg_cmd[CMDLINE_MAX - 1] = '\0';
    	bg_process_pids = bg_pids;
   	bg_in_process = 1;
        bg_count = count;
	//fprintf(stderr, "[debug] store_bg: storing pid = %d for '%s'\n", bg_pid, cmd);	
}

int main(void){
        char cmd[CMDLINE_MAX];
        char *eof;
        struct cmd_string command;

        while (1) {
                char *nl;

                //////////his code///////////////////////////////////////////////
                /* Print prompt */
                printf("sshell@ucd$ ");
                fflush(stdout);

                /* Get command line */
                eof = fgets(cmd, CMDLINE_MAX, stdin);
                if (!eof)
                        /* Make EOF equate to exit */
                        strncpy(cmd, "exit\n", CMDLINE_MAX);

                /* Print command line if stdin is not provided by terminal */
                if (!isatty(STDIN_FILENO)) {
                        printf("%s", cmd);
                        fflush(stdout);
                }

                /* Remove trailing newline from command line */
                nl = strchr(cmd, '\n');
                if (nl)
                        *nl = '\0';
                        //////////his code///////////////////////////////////////////////
                
                //no segfault at enter
                /**/
                if (cmd[0] == '\0') {
    			continue;
		}

                int bg_flag = check_background(cmd);
                if (bg_flag == -1) {
                        continue; // Mislocated '&'
                }
                if (bg_flag == 1) {
                        char *amp = strchr(cmd, '&');
                        if (amp) {
                                *amp = '\0'; // Remove the '&'
                                while (amp > cmd && *(amp - 1) == ' ') {
                                        *(--amp) = '\0'; // Remove trailing spaces
                                }
                        }
                }
                /* Builtin commands */
                if (!strcmp(cmd, "exit")) {
                        monitor_bg();
                        if(bg_in_process){
                                fprintf(stderr, "Error: active job still running\n");
                                continue;
                        }
                        fprintf(stderr, "Bye...\n");
                        fprintf(stderr, "+ completed '%s' [0]\n", cmd);
                        exit(0);
                }
                //CD COMMAND
                parse_command(cmd, &command, CDCHECK);
                if(!strcmp(command.argv[0], "cd")){
                    int res = chdir(command.argv[1]);
                    if(res != 0){
                        fprintf(stderr,"Error: cannot cd into directory\n");
                    }
                    fprintf(stderr, "+ completed '%s' [0]\n", cmd);
                    continue;
                }
                //CD COMMAND^^^^
                //PWD COMMAND
                parse_command(cmd, &command,PWD);
                if(!strcmp(command.argv[0], "pwd")) {
                    	char* buffer = NULL;
                    	buffer = getcwd(buffer, CMDLINE_MAX);
                    	fprintf(stderr, "%s\n",buffer);
                        fprintf(stderr, "+ completed '%s' [0]\n", cmd);
                    	continue;
                }
                //PWD COMMAND^^^^

                //PIPELINING
                if(strchr(cmd, '|') != NULL){
                        int* exitStatus;
                        int count = 0;
                        int x = 0;
                        exitStatus=multipipe(cmd,&count);
                        for(int i = 0; i < count; i++){
                                if(WEXITSTATUS(exitStatus[i])==SKIP){
                                        x = 1;
                                }
                        }
                        if(x)
                                continue;
                        fprintf(stderr, "+ completed '%s' ", cmd);
                        for(int i = 0; i <count; i++){
                                fprintf(stderr,"[%d]",WEXITSTATUS(exitStatus[i]));
                        }
                        fprintf(stderr, "\n");
                        free(exitStatus);
                        continue; //last command handled normally
                }
                //PIPELINING^^^^^^

               /* Regular command */
                pid_t pid = fork();

                if(pid > 0){
                        //PARENT PROCESS////////////////////////////////////////////////////////////////
                        int status;
                        pid_t result = -1;
            
                        if (bg_flag == 1) {
                            result = waitpid(pid, &status, WNOHANG);
                            if (result == 0) {
                                        pid_t *pidptr = (pid_t*)malloc(sizeof(pid_t));
                                        pidptr[0] = pid;
                                        store_bg(pidptr, cmd,1);
                            } else if (result == pid) {
                                        fprintf(stderr, "+ completed '%s&' [%d]\n", cmd, WEXITSTATUS(status));
                            } else {
                                        perror("waitpid (early reap)");
                            }

                        } else {
                            waitpid(pid, &status, 0);
                            monitor_bg();
                            if (WIFEXITED(status)) {
                                        if (WEXITSTATUS(status) != SKIP) {
                                            fprintf(stderr, "+ completed '%s' [%d]\n", cmd, WEXITSTATUS(status));
                                        }
                            } else {
                                        fprintf(stderr, "error?");
                            }
                        }
                }
                else if(pid==0){
                        //CHILD PROCESS/////////////////////////////////////////////////////////////////
                        //get
                        //fprintf(stderr, "[debug] execing in child, pid = %d\n", getpid());
                        if (strchr(cmd, '>') != NULL || strchr(cmd, '<') != NULL) {
                                parse_command(cmd, &command, REDIRECT);
                                redirect(&command);
                        } 
                        else {
                                parse_command(cmd, &command, 0);
                                execvp(command.argv[0], command.argv);
                                fprintf(stderr, "Error: command not found\n");
                                exit(EXIT_FAILURE);
                        }
                }
                else{
                        perror("fork");
                        exit(EXIT_FAILURE);
                }
	}
	return EXIT_SUCCESS;
} 
