/* Written by Alex 'AdUser' Z <ad_user@mail.ru> */

#include "polipo.h"

AtomPtr configFile = NULL;
AtomPtr diskCacheRoot = NULL;
AtomPtr outputDir = NULL;

enum { quiet, normal, extra } verbosity = normal;
enum msglevel { error, warn, info, debug };
int daemonise = 0; /* unused var */

void
usage(int exitcode)
  {
    fprintf(stderr, "\
  -h        This help.\n\
  -q        Show only errors.\n\
  -v        Show extra messages.\n\
  -c <file> Config file of polipo. (default: try known locations)\n\
  -r <dir>  Cache root. (default: try to detect from config file)\n\
  -O <dir>  Output directory.\n\
");
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
        fprintf(stderr, format, ap);
        va_end(ap);
      }

    if (level <= error)
      exit(EXIT_FAILURE);
  }

int main(int argc, char **argv)
  {
    char opt = 0;
    int rc = 0;
    initAtoms();
    preinitDiskcache();

    while ((opt = getopt(argc, argv, "qvr:O:")) != -1)
      {
        switch (opt)
          {
            case 'q' : verbosity = quiet; break;
            case 'v' : verbosity = extra; break;
            case 'c' :
              if (configFile)
                releaseAtom(configFile);
              configFile = internAtom(optarg);
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

    exit(EXIT_SUCCESS);
  }

/*
  vim: ts=2:expandtab:autoindent:sw=79:syntax=c
 */
