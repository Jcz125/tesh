#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <pwd.h>

#define BUFFER_LENGTH 4096
#define ARG_MAX 512

#define STDIN_FD 0
#define STDOUT_FD 1
#define STDERR_FD 2

#define NB_SPECIAL_STRING 7
const char *special_string[] = {
    ";",
    ">",
    ">>",
    "|",
    "&&",
    "||",
    "<"
};

bool stop = false;

int decouper(char* entree, char **sortie) {
    int i = 0;
    char *base = entree;
    char *next = strchr(base, ' ');

    while(next != NULL) {
        sortie[i++] = base;

        // On enlève les espaces en trop
        while (next[0] == ' ' || next[0] == '\t') {
            next[0] = '\0'; // On remplace l'espace par un \0
            next++;
        }

        // Si il y avait des espaces inutiles à la fin de la commande
        if (next[0] == '\n') {
            next[0] = '\0';
            sortie[i] = NULL;
            return i;
        }

        base = next;
        next = strchr(base, ' ');
    }

    if(entree[0] != '\0') {
        sortie[i++] = base;
        strchr(base, '\n')[0] = '\0';
    }

    sortie[i] = NULL;

    return i;
}

/*
Lance un exécutable et gère la sortie et l'entrée
 char* file : fichier à éxécuter
 char *argv[] : paramètres
 int input : file descriptor pour l'entrée (par défaut stdin)
 bool stdout : si le retour doit être stdout ou un pipe créé

 int de retour : file descritor de sortie (par défaut stdout)
*/
int run(const char* file, char *args[], int input, bool to_stdout) {
    int fd[2];
    if (!to_stdout)
        pipe(fd);

    if(!fork()) {
        //si fils
        if (input != STDIN_FD) {
            dup2(input, STDIN_FD);
        }
        if (!to_stdout) {
            dup2(fd[1], STDOUT_FD);
        }

        int status = execvp(file, args);
        if (status == -1) {
            fprintf(stderr, "Ne peut pas exécuter \"%s\" Code d'erreur : %d\n", file, status);
            exit(EXIT_FAILURE);
        }
    }

    // Plus besoin d'écrire dans le pipe, c'est au fils de le faire
    close(fd[1]);

    if (input != STDIN_FD)
        close(input);

    if (!to_stdout)
        return fd[0];

    return STDOUT_FD;
}


int main(int argc, char *argv[]) {
    char input_buffer[BUFFER_LENGTH];
    char hostname[HOST_NAME_MAX];
    char path[PATH_MAX];
    char *username = getlogin();
    char *user_home;

    char entree[BUFFER_LENGTH];
    char *entree_decoupee[ARG_MAX]; // Tableau pour découper l'entrée

    bool next_silent = false;

    if (username == NULL) {
        printf("Erreur lors de la récupération du nom d'utilisateur.\n");
        return EXIT_FAILURE;
    }

    if (gethostname(hostname, HOST_NAME_MAX) != 0) {
        printf("Erreur lors de la récupération du hostname.\n");
        return EXIT_FAILURE;
    }

    if ((user_home = getenv("HOME")) == NULL) {
        if ((user_home = getpwuid(getuid())->pw_dir) == NULL) {
            printf("Impossible de réccupérer le Home de l'utilisateur.\n");
            return EXIT_FAILURE;
        }
    }

    while(!stop) {
        if (getcwd(path, PATH_MAX) == NULL) {
            printf("Erreur lors de la récupération du répertoire courant.\n");
            return EXIT_FAILURE;
        }
        printf("%s@%s:%s$ ", username, hostname, path);
        // Flush le buffer de stdout pour que le USER@HOSTNAME:REPCOURANT$ sans retour à la ligne
        fflush(stdout);
        if (fgets(input_buffer, BUFFER_LENGTH, stdin) == NULL) {
            // Si un CTRL+D a été détecté
            return EXIT_SUCCESS;
        }
        memcpy(entree, input_buffer, BUFFER_LENGTH);
        int nbargs = decouper(entree, entree_decoupee);

        if (nbargs == 0 || entree_decoupee[0][0] == '\0') {
            continue;
        }

        if (strcmp(entree_decoupee[0], "cd")  == 0) {
            char *path = input_buffer + 3; // Après "cd "
            if (nbargs == 1) {
                path = user_home;
            }
            else {
                strchr(path, '\n')[0] = '\0';
            }
            if (chdir(path) != 0) {
                printf("Erreur lors du changement du répertoire courant à %s\n", path);
            }
        }
        else {
            int base = 0;
            int status;
            int next;
            while (base < nbargs) {
                int spe_i = -1;
                for (next = base; next < nbargs; ++next)
                {
                    for (int i = 0; i < NB_SPECIAL_STRING; ++i)
                    {
                        if (strcmp(entree_decoupee[next], special_string[i]) == 0) {
                            spe_i = i;
                            break;
                        }
                    }
                    if (spe_i != -1)
                        break;
                }

                // on en lève le caractère spécial pour que (entree_decoupee + base) puisse être donné directement à execvp
                entree_decoupee[next] = NULL;
                switch (spe_i) {
                    case 0: // ;
                        run((entree_decoupee + base)[0], (entree_decoupee + base), STDIN_FD, true);
                        waitpid(-1, &status, 0);
                        run((entree_decoupee + base + next + 1)[0], (entree_decoupee + base + next + 1), STDIN_FD, true);
                        waitpid(-1, &status, 0);
                    case 1: // >
                        int out = run((entree_decoupee + base)[0], (entree_decoupee + base), STDIN_FD, false);
                        waitpid(-1, &status, 0);
                        run((entree_decoupee + base + next + 1)[0], (entree_decoupee + base + next + 1), out, true);
                        waitpid(-1, &status, 0);
                    default:
                        run((entree_decoupee + base)[0], (entree_decoupee + base + next + 1), STDIN_FD, true);
                        waitpid(-1, &status, 0);
                        break;
                }
                base = ++next;
            }
        }
    }

    return EXIT_SUCCESS;
}
