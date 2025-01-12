#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <string.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdarg.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <signal.h>

bool do_pspawn_hook = false;
struct dyld_interpose_tuple {
	const void* replacement;
	const void* replacee;
};
void dyld_dynamic_interpose(const struct mach_header* mh, const struct dyld_interpose_tuple array[], size_t count);

int sandbox_check_by_audit_token(audit_token_t au, const char *operation, int sandbox_filter_type, ...);

typedef  void *posix_spawnattr_t;
typedef  void *posix_spawn_file_actions_t;
int posix_spawn(pid_t *, const char *,const posix_spawn_file_actions_t *,const posix_spawnattr_t *,char *const __argv[],char *const __envp[]);

typedef void* xpc_object_t;
typedef void* xpc_type_t;
typedef void* launch_data_t;
typedef bool (^xpc_dictionary_applier_t)(const char *key, xpc_object_t value);

xpc_object_t xpc_dictionary_create(const char * const *keys, const xpc_object_t *values, size_t count);
void xpc_dictionary_set_uint64(xpc_object_t dictionary, const char *key, uint64_t value);
void xpc_dictionary_set_string(xpc_object_t dictionary, const char *key, const char *value);
int64_t xpc_dictionary_get_int64(xpc_object_t dictionary, const char *key);
xpc_object_t xpc_dictionary_get_value(xpc_object_t dictionary, const char *key);
bool xpc_dictionary_get_bool(xpc_object_t dictionary, const char *key);
void xpc_dictionary_set_fd(xpc_object_t dictionary, const char *key, int value);
void xpc_dictionary_set_bool(xpc_object_t dictionary, const char *key, bool value);
const char *xpc_dictionary_get_string(xpc_object_t dictionary, const char *key);
void xpc_dictionary_set_value(xpc_object_t dictionary, const char *key, xpc_object_t value);
xpc_type_t xpc_get_type(xpc_object_t object);
bool xpc_dictionary_apply(xpc_object_t xdict, xpc_dictionary_applier_t applier);
int64_t xpc_int64_get_value(xpc_object_t xint);
char *xpc_copy_description(xpc_object_t object);
void xpc_dictionary_set_int64(xpc_object_t dictionary, const char *key, int64_t value);
const char *xpc_string_get_string_ptr(xpc_object_t xstring);
xpc_object_t xpc_array_create(const xpc_object_t *objects, size_t count);
xpc_object_t xpc_string_create(const char *string);
size_t xpc_dictionary_get_count(xpc_object_t dictionary);
void xpc_array_append_value(xpc_object_t xarray, xpc_object_t value);

#define XPC_ARRAY_APPEND ((size_t)(-1))
#define XPC_ERROR_CONNECTION_INVALID XPC_GLOBAL_OBJECT(_xpc_error_connection_invalid)
#define XPC_ERROR_TERMINATION_IMMINENT XPC_GLOBAL_OBJECT(_xpc_error_termination_imminent)
#define XPC_TYPE_ARRAY (&_xpc_type_array)
#define XPC_TYPE_BOOL (&_xpc_type_bool)
#define XPC_TYPE_DICTIONARY (&_xpc_type_dictionary)
#define XPC_TYPE_ERROR (&_xpc_type_error)
#define XPC_TYPE_STRING (&_xpc_type_string)


extern const struct _xpc_dictionary_s _xpc_error_connection_invalid;
extern const struct _xpc_dictionary_s _xpc_error_termination_imminent;
extern const struct _xpc_type_s _xpc_type_array;
extern const struct _xpc_type_s _xpc_type_bool;
extern const struct _xpc_type_s _xpc_type_dictionary;
extern const struct _xpc_type_s _xpc_type_error;
extern const struct _xpc_type_s _xpc_type_string;

#define DYLD_INTERPOSE(_replacment,_replacee) \
__attribute__((used)) static struct{ const void* replacment; const void* replacee; } _interpose_##_replacee \
__attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacment, (const void*)(unsigned long)&_replacee };

void spin() {
  while(1) {sleep(5);}
}

/*
  Launch our Daemon *correctly*
*/
xpc_object_t hook_xpc_dictionary_get_value(xpc_object_t dict, const char *key){
  xpc_object_t retval = xpc_dictionary_get_value(dict,key);
  if (strcmp(key,"LaunchDaemons") == 0) {
    xpc_object_t submitJob = xpc_dictionary_create(NULL, NULL, 0);
    xpc_object_t programArguments = xpc_array_create(NULL, 0);

    xpc_array_append_value(programArguments, xpc_string_create("/cores/jbloader"));
    if(getenv("XPC_USERSPACE_REBOOTED") != NULL) {
      xpc_array_append_value(programArguments, xpc_string_create("-u"));
    }
    xpc_array_append_value(programArguments, xpc_string_create("-j"));

    xpc_dictionary_set_bool(submitJob, "KeepAlive", false);
    xpc_dictionary_set_bool(submitJob, "RunAtLoad", true);
    xpc_dictionary_set_string(submitJob, "ProcessType", "Interactive");
    xpc_dictionary_set_string(submitJob, "UserName", "root");
    xpc_dictionary_set_string(submitJob, "Program", "/cores/jbloader");
    xpc_dictionary_set_string(submitJob, "StandardInPath", "/dev/console");
    xpc_dictionary_set_string(submitJob, "StandardOutPath", "/dev/console");
    xpc_dictionary_set_string(submitJob, "StandardErrorPath", "/dev/console");
    xpc_dictionary_set_string(submitJob, "Label", "in.palera.jbloader");
    xpc_dictionary_set_value(submitJob, "ProgramArguments", programArguments);
#ifdef DEV_BUILD
    xpc_object_t environmentVariables = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_string(environmentVariables, "DYLD_INSERT_LIBRARIES", "/cores/xpchook.dylib");
    xpc_dictionary_set_value(submitJob, "EnvironmentVariables", environmentVariables);
#endif
    xpc_dictionary_set_value(retval, "/System/Library/LaunchDaemons/in.palera.jbloader.plist", submitJob);
  } else if (strcmp(key, "sysstatuscheck") == 0) {
    xpc_object_t programArguments = xpc_array_create(NULL, 0);
    xpc_array_append_value(programArguments, xpc_string_create("/cores/jbloader"));
    if(getenv("XPC_USERSPACE_REBOOTED") != NULL) {
      xpc_array_append_value(programArguments, xpc_string_create("-u"));
    }
    xpc_array_append_value(programArguments, xpc_string_create("-s"));
    xpc_object_t newTask = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_bool(newTask, "PerformAfterUserspaceReboot", true);
    xpc_dictionary_set_bool(newTask, "RebootOnSuccess", true);
    xpc_dictionary_set_string(newTask, "Program", "/cores/jbloader");
    xpc_dictionary_set_value(newTask, "ProgramArguments", programArguments);
    return newTask;
  }
  return retval;
}
DYLD_INTERPOSE(hook_xpc_dictionary_get_value, xpc_dictionary_get_value);

bool hook_xpc_dictionary_get_bool(xpc_object_t dictionary, const char *key) {
  if (!strcmp(key, "LogPerformanceStatistics")) return true;
  else return xpc_dictionary_get_bool(dictionary, key);
}
DYLD_INTERPOSE(hook_xpc_dictionary_get_bool, xpc_dictionary_get_bool);

int hook__NSGetExecutablePath(char* buf, uint32_t* bufsize) {
  if (getpid() != 1) return _NSGetExecutablePath(buf, bufsize);
  else if (*bufsize > sizeof("/cores/jbloader")) {
    snprintf(buf, (size_t)(*bufsize), "/cores/jbloader");
    return 0;
  } else {
    *bufsize = sizeof("/cores/jbloader");
    return -1;
  }
  return 0;
}
DYLD_INTERPOSE(hook__NSGetExecutablePath, _NSGetExecutablePath);

int posix_spawnp(pid_t *pid,
                 const char *path,
                 const posix_spawn_file_actions_t *action,
                 const posix_spawnattr_t *attr,
                 char *const argv[], char *const envp[]);
int hook_posix_spawnp(pid_t *pid,
                      const char *path,
                      const posix_spawn_file_actions_t *action,
                      const posix_spawnattr_t *attr,
                      char *const argv[], char *envp[]) {
  if (do_pspawn_hook == false || strcmp(argv[0], "xpcproxy") || argv[1] == NULL || strcmp(argv[1], "com.apple.cfprefsd.xpc.daemon"))
    return posix_spawnp(pid, path, action, attr, argv, envp);
    char *inj = NULL;
    int envcnt = 0;
    while (envp[envcnt] != NULL) {
      envcnt++;
    }
    char** newenvp = malloc((envcnt + 2) * sizeof(char **));
    int j = 0;
    char* currentenv = NULL;
    for (int i = 0; i < envcnt; i++){
      if (strstr(envp[j], "DYLD_INSERT_LIBRARIES") != NULL) {
        currentenv = envp[j];
        continue;
      }
        newenvp[i] = envp[j];
        j++;
    }
            
    char *newlib = "/cores/injector.dylib";
    if(currentenv) {
      size_t inj_len = strlen(currentenv) + 1 + strlen(newlib) + 1;
      inj = malloc(inj_len);
      if (inj == NULL) {
        perror(NULL);
        abort();
      }
      snprintf(inj, inj_len, "%s:%s", currentenv, newlib);
    } else {
      size_t inj_len = strlen("DYLD_INSERT_LIBRARIES=") + strlen(newlib) + 1;
      inj = malloc(inj_len);
      if (inj == NULL) {
        perror(NULL);
        abort();
      }
      snprintf(inj, inj_len, "DYLD_INSERT_LIBRARIES=%s", newlib);
    }
    newenvp[j] = inj;
    newenvp[j + 1] = NULL;
            
    int ret = posix_spawnp(pid, path, action, attr, argv, newenvp);
    if (inj != NULL) free(inj);
    if (currentenv != NULL) free(currentenv);
    return ret;
}

DYLD_INTERPOSE(hook_posix_spawnp, posix_spawnp);

void DoNothingHandler(int __unused _) {}

__attribute__((constructor))
static void customConstructor(int argc, const char **argv){
  int fd_console = open("/dev/console",O_RDWR,0);
  dprintf(fd_console,"================ Hello from jb.dylib ================ \n");
  signal(SIGBUS, DoNothingHandler);
  if (access("/cores/injector.dylib", F_OK) == 0) {
    do_pspawn_hook = true;
  }
  dprintf(fd_console, "do_pspawn_hook: %d\n", do_pspawn_hook);
  dprintf(fd_console,"========= Goodbye from jb.dylib constructor ========= \n");
  close(fd_console);
}
