#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <limits.h>
#include <pwd.h>

#define BUFFER_LENGTH 4096
#define ARG_MAX 512

bool stop = false;

int decouper(char* entree, char **sortie) {
    int i = 0;
    char *base = entree;
    char *next = strchr(base, ' ');

    while(next != NULL) {
        next[0] = '\0';
        sortie[i++] = base;
        char *temp = next + 1;
        next = strchr(entree, ' ');
        entree = temp;
    }

    if(entree[0] != '\0') {
        sortie[i++] = base;
        strchr(entree, '\n')[0] = '\0';
    }

    return i;
}



int main(int argc, char *argv[]) {
    char input_buffer[BUFFER_LENGTH];
    char hostname[HOST_NAME_MAX];
    char path[PATH_MAX];
    char *username = getlogin();
    char *user_home;

    char entree[BUFFER_LENGTH];
    char *current_args[ARG_MAX]; // Tableau pour découper l'entrée
    char *next_args[ARG_MAX]; // Tableau à donner à exec

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
        if (!next_silent) {
            printf("%s@%s:%s$ ", username, hostname, path);
        }
        else {
            next_silent = false;
        }
        // Flush le buffer de stdout pour que le USER@HOSTNAME:REPCOURANT$ sans retour à la ligne
        fflush(stdout);
        if (fgets(input_buffer, BUFFER_LENGTH, stdin) == NULL) {
            // Si un CTRL+D a été détecté
            return EXIT_SUCCESS;
        }
        memcpy(entree, input_buffer, BUFFER_LENGTH);
        int nbargs = decouper(entree, current_args);

        if (nbargs == 0) {
            next_silent = true;
            continue;
        }

        if (strcmp(current_args[0], "cd")  == 0) {
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
    }

    return EXIT_SUCCESS;
}
