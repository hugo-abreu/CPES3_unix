#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/select.h>

/*
 * Auteur: Hugo Abreu
 * Titre: Projet Unix
 *
 * On realise un simulateur de boutique pour articles de bebes.
 *
 * Le main() de ce programme realise les forks representant les agents specifies
 * dans les parametres du programme.
 * Il cree ensuite des pipes liant le main aux processus (le main choisit sur
 * quel FD il veut ecrire) et un seul pipe sur lequel les processus communiquent
 * avec le main.
 * Pour chaque fork, vu qu'ils ne doivent lire que d'un FD et ecrire sur un
 * autre, on passe les FD de lecture et d'ecriture de main sur stdin et stdout.
 *
 * Le transport de messages entre les differents processus est assure par le
 * main, a travers d'une structure de donnees appellee IPC.
 * Cette structure de donnees permet de transmettre au main le destinataire et
 * l'expediteur, ainsi que les donnees qui veulent etre transmises. Le main peut
 * donc passer les donnees au destinataire pretendu.
 * Comme les donnees peuvent avoir des types differents, on realise un union qui
 * nous permet de transmettre de facon unifiee ces differentes donnees.
 *
 */

/* structures de données */

/* déclaration et définition des agents (vendeurs, clients et caissières) */
enum type_agents { VENDEUR, CLIENT, CAISSIERE };

typedef struct agent {
  pid_t pid;                    /* pid du proces representant l'agent */
  enum type_agents type;        /* type de l'agent */
  int fd;                       /* file descriptor du pipe main->agent */
  char *nom;                    /* nom de l'agent */
} agent;

agent a[9] = {
              { 0, VENDEUR,   0, "Jacques" },
              { 0, VENDEUR,   0, "Pierre"  },
              { 0, VENDEUR,   0, "Paul"    },
              { 0, CLIENT,    0, "Chloe"   },
              { 0, CLIENT,    0, "Elise"   },
              { 0, CLIENT,    0, "Léa"     },
              { 0, CAISSIERE, 0, "Lilou"   },
              { 0, CAISSIERE, 0, "Laura"   },
              { 0, CAISSIERE, 0, "Nadia"   },
};

/* déclaration et définition des articles (body, brassière et pyjama) */
typedef struct article {
  int  prix;                    /* prix de l'article */
  char nom[10];                 /* nom de l'article */
} article;

article arts [3] = {
                    { 13, "body"      },
                    { 10, "brassiere" },
                    { 20, "pyjama"    },
};


/* ticket */
struct ticket {
  int addition;                 /* addition: prix de l'article dans le ticket */
};

/* sac */
struct sac {
  struct article art;           /* article dans le sac */
  struct ticket  tck;           /* ticket dans le sac */
};


/* type Data unifié pour passer des données entre processus */
enum datatype { MESSAGE, ARTICLE, PAIEMENT, TICKET, SAC, DEBUT, FIN};

union Data {
  char           message[80];   /* message textuel envoye entre processus */
  struct article article;       /* article: avec un nom et un prix */
  struct ticket  ticket;        /* ticket: avec un total */
  struct sac     sac;           /* sac: avec un article et un ticket */
  int            paiement;      /* paiement: entier, transaction entre agents */
};


/* type IPC: Inter Processus Communicator. Data + destinataire et envoyeur */
struct IPC {
  int           from, to;       /* position des agents dans a[] */
  enum datatype type;           /* type de donnees dans data */
  union Data    data;           /* addresse de data envoye entre processus */
};

/* fonction pour créer des IPC's (retourne directement l'addresse de l'IPC) */
struct IPC* ipc (int from, int to, enum datatype type, char *s, int i) {
  struct IPC *p = malloc (sizeof *p);
  p->from = from;
  p->to   = to;
  p->type = type;
  switch (type) {
  case MESSAGE:   strcpy (p->data.message, s);     break;
  case ARTICLE:   strcpy (p->data.article.nom, s); break;
  case PAIEMENT:  p->data.paiement = i;            break;
  case TICKET:    p->data.ticket.addition = i;     break;
  case SAC:       strcpy (p->data.sac.art.nom, s); break;
  default:                                         break;
  }
  return p;
}


/*
 * variables globales pour référencer les agents (et le produit) à travers les
 * differents processus: elles representent la position de l'entree representant
 * l'article choisi dans les array agents a[] et produits arts[].
 */
int nv;                         /* numero vendeur */
int ncl;                        /* numero client */
int nca;                        /* numero caissiere */
int npr;                        /* numero produit */



/* FONCTION PRINT POUR MAIN */

void print_ipc(struct IPC buffer) {
  switch (buffer.type) {        /* selon le type de message dans buffer, on
                                 veut print des donnees differentes*/
  case MESSAGE:
    printf ("%s a dit à %s: %s\n",
            a[buffer.from].nom,
            a[buffer.to].nom,
            buffer.data.message); fflush(stdout);
    break;
  case ARTICLE:
    printf ("%s a donné à %s l'article %s\n",
            a[buffer.from].nom,
            a[buffer.to].nom,
            buffer.data.article.nom); fflush(stdout);
    break;
  case PAIEMENT:
    printf ("%s a réalisé un paiement de %d à %s\n",
            a[buffer.from].nom,buffer.data.paiement,
            a[buffer.to].nom); fflush(stdout);
    break;
  case SAC:
    printf ("%s donne un sac à %s avec l'article %s et le ticket de total %d\n",
            a[buffer.from].nom,
            a[buffer.to].nom,
            buffer.data.sac.art.nom,
            buffer.data.sac.tck.addition); fflush(stdout);
    break;
  default:
    break;
  }
}
/* FONCTIONS INIT POUR LES AGENTS */

#define BONJOUR "Bonjour!"
#define MERCI   "Merci et au revoir!"
#define PLAISIR "Qu'est ce qui vous ferait plaisir?"


/* vendeur */
int vendeur (char *nom) {
  fprintf (stderr, "vendeur %s instancié\n", nom); fflush(stderr);
  char nom_article[80];

  for (;;) {
     struct IPC buffer;
    read (0, &buffer, sizeof buffer); /* proces toujours a l'ecoute d'un
                                       message venant de main */

    if (buffer.type == FIN)     /* message FIN de main: => fin du processus */
      return (0);

    /* PROTOCOLE */
    else if (buffer.type == DEBUT) /* message DEBUT de main: => commence protocole */
      write (1, ipc(nv, ncl, MESSAGE, BONJOUR,0), sizeof (struct IPC));

    else if (buffer.from == ncl && !strcmp (buffer.data.message, BONJOUR))
      write (1, ipc(nv, ncl, MESSAGE,PLAISIR, 0),
             sizeof (struct IPC));
    else if (buffer.from == ncl && buffer.type == MESSAGE
             && sscanf (buffer.data.message, "Je souhaite l'article %s",
                        nom_article)) { /* cherche un message qui commence par
                                           "je souhaite l'article" et garde le
                                           reste dans une variable nom_article */
      struct IPC *art = ipc(nv, ncl, ARTICLE, nom_article, 0);
      int prix = -1;
      for (int i=0; i<3;i++) { /* va chercher le prix de l'article de
                                  nom donne dans le message */
        if (!strcmp (arts[i].nom, nom_article)) {
          prix = arts[i].prix;
          break;
        }
      }
      art->data.article.prix = prix; /* necessaire car ma fonction ipc ne permet
                                      pas de definir tous les parametres*/
      write (1, art, sizeof (struct IPC));
    }
    /* PROTOCOLE */

    else
      fprintf (stderr, "vendeur %s: Pardon?\n", nom); fflush(stderr);
  }
}


/* client */
int client (char *nom, int nproduit) {
  fprintf (stderr, "client %s instancié\n", nom); fflush(stderr);
  char prix_article[80];

  for (;;) {
    struct IPC buffer;
    read (0, &buffer, sizeof buffer); /* proces toujours a l'ecoute d'un
                                        message venant de main */

    if (buffer.type == FIN)     /* message FIN de main: => fin du processus */
      return (0);

    /* PROTOCOLE */
    else if (buffer.from == nv && !strcmp (buffer.data.message, BONJOUR))
      write (1, ipc(ncl, nv, MESSAGE, BONJOUR, 0), sizeof (struct IPC));

    else if (buffer.from == nv
             && !strcmp (buffer.data.message,
                         PLAISIR)) {
      char messg[40];
      sprintf(messg, "Je souhaite l'article %s", arts[nproduit].nom);
      write (1, ipc(ncl, nv, MESSAGE, messg, 0), sizeof (struct IPC));
    }

    else if (buffer.from == nv && buffer.type == ARTICLE) {
      fprintf (stderr,"%s: j'ai pris l'article %s\n",
               nom, buffer.data.article.nom);
      struct IPC *ipc_cliente = ipc(ncl, nca, ARTICLE,
                                    buffer.data.article.nom, 0);
      ipc_cliente->data.article.prix = buffer.data.article.prix;
      write (1, ipc_cliente, sizeof (struct IPC));
    }

    else if (buffer.from == nca
             && sscanf (buffer.data.message, "le prix de l'article est: %s",
                        prix_article)) {
      int total = atoi(prix_article);
      write (1, ipc(ncl, nca, PAIEMENT, 0, total), sizeof (struct IPC));
    }

    else if (buffer.from == nca && buffer.type == SAC)
      write (1, ipc(ncl, nca, MESSAGE, MERCI, 0),
             sizeof (struct IPC));
    /* PROTOCOLE */

    else
      fprintf (stderr, "cliente %s: Pardon?\n", nom); fflush(stderr);
  }
}

/* caissiere */
int caissiere (char *nom) {
  fprintf (stderr, "caissiere %s instancié\n", nom); fflush(stderr);
  article article_cliente;

  for (;;) {
    struct IPC buffer;
    read (0, &buffer, sizeof buffer); /* proces toujours a l'ecoute d'un
                                        message venant de main */

    if (buffer.type == FIN)     /* message FIN de main: => fin du processus */
      return (0);

    /* PROTOCOLE */
    else if (buffer.from == ncl && buffer.type == ARTICLE) {
      int total = buffer.data.article.prix;
      char mssg[80];
      article_cliente = buffer.data.article;
      sprintf(mssg, "le prix de l'article est: %d", total);
      write (1, ipc(nca, ncl, MESSAGE, mssg, 0), sizeof (struct IPC));
    }

    else if (buffer.from == ncl && buffer.type == PAIEMENT) {
      fprintf (stderr, "%s: j'encaisse un paiement de %d\n",
               nom, buffer.data.paiement); fflush(stderr);
      struct ticket ticket_cliente;
      ticket_cliente.addition = buffer.data.paiement;
      struct IPC *ipc_caissiere = ipc(nca, ncl, SAC, article_cliente.nom, 0);
      ipc_caissiere->data.sac.art.prix = article_cliente.prix;
      ipc_caissiere->data.sac.tck = ticket_cliente;
      write(1, ipc_caissiere, sizeof (struct IPC));
    }

    else if (buffer.from == ncl
             && !strcmp(buffer.data.message,MERCI))
      write (1, ipc(nca, ncl, MESSAGE, MERCI, 0),
             sizeof (struct IPC));
    /* PROTOCOLE */

    else
      fprintf (stderr, "caissière %s: Pardon?\n", nom); fflush(stderr);
  }
}



/* MAIN */

int main (int argc, char *argv[]) {

  if (argc == 1) {
    printf("usage: executable arg1 arg2 arg3 arg4\n"
           "arg1: vendeur\n"
           "    0: Jacques, 1: Pierre, 2: Paul\n"
           "arg2: cliente\n"
           "    3: Chloe, 4: Elise, 5: Lea\n"
           "arg3: caissiere\n"
           "    6: Lilou, 7: Laura, 8: Nadia\n"
           "arg4: produit\n"
           "    0: body, 1: brassiere, 2: pyjama\n");
    return(0);
  }
  else {
    int mfd;

    nv  = atoi(argv[1]);          /* premier argument: choix vendeur */
    ncl = atoi(argv[2]);          /* deuxieme argument: choix client */
    nca = atoi(argv[3]);          /* troisieme argument: choix caissiere */
    npr = atoi(argv[4]);          /* quatrieme argument: choix produit */

    int xfd[2];                   /* file descriptor du pipe processus -> main */
    pipe (xfd);                   /* creation du pipe processus -> main */

    /* fork des processus choisis */
    for (int i=0; i<9; ++i) {
      int afd[2];
      int pid;

      pipe(afd);                 /* creation du pipe main -> processus */
      if ((pid = fork())) {       /* fork des processus */
        a[i].fd  = afd[1];        /* affectation du file descriptor du processus */
        a[i].pid = pid;           /* affectation du pid */
        close(afd[0]);
      }
      else {                      /* quand on est dans le fork: */
        dup2(afd[0], 0);          /* pipe passe sur stdin */
        dup2(xfd[1], 1);          /* pipe vers main sur stdout */
        close(afd[1]);            /* fermeture des i/o non utilises */
        close(xfd[0]);
        switch (a[i].type) {      /* choix de fonction INIT selon type agent */
        case VENDEUR:
          if (i == nv)  return vendeur(a[i].nom);
          else          return 0; /* si l'agent n'a pas ete choisit dans
                                     les parametres, on termine le processus */
        case CLIENT:
          if (i == ncl) return client(a[i].nom, npr);
          else          return 0; /* idem */
        case CAISSIERE:
          if (i == nca) return caissiere(a[i].nom);
          else          return 0; /* idem */
        }
      }
    }

    setbuf(stdout, NULL);

    close(xfd[1]);                /* on ferme le write du pipe processus->main
                                     pour main */
    mfd = xfd[0];                 /* definition de mfd: main file descriptor */

    /* message de debut pour vendeur */
    struct IPC debut;
    debut.type = DEBUT;
    write (a[nv].fd , &debut, sizeof (struct IPC));

    /* boucle principale: transport de messages entre processus */
    for (;;) {
      struct IPC buffer;
      read (mfd, &buffer, sizeof buffer); /* attend un message dans le pipe
                                             processus -> main */

      if (buffer.from == nca
          && !strcmp(buffer.data.message,MERCI)) { /* message de FIN => termine
                                                      tous les autres processus */
        printf ("%s a dit à %s: %s\n",
                a[buffer.from].nom,
                a[buffer.to].nom,
                buffer.data.message); fflush(stdout);
        struct IPC fin;
        fin.type = FIN;
        write(a[nv].fd , &fin, sizeof (struct IPC));
        write(a[ncl].fd, &fin, sizeof (struct IPC));
        write(a[nca].fd, &fin, sizeof (struct IPC));
        return(0);
      }
      else if (buffer.to == nca) { /* message pour caissiere => transmet a
                                      caissiere et fait print de l'action */
        print_ipc(buffer);      /* print selon le type de message, fonction
                                 definie avant le main*/
        write (a[nca].fd, &buffer, sizeof (struct IPC));
      }
      else if (buffer.to == ncl) { /* message pour cliente => transmet a cliente
                                      et fait print de l'action */
        print_ipc(buffer);      /* idem */
        write (a[ncl].fd, &buffer, sizeof (struct IPC));
      }
      else if (buffer.to == nv) { /* message pour vendeur => transmet a vendeur
                                     et fait print de l'action */
        print_ipc(buffer);      /* idem */
        write (a[nv].fd, &buffer, sizeof (struct IPC));
      }
      else {                      /* n'arrive jamais: au cas ou probleme */
        printf("problème"); fflush(stdout);
      }
    }
  }
}
