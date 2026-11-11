#pragma once
/* Decides whether the virtual display helper may be elevated.
 * A user-writable image is never the process that receives elevation. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    /* Source bytes do not match the build pin, and no protected staged copy does either. */
    FR_HELPER_LAUNCH_REJECT_HASH = 1,
    /* The on-disk helper already lives in a directory the unelevated user cannot modify. */
    FR_HELPER_LAUNCH_IN_PLACE = 2,
    /* A protected staged copy matches the build pin. Elevate that copy. */
    FR_HELPER_LAUNCH_STAGED = 3,
    /* Hash matches, but the image is user-writable. Copy it to a protected directory first. */
    FR_HELPER_LAUNCH_NEED_STAGE = 4
};

typedef struct fr_helper_sha256_ctx {
    unsigned state[8];
    unsigned long long bitlen;
    unsigned char buffer[64];
    unsigned buflen;
} fr_helper_sha256_ctx;

void fr_helper_sha256_init(fr_helper_sha256_ctx* ctx);
void fr_helper_sha256_update(fr_helper_sha256_ctx* ctx, const void* data, size_t length);
void fr_helper_sha256_final(fr_helper_sha256_ctx* ctx, char out[65]);
void fr_helper_sha256(const void* data, size_t length, char out[65]);

/* staged_dir_user_writable is ignored unless staged_present is set.
 * A protected staged copy that matches the pin wins over a tampered source. */
int fr_helper_launch_decide(int source_user_writable, int hash_matches, int staged_present,
                            int staged_hash_matches, int staged_dir_user_writable);

/* Nonzero when a recorded helper should be stopped and its files removed.
 * parent_alive is meaningful only when process_alive is set. */
int fr_helper_residue_stale(int pid_recorded, int process_alive, int parent_alive);

/* Nonzero for an empty file or a truncated prefix of the shipped template.
 * A document that already contains </vdd_settings> is kept, as is any file that
 * is not a byte-for-byte prefix of the template (custom settings survive). */
int fr_helper_settings_incomplete(const char* text, size_t length, const char* templ,
                                  size_t template_length);

#ifdef _WIN32
/* Nonzero when an unelevated token for this logon can modify the path.
 * API failures are treated as writable so elevation fails closed. */
int fr_helper_directory_user_writable(const wchar_t* path);
int fr_helper_object_user_writable(const wchar_t* path);
/* Nonzero when this process image, or its directory, is writable by the unelevated user. */
int fr_helper_image_user_writable(void);
void fr_helper_note_own_pid(void);
void fr_helper_clear_own_pid(void);
int fr_helper_file_sha256(const wchar_t* path, char out[65]);
/* Replace the DACL so only SYSTEM and Administrators can write. Returns 0 on success. */
int fr_helper_protect_directory(wchar_t* path);
int fr_helper_protect_file(wchar_t* path);
/* Stop an orphaned protected helper, then delete its pid file and our temp files. */
void fr_helper_reap_protected_helper(void);
/* Drop stale stager pid/temp files. Does not kill a live installer. */
void fr_helper_cleanup_user_residue(void);
int fr_helper_stager_busy(void);
void fr_helper_note_stager_pid(unsigned long pid);
void fr_helper_clear_install_pid(void);
/* On success, *process is the elevated process to wait on and *lock keeps the source
 * image unwritable until fr_helper_reap_launch_lock sees that process exit. */
int fr_helper_start_install(const wchar_t* helper_path, const wchar_t* package_path, void** process,
                            void** lock);
void fr_helper_track_launch_lock(void* process, void* lock);
void fr_helper_reap_launch_lock(void);
#endif
#ifdef __cplusplus
}
#endif
