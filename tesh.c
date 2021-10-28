#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <limits.h>


#define BUFFER_LENGTH 4096

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define SCANF_FMT "%" STR(BUFFER_LENGTH) "s"

bool stop = false;

void signal_handler(int s) {
    stop = true;
}

int main() {
    char input_buffer[BUFFER_LENGTH];
    char hostname[HOST_NAME_MAX];
    char path[PATH_MAX];
    char *username = getlogin();

    if (username == NULL) {
        printf("Erreur lors de la récupération du nom d'utilisateur.\n");
        return EXIT_FAILURE;
    }

    if (gethostname(hostname, HOST_NAME_MAX) != 0) {
        printf("Erreur lors de la récupération du hostname.\n");
        return EXIT_FAILURE;
    }

    while(!stop) {
        if (getcwd(path, PATH_MAX) == NULL) {
            printf("Erreur lors de la récupération du répertoire courant.\n");
            return EXIT_FAILURE;
        }
        printf("%s@%s:%s$ ", username, hostname, path);
        // Flush le buffer de stdout pour que le USER@HOSTNAME:REPCOURANT$ sans retour à la ligne
        fflush(stdout);
        int status = scanf(SCANF_FMT, input_buffer);
        if (status == EOF) {
            // Si un CTRL+D a été détecté
            return EXIT_SUCCESS;
        }
        printf("%s\n", input_buffer);
    }

    return EXIT_SUCCESS;
}
