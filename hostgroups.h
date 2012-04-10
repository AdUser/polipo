/* Written by Alex 'AdUser' Z <ad_user@mail.ru> */

typedef struct _Hostgroup {
  char *pattern; /* grouping patern {substring, regex}*/
  regex_t regex; /* can be NULL */
  char groupname[1];
} HostgroupRec, *HostgroupPtr;

typedef struct _HostgroupList {
  unsigned int length;
  unsigned int size;
  HostgroupPtr *list
} HostgroupListRec, *HostgroupListPtr;
