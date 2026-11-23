/* Making a folder: POSIX mkdir takes a mode, Windows' _mkdir doesn't. */
#ifndef PD_FS_H
#define PD_FS_H

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define fsync(fd) _commit(fd) // a save on its way to the card: Windows flushes a file handle with _commit
static inline int pd_mkdir(const char *path) {
  return _mkdir(path);
}
// POSIX rename replaces a file that is already there, Windows' plain rename refuses to: every write after the first one
// (a battery save, a state, host.ini) failed. MoveFileEx replaces it, and waits for the disk like fsync does.
// Declared here rather than through <windows.h>, whose min / max macros this frontend doesn't want.
__declspec(dllimport) int __stdcall MoveFileExA(const char *from, const char *to, unsigned long flags);
static inline int pd_rename(const char *from, const char *to) {
  return MoveFileExA(from, to, 0x1ul /* REPLACE_EXISTING */ | 0x8ul /* WRITE_THROUGH */) ? 0 : -1;
}
#else
#include <sys/stat.h>
static inline int pd_mkdir(const char *path) {
  return mkdir(path, 0755);
}
#define pd_rename(from, to) rename(from, to)
#endif

/* Another ROM beside romPath with its stem (X.gb beside X.gba): then a save named after the stem could be either's. */
static inline int pd_stem_shared(const char *romPath) {
  const char *slash = strrchr(romPath, '/'), *back = strrchr(romPath, '\\');
  if (back && (!slash || back > slash))
    slash = back;
  const char *name = slash ? slash + 1 : romPath, *dot = strrchr(name, '.');
  if (!dot || dot == name)
    return 0;
  char dir[4096];
  snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - romPath) : 1, slash ? romPath : ".");
  DIR *d = opendir(dir);
  if (!d)
    return 1; // can't tell: don't guess
  int shared = 0;
  struct dirent *e;
  static const char *const exts[] = {".gb", ".gbc", ".gba", ".nds", ".zip", ".7z"};
  while (!shared && (e = readdir(d))) {
    const char *edot = strrchr(e->d_name, '.');
    if (!edot || edot - e->d_name != dot - name || strncmp(e->d_name, name, (size_t)(dot - name)) || !strcmp(e->d_name, name))
      continue;
    for (size_t k = 0; k < sizeof exts / sizeof *exts; k++)
      if (!strcasecmp(edot, exts[k]))
        shared = 1;
  }
  closedir(d);
  return shared;
}

/* The battery save in DIR for the ROM at romPath: DIR/<name>.sav (the ROM's whole file name), or, when that one isn't
 * there, a save named after the ROM without its extension (DIR/<base>.sav), the way other emulators leave them
 * (pokeblack2.nds -> pokeblack2.sav) - unless another ROM beside it has the same base name, whose save it could be.
 * [out] may name a file that doesn't exist yet: then it is <name>.sav, the name this host writes. */
static inline void pd_save_path(const char *dir, const char *romPath, char *out, size_t size) {
  const char *slash = strrchr(romPath, '/'), *back = strrchr(romPath, '\\');
  if (back && (!slash || back > slash))
    slash = back;
  const char *romName = slash ? slash + 1 : romPath;
  snprintf(out, size, "%s/%s.sav", dir, romName);
  FILE *f = fopen(out, "rb");
  if (f) {
    fclose(f);
    return;
  }
  const char *dot = strrchr(romName, '.');
  if (!dot || dot == romName)
    return;
  char alt[4096];
  int n = snprintf(alt, sizeof alt, "%s/%.*s.sav", dir, (int)(dot - romName), romName);
  if (n < 0 || (size_t)n >= sizeof alt || (size_t)n >= size)
    return; // too long to name: keep <romName>.sav
  f = fopen(alt, "rb");
  if (f) {
    fclose(f);
    if (!pd_stem_shared(romPath))
      snprintf(out, size, "%s", alt);
  }
}

#endif
