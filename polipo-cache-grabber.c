/* Written by Alex 'AdUser' Z <ad_user@mail.ru> */

#include "polipo.h"

AtomPtr configFile = NULL;
AtomPtr diskCacheRoot = NULL;
AtomPtr outputDir = NULL;

typedef struct _DiskObjectFilter
  {
    uint32_t size_min;
    uint32_t size_max;
    AtomPtr hosts;
    AtomPtr paths;
    AtomPtr ctypes;
  }
DiskObjectFilter;

enum { quiet, normal, extra } verbosity = normal;
enum msglevel { error, warn, info, debug };
int daemonise = 0; /* unused var */

DiskObjectFilter filter;

#define BUFSIZE 4096

void
usage(int exitcode)
  {
    fprintf(stderr, "\
Usage: polipo-cache-grabber <options>\n\
\n\
  -h        This help.\n\
  -q        Show only errors.\n\
  -v        Show extra messages.\n\
  -c <file> Config file of polipo. (default: try known locations)\n\
  -r <dir>  Cache root. (default: try to detect from config file)\n\
  -O <dir>  Output directory.\n\
  -F type:<string>  Extract only files conforming some conditions.\n\
Filter types:\n\
    * host  Match against hostname. No regexps, only substring please.\n\
    * path  Match against path to file.\n\
    * size  Match file size. Modifiers:\n\
              + (equal or greater),\n\
              - (equal or smaller),\n\
              = (exact size) (default, can be omitted)\n\
");
    exit(exitcode);
  }

void
msg(enum msglevel level, char *format, ...)
  {
    va_list ap;

    if ((verbosity == quiet  && level <= error) || \
        (verbosity == normal && level <= info)  || \
        (verbosity == extra  && level <= debug))
      {
        va_start(ap, format);
        vfprintf(stderr, format, ap);
        va_end(ap);
      }

    if (level <= error)
      exit(EXIT_FAILURE);
  }

uint32_t
parse_size(char *str)
  {
    char c = '\0';
    char *p = str;
    uint32_t size = 0;

    if (str == NULL) return 0;

    for (p = str; *p != '\0'; p++)
      {
        c = (*p >= '0' && *p <= '9') ? '0' : *p ;
        switch (c)
          {
            case '0':
              size *= 10;
              size += *p - '0';
              break;
            case 'G':
            case 'g':
              size *= 1024;
            case 'M':
            case 'm':
              size *= 1024;
            case 'K':
            case 'k':
              size *= 1024;
            case 'B':
            case 'b':
              size *= 1;
              return size; /* after first non-digit char */
              break;
            default :
              break;
          }
      }

    return size; /* if value looks like '32456' */
  }

#define ADD_FILTER(cmp,cmplen,ptr) \
  else if (strncmp((str), (cmp), (cmplen)) == 0) \
    { \
      t = (ptr); \
      (ptr) = internAtom(str + (cmplen)); \
      (ptr)->next = t; \
    }

void
parse_filter_type(DiskObjectFilter *filter, char *str)
  {
    AtomPtr t = NULL;
    uint32_t size = 0;

    if (strncmp(str, "size:", 5) == 0)
      {
        switch (*(str + 5))
          {
            case '+':
              size = parse_size(str + 5 + 1);
              filter->size_min = size;
              filter->size_max = -1;
              break;
            case '-':
              size = parse_size(str + 5 + 1);
              filter->size_min = 0;
              filter->size_max = size;
              break;
            case '=':
              size = parse_size(str + 5 + 1);
              filter->size_min = size;
              filter->size_max = size;
              break;
            default :
              size = parse_size(str + 5);
              filter->size_min = size;
              filter->size_max = size;
              break;
          }
      }
    ADD_FILTER("path:", 5, filter->paths)
    ADD_FILTER("host:", 5, filter->hosts)
    ADD_FILTER("ctype:", 6, filter->ctypes)
    else
      {
        fprintf(stderr, "Unknown filter type: %s.\n", str);
        exit(EXIT_FAILURE);
      }
  }

int
cache_walk(AtomPtr diskCacheRoot)
  {
    DIR *dir;
    struct dirent *dirent;

    FTS *fts;
    FTSENT *ftsent;
    char *fts_argv[2];

    char buf[BUFSIZE];
    unsigned long int i = 0, isdir;

    DiskObjectPtr dobjects = NULL;
    DiskObjectPtr dobject  = NULL;

    if(diskCacheRoot == NULL || diskCacheRoot->length <= 0)
      msg(error, "Can't find cache root. Try to specify it manually. ('-r' option)\n");

    if (diskCacheRoot->length >= BUFSIZE)
      msg(error, "Path to cache root too long for me, increase BUFSIZE.\n");

    memcpy(buf, diskCacheRoot->string, diskCacheRoot->length);
    buf[diskCacheRoot->length] = '\0';

    i = 0;
    dir = NULL;
    fts_argv[0] = buf;
    fts_argv[1] = NULL;
    fts = fts_open(fts_argv, FTS_LOGICAL, NULL);
    if (fts)
      {
        msg(info, "Reading cache...");
        while ((ftsent = fts_read(fts)) != NULL)
          if (ftsent->fts_info != FTS_DP)
            {
              dobjects = processObject(dobjects,
                ftsent->fts_path,
                ftsent->fts_info == FTS_NS ||
                ftsent->fts_info == FTS_NSOK ?
                ftsent->fts_statp : NULL);

              if (ftsent->fts_info == FTS_F)
                i++;
            }

        fts_close(fts);
        msg(info, " done. %lu objects found.\n", i);
      }

    for (dobject = dobjects; dobject != NULL; dobject = dobject->next)
      {
        i = strlen(dobject->location);
        isdir = (i == 0 || dobject->location[i - 1] == '/');
        if (!isdir)
          fprintf(stdout, "%s\n", dobject->filename);
        /* more filters here */
      }

    return 0;
  }

int main(int argc, char **argv)
  {
    char opt = 0;
    int rc = 0;

    filter.size_min =  0;
    filter.size_max = -1;

    initAtoms();

    preinitChunks();
    preinitHttp();
    preinitDiskcache();

    if (argc < 2)
      usage(EXIT_FAILURE);

    while ((opt = getopt(argc, argv, "hqvc:r:O:F:")) != -1)
      {
        switch (opt)
          {
            case 'q' : verbosity = quiet; break;
            case 'v' : verbosity = extra; break;
            case 'c' :
              if (configFile)
                releaseAtom(configFile);
              rc = parseConfigFile(internAtom(optarg));
              break;
            case 'r' :
              if (diskCacheRoot)
                releaseAtom(diskCacheRoot);
              diskCacheRoot = internAtom(optarg);
              break;
            case 'O' :
              if (outputDir)
                releaseAtom(outputDir);
              outputDir = internAtom(optarg);
              break;
            case 'F' :
              parse_filter_type(&filter, optarg);
              break;
            case 'h' : usage(EXIT_SUCCESS); break;
            default  : usage(EXIT_FAILURE); break;
          }
      }

    initChunks();
    initHttp();
/*  initDiskcache(); */

    diskCacheRoot = expandTilde(diskCacheRoot);
    cache_walk(diskCacheRoot);

    exit(EXIT_SUCCESS);
  }

/*
  vim: ts=2:expandtab:autoindent:sw=79:syntax=c
 */
