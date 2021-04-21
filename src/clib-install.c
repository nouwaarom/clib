//
// clib-install.c
//
// Copyright (c) 2012-2020 clib authors
// MIT licensed
//

#include "commander/commander.h"
#include "common/clib-cache.h"
#include "common/clib-package.h"
#include "common/clib-validate.h"
#include "debug/debug.h"
#include "fs/fs.h"
#include "logger/logger.h"
#include "parson/parson.h"
#include "version.h"
#include <clib-secrets.h>
#include <curl/curl.h>
#include <limits.h>
#include <registry-manager.h>
#include <repository.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clib-package-installer.h"
#include "clib-settings.h"
#include "strdup/strdup.h"

#define SX(s) #s
#define S(s) SX(s)

#if defined(_WIN32) || defined(WIN32) || defined(__MINGW32__) ||               \
    defined(__MINGW64__)
#define setenv(k, v, _) _putenv_s(k, v)
#define realpath(a, b) _fullpath(a, b, strlen(a))
#endif

extern CURLSH *clib_package_curl_share;

debug_t debugger = {0};

struct options {
  const char *dir;
  char *prefix;
  char *token;
  int verbose;
  int dev;
  int save;
  int savedev;
  int force;
  int global;
  int skip_cache;
#ifdef HAVE_PTHREADS
  unsigned int concurrency;
#endif
};

static struct options opts = {0};

static clib_secrets_t secrets = NULL;
static registries_t registries = NULL;

/**
 * Option setters.
 */

static void setopt_dir(command_t *self) {
  opts.dir = (char *)self->arg;
  debug(&debugger, "set dir: %s", opts.dir);
}

static void setopt_prefix(command_t *self) {
  opts.prefix = (char *)self->arg;
  debug(&debugger, "set prefix: %s", opts.prefix);
}

static void setopt_token(command_t *self) {
  opts.token = (char *)self->arg;
  debug(&debugger, "set token: %s", opts.token);
}

static void setopt_quiet(command_t *self) {
  opts.verbose = 0;
  debug(&debugger, "set quiet flag");
}

static void setopt_dev(command_t *self) {
  opts.dev = 1;
  debug(&debugger, "set development flag");
}

static void setopt_save(command_t *self) {
  opts.save = 1;
  debug(&debugger, "set save flag");
}

static void setopt_savedev(command_t *self) {
  opts.savedev = 1;
  debug(&debugger, "set savedev flag");
}

static void setopt_force(command_t *self) {
  opts.force = 1;
  debug(&debugger, "set force flag");
}

static void setopt_global(command_t *self) {
  opts.global = 1;
  debug(&debugger, "set global flag");
}

#ifdef HAVE_PTHREADS
static void setopt_concurrency(command_t *self) {
  if (self->arg) {
    opts.concurrency = atol(self->arg);
    debug(&debugger, "set concurrency: %lu", opts.concurrency);
  }
}
#endif

static void setopt_skip_cache(command_t *self) {
  opts.skip_cache = 1;
  debug(&debugger, "set skip cache flag");
}

/**
 * Install dependency packages at `pwd`.
 */
static int install_local_packages(clib_package_t* root_package) {
  if (root_package && root_package->prefix) {
    setenv("PREFIX", root_package->prefix, 1);
  }

  int rc = clib_package_install_dependencies(root_package, opts.dir, opts.verbose);
  if (-1 == rc)
    return 1;

  if (opts.dev) {
    rc = clib_package_install_development(root_package, opts.dir, opts.verbose);
    if (-1 == rc)
      return 1;
  }

  return 0;
}

static int write_dependency_with_package_name(clib_package_t *pkg, char *prefix,
                                              const char *file) {
  JSON_Value *packageJson = json_parse_file(file);
  JSON_Object *packageJsonObject = json_object(packageJson);
  JSON_Value *newDepSectionValue = NULL;

  if (NULL == packageJson || NULL == packageJsonObject)
    return 1;

  // If the dependency section doesn't exist then create it
  JSON_Object *depSection =
      json_object_dotget_object(packageJsonObject, prefix);
  if (NULL == depSection) {
    newDepSectionValue = json_value_init_object();
    depSection = json_value_get_object(newDepSectionValue);
    json_object_set_value(packageJsonObject, prefix, newDepSectionValue);
  }

  // Add the dependency to the dependency section
  json_object_set_string(depSection, pkg->repo, pkg->version);

  // Flush package.json
  int rc = json_serialize_to_file_pretty(packageJson, file);
  json_value_free(packageJson);
  return rc;
}

/**
 * Writes out a dependency to clib.json or package.json
 */
static int write_dependency(clib_package_t *pkg, char *prefix) {
  const char *name = NULL;
  unsigned int i = 0;
  int rc = 0;

  do {
    name = manifest_names[i];
    rc = write_dependency_with_package_name(pkg, prefix, name);
  } while (NULL != manifest_names[++i] && 0 != rc);

  return rc;
}

/**
 * Save a dependency to clib.json or package.json.
 */
static int save_dependency(clib_package_t *pkg) {
  debug(&debugger, "saving dependency %s at %s", pkg->name, pkg->version);
  return write_dependency(pkg, "dependencies");
}

/**
 * Save a development dependency to clib.json or package.json.
 */
static int save_dev_dependency(clib_package_t *pkg) {
  debug(&debugger, "saving dev dependency %s at %s", pkg->name, pkg->version);
  return write_dependency(pkg, "development");
}

/**
 * Create and install a package from `slug`.
 */
static int install_package(clib_package_t* root_package, const char *slug) {
  clib_package_t *pkg = NULL;
  int rc;

#ifdef PATH_MAX
  long path_max = PATH_MAX;
#elif defined(_PC_PATH_MAX)
  long path_max = pathconf(slug, _PC_PATH_MAX);
#else
  long path_max = 4096;
#endif

  if ('.' == slug[0]) {
    if (1 == strlen(slug) || ('/' == slug[1] && 2 == strlen(slug))) {
      char dir[path_max];
      realpath(slug, dir);
      slug = dir;
      return install_local_packages(root_package);
    }
  }

  if (0 == fs_exists(slug)) {
    fs_stats *stats = fs_stat(slug);
    if (NULL != stats && (S_IFREG == (stats->st_mode & S_IFMT)
#if defined(__unix__) || defined(__linux__) || defined(_POSIX_VERSION)
                          || S_IFLNK == (stats->st_mode & S_IFMT)
#endif
                              )) {
      free(stats);
      return install_local_packages(root_package);
    }

    if (stats) {
      free(stats);
    }
  }

  char* author = clib_package_parse_author(slug);
  char* name = clib_package_parse_name(slug);
  char* package_id = clib_package_get_id(author, name);
  registry_package_ptr_t package_info = registry_manager_find_package(registries, package_id);
  if (!package_info) {
    debug(&debugger, "Package %s not found in any registry.", slug);
    return -1;
  }

  pkg = clib_package_new_from_slug_and_url(slug, registry_package_get_href(package_info), opts.verbose);
  if (NULL == pkg)
    return -1;

  if (root_package && root_package->prefix) {
    package_opts.prefix = root_package->prefix;
  }

  rc = clib_package_install(pkg, opts.dir, opts.verbose);
  if (0 != rc) {
    goto cleanup;
  }

  if (0 == rc && opts.dev) {
    rc = clib_package_install_development(pkg, opts.dir, opts.verbose);
    if (0 != rc) {
      goto cleanup;
    }
  }

  if (opts.save)
    save_dependency(pkg);
  if (opts.savedev)
    save_dev_dependency(pkg);

cleanup:
  clib_package_free(pkg);
  return rc;
}

/**
 * Install the given `pkgs`.
 */

static int install_packages(clib_package_t* root_package, int n, char *pkgs[]) {
  for (int i = 0; i < n; i++) {
    debug(&debugger, "install %s (%d)", pkgs[i], i);
    if (-1 == install_package(root_package, pkgs[i])) {
      logger_error("error", "Unable to install package %s", pkgs[i]);
      return 1;
    }
  }
  return 0;
}

/**
 * Entry point.
 */

int main(int argc, char *argv[]) {
#ifdef _WIN32
  opts.dir = ".\\deps";
#else
  opts.dir = "./deps";
#endif
  opts.verbose = 1;
  opts.dev = 0;

#ifdef PATH_MAX
  long path_max = PATH_MAX;
#elif defined(_PC_PATH_MAX)
  long path_max = pathconf(opts.dir, _PC_PATH_MAX);
#else
  long path_max = 4096;
#endif

  debug_init(&debugger, "clib-install");

  // 30 days expiration
  clib_cache_init(CLIB_PACKAGE_CACHE_TIME);

  command_t program;

  command_init(&program, "clib-install", CLIB_VERSION);

  program.usage = "[options] [name ...]";

  command_option(&program, "-o", "--out <dir>",
                 "change the output directory [deps]", setopt_dir);
  command_option(&program, "-P", "--prefix <dir>",
                 "change the prefix directory (usually '/usr/local')",
                 setopt_prefix);
  command_option(&program, "-q", "--quiet", "disable verbose output",
                 setopt_quiet);
  command_option(&program, "-d", "--dev", "install development dependencies",
                 setopt_dev);
  command_option(&program, "-S", "--save",
                 "save dependency in clib.json or package.json", setopt_save);
  command_option(&program, "-D", "--save-dev",
                 "save development dependency in clib.json or package.json",
                 setopt_savedev);
  command_option(&program, "-f", "--force",
                 "force the action of something, like overwriting a file",
                 setopt_force);
  command_option(&program, "-c", "--skip-cache", "skip cache when installing",
                 setopt_skip_cache);
  command_option(&program, "-g", "--global",
                 "global install, don't write to output dir (default: deps/)",
                 setopt_global);
  command_option(&program, "-t", "--token <token>",
                 "Access token used to read private content", setopt_token);
#ifdef HAVE_PTHREADS
  command_option(&program, "-C", "--concurrency <number>",
                 "Set concurrency (default: " S(MAX_THREADS) ")",
                 setopt_concurrency);
#endif
  command_parse(&program, argc, argv);

  debug(&debugger, "%d arguments", program.argc);

  if (0 != curl_global_init(CURL_GLOBAL_ALL)) {
    logger_error("error", "Failed to initialize cURL");
  }

  if (opts.prefix) {
    char prefix[path_max];
    memset(prefix, 0, path_max);
    realpath(opts.prefix, prefix);
    unsigned long int size = strlen(prefix) + 1;
    opts.prefix = strndup(prefix, size);
  }

  clib_cache_init(CLIB_PACKAGE_CACHE_TIME);

  clib_package_opts_t install_package_opts = {0};
  install_package_opts.skip_cache = opts.skip_cache;
  install_package_opts.prefix = opts.prefix;
  install_package_opts.global = opts.global;
  install_package_opts.force = opts.force;
  install_package_opts.token = opts.token;

#ifdef HAVE_PTHREADS
  install_package_opts.concurrency = opts.concurrency;
#endif

  clib_package_set_opts(install_package_opts);

  // Read local config files.
  secrets = clib_secrets_load_from_file("clib_secrets.json");
  clib_package_t* root_package = clib_package_load_local_manifest(0);

  repository_init(secrets); // The repository requires the secrets for authentication.
  registries = registry_manager_init_registries(root_package ? root_package->registries : NULL, secrets);
  registry_manager_fetch_registries(registries);

  clib_package_installer_init(registries, secrets);

  // TODO, move argument parsing here.
  int code = 0 == program.argc ? install_local_packages(root_package)
                               : install_packages(root_package, program.argc, program.argv);

  curl_global_cleanup();
  clib_package_cleanup();
  clib_package_free(root_package);

  command_free(&program);
  return code;
}
