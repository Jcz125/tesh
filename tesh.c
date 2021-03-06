#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <dlfcn.h>

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
        char *search = base;
        while(search[0] != '\n' && search[0] != '\0') {
            search++;
        }
        search[0] = '\0';
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
    int nb_supprime = 0;
    for (int i = 0; base + i < next; ++i) {
        if(strcmp(base[i], ">") == 0 || strcmp(base[i], ">>") == 0) {
            if (++i < nbargs) {
                redirect_to = base[i - 1];
                redirect_to_file = base[i];
                base[i - 1] = NULL;
                base[i] = NULL;
                nb_supprime += 2;
            }
        }
        else if (strcmp(base[i], "<") == 0) {
            if (++i < nbargs) {
                redirect_from = base[i - 1];
                redirect_from_file = base[i];
                base[i - 1] = NULL;
                base[i] = NULL;
                nb_supprime += 2;
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
    // but : <---decalage-->Reste<--nb_supprime-->
    // étape1 : Reste<---decalage--><--nb_supprime-->
    char **from = base;
    char **to = base;
    while (from < next) {
        if (from[0] != NULL) {
            if (to != from) {
                to[0] = from[0];
                from[0] = NULL;
            }
            to++;
        }
        from++;
    }
    //étape 2, le décalage
    for (int i = nbargs - nb_supprime + decalage - 1; i >= decalage; --i) {
        base[i] = base[i - decalage];
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
Créer le file descriptor en cas d'existence d'un élément de redirection entre base et la 1ère commande à exécuter
 char*** base_adr : adresse de position du pointeur de lecture d'argument (initialement au début de la table)
 int* last_out_adr : adresse de la dernière sortie (en général un fd ou un stdin s'il n'y a eu aucune sortie de commande)

 int de retour : retourne le file descriptor s'il a été créé, sinon le stdout
 
 exemple : > fichier ls | grep "d"
 base pointe sur >
 last_out = 0 (ici)
 create_fd voit un >, elle crée le fd et return le fd sans modifier last_out (car inutile)
 dans le cas de <, elle crée le fd et return le fd en modifiant last_out 
 (car on lit dans le fichier pour passer le contenu dans la commande pointée par base via last_out qui sera mis à jour)
*/
int create_fd(char*** base_adr, int* last_out_adr) {
    int fd = STDOUT_FD;
    if (!strcmp((*base_adr)[0], ">")) {
        fd = open((*base_adr)[1], O_WRONLY | O_TRUNC | O_CREAT, 0664);
        *base_adr += 2;
    }
    else if (!strcmp((*base_adr)[0], ">>")) {
        fd = open((*base_adr)[1], O_WRONLY | O_APPEND | O_CREAT, 0664);
        *base_adr += 2;
    }
    if (!strcmp((*base_adr)[0], "<")) {
        if (*last_out_adr != STDIN_FD)
            close(*last_out_adr);           // on ferme le file descriptor car inutile ici la fichier sera pris en stdin via <
        *last_out_adr = open((*base_adr)[1], O_RDONLY);
        *base_adr += 2;
    }
    return fd;
}

/*
Permet de mettre en frontground l'exécution en background d'un processus
 char*** base_adr : adresse de position du pointeur de lecture d'argument (initialement au début de la table)
 char*** next_adr : adresse de position du prochain argument à réaliser
 pid_t* pid_tab : tableau de pid en background

 bool de retour : true s'il y a eu un fg, false sinon
*/
bool fg(char*** base_adr, char*** next_adr, pid_t* pid_tab, int* status_adr, int* nb_bg_adr) {
    if (!strcmp((*base_adr)[0], "fg")) {
        if (*nb_bg_adr != 0) {
            pid_t pid = pid_tab[--(*nb_bg_adr)];
            if (*base_adr - *next_adr < -1) {   // on regarde si next est juste après base
                // il y a des arguments après pour fg
                pid = atoi((*base_adr)[1]);
                // on permute le pid choisit et le dernier pid de la liste
                for (int i=0; i<*nb_bg_adr; i++)
                    if (pid_tab[i] == pid)
                        pid_tab[i] = pid_tab[*nb_bg_adr];
                }
            waitpid(pid, status_adr, 0);
            // if (WIFEXITED(*status_adr))      // utile ?
            printf("[%d->%d]\n", pid, WEXITSTATUS(*status_adr));
        } else
            printf("Nothing in background\n");
        return true;
    }
    return false;
}

/*
Lance un exécutable et gère la sortie et l'entrée
 char* file : fichier à éxécuter
 char *argv[] : paramètres
 int input : file descriptor pour l'entrée (par défaut stdin)
 bool stdout : si le retour doit être stdout ou un pipe créé

 int de retour : file descriptor de sortie (par défaut stdout)
*/
int run(const char* file, char *args[], int input, int out, pid_t* child_pid_adr) {
    int fd[2];
    if (out == -1)
        pipe(fd);
    else {
        fd[0] = out;
        fd[1] = out;
    }

    if(!(*child_pid_adr = fork())) {
        //si fils
        if (input != STDIN_FD) {
            dup2(input, STDIN_FD);
            close(input);
        }

        if (fd[1] != STDOUT_FD) {
            dup2(fd[1], STDOUT_FD);
            close(fd[1]);
        }

        if (fd[0] != STDOUT_FD)
            close(fd[0]);

        int status = execvp(file, args);
        fprintf(stderr, "Ne peut pas exécuter \"%s\" Code d'erreur : %d\n", file, status);
        exit(EXIT_FAILURE);
    }

    if (input != STDIN_FD)
        close(input);

    // Plus besoin d'écrire dans l'entée, c'est au fils de le faire
    if (fd[1] != STDOUT_FD)
        close(fd[1]);
    // si out=STDOUT_FD alors fd[0]=STDOUT_FD et ça change rien ici
    return fd[0];
}


int main(int argc, char *argv[]) {
    char hostname[HOST_NAME_MAX];
    char path[PATH_MAX];
    char *username;
    char *user_home;
    struct passwd *pw = getpwuid(getuid());
    pid_t* pid_tab = calloc(100, sizeof(pid_t));

    int nb_bg = 0;

    char *entree_raw = NULL;
    char *entree = NULL; // Copie de entree_raw mais apres l'appel à decoupage, les espaces remplacés par des \0
    char *entree_decoupee[ARG_MAX]; // Tableau pour découper l'entrée (pointe vers entree)
    char prompt[HOST_NAME_MAX + PATH_MAX + 4 + 512];

    void* libreadline = NULL;
    int (*read_history)(char*) = NULL;
    char* (*readline)(char*) = NULL;
    void (*add_history)(char*) = NULL;
    int (*write_history)(char*) = NULL;

    bool showprompt = true;
    int activate_readline = 0, stop_on_error = 0;
    int fileentree_raw = -1;

    int c;
    while ((c = getopt(argc, argv, "re")) != -1) {
        switch (c) {
            case 'r':
                activate_readline = 1;
                break;
            case 'e':
                stop_on_error = 1;
                break;
            }
    }

    if (optind != -1 && argv[optind] != NULL) { //if getopt_long has found other option than -e or -r
        fileentree_raw = open(argv[optind], O_RDONLY);
        dup2(fileentree_raw, STDIN_FD);
        showprompt = false;
    }

    if (isatty(STDIN_FD) != 1) {
        showprompt = false;
    }

    if ((username = getlogin()) == NULL) {
        if (pw == NULL || (username = pw->pw_name) == NULL) {
            fprintf(stderr, "Erreur lors de la récupération du nom d'utilisateur.\n");
            return EXIT_FAILURE;
        }
    }

    if (gethostname(hostname, HOST_NAME_MAX) != 0) {
        fprintf(stderr, "Erreur lors de la récupération du hostname.\n");
        return EXIT_FAILURE;
    }

    if ((user_home = getenv("HOME")) == NULL) {
        if (pw == NULL || (user_home = pw->pw_dir) == NULL) {
            fprintf(stderr, "Impossible de réccupérer le Home de l'utilisateur.\n");
            return EXIT_FAILURE;
        }
    }

    if (activate_readline) {
        libreadline =  dlopen("libreadline.so", RTLD_LAZY);
        if (libreadline == NULL) {
            fprintf(stderr, "Ne peut pas charger libreadline.so : %s\n", dlerror());
            return EXIT_FAILURE;
        }
        if ((readline = (char* (*)(char*))dlsym(libreadline, "readline")) == NULL
            || (add_history = (void (*)(char*))dlsym(libreadline, "add_history")) == NULL
            || (read_history = (int (*)(char*))dlsym(libreadline, "read_history")) == NULL
            || (write_history = (int (*)(char*))dlsym(libreadline, "write_history")) == NULL) {
            fprintf(stderr, "Ne peut pas charger un symbole de libreadline.so : %s\n", dlerror());
            return EXIT_FAILURE;
        }
        read_history(NULL);
    }
    else {
        entree = calloc(BUFFER_LENGTH, sizeof(char));
        entree_raw = calloc(BUFFER_LENGTH, sizeof(char));
    }

    while(!stop) {
        if (getcwd(path, PATH_MAX) == NULL) {
            fprintf(stderr, "Erreur lors de la récupération du répertoire courant.\n");
            return EXIT_FAILURE;
        }
        if (showprompt)
            sprintf(prompt, "%s@%s:%s$ ", username, hostname, path);

        if (activate_readline) {
            if (entree_raw)
                free(entree_raw);
            if (entree)
                free(entree);
            entree_raw = readline(prompt);
            if (entree_raw == NULL) // Si un CTRL+D a été détecté
                return EXIT_SUCCESS;
            entree = strdup(entree_raw);
            add_history(entree);
            write_history(NULL);
        }
        else {
            if (showprompt) {
                printf("%s", prompt);
                fflush(stdout); // Flush le buffer de stdout pour que le USER@HOSTNAME:REPCOURANT$ sans retour à la ligne
            }
            if (fgets(entree_raw, BUFFER_LENGTH, stdin) == NULL) // Si un CTRL+D a été détecté
                return EXIT_SUCCESS;
            memcpy(entree, entree_raw, BUFFER_LENGTH);
        }

        int nbargs = decouper(entree, entree_decoupee);

        if (nbargs == 0 || entree_decoupee[0][0] == '\0') {
            continue;
        }

        if (strcmp(entree_decoupee[0], "cd")  == 0) {
            char *path = entree_raw + 3; // Après "cd "
            if (nbargs == 1) {
                path = user_home;
            }
            else {
                char *search = path;
                while(search[0] != '\n' && search[0] != '\0') {
                    search++;
                }
                search[0] = '\0';
            }
            if (chdir(path) != 0) {
                fprintf(stderr, "Erreur lors du changement du répertoire courant à %s\n", path);
            }
        }
        else {
            char **base = entree_decoupee;
            char **next;
            char **end = entree_decoupee + nbargs;
            int status;
            int last_out = STDIN_FD;
            bool run_next = true;
            pid_t child_pid = -1;       // utile plus tard pour pouvoir récupérer le pid du fils qui a terminé
            int fd = STDOUT_FD;
            int nb_run = 0;
            while (base < end) {
                int spe_i;
                next = search(base, end, &spe_i);
                reorder(base, next);    // base tombe sur la commande à exécuter

                next[0] = NULL; // on enlève le caractère spécial pour que base puisse être donné directement à execvp
                if (fg(&base, &next, pid_tab, &status, &nb_bg)) { // supposons que fd s'exécute tout seul ou en dernière position de commande
                    if (stop_on_error && status != 0)
                        stop = true;
                    break;
                }
                switch (spe_i) {
                    case 1: // |
                        if (run_next) {
                            fd = create_fd(&base, &last_out);
                            if (fd > 1) {
                                run(base[0], base, last_out, fd, &child_pid);
                                last_out = open("/dev/null", O_RDONLY);
                            } else {
                                last_out = run(base[0], base, last_out, -1, &child_pid);
                            }
                            nb_run++;
                        }
                        break;

                    case 2: // &&
                        if (run_next) {
                            fd = create_fd(&base, &last_out);
                            run(base[0], base, last_out, fd, &child_pid);
                            waitpid(child_pid, &status, 0);
                        }
                        last_out = STDIN_FD;
                        if (status == 0)                        // si la commande avant && ne s'est pas exécutée correctement
                            run_next = true;                    // on ignore la commande après &&
                        else {
                            run_next = false;
                            if (stop_on_error)
                                stop = true;
                        }
                        break;

                    case 3: // ||
                        if (run_next) {
                            fd = create_fd(&base, &last_out);
                            run(base[0], base, last_out, fd, &child_pid);
                            waitpid(child_pid, &status, 0);
                        }
                        last_out = STDIN_FD;
                        if (status == 0)                         // si la commande avant || s'est exécutée sans erreur
                            run_next = false;                    // on ignore la commande après ||
                        else {
                            run_next = true;
                            if (stop_on_error)
                                stop = true;
                        }
                        break;

                    case 4: // &
                        fd = create_fd(&base, &last_out);
                        run(base[0], base, last_out, fd, &child_pid);
                        pid_tab[nb_bg++] = child_pid;
                        printf("[%d]\n", child_pid);
                        fflush(stdout);
                        last_out = STDIN_FD;
                        run_next = true;
                        break;

                    default:
                        if (run_next) {
                            fd = create_fd(&base, &last_out);
                            run(base[0], base, last_out, fd, &child_pid);
                            waitpid(child_pid, &status, 0);
                            if (stop_on_error && status != 0)
                                stop = true;
                        }
                        last_out = STDIN_FD;
                        run_next = true;
                        break;
                }
                if (stop)
                    break;
                base = ++next;
                fd = STDOUT_FD;
                child_pid = -1;
            }
            if (last_out != STDIN_FD)
                close(last_out);
            for (;nb_run != 0;nb_run--) {
                waitpid(-1, &status, 0);
                if (stop_on_error && status != 0) {
                    stop = true;
                }
            }
        }
    }

    if (entree_raw)
        free(entree_raw);
    if (entree)
        free(entree);

    if (fileentree_raw != -1)
        close(fileentree_raw);

    if (activate_readline && readline != NULL) {
        if (dlclose(libreadline) != 0) {
            fprintf(stderr, "N'arrive pas à fermer libreadline.so : %s", dlerror());
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
