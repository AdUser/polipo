/* Written by Alex 'AdUser' Z <ad_user@mail.ru> */

#include "polipo.h"

AtomPtr configFile = NULL;
AtomPtr diskCacheRoot = NULL;
AtomPtr outputDir = NULL;

/**
 * filter by size  | min | max
 * ----------------+-----+-----
 * unset           |  0  |  0
 * lower X bytes   |  0  |  X
 * exact X bytes   |  X  |  X
 * greater X bytes |  X  |  0
 **/
typedef struct _DiskObjectFilter
  {
    uint32_t size_min;
    uint32_t size_max;
    AtomPtr hosts;
    AtomPtr paths;
    AtomPtr ctypes;
  }
DiskObjectFilter;

enum { quiet, normal, extra, all } verbosity = normal;
enum msglevel { error, warn, status, info, debug };
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

    if ((verbosity == quiet  && level <= error)  || \
        (verbosity == normal && level <= status) || \
        (verbosity == extra  && level <= info)   || \
        (verbosity == all    && level <= debug))
      {
        va_start(ap, format);
        vfprintf(stderr, format, ap);
        va_end(ap);
      }

    if (level <= error)
      exit(EXIT_FAILURE);
  }

#define OVERFLOW(size, m) \
  if (((uint32_t) ~0 / (m)) <= (size)) \
  { \
      fprintf(stderr, "Defined filter size too large.\n"); \
      exit(EXIT_FAILURE); \
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
              OVERFLOW(size, 10)
              size *= 10;
              size += *p - '0';
              break;
            case 'G':
            case 'g':
              OVERFLOW(size, 1024)
              size *= 1024;
            case 'M':
            case 'm':
              OVERFLOW(size, 1024)
              size *= 1024;
            case 'K':
            case 'k':
              OVERFLOW(size, 1024)
              size *= 1024;
            case 'B':
            case 'b':
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
              filter->size_min = 1;
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

/**
 * return value: 1 - all ok, 0 - nothing found, -1 - error
 **/
int
getFilename(char *buf, int buf_len, char *url)
  {
    char *p = NULL;

    if (url == NULL) return -1;

    if ((p = strrchr(url, '/')) != NULL)
      {
        p += 1;
        strncpy(buf, p, buf_len);
        buf[buf_len - 1] = '\0';
        return 1;
      }

    return 0;
  }

/**
 * return value: 1 - all ok, 0 - nothing found, -1 - error
 **/
int
getHostname(char *buf, int buf_len, char *url)
  {
    char *start = NULL;
    char *end = NULL;
    char *p = NULL;

    if (url == NULL) return -1;

    if ((p = strstr(url, "://")) != NULL)
      start = p + 3;
    else
      start = url; /* ok, nice try */

    end = start;

    /* exclude hostname[/path/to/...] */
    if ((p = strchr(start, '/')) != NULL && p != start)
      end = p;

    /* exclude [username:password@]hostname */
    if ((p = strchr(start, '@')) != NULL && (p >= start && p <= end))
      start = p + 1;

    /* exclude hostname[:port] */
    if ((p = strchr(start, ':')) != NULL && (p >= start && p <= end))
      end = p;

    strncpy(buf, start, end - start);
    buf[end - start] = '\0';

    return 1;
  }

/**
 * return value: 1 - all ok, 0 - nothing found, -1 - error
 **/
int
getPath(char *buf, int buf_len, char *url)
  {
    char *start = NULL;
    char *p = NULL;

    if (url == NULL) return -1;

    start = ((p = strstr(url, "://")) == NULL) ? url : (p + 3);

    start = ((p = strchr(start, '/')) == NULL) ? start : p;

    strncpy(buf, start, buf_len);
    buf[buf_len] = '\0';

    if (url == start)
      return 0;

    return 1;
  }

/**
  * write file from cache to output directory
  * return value: 1 - all ok, 0 - error, can continue, -1 - fatal error
  **/
int
extractFile(DiskObjectPtr dobject)
  {
    char hostname[BUFSIZE];
    char filename[BUFSIZE];
    char buf[BUFSIZE * 2];
    const size_t read_buf_size = 1024 * 64;
    uint8_t read_buf[read_buf_size];
    int rc = 0;
    int fd_in, fd_out;
    ssize_t bytes_read = 0;

    if (getHostname(hostname, BUFSIZE, dobject->location) < 0)
      return -1;

    if (getFilename(filename, BUFSIZE, dobject->location) < 0)
      return -1;

    /* handles "filename too long" case */
    if (strlen(filename) > NAME_MAX)
      {
        msg(warn, "Filename extracted from url exeeds NAME_MAX limit. "
                  "Will use generated filename.\n");
        if (getFilename(filename, BUFSIZE, dobject->filename) < 0)
          return -1;
      }

    /* make output directory */
    snprintf(buf, BUFSIZE * 2, "%s/%s",
             atomString(outputDir), hostname);

    rc = mkdir(buf, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    if (rc != 0 && errno != EEXIST)
      msg(error, "%s\n", strerror(errno));

    /* make output file */
    snprintf(buf, BUFSIZE * 2, "%s/%s/%s",
             atomString(outputDir), hostname, filename);

    errno = 0;
    if ((fd_out = open(buf, O_WRONLY | O_CREAT | O_EXCL | O_BINARY,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)) < 0)
      switch (errno)
        {
          case EEXIST :
            msg(info, "File exists, skipped.\n");
            return 1;
            break;
          default :
            msg(warn, "%s\n", strerror(errno));
            return 0;
            break;
        }

    if ((fd_in = open(dobject->filename, O_RDONLY | O_BINARY)) < 0)
      msg(warn, strerror(errno));

    if (lseek(fd_in, dobject->body_offset, SEEK_SET) < 0)
      {
        msg(warn, "%s\n", strerror(errno));
        if (fd_out)
          close(fd_out);
        return 0;
      }

    while ((bytes_read = read(fd_in, read_buf, read_buf_size)) > 0)
      if (write(fd_out, read_buf, ((bytes_read == read_buf_size) ? read_buf_size : bytes_read)) != bytes_read)
        msg(warn, "%s\n", strerror(errno));

    if (bytes_read < 0)
      {
        msg(warn, strerror(errno));
        return 0;
      }

    close(fd_in);
    close(fd_out);

    return 1;
  }

int
matchByHostname(DiskObjectFilter *filter, char *location)
  {
    AtomPtr atom;
    char hostname[BUFSIZE];

    if (getHostname(hostname, BUFSIZE, location) == -1)
      {
        msg(warn, "BUFSIZE too small to fit hostname.\n");
        return -1;
      }

    for (atom = filter->hosts; atom != NULL; atom = atom->next)
      if (strstr(hostname, atomString(atom)) != NULL)
        return 1;

    return 0;
  }

int
matchByPath(DiskObjectFilter *filter, char *location)
  {
    char path[BUFSIZE];
    AtomPtr atom;

    if (getPath(path, BUFSIZE, location) != 1)
      return -1;

    for (atom = filter->paths; atom != NULL; atom = atom->next)
      if (strstr(path, atomString(atom)) != NULL)
        return 1;

    return 0;
  }

int
cache_walk(AtomPtr diskCacheRoot)
  {
    FTS *fts;
    FTSENT *ftsent;
    char *fts_argv[2];

    struct stat *st = NULL;
    char buf[BUFSIZE];
    unsigned long int i = 0, isdir;
    unsigned long int matched = 0;
    unsigned long int obj_found = 0;
    unsigned long int obj_match = 0;

    DiskObjectPtr dobjects = NULL;
    DiskObjectPtr dobject  = NULL;

    if(diskCacheRoot == NULL || diskCacheRoot->length <= 0)
      msg(error, "Can't find cache root. Try to specify it manually. ('-r' option)\n");

    if (diskCacheRoot->length >= BUFSIZE)
      msg(error, "Path to cache root too long for me, increase BUFSIZE.\n");

    memcpy(buf, diskCacheRoot->string, diskCacheRoot->length);
    buf[diskCacheRoot->length] = '\0';

    fts_argv[0] = buf;
    fts_argv[1] = NULL;
    fts = fts_open(fts_argv, FTS_LOGICAL, NULL);
    if (fts)
      {
        msg(status, "Reading cache...\n");
        while ((ftsent = fts_read(fts)) != NULL)
          if (ftsent->fts_info != FTS_DP)
            {
              if (ftsent->fts_info == FTS_F)
                obj_found++;

              /* do anything possible to reduce number *
               * of objects before reading them        */
              if (filter.hosts != NULL &&
                  matchByHostname(&filter, ftsent->fts_path + diskCacheRoot->length) != 1)
                {
                  msg(debug, "Not matched by any hostname filter.\n");
                  continue;
                }
              st = (ftsent->fts_info == FTS_NS ||
                    ftsent->fts_info == FTS_NSOK) ?
                    NULL : ftsent->fts_statp;
              /* we can do this, because size(diskobject) > size(body) */
              if (filter.size_min != 0 && st != NULL &&
                  filter.size_min > st->st_size)
                {
                  msg(debug, "Not matched by minimal object size.\n");
                  continue;
                }
              /* maybe in next line is a bug with "st" */
              obj_match++;
              dobjects = processObject(dobjects, ftsent->fts_path, st);
            }

        fts_close(fts);
        msg(status, "\r... done. %lu objects found (%lu filtered).\n",
                  obj_found, obj_found - obj_match);
      }

    if (stat(atomString(outputDir), st) != 0)
       msg(error, "%s: %s\n", atomString(outputDir), strerror(errno));

    for (dobject = dobjects; dobject != NULL; dobject = dobject->next)
      {
        i = strlen(dobject->location);
        isdir = (i == 0 || dobject->location[i - 1] == '/');
        if (isdir)
          continue;
          msg(debug, "Analyzing: '%s'.\n", dobject->location);
        if (filter.size_max != 0 &&
            filter.size_max < dobject->size)
          {
            msg(debug, "Not matched by maximum object size.\n");
            continue;
          }
        if (filter.paths != NULL)
          if (matchByPath(&filter, dobject->location) != 1)
            {
              msg(debug, "Not matched by any path filter.\n");
              continue;
            }
        msg(info, "Matched: %s\n", dobject->location);
        matched++;

        extractFile(dobject);
      }

    msg(status, "Total matched: %lu objects.\n", matched);

    return 0;
  }

int main(int argc, char **argv)
  {
    char opt = 0;
    int rc = 0;

    filter.size_min =  0;
    filter.size_max =  0;

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
            case 'q' : if (verbosity > quiet) verbosity--; break;
            case 'v' : if (verbosity < all)   verbosity++; break;
            case 'c' :
              if (configFile)
                releaseAtom(configFile);
              if ((rc = parseConfigFile(internAtom(optarg))) != 1)
                msg(error, "Config file parsing failed.\n");
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

    if (outputDir == NULL)
      {
        msg(warn, "Output directory not set, assuming current dir.\n");
        outputDir = internAtom("./");
      }

    if (filter.size_min != 0 || filter.size_max != 0)
      msg(info, "Filter objects by size: %lu - %lu bytes\n", \
                filter.size_min, filter.size_max);

    diskCacheRoot = expandTilde(diskCacheRoot);
    cache_walk(diskCacheRoot);

    exit(EXIT_SUCCESS);
  }

/*
  vim: ts=2:expandtab:autoindent:sw=79:syntax=c:foldmethod=syntax
 */
