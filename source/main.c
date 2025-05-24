#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sys/mman.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef RUNSHSTR
#define RUNSHSTR "mkdir build\nmkdir install1\ncontrib/download_prerequisites\ncd build\n../configure --prefix=\"$(realpath ../install1)\"\nmake\nmake install"
#endif

static int (*fmkdir) (const char *, int) = (int (*)(const char *, int)) mkdir;

static void
mkdir_p (const char *path)
{
  char *tmp;
  char *p;
  tmp = strdup (path);
  for (p = tmp + 1; *p; p++)
    {
      if (*p == '/')
	{
	  *p = 0;
	  fmkdir (tmp, 0755);
	  *p = '/';
	}
    }
  fmkdir (tmp, 0755);
  free (tmp);
}

static void
copy_dir (const char *src, const char *dst, bool enable_in_runconfig,
	  int in_runconfig)
{
  DIR *dir;
  struct dirent *entry;
  char *end;
  int num;
  size_t src_len;
  size_t dst_len;
  char *src_path;
  char *dst_path;
  int new_rc;
  DIR *dirp;
  int src_fd;
  int dst_fd;
  struct stat st;
  char *buf;
  ssize_t bytes;
  dir = opendir (src);
  if (!dir)
    return;
  while ((entry = readdir (dir)))
    {
      if (!strcmp (entry->d_name, ".") || !strcmp (entry->d_name, ".."))
	continue;
      if (in_runconfig)
	{
	  num = strtol (entry->d_name, &end, 10);
	  if (num >= 1 && num <= 7)
	    {
	      continue;
	    }
	}
      src_len = strlen (src) + strlen (entry->d_name) + 2;
      dst_len = strlen (dst) + strlen (entry->d_name) + 2;
      src_path = (char *) malloc (src_len);
      dst_path = (char *) malloc (dst_len);
      snprintf (src_path, src_len, "%s/%s", src, entry->d_name);
      snprintf (dst_path, dst_len, "%s/%s", dst, entry->d_name);
      dirp = opendir (src_path);
      if (dirp)
	{
	  closedir (dirp);
	  new_rc = 0;
	  if (enable_in_runconfig && !in_runconfig
	      && !strcmp (entry->d_name, "runconfig"))
	    {
	      new_rc = 1;
	    }
	  fmkdir (dst_path, 0755);
	  copy_dir (src_path, dst_path, false, new_rc);
	}
      else
	{
	  src_fd = open (src_path, O_RDONLY | O_BINARY);
	  if (src_fd != -1)
	    {
	      fstat (src_fd, &st);
	      dst_fd =
		open (dst_path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY,
		      st.st_mode);
	      if (dst_fd != -1)
		{
		  buf = (char *) malloc (4096);
		  while ((bytes = read (src_fd, buf, 4096)) > 0)
		    {
		      write (dst_fd, buf, bytes);
		    }
		  free (buf);
		  close (dst_fd);
		}
	      close (src_fd);
	    }
	}
      free (src_path);
      free (dst_path);
    }
  closedir (dir);
}

static void
write_file (const char *path, const char *content)
{
  char *dir;
  char *last;
  int fd;
  dir = strdup (path);
  last = strrchr (dir, '/');
  if (last)
    {
      *last = 0;
      mkdir_p (dir);
    }
  free (dir);
  fd = open (path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
  if (fd != -1)
    {
      write (fd, content, strlen (content));
      close (fd);
    }
}

static void
update_config (const char *path)
{
  char *dir;
  char *last;
  int fd;
  struct stat st;
  size_t size;
  char *map;
  bool found;
  const char *flag;
  size_t i;
  dir = strdup (path);
  last = strrchr (dir, '/');
  if (last)
    {
      *last = 0;
      mkdir_p (dir);
    }
  free (dir);
  fd = open (path, O_RDWR | O_CREAT | O_APPEND | O_BINARY, 0644);
  if (fd == -1)
    {
      return;
    }
  fstat (fd, &st);
  size = st.st_size;
  map = mmap (0, size, PROT_READ, MAP_SHARED, fd, 0);
  found = false;
  flag = "--disable-multilib";
  if (map != MAP_FAILED)
    {
      madvise (map, size, MADV_WILLNEED);
      for (i = 0; i < size; i++)
	{
	  if (i + strlen (flag) > size)
	    break;
	  if (!strncmp (&map[i], flag, strlen (flag)))
	    {
	      found = true;
	      break;
	    }
	}
      munmap (map, size);
    }
  if (!found)
    {
      write (fd, "\n", 1);
      write (fd, flag, strlen (flag));
    }
  close (fd);
}

int
main (int argc, char **argv, char *envp[])
{
  struct stat st;
  Dl_info dlInfo;
  const char *installDirLocStr = "/installDir.dir";
  const size_t installDirLocStrSize = strlen (installDirLocStr);
  const char *agLocStr = "/a-g";
  const size_t agLocStrSize = strlen (agLocStr);
  int installDirFile;
  size_t installDirNameSize;
  char *installDir;
  size_t i, j, k;
  pid_t spid;
  char *sargv[4];
  siginfo_t ssiginfo;
  char *startCommandStr;
  size_t startCommandStrSize;
  const char *configurationLoc = "etc/a-g/";
  const size_t configurationLocSize = strlen (configurationLoc);
  const char *defaulticLoc = "default/";
  const size_t defaulticLocSize = strlen (defaulticLoc);
  const char *runshStr = "" RUNSHSTR;
  char *dPath;
  size_t dPathSize;
  char *packageManager;
  bool packageManagerCanFree;
  installDirNameSize = 0;
  dlInfo.dli_fname = 0;
  dlInfo.dli_fbase = 0;
  dlInfo.dli_sname = 0;
  dlInfo.dli_saddr = 0;
  dladdr (&main, &dlInfo);
  for (j = i = 0; dlInfo.dli_fname[i]; i++)
    {
      k = dlInfo.dli_fname[i] == '/' || dlInfo.dli_fname[i] == '\\';
      k--;
      installDirNameSize = (i & (0 - k - 1)) + (installDirNameSize & k);
    }
  installDir =
    (char *) malloc (installDirNameSize + installDirLocStrSize + 1);
  for (i = 0; i != installDirNameSize; i++)
    {
      installDir[i] = dlInfo.dli_fname[i];
      j = installDir[i] == '\\';
      installDir[i] = (installDir[i] & (j - 1)) + ('/' & (0 - j));
    }
  memcpy (installDir + installDirNameSize, agLocStr, agLocStrSize);
  installDir[installDirNameSize + agLocStrSize] = 0;
  if (installDirNameSize)
    {
      packageManager = strdup (installDir);
      packageManagerCanFree = packageManager != 0;
    }
  else
    {
      packageManager = strrchr (agLocStr, '/') + 1;
      packageManagerCanFree = false;
    }
  memcpy (installDir + installDirNameSize, installDirLocStr,
	  installDirLocStrSize);
  installDirNameSize += installDirLocStrSize;
  installDir[installDirNameSize] = 0;
  installDirFile = open (installDir, O_BINARY | O_RDONLY);
  if (installDirFile == -1)
    {
      for (; installDirNameSize && installDir[installDirNameSize] != '/';
	   installDirNameSize--);
      installDirNameSize -= installDirNameSize != 0;
      for (; installDirNameSize && installDir[installDirNameSize] != '/';
	   installDirNameSize--);
      installDir[installDirNameSize] = '/';
      installDirNameSize++;
    }
  else
    {
      fstat (installDirFile, &st);
      free (installDir);
      installDirNameSize = st.st_size;
      installDir = (char *) malloc (installDirNameSize + 2);
      read (installDirFile, installDir, installDirNameSize);
      close (installDirFile);
      for (i = 0; i != installDirNameSize; i++)
	{
	  if (installDir[i] == '\\')
	    {
	      installDir[i] = '/';
	    }
	}
      installDirFile = -1;
      installDir[installDirNameSize] = '/';
      installDirNameSize++;
    }
  startCommandStrSize =
    installDirNameSize + configurationLocSize + defaulticLocSize +
    sizeof (*startCommandStr);
  startCommandStr = (char *) malloc (startCommandStrSize);
  memcpy (startCommandStr, installDir, installDirNameSize);
  startCommandStrSize = installDirNameSize;
  memcpy (startCommandStr + startCommandStrSize, configurationLoc,
	  configurationLocSize);
  startCommandStrSize += configurationLocSize;
  memcpy (startCommandStr + startCommandStrSize, defaulticLoc,
	  defaulticLocSize);
  startCommandStrSize += defaulticLocSize;
  startCommandStr[startCommandStrSize] = 0;
  dPathSize =
    snprintf (NULL, 0, "%.*s%soverride/gcc/", installDirNameSize, installDir,
	      configurationLoc) + 1;
  dPath = (char *) malloc (dPathSize + 22);
  snprintf (dPath, dPathSize, "%.*s%soverride/gcc/", installDirNameSize,
	    installDir, configurationLoc);
  free (installDir);
  mkdir_p (dPath);
  copy_dir (startCommandStr, dPath, true, 0);
  sprintf (dPath, "%srun/run.sh", dPath);
  write_file (dPath, runshStr);
  dPath[dPathSize - 1] = 0;
  sprintf (dPath, "%srunconfig/5", dPath);
  write_file (dPath, "configure.conf");
  dPath[dPathSize - 1] = 0;
  sprintf (dPath, "%srunconfig/6", dPath);
  write_file (dPath, "make.conf");
  dPath[dPathSize - 1] = 0;
  sprintf (dPath, "%sconfig/configure.conf", dPath);
  update_config (dPath);
  *sargv = packageManager;
  sargv[1] = "install";
  sargv[2] = "gcc";
  sargv[3] = 0;
  if (*sargv && posix_spawnp (&spid, *sargv, NULL, NULL, sargv, envp))
    {
      *sargv = 0;
    }
  if (packageManagerCanFree)
    {
      free (packageManager);
    }
  free (dPath);
  free (startCommandStr);
  if (*sargv)
    {
      waitid (P_PID, spid, &ssiginfo, WEXITED);
    }
  return 0;
}
