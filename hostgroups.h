/* Written by Alex 'AdUser' Z <ad_user@mail.ru> */

#include <regex.h>

typedef struct _Hostgroup {
  AtomPtr groupname;
  regex_t *regex; /* can be NULL */
  char pattern[1]; /* matching patern {substring, regex} */
} HostgroupRec, *HostgroupPtr;

typedef struct _HostgroupList {
  unsigned int length;
  unsigned int size;
  HostgroupPtr *list;
} HostgroupListRec, *HostgroupListPtr;

void preinitHostgroups(void);
void initHostgroups(void);
int   readHostgroupsFile(void);
void parseHostgroupsFile(void);
HostgroupPtr hostgroupFind(char *);
void hostnameMangle(char *, int *, char *, int);
