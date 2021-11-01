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

#define NB_SPECIAL_STRING 5
const char *special_string[] = {
    ";",
    "|",
    "&&",
    "||",
    "&"
};

bool stop = false;

/*Decoupe l'entree en un tableau
 char* entree : L'entree à decouper,
 char **sortie : Tableau pointant vers chaque début d'argument dans entree

 int de retour : nombre d'élèment dans sortie
*/
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

char** search(char** base, char** end, int *spe_i) {
    *spe_i = -1;
    char** next;
    for (next = base; next < end; next++) {
        for (int i = 0; i < NB_SPECIAL_STRING; ++i) {
            if (strcmp(next[0], special_string[i]) == 0) {
                *spe_i = i;
                return next;
            }
        }
    }
    return next;
}

/* Reordonne l'entree decoupé pour :
 avoir la redirection vers des fichiers au début (que la dernière redirection donnée est gardée)
 avoir la redirection d'un fichier en entree au début (que la dernière redirection donnée est gardée)
 Exemples :
    ls > fichier1 -la > fichier2
    à
    ls -la > fichier2 NULL NULL

    grep > fichier3 < fichier1 "abc" < fichier2
    à
    < fichier2 grep "abc" > fichier3 NULL NULL
*/
void reorder(char** base, char** next) {
    char *redirect_to = NULL; // pour les > >>
    char *redirect_to_file = NULL;
    char *redirect_from = NULL; // pour les <
    char *redirect_from_file = NULL;
    int nbargs = next - base;
    for (int i = 0; base + i < next; ++i) {
        if(strcmp(base[i], ">") == 0 || strcmp(base[i], ">>") == 0) {
            if (++i < nbargs) {
                redirect_to = base[i - 1];
                redirect_to_file = base[i];
                base[i - 1] = NULL;
                base[i] = NULL;
            }
        }
        else if (strcmp(base[i], "<") == 0) {
            if (++i < nbargs) {
                redirect_from = base[i - 1];
                redirect_from_file = base[i];
                base[i - 1] = NULL;
                base[i] = NULL;
            }
        }
    }
    int decalage = 0;
    if (redirect_to != NULL)
        decalage += 2;
    if (redirect_from != NULL)
        decalage += 2;
    if (decalage == 0)
        return;
    char **from = base;
    char **to = base + decalage;
    while (from < next) {
        if (from[0] != NULL) {
            (to++)[0] = from[0];
            from[0] = NULL;
        }
        from++;
    }
    if (redirect_to != NULL) {
        (base++)[0] = redirect_to;
        (base++)[0] = redirect_to_file;
    }
    if (redirect_from != NULL) {
        (base++)[0] = redirect_from;
        (base++)[0] = redirect_from_file;
    }
}

/*
Créer le file descriptor en cas d'existance d'un élément de redirection entre start et base
 char** start : début de la séquence à traiter
 char** base : position de la commande
 int* last_out : sortie de la denière commande exécutée (en général un fd ou un stdin si aucune sortie)
 
 exemple : > fichier ls | grep "d"
 start pointe sur >
 base pointe sur ls
 last_out = 0 (ici)
 create_fd voit un >, elle crée le fd et return le fd sans modifier last_out (car inutile)
 dans le cas de <, elle crée le fd et return le fd en modifiant last_out 
 (car on lit dans le fichier pour passer le contenu dans la commande pointée par base via last_out qui sera mis à jour)
*/
int create_fd(char** start, char** base, int* last_out) {
    // à compléter (soon)
    return 0;
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
        if (input != STDIN_FD)
            dup2(input, STDIN_FD);
        if (!to_stdout)
            dup2(fd[1], STDOUT_FD);

        int status = execvp(file, args);
        fprintf(stderr, "Ne peut pas exécuter \"%s\" Code d'erreur : %d\n", file, status);
        exit(EXIT_FAILURE);
    }

    if (input != STDIN_FD)
        close(input);

    if (!to_stdout) {
        // Plus besoin d'écire dans le pipe, c'est au fils de le faire
        close(fd[1]);
        return fd[0];
    }

    return STDOUT_FD;
}


int main(int argc, char *argv[]) {
    char input_buffer[BUFFER_LENGTH];
    char hostname[HOST_NAME_MAX];
    char path[PATH_MAX];
    char *username;
    char *user_home;
    struct passwd *pw = getpwuid(getuid());

    char entree[BUFFER_LENGTH]; // Copie de input_buffer mais apres l'appel à decoupage, les espaces remplacés par des \0
    char *entree_decoupee[ARG_MAX]; // Tableau pour découper l'entrée (pointe vers entree)

    if ((username = getlogin()) == NULL) {
        if (pw == NULL || (username = pw->pw_name) == NULL) {
            printf("Erreur lors de la récupération du nom d'utilisateur.\n");
            return EXIT_FAILURE;
        }
    }

    if (gethostname(hostname, HOST_NAME_MAX) != 0) {
        printf("Erreur lors de la récupération du hostname.\n");
        return EXIT_FAILURE;
    }

    if ((user_home = getenv("HOME")) == NULL) {
        if (pw == NULL || (user_home = pw->pw_dir) == NULL) {
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
            char **base = entree_decoupee;
            char **next;
            char **end = entree_decoupee + nbargs;
            int status;
            int last_out = STDIN_FD;
            int old_out = 0;
            char **start = entree_decoupee;
            int fd = 0;
            while (base < end) {
                int spe_i;
                next = search(base, end, &spe_i);
                reorder(base, next);    // base tombe sur la commande à exécuter

                // on enlève le caractère spécial pour que base puisse être donné directement à execvp
                next[0] = NULL;
                switch (spe_i) {
                    case 0: // ;
                        old_out = last_out;
                        fd = create_fd(start, base, &last_out);         // à écrire : créer le file director s'il y a un élément de redirection entre debut et base (qui pointe sur la première commande à exécuter), sinon renvoi NULL
                        run((base)[0], (base), last_out, true);
                        if (old_out == last_out && fd)                  // ça veut dire qu'on est dans le cas de > ou >>
                            dup2(fd, last_out);
                        last_out = STDIN_FD;
                        waitpid(-1, &status, 0);
                        break;
/*
                    case 1: // >
                        last_out = run((base)[0], (base), last_out, false);
                        waitpid(-1, &status, 0);
                        int fd = fopen("%c", "w", (next + 1)[0]);     // ouvre ou créer le fichier (en écriture) avec la chaîne se trouvant après >
                        dup2(last_out, fd);     // peut-on utiliser write ?
                        close(fd);
                        base = ++next;
                        break;

                    case 2: // >>
                        last_out = run((base)[0], (base), last_out, false);
                        waitpid(-1, &status, 0);
                        int fd = fopen("%c", "a", (next + 1)[0]);     // ouvre ou créer le fichier (en ajout) avec la chaîne se trouvant après >
                        dup2(last_out, fd);
                        close(fd);
                        base = ++next;
                        break;
*/
                    case 1: // |
                        old_out = last_out;
                        fd = create_fd(start, base, &last_out);
                        if ((old_out == last_out) & fd) {
                            last_out = run((base)[0], (base), last_out, false);
                            dup2(fd, last_out);
                            last_out = STDIN_FD;
                        }
                        else
                            last_out = run((base)[0], (base), last_out, false);
                        waitpid(-1, &status, 0);        // dois-je décaler ça pour mettre juste après les run ?
                        break;

                    case 2: // &&
                        old_out = last_out;
                        fd = create_fd(start, base, &last_out);
                        run((base)[0], (base), last_out, true);
                        if (old_out == last_out && fd)
                            dup2(fd, last_out);
                        last_out = STDIN_FD;
                        waitpid(-1, &status, 0);
                        if (status != 0)            // si la commande avant && ne s'est pas exécutée correctement
                            base = end;             // on ignore la commande après && donc ici on quitte la boucle while(base < end)
                        break;

                    case 3: // ||
                        old_out = last_out;
                        fd = create_fd(start, base, &last_out);
                        run((base)[0], (base), last_out, true);
                        if (old_out == last_out && fd)
                            dup2(fd, last_out);
                        last_out = STDIN_FD;
                        waitpid(-1, &status, 0);
                        if (status == 0)        // si la commande avant || s'est exécutée sans erreur
                            base = end;         // on ignore la commande après || donc ici on quitte la boucle while(base < end)
                        break;
/*
                    case 6: // <
                        base = ++next;
                        int fd = fopen("%c", "r", (base)[0]);     // ouvre le fichier (en lecture)
                        last_out = run((base - 2)[0], (base - 2), fd, false);
                        break;
*/
                    default:
                        old_out = last_out;
                        fd = create_fd(start, base, &last_out);
                        run(base[0], base, last_out, true);
                        if (old_out == last_out && fd)
                            dup2(fd, last_out);
                        waitpid(-1, &status, 0);
                        break;
                }
                base = ++next;
                start = base;
            }
        }
    }

    return EXIT_SUCCESS;
}
