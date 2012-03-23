/* Written by Alex 'AdUser' Z <ad_user@mail.ru> */

#include "polipo.h"

AtomPtr configFile = NULL;
AtomPtr diskCacheRoot = NULL;
AtomPtr outputDir = NULL;

enum { quiet, normal, extra } verbosity = normal;
enum msglevel { error, warn, info, debug };
int daemonise = 0; /* unused var */

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

int
cache_walk(AtomPtr diskCacheRoot)
  {
    DIR *dir;
    struct dirent *dirent;

    FTS *fts;
    FTSENT *ftsent;
    char *fts_argv[2];

    char buf[BUFSIZE];
    unsigned long int i = 0;

    DiskObjectPtr dobjects = NULL;

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

    if (dobjects)
      {
        /* blah */
      }

    return 0;
  }

int main(int argc, char **argv)
  {
    char opt = 0;
    int rc = 0;
    initAtoms();
    preinitDiskcache();

    if (argc < 2)
      usage(EXIT_FAILURE);

    while ((opt = getopt(argc, argv, "hqvc:r:O:")) != -1)
      {
        switch (opt)
          {
            case 'q' : verbosity = quiet; break;
            case 'v' : verbosity = extra; break;
            case 'c' :
              if (configFile)
                releaseAtom(configFile);
              configFile = internAtom(optarg);
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
            case 'h' : usage(EXIT_SUCCESS); break;
            default  : usage(EXIT_FAILURE); break;
          }
      }

    rc = parseConfigFile(configFile);

    cache_walk(diskCacheRoot);

    exit(EXIT_SUCCESS);
  }

/*
  vim: ts=2:expandtab:autoindent:sw=79:syntax=c
 */
