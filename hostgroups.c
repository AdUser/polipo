/* Written by Alex 'AdUser' Z <ad_user@mail.ru> */
/* based on forbidden.[ch] */
#include "polipo.h"

#include <ctype.h>

AtomPtr hostgroupsFile = NULL;

HostgroupPtr hostgroup;
HostgroupListRec hostgroups = { .list = NULL } ;

void preinitHostgroups(void)
  {
    CONFIG_VARIABLE_SETTABLE(hostgroupsFile, CONFIG_ATOM, configAtomSetter,
                             "File specifying groups of hosts.");
  }

int 
hostgroupListCons(HostgroupPtr hg, HostgroupListPtr list)
  {
    HostgroupPtr *new_list = NULL;
    const int s = sizeof(HostgroupPtr);

    if (list->list == NULL)
      {
        list->size = 0;
        list->length = 0;
        if ((list->list = calloc(5, sizeof(HostgroupPtr))) == NULL)
          {
            do_log(L_ERROR, "Can't allocate hostgroup list");
            return -1;
          }
        list->size = 5;
      }

    if (list->size == list->length + 1)
      {
        new_list = realloc(list->list, (list->size + 5) * s);
        if (new_list == NULL)
          {
            do_log(L_ERROR, "Can't realloc hostgroup list.\n");
            return -1;
          }
        memset((HostgroupPtr *)new_list + 5, '\0', 5 * s);
        list->list = new_list;
        list->size += 5;
      }

    list->list[list->length] = hg;
    list->length += 1;

    return 1;
  }

int readHostgroupsFile(void)
  {
    FILE *file;
    char buf[1024];
    char *s, *e; /* start, end */
    size_t len = 0;
    HostgroupPtr hg = NULL;

    s = atomString(hostgroupsFile);

    if ((file = fopen(s, "r")) == NULL)
      {
        do_log_error(L_ERROR, errno, "Couldn't open file %s", s);
        return -1;
      }

    buf[1023] = '\0';
    while (fgets(buf, 1023, file) != NULL)
      {
        /* buf: " \t  hostname  : regex # comment \r" */
        s = buf;
        while (isblank(*s)) s++;
        for (e = s; *e != '\0'; e++)
          if (*e == '#' || *e == '\n' || *e == '\r')
            {
              *e = '\0';
              break;
            }

        /* empty string */
        if (buf[0] == '\0')
          continue;

        /* buf: " \t  hostname  : regex.# comment \r"; '.' is '\0' */
        if ((e = strchr(s, ':')) == NULL)
          {
            do_log_error(L_ERROR, ESYNTAX, " in file hostgroups: %s\n", s);
            continue;
          }

        for (e--; isspace(*e) && e > s; e--);
        e++;
        /* buf: " \t  hostname  : regex.# comment \r" */
        /*           s^      e^          */
        *e = '\0';
        for (e++; isspace(*e) || *e == ':'; e++);
        /* buf: " \t  hostname. : regex.# comment \r" */
        /*           s^          e^      */

        len = strlen(e);
        if ((hg = malloc(sizeof(HostgroupRec) + len)) == NULL)
          {
            do_log(L_ERROR, "Can't allocate hostgroup item.\n");
            continue;
          }
        *(hg->pattern + len) = '\0';
        /* {[ptr:ptr:g]roupname.} */

        hg->groupname = internAtom(s);
        strncpy(hg->pattern, e, len);

        if (hostgroupListCons(hg, &hostgroups) == -1)
          free(hg);
        /* continue */
      }

    fclose(file);
    return 1;
  }

/* dumb version. if string contains *
 * only a-z, A-Z chars, '.' and '-' *
 * consider it as not regex         */
int
isRegex(char *str)
  {
    char *p;

    if (str == NULL)
      return 0;

    for (p = str; *p != '\0'; p++)
      if (!(isalnum(*p) || *p == '-' || *p == '.'))
        return 1;

    return 0;
  }

void parseHostgroupsFile(void)
  {
    HostgroupPtr hg;
    int rc = 0;
    unsigned int i = 0;
    char err_buf[512];
    regex_t *re = NULL;

    readHostgroupsFile();

    for (i = 0; hostgroups.list[i] != NULL; i++)
      {
        hg = hostgroups.list[i];
        if (isRegex(hg->pattern) == 0)
          continue;

        if ((re = malloc(sizeof(regex_t))) == NULL)
          {
            do_log(L_ERROR, "Can't allocate regex.\n");
            continue;
          }

        if ((rc = regcomp(re, hg->pattern, REG_NOSUB | REG_ICASE)) != 0)
          {
            regerror(rc, hg->regex, err_buf, 512);
            do_log_error(L_ERROR, ESYNTAX, " regex compilation failed: %s.\n", err_buf);
            /* exit */
          }

        hg->regex = re;
       }
  } 

void initHostgroups(void)
  {
    if (hostgroupsFile)
      hostgroupsFile = expandTilde(hostgroupsFile);

    if (hostgroupsFile == NULL)
      {
        hostgroupsFile = expandTilde(internAtom("~/.polipo-hostgroups"));
        if (hostgroupsFile && access(hostgroupsFile->string, F_OK) < 0)
          {
            releaseAtom(hostgroupsFile);
            hostgroupsFile = NULL;
          }
      } 

    if (hostgroupsFile == NULL)
      {
        hostgroupsFile = internAtom("/etc/polipo/hostgroups");
        if (access(hostgroupsFile->string, F_OK) < 0)
          {
            releaseAtom(hostgroupsFile);
            hostgroupsFile = NULL;
          }
      }

    if (hostgroupsFile == NULL)
      return;

    parseHostgroupsFile();

  }

HostgroupPtr
hostgroupFind(char *hostname)
  {
    HostgroupPtr hg = NULL;
    unsigned int i = 0;

    for (i = 0; (hg = hostgroups.list[i]) != 0; i++)
       {
        if (hg->regex == NULL && strstr(hostname, hg->pattern) != NULL)
          return hg;
        else if (hg->regex != NULL && regexec(hg->regex, hostname, 0, NULL, 0) == 0)
          return hg;
      }

    return NULL;
  }

void
hostnameMangle(char *buf, int n)
  {
    HostgroupPtr hg = NULL;
    unsigned int i = 0;

    for (i = 0; (hg = hostgroups.list[i]) != 0; i++)
      {
        if (hg->regex == NULL && strstr(buf, hg->pattern) != NULL)
          strncpy(buf, atomString(hg->groupname), n);
        else if (hg->regex != NULL && regexec(hg->regex, buf, 0, NULL, 0) == 0)
          strncpy(buf, atomString(hg->groupname), n);
      }
  }
