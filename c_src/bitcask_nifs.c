// -------------------------------------------------------------------
//
// bitcask: Eric Brewer-inspired key/value store
//
// Copyright (c) 2010 Basho Technologies, Inc. All Rights Reserved.
//
// This file is provided to you under the Apache License,
// Version 2.0 (the "License"); you may not use this file
// except in compliance with the License.  You may obtain
// a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// -------------------------------------------------------------------
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include <string.h>             /* memset */

#include "erl_nif.h"
#include "erl_driver.h"
#include "erl_nif_compat.h"
#include "erl_nif_util.h"

#include "red_black_tree.h"

#define MAX_KEY_LEN     4096
#define MAX_PATH_LEN    4096

static ErlNifResourceType* bitcask_keydir_RESOURCE;

static ErlNifResourceType* bitcask_lock_RESOURCE;

typedef struct
{
    uint32_t file_id;
    uint32_t total_sz;
    uint64_t offset;
    uint32_t tstamp;
    uint32_t key_sz;            /* must match bitcask_keydir_entry_keycheat */
    char     key[0];            /* must match bitcask_keydir_entry_keycheat */
} bitcask_keydir_entry;
typedef struct {          /* MUST OVERLAP exactly with end of above struct */
    uint32_t key_sz;            /* must match bitcask_keydir_entry */
    char     key[0];            /* must match bitcask_keydir_entry */
} bitcask_keydir_entry_keycheat;

typedef struct
{
    uint32_t file_id;
    uint32_t live_keys;
    uint32_t total_keys;
    uint64_t live_bytes;
    uint64_t total_bytes;
} bitcask_fstats_entry;

static ErlNifEnv *my_env_copy = NULL;
static int keydir_entry_equal(const void *, const void*);
static void keydir_destroy_key(void *);
static void keydir_destroy_info(void *);
static void keydir_print_key(const void *);
static void keydir_print_info(void *);
static int fstats_entry_equal(const void *, const void *);
static void fstats_destroy_key(void *);
static void fstats_destroy_info(void *);
static void fstats_print_key(const void *);
static void fstats_print_info(void *);
static int bitcask_keydir_entry_equal(const void *, const void *);
static void bitcask_keydir_destroy_key(void *);
static void bitcask_keydir_destroy_info(void *);
static void bitcask_keydir_print_key(const void *);
static void bitcask_keydir_print_info(void *);
static rb_red_blk_tree* new_entries_tree(void);
static rb_red_blk_tree* new_fstats_tree(void);
static rb_red_blk_tree* new_global_tree(void);
static void my_tree_insert(rb_red_blk_tree *, void *, void *);

typedef struct
{
    rb_red_blk_tree* entries;
    rb_red_blk_tree*  fstats;
    size_t        key_count;
    size_t        key_bytes;
    unsigned int  refcount;
    ErlNifRWLock* lock;
    char          is_ready;
    char          name[0];
} bitcask_keydir;

typedef struct
{
    bitcask_keydir* keydir;
    ErlNifTid       il_thread;  /* Iterator lock thread */
    ErlNifMutex*    il_signal_mutex;
    ErlNifCond*     il_signal;
    int             il_signal_flag;
    uint16_t        last_key_sz; /* RB tree substitute for iterator */
    char            last_key[MAX_KEY_LEN];
} bitcask_keydir_handle;

typedef struct
{
    int   fd;
    int   is_write_lock;
    char  filename[0];
} bitcask_lock_handle;

typedef struct
{
    rb_red_blk_tree*         global_keydirs;
    ErlNifMutex*             global_keydirs_lock;
} bitcask_priv_data;

static rb_red_blk_node* find_keydir_entry(ErlNifEnv* env, bitcask_keydir* keydir, ErlNifBinary* key);
static rb_red_blk_node* find_fstats_entry(bitcask_keydir* keydir, uint32_t file_id);

// Handle lock helper functions
#define R_LOCK(keydir)    { if (keydir->lock) enif_rwlock_rlock(keydir->lock); }
#define R_UNLOCK(keydir)  { if (keydir->lock) enif_rwlock_runlock(keydir->lock); }
#define RW_LOCK(keydir)   { if (keydir->lock) enif_rwlock_rwlock(keydir->lock); }
#define RW_UNLOCK(keydir) { if (keydir->lock) enif_rwlock_rwunlock(keydir->lock); }

// Atoms (initialized in on_load)
static ERL_NIF_TERM ATOM_ALLOCATION_ERROR;
static ERL_NIF_TERM ATOM_ALREADY_EXISTS;
static ERL_NIF_TERM ATOM_BITCASK_ENTRY;
static ERL_NIF_TERM ATOM_ERROR;
static ERL_NIF_TERM ATOM_FALSE;
static ERL_NIF_TERM ATOM_FSTAT_ERROR;
static ERL_NIF_TERM ATOM_FTRUNCATE_ERROR;
static ERL_NIF_TERM ATOM_GETFL_ERROR;
static ERL_NIF_TERM ATOM_ILT_CREATE_ERROR; /* Iteration lock thread creation error */
static ERL_NIF_TERM ATOM_ITERATION_IN_PROCESS;
static ERL_NIF_TERM ATOM_ITERATION_NOT_PERMITTED;
static ERL_NIF_TERM ATOM_ITERATION_NOT_STARTED;
static ERL_NIF_TERM ATOM_LOCK_NOT_WRITABLE;
static ERL_NIF_TERM ATOM_NOT_FOUND;
static ERL_NIF_TERM ATOM_NOT_READY;
static ERL_NIF_TERM ATOM_OK;
static ERL_NIF_TERM ATOM_PREAD_ERROR;
static ERL_NIF_TERM ATOM_PWRITE_ERROR;
static ERL_NIF_TERM ATOM_READY;
static ERL_NIF_TERM ATOM_SETFL_ERROR;
static ERL_NIF_TERM ATOM_TRUE;

// Prototypes
ERL_NIF_TERM bitcask_nifs_keydir_new0(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_keydir_new1(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_keydir_mark_ready(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_keydir_get_int(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_keydir_put_int(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_keydir_remove(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_keydir_copy(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_keydir_itr(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_keydir_itr_next(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_keydir_itr_release(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_keydir_info(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_keydir_release(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);

ERL_NIF_TERM bitcask_nifs_create_file(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_create_tmp_file(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);

ERL_NIF_TERM bitcask_nifs_set_osync(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);

ERL_NIF_TERM bitcask_nifs_lock_acquire(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_lock_release(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_lock_readdata(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM bitcask_nifs_lock_writedata(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);

ERL_NIF_TERM errno_atom(ErlNifEnv* env, int error);
ERL_NIF_TERM errno_error_tuple(ErlNifEnv* env, ERL_NIF_TERM key, int error);

static void lock_release(bitcask_lock_handle* handle);

static void bitcask_nifs_keydir_resource_cleanup(ErlNifEnv* env, void* arg);

static ErlNifFunc nif_funcs[] =
{
    {"keydir_new", 0, bitcask_nifs_keydir_new0},
    {"keydir_new", 1, bitcask_nifs_keydir_new1},
    {"keydir_mark_ready", 1, bitcask_nifs_keydir_mark_ready},
    {"keydir_put_int", 6, bitcask_nifs_keydir_put_int},
    {"keydir_get_int", 2, bitcask_nifs_keydir_get_int},
    {"keydir_remove", 2, bitcask_nifs_keydir_remove},
    {"keydir_remove_int", 5, bitcask_nifs_keydir_remove},
    {"keydir_copy", 1, bitcask_nifs_keydir_copy},
    {"keydir_itr", 1, bitcask_nifs_keydir_itr},
    {"keydir_itr_next_int", 1, bitcask_nifs_keydir_itr_next},
    {"keydir_itr_release", 1, bitcask_nifs_keydir_itr_release},
    {"keydir_info", 1, bitcask_nifs_keydir_info},
    {"keydir_release", 1, bitcask_nifs_keydir_release},

    {"create_file", 1, bitcask_nifs_create_file},

    {"set_osync", 1, bitcask_nifs_set_osync},

    {"lock_acquire",   2, bitcask_nifs_lock_acquire},
    {"lock_release",   1, bitcask_nifs_lock_release},
    {"lock_readdata",  1, bitcask_nifs_lock_readdata},
    {"lock_writedata", 2, bitcask_nifs_lock_writedata}
};

ERL_NIF_TERM bitcask_nifs_keydir_new0(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
fprintf(stderr, "new0\n");
    // First, setup a resource for our handle
    bitcask_keydir_handle* handle = enif_alloc_resource_compat(env,
                                                        bitcask_keydir_RESOURCE,
                                                        sizeof(bitcask_keydir_handle));
    memset(handle, '\0', sizeof(bitcask_keydir_handle));

    // Now allocate the actual keydir instance. Because it's unnamed/shared, we'll
    // leave the name and lock portions null'd out
    bitcask_keydir* keydir = enif_alloc_compat(env, sizeof(bitcask_keydir));
    memset(keydir, '\0', sizeof(bitcask_keydir));
    keydir->entries  = new_entries_tree();
    keydir->fstats   = new_fstats_tree();

    // Assign the keydir to our handle and hand it back
    handle->keydir = keydir;
    ERL_NIF_TERM result = enif_make_resource(env, handle);
    enif_release_resource_compat(env, handle);
    return enif_make_tuple2(env, ATOM_OK, result);
}

ERL_NIF_TERM bitcask_nifs_keydir_new1(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char name[MAX_PATH_LEN];
    size_t name_sz;

fprintf(stderr, "new1\n");
    if (enif_get_string(env, argv[0], name, sizeof(name), ERL_NIF_LATIN1))
    {
        name_sz = strlen(name);

        // Get our private stash and check the global hash table for this entry
        bitcask_priv_data* priv = (bitcask_priv_data*)enif_priv_data(env);
        enif_mutex_lock(priv->global_keydirs_lock);

        bitcask_keydir* keydir;
        rb_red_blk_node* node = RBExactQuery(priv->global_keydirs, name);
        if (node != NULL)
        {
            keydir = node->info;
            // Existing keydir is available. Check the is_ready flag to determine if
            // the original creator is ready for other processes to use it.
            if (!keydir->is_ready)
            {
                // Notify the caller that while the requested keydir exists, it's not
                // ready for public usage.
                enif_mutex_unlock(priv->global_keydirs_lock);
                return enif_make_tuple2(env, ATOM_ERROR, ATOM_NOT_READY);
            }
            else
            {
                keydir->refcount++;
            }
        }
        else
        {
            // No such keydir, create a new one and add to the globals list. Make sure
            // to allocate enough room for the name.
            keydir = enif_alloc_compat(env, sizeof(bitcask_keydir) + name_sz + 1);
            memset(keydir, '\0', sizeof(bitcask_keydir) + name_sz + 1);
            strncpy(keydir->name, name, name_sz + 1);

            // Initialize hash tables
            keydir->entries = new_entries_tree();
            keydir->fstats  = new_fstats_tree();

            // Be sure to initialize the rwlock and set our refcount
            keydir->lock = enif_rwlock_create(name);
            keydir->refcount = 1;

            // Finally, register this new keydir in the globals
            my_tree_insert(priv->global_keydirs, keydir->name, keydir);
        }

        enif_mutex_unlock(priv->global_keydirs_lock);

        // Setup a resource for the handle
        bitcask_keydir_handle* handle = enif_alloc_resource_compat(env,
                                                            bitcask_keydir_RESOURCE,
                                                            sizeof(bitcask_keydir_handle));
        memset(handle, '\0', sizeof(bitcask_keydir_handle));
        handle->keydir = keydir;
        ERL_NIF_TERM result = enif_make_resource(env, handle);
        enif_release_resource_compat(env, handle);

        // Return to the caller a tuple with the reference and an atom
        // indicating if the keydir is ready or not.
        ERL_NIF_TERM is_ready_atom = keydir->is_ready ? ATOM_READY : ATOM_NOT_READY;
        return enif_make_tuple2(env, is_ready_atom, result);
    }
    else
    {
        return enif_make_badarg(env);
    }
}

ERL_NIF_TERM bitcask_nifs_keydir_mark_ready(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_keydir_handle* handle;

    if (enif_get_resource(env, argv[0], bitcask_keydir_RESOURCE, (void**)&handle))
    {
        bitcask_keydir* keydir = handle->keydir;
        RW_LOCK(keydir);
        keydir->is_ready = 1;
        RW_UNLOCK(keydir);
        return ATOM_OK;
    }
    else
    {
        return enif_make_badarg(env);
    }
}

static void update_fstats(ErlNifEnv* env, bitcask_keydir* keydir,
                          uint32_t file_id,
                          int32_t live_increment, int32_t total_increment,
                          int32_t live_bytes_increment, int32_t total_bytes_increment)
{
    bitcask_fstats_entry* entry = 0;
    rb_red_blk_node* node = find_fstats_entry(keydir, file_id);
    if (node == NULL)
    {
        // Need to initialize new entry and add to the table
        entry = enif_alloc_compat(env, sizeof(bitcask_fstats_entry));
        memset(entry, '\0', sizeof(bitcask_fstats_entry));
        entry->file_id = file_id;

        my_tree_insert(keydir->fstats, &entry->file_id, entry);
    }
    else
    {
        entry = node->info;
    }

    entry->live_keys   += live_increment;
    entry->total_keys  += total_increment;
    entry->live_bytes  += live_bytes_increment;
    entry->total_bytes += total_bytes_increment;
}

static rb_red_blk_node* find_keydir_entry(ErlNifEnv* env, bitcask_keydir* keydir, ErlNifBinary* key)
{
    if (key->size < (MAX_KEY_LEN - sizeof(bitcask_keydir_entry) - 1))
    {
        char buf[MAX_KEY_LEN];
        bitcask_keydir_entry_keycheat* e = (bitcask_keydir_entry_keycheat*)buf;

        e->key_sz = key->size;
        memcpy(e->key, key->data, key->size);
        rb_red_blk_node* xx = RBExactQuery(keydir->entries, e);
        {
            char foobuf[99999];
            memcpy(foobuf, key->data, key->size);
            foobuf[key->size] = '\0';
            fprintf(stderr, "find_keydir_entry: %s -> 0x%lx info 0x%lx\r\n", foobuf, xx, (xx == NULL) ? 0 : xx->info);
        }
        return xx;
        /* return RBExactQuery(keydir->entries, e); */
    }
    else
    {
        fprintf(stderr, "LAZY SLF at line %d\n", __LINE__);
        abort(); /* SLF: TODO check git repo for code that lived here. */
    }
}

static rb_red_blk_node* find_fstats_entry(bitcask_keydir* keydir, uint32_t file_id)
{
    static bitcask_fstats_entry f;
    f.file_id = file_id;
    return RBExactQuery(keydir->fstats, &f);
}

ERL_NIF_TERM bitcask_nifs_keydir_put_int(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_keydir_handle* handle;
    bitcask_keydir_entry entry;
    ErlNifBinary key;

    if (enif_get_resource(env, argv[0], bitcask_keydir_RESOURCE, (void**)&handle) &&
        enif_inspect_binary(env, argv[1], &key) &&
        enif_get_uint(env, argv[2], (unsigned int*)&(entry.file_id)) &&
        enif_get_uint(env, argv[3], &(entry.total_sz)) &&
        enif_get_uint64_bin(env, argv[4], &(entry.offset)) &&
        enif_get_uint(env, argv[5], &(entry.tstamp)))
    {
        bitcask_keydir* keydir = handle->keydir;
        RW_LOCK(keydir);

        // Now that we've marshalled everything, see if the tstamp for this key is >=
        // to what's already in the hash. Otherwise, we don't bother with the update.
        rb_red_blk_node* node = find_keydir_entry(env, keydir, &key);
        if (node == NULL)
        {
            // No entry exists at all yet; add one
            bitcask_keydir_entry* new_entry = enif_alloc_compat(env,
                                                                sizeof(bitcask_keydir_entry) +
                                                                key.size);
            new_entry->file_id = entry.file_id;
            new_entry->total_sz = entry.total_sz;
            new_entry->offset = entry.offset;
            new_entry->tstamp = entry.tstamp;
            new_entry->key_sz = key.size;
            memcpy(new_entry->key, key.data, key.size);
            /* key.data[key.size] = '\0'; */
            /* Use bitcask_keydir_entry_keycheat hack */
            {
                char foobuf[99999];
                memcpy(foobuf, key.data, key.size);
                foobuf[key.size] = '\0';
                fprintf(stderr, "inserting keydir for key %s\r\n", foobuf);
            }
            my_tree_insert(keydir->entries, &(new_entry->key_sz), new_entry);
            fprintf(stderr, "re-read keydir for key %s: 0x%lx\r\n", key.data, RBExactQuery(keydir->entries, &(new_entry->key_sz)));

            // Update the stats
            keydir->key_count++;
            keydir->key_bytes += key.size;

            // First entry for this key -- increment both live and total counters
            update_fstats(env, keydir, entry.file_id, 1, 1,
                          entry.total_sz, entry.total_sz);

            RW_UNLOCK(keydir);
            return ATOM_OK;
        }

        bitcask_keydir_entry* old_entry = node->info;
        if ((old_entry->tstamp < entry.tstamp) ||

            ((old_entry->tstamp == entry.tstamp) &&
             (old_entry->file_id < entry.file_id)) ||

            ((old_entry->tstamp == entry.tstamp) &&
             ((old_entry->file_id == entry.file_id) &&
              (old_entry->offset < entry.offset))))
        {
            // Entry already exists. Decrement live counter on the fstats entry
            // for the old file ID and update both counters for new file. Note
            // that this only needs to be done if the file_ids are not the
            // same.
            if (old_entry->file_id != entry.file_id)
            {
                update_fstats(env, keydir, old_entry->file_id, -1, 0,
                              -1 * old_entry->total_sz, 0);
                update_fstats(env, keydir, entry.file_id, 1, 1,
                              entry.total_sz, entry.total_sz);
            }
            else // file_id is same, change live/total in one entry
            {
                update_fstats(env, keydir, entry.file_id, 0, 1,
                        entry.total_sz - old_entry->total_sz, entry.total_sz);

            }

            // Update the entry info. Note that if you do multiple updates in a
            // second, the last one in wins!
            // TODO: Safe?
            old_entry->file_id = entry.file_id;
            old_entry->total_sz = entry.total_sz;
            old_entry->offset = entry.offset;
            old_entry->tstamp = entry.tstamp;

            RW_UNLOCK(keydir);
            return ATOM_OK;
        }
        else
        {
            // If the keydir is in the process of being loaded, it's safe to update
            // fstats on a failed put. Once the keydir is live, any attempts to put
            // in old data would just be ignored to avoid double-counting problems.
            if (!keydir->is_ready)
            {
                // Increment the total # of keys and total size for the entry that
                // was NOT stored in the keydir.
                update_fstats(env, keydir, entry.file_id, 0, 1,
                              0, entry.total_sz);
            }

            RW_UNLOCK(keydir);
            return ATOM_ALREADY_EXISTS;
        }
    }
    else
    {
        return enif_make_badarg(env);
    }
}

ERL_NIF_TERM bitcask_nifs_keydir_get_int(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_keydir_handle* handle;
    ErlNifBinary key;

    if (enif_get_resource(env, argv[0], bitcask_keydir_RESOURCE, (void**)&handle) &&
        enif_inspect_binary(env, argv[1], &key))
    {
        bitcask_keydir* keydir = handle->keydir;
        R_LOCK(keydir);

        rb_red_blk_node* node = find_keydir_entry(env, keydir, &key);
        if (node != NULL)
        {
            bitcask_keydir_entry* entry = node->info;
            ERL_NIF_TERM result = enif_make_tuple6(env,
                                                   ATOM_BITCASK_ENTRY,
                                                   argv[1], /* Key */
                                                   enif_make_uint(env, entry->file_id),
                                                   enif_make_uint(env, entry->total_sz),
                                                   enif_make_uint64_bin(env, entry->offset),
                                                   enif_make_uint(env, entry->tstamp));
            R_UNLOCK(keydir);
            return result;
        }
        else
        {
            R_UNLOCK(keydir);
            return ATOM_NOT_FOUND;
        }
    }
    else
    {
        return enif_make_badarg(env);
    }
}


ERL_NIF_TERM bitcask_nifs_keydir_remove(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_keydir_handle* handle;
    ErlNifBinary key;

    if (enif_get_resource(env, argv[0], bitcask_keydir_RESOURCE, (void**)&handle) &&
        enif_inspect_binary(env, argv[1], &key))
    {
        bitcask_keydir* keydir = handle->keydir;
        RW_LOCK(keydir);

        rb_red_blk_node* node = find_keydir_entry(env, keydir, &key);
        if (node != NULL)
        {
            bitcask_keydir_entry* entry = node->info;

            // If this call has 5 arguments, this is a conditional removal. We
            // only want to actually remove the entry if the tstamp, fileid and
            // offset matches the one provided. A sort of poor-man's CAS.
            if (argc == 5)
            {
                uint32_t tstamp;
                uint32_t file_id;
                uint64_t offset;
                if (enif_get_uint(env, argv[2], (unsigned int*)&tstamp) &&
                    enif_get_uint(env, argv[3], (unsigned int*)&file_id) &&
                    enif_get_uint64_bin(env, argv[4], (uint64_t*)&offset))
                {
                    if (entry->tstamp != tstamp || entry->file_id != file_id ||
                        entry->offset != offset)
                    {
                        // Either tstamp or file_id didn't match precisely. Ignore
                        // this attempt to delete the record.
                        RW_UNLOCK(keydir);
                        return ATOM_OK;
                    }
                }
                else
                {
                    RW_UNLOCK(keydir);
                    return enif_make_badarg(env);
                }
            }

            // Update fstats for the current file id -- one less live
            // key is the assumption here.
            update_fstats(env, keydir, entry->file_id, -1, 0,
                          -1 * entry->total_sz, 0);

            // Update the stats
            keydir->key_count--;
            keydir->key_bytes -= entry->key_sz;

            RBDelete(keydir->entries, node);

            enif_free_compat(env, entry);
        }

        RW_UNLOCK(keydir);
        return ATOM_OK;
    }
    else
    {
        return enif_make_badarg(env);
    }
}

ERL_NIF_TERM bitcask_nifs_keydir_copy(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_keydir_handle* handle;

    if (enif_get_resource(env, argv[0], bitcask_keydir_RESOURCE, (void**)&handle))
    {
        bitcask_keydir* keydir = handle->keydir;
        R_LOCK(keydir);

        bitcask_keydir_handle* new_handle = enif_alloc_resource_compat(env,
                                                                bitcask_keydir_RESOURCE,
                                                                sizeof(bitcask_keydir_handle));
        memset(handle, '\0', sizeof(bitcask_keydir_handle));

        // Now allocate the actual keydir instance. Because it's unnamed/shared, we'll
        // leave the name and lock portions null'd out
        bitcask_keydir* new_keydir = enif_alloc_compat(env, sizeof(bitcask_keydir));
        new_handle->keydir = new_keydir;
        memset(new_keydir, '\0', sizeof(bitcask_keydir));
        new_keydir->entries = new_entries_tree();
        new_keydir->fstats  = new_fstats_tree();

        fprintf(stderr, "LAZY deep copy SLF at line %d\n", __LINE__);
        abort(); /* SLF: TODO check git repo for code that lived here. */
        // Deep copy each item from the existing handle
    }
    else
    {
        return enif_make_badarg(env);
    }
}

static void* iterator_lock_thread(void* arg)
{
    bitcask_keydir_handle* handle = (bitcask_keydir_handle*)arg;

    // First, grab the read lock for iteration
    R_LOCK(handle->keydir);

    // Set the flag that we are ready to begin iteration
    handle->il_signal_flag = 1;

    // Signal the invoking thread to indicate that the read lock is acquired
    // and then wait for a signal back to release the read lock.
    enif_mutex_lock(handle->il_signal_mutex);
    enif_cond_signal(handle->il_signal);
    while (handle->il_signal_flag)
    {
        enif_cond_wait(handle->il_signal, handle->il_signal_mutex);
    }

    // Iterating thread is all done with the read lock; release it and exit
    R_UNLOCK(handle->keydir);
    enif_mutex_unlock(handle->il_signal_mutex);
    return 0;
}

ERL_NIF_TERM bitcask_nifs_keydir_itr(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_keydir_handle* handle;

    if (enif_get_resource(env, argv[0], bitcask_keydir_RESOURCE, (void**)&handle))
    {
        // If a iterator thread is already active for this keydir, bail
        if (handle->il_thread)
        {
            return enif_make_tuple2(env, ATOM_ERROR, ATOM_ITERATION_IN_PROCESS);
        }

        // Create the mutex and signal
        handle->il_signal_flag = 0;
        handle->il_signal_mutex = enif_mutex_create("bitcask_itr_signal_mutex");
        handle->il_signal = enif_cond_create("bitcask_itr_signal");

        // Grab the mutex BEFORE spawning the thread; otherwise we might miss the signal
        enif_mutex_lock(handle->il_signal_mutex);

        // Spawn the lock thread
        int rc = enif_thread_create("bitcask_itr_lock_thread", &(handle->il_thread),
                                    iterator_lock_thread, handle, 0);
        if (rc != 0)
        {
            // Failed to create the lock holder -- release the lock and cleanup
            enif_mutex_unlock(handle->il_signal_mutex);
            enif_cond_destroy(handle->il_signal);
            enif_mutex_destroy(handle->il_signal_mutex);
            handle->il_thread = 0;
            return enif_make_tuple2(env, ATOM_ERROR,
                                    enif_make_tuple2(env, ATOM_ILT_CREATE_ERROR, rc));
        }

        // Wait for the signal from the lock thread that iteration may commence
        while (!handle->il_signal_flag)
        {
            enif_cond_wait(handle->il_signal, handle->il_signal_mutex);
        }

        // Ready to go; initialize the faux-iterator and unlock the signal mutex
        handle->last_key_sz = 0;
        memset(handle->last_key, '\0', sizeof(handle->last_key)); /* paranoia */
        enif_mutex_unlock(handle->il_signal_mutex);

        return ATOM_OK;
    }
    else
    {
        return enif_make_badarg(env);
    }
}

ERL_NIF_TERM bitcask_nifs_keydir_itr_next(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_keydir_handle* handle;
    bitcask_keydir_entry* entry;

    if (enif_get_resource(env, argv[0], bitcask_keydir_RESOURCE, (void**)&handle))
    {
        bitcask_keydir* keydir = handle->keydir;

        if (handle->il_signal_flag != 1)
        {
            // Iteration not started!
            return enif_make_tuple2(env, ATOM_ERROR, ATOM_ITERATION_NOT_STARTED);
        }

        // SLF TODO: We need to handle items larger than
        //           MAX_KEY_LEN, this fixed buffer stuff is
        //           an ugly hack.
        char buf[MAX_KEY_LEN];
        bitcask_keydir_entry* e = (bitcask_keydir_entry*)buf;
        e->key_sz = handle->last_key_sz;
        memcpy(e->key, handle->last_key, handle->last_key_sz);
        rb_red_blk_node *node;
        while ((node = TreeNext(keydir->entries, e)) != NULL)
        {
            entry = node->info;
            if (1)
            {
                ErlNifBinary key;

                // Alloc the binary and make sure it succeeded
                if (!enif_alloc_binary_compat(env, entry->key_sz, &key))
                {
                    return ATOM_ALLOCATION_ERROR;
                }

                // Copy the data from our key to the new allocated binary
                // TODO: If we maintained a ErlNifBinary in the original entry, could we
                // get away with not doing a copy here?
                memcpy(key.data, entry->key, entry->key_sz);
                ERL_NIF_TERM curr = enif_make_tuple6(env,
                                                     ATOM_BITCASK_ENTRY,
                                                     enif_make_binary(env, &key),
                                                     enif_make_uint(env, entry->file_id),
                                                     enif_make_uint(env, entry->total_sz),
                                                     enif_make_uint64_bin(env, entry->offset),
                                                     enif_make_uint(env, entry->tstamp));

                // Update the iterator to the next entry
                handle->last_key_sz = e->key_sz; /* SLF fixme? */
                memcpy(handle->last_key, entry->key, entry->key_sz); /* SLF fixme? */
                return curr;
            }
        }

        // The iterator is at the end of the table
        return ATOM_NOT_FOUND;
    }
    else
    {
        return enif_make_badarg(env);
    }
}

ERL_NIF_TERM bitcask_nifs_keydir_itr_release(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_keydir_handle* handle;

    if (enif_get_resource(env, argv[0], bitcask_keydir_RESOURCE, (void**)&handle))
    {
        if (handle->il_signal_flag != 1)
        {
            // Iteration not started!
            return enif_make_tuple2(env, ATOM_ERROR, ATOM_ITERATION_NOT_STARTED);
        }

        // Lock the signal mutex, clear the il_signal_flag and tell the lock thread
        // to drop the read lock
        enif_mutex_lock(handle->il_signal_mutex);
        handle->il_signal_flag = 0;
        enif_cond_signal(handle->il_signal);
        enif_mutex_unlock(handle->il_signal_mutex);
        enif_thread_join(handle->il_thread, 0);

        // Cleanup
        enif_cond_destroy(handle->il_signal);
        enif_mutex_destroy(handle->il_signal_mutex);
        handle->il_thread = 0;

        return ATOM_OK;
    }
    else
    {
        return enif_make_badarg(env);
    }
}


ERL_NIF_TERM bitcask_nifs_keydir_info(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_keydir_handle* handle;

    if (enif_get_resource(env, argv[0], bitcask_keydir_RESOURCE, (void**)&handle))
    {
        bitcask_keydir* keydir = handle->keydir;
        R_LOCK(keydir);

        // Dump fstats info into a list of [{file_id, live_keys, total_keys,
        //                                   live_bytes, total_bytes}]
        ERL_NIF_TERM fstats_list = enif_make_list(env, 0);
        bitcask_fstats_entry* curr_f;
        bitcask_fstats_entry f;
        f.file_id = 0;
        rb_red_blk_node* node;
        while ((node = TreeNext(keydir->entries, &f)) != NULL)
        {
            curr_f = node->info;
            if (1)
            {
                ERL_NIF_TERM fstat = enif_make_tuple5(env,
                                                      enif_make_uint(env, curr_f->file_id),
                                                      enif_make_uint(env, curr_f->live_keys),
                                                      enif_make_uint(env, curr_f->total_keys),
                                                      enif_make_ulong(env, curr_f->live_bytes),
                                                      enif_make_ulong(env, curr_f->total_bytes));
                fstats_list = enif_make_list_cell(env, fstat, fstats_list);
            }
        }

        ERL_NIF_TERM result = enif_make_tuple3(env,
                                               enif_make_ulong(env, keydir->key_count),
                                               enif_make_ulong(env, keydir->key_bytes),
                                               fstats_list);
        R_UNLOCK(keydir);
        return result;
    }
    else
    {
        return enif_make_badarg(env);
    }
}

ERL_NIF_TERM bitcask_nifs_keydir_release(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_keydir_handle* handle;

    if (enif_get_resource(env, argv[0], bitcask_keydir_RESOURCE, (void**)&handle))
    {
        bitcask_nifs_keydir_resource_cleanup(env, handle);
        return ATOM_OK;
    }
    else
    {
        return enif_make_badarg(env);
    }
}

ERL_NIF_TERM bitcask_nifs_create_file(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char filename[MAX_PATH_LEN];
    if (enif_get_string(env, argv[0], filename, sizeof(filename), ERL_NIF_LATIN1) > 0)
    {
        // Try to open the provided filename exclusively.
        int fd = open(filename, O_CREAT | O_EXCL, S_IREAD | S_IWRITE);
        if (fd > -1)
        {
            close(fd);
            return ATOM_TRUE;
        }
        else
        {
            return ATOM_FALSE;
        }
    }
    else
    {
        return enif_make_badarg(env);
    }
}


ERL_NIF_TERM bitcask_nifs_set_osync(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    int fd;
    if (enif_get_int(env, argv[0], &fd))
    {
        // Get the current flags for the file handle in question
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags != -1)
        {
            if (fcntl(fd, F_SETFL, flags | O_SYNC) != -1)
            {
                return ATOM_OK;
            }
            else
            {
                ERL_NIF_TERM error = enif_make_tuple2(env, ATOM_SETFL_ERROR,
                                                      enif_make_atom(env, erl_errno_id(errno)));
                return enif_make_tuple2(env, ATOM_ERROR, error);
            }
        }
        else
        {
            ERL_NIF_TERM error = enif_make_tuple2(env, ATOM_GETFL_ERROR,
                                                  enif_make_atom(env, erl_errno_id(errno)));
            return enif_make_tuple2(env, ATOM_ERROR, error);
        }

    }
    else
    {
        return enif_make_badarg(env);
    }
}


ERL_NIF_TERM bitcask_nifs_lock_acquire(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char filename[MAX_PATH_LEN];
    int is_write_lock = 0;
    if (enif_get_string(env, argv[0], filename, sizeof(filename), ERL_NIF_LATIN1) > 0 &&
        enif_get_int(env, argv[1], &is_write_lock))
    {
        // Setup the flags for the lock file
        int flags = O_RDONLY;
        if (is_write_lock)
        {
            // Use O_SYNC (in addition to other flags) to ensure that when we write
            // data to the lock file it is immediately (or nearly) available to any
            // other reading processes
            flags = O_CREAT | O_EXCL | O_RDWR | O_SYNC;
        }

        // Try to open the lock file -- allocate a resource if all goes well.
        int fd = open(filename, flags, 0600);
        if (fd > -1)
        {
            // Successfully opened the file -- setup a resource to track the FD.
            unsigned int filename_sz = strlen(filename) + 1;
            bitcask_lock_handle* handle = enif_alloc_resource_compat(env, bitcask_lock_RESOURCE,
                                                              sizeof(bitcask_lock_handle) +
                                                              filename_sz);
            handle->fd = fd;
            handle->is_write_lock = is_write_lock;
            strncpy(handle->filename, filename, filename_sz);
            ERL_NIF_TERM result = enif_make_resource(env, handle);
            enif_release_resource_compat(env, handle);

            return enif_make_tuple2(env, ATOM_OK, result);
        }
        else
        {
            return enif_make_tuple2(env, ATOM_ERROR, errno_atom(env, errno));
        }
    }
    else
    {
        return enif_make_badarg(env);
    }
}

ERL_NIF_TERM bitcask_nifs_lock_release(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_lock_handle* handle;

    if (enif_get_resource(env, argv[0], bitcask_lock_RESOURCE, (void**)&handle))
    {
        lock_release(handle);
        return ATOM_OK;
    }
    else
    {
        return enif_make_badarg(env);
    }
}

ERL_NIF_TERM bitcask_nifs_lock_readdata(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_lock_handle* handle;

    if (enif_get_resource(env, argv[0], bitcask_lock_RESOURCE, (void**)&handle))
    {
        // Stat the filehandle so we can read the entire contents into memory
        struct stat sinfo;
        if (fstat(handle->fd, &sinfo) != 0)
        {
            return errno_error_tuple(env, ATOM_FSTAT_ERROR, errno);
        }

        // Allocate a binary to hold the contents of the file
        ErlNifBinary data;
        if (!enif_alloc_binary_compat(env, sinfo.st_size, &data))
        {
            return enif_make_tuple2(env, ATOM_ERROR, ATOM_ALLOCATION_ERROR);
        }

        // Read the whole file into our binary
        if (pread(handle->fd, data.data, data.size, 0) == -1)
        {
            return errno_error_tuple(env, ATOM_PREAD_ERROR, errno);
        }

        return enif_make_tuple2(env, ATOM_OK, enif_make_binary(env, &data));
    }
    else
    {
        return enif_make_badarg(env);
    }
}

ERL_NIF_TERM bitcask_nifs_lock_writedata(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    bitcask_lock_handle* handle;
    ErlNifBinary data;

    if (enif_get_resource(env, argv[0], bitcask_lock_RESOURCE, (void**)&handle) &&
        enif_inspect_binary(env, argv[1], &data))
    {
        if (handle->is_write_lock)
        {
            // Truncate the file first, to ensure that the lock file only contains what
            // we're about to write
            if (ftruncate(handle->fd, 0) == -1)
            {
                return errno_error_tuple(env, ATOM_FTRUNCATE_ERROR, errno);
            }

            // Write the new blob of data to the lock file. Note that we use O_SYNC to
            // ensure that the data is available ASAP to reading processes.
            if (pwrite(handle->fd, data.data, data.size, 0) == -1)
            {
                return errno_error_tuple(env, ATOM_PWRITE_ERROR, errno);
            }

            return ATOM_OK;
        }
        else
        {
            // Tried to write data to a read lock
            return enif_make_tuple2(env, ATOM_ERROR, ATOM_LOCK_NOT_WRITABLE);
        }
    }
    else
    {
        return enif_make_badarg(env);
    }
}

ERL_NIF_TERM errno_atom(ErlNifEnv* env, int error)
{
    return enif_make_atom(env, erl_errno_id(error));
}

ERL_NIF_TERM errno_error_tuple(ErlNifEnv* env, ERL_NIF_TERM key, int error)
{
    // Construct a tuple of form: {error, {Key, ErrnoAtom}}
    return enif_make_tuple2(env, ATOM_ERROR,
                            enif_make_tuple2(env, key, errno_atom(env, error)));
}

static void lock_release(bitcask_lock_handle* handle)
{
    if (handle->fd > 0)
    {
        // If this is a write lock, we need to delete the file as part of cleanup. But be
        // sure to do this BEFORE letting go of the file handle so as to ensure consistency
        // with other readers.
        if (handle->is_write_lock)
        {
            // TODO: Come up with some way to complain/error log if this unlink failed for some
            // reason!!
            unlink(handle->filename);
        }

        close(handle->fd);
        handle->fd = -1;
    }
}

static void free_keydir(ErlNifEnv* env, bitcask_keydir* keydir)
{
    // Delete all the entries in the hash table, which also has the effect of
    // freeing up all resources associated with the table.

    fprintf(stderr, "SLF: free_keydir\n");
    my_env_copy = env;         /* RBTreeDestroy hack setup */

    RBTreeDestroy(keydir->entries);
    RBTreeDestroy(keydir->fstats);

    my_env_copy = NULL;         /* RBTreeDestroy hack finished */

    keydir->entries = NULL;
    keydir->fstats = NULL;
}


static void bitcask_nifs_keydir_resource_cleanup(ErlNifEnv* env, void* arg)
{
    bitcask_keydir_handle* handle = (bitcask_keydir_handle*)arg;
    bitcask_keydir* keydir = handle->keydir;

    // First, check that there is even a keydir available. If keydir_release
    // was invoked manually, we might have already cleaned up the keydir
    // and this round of cleanup can noop. Otherwise, clear out the handle's
    // reference to the keydir so that repeat calls function as expected
    if (!handle->keydir)
    {
        return;
    }
    else
    {
        handle->keydir = 0;
    }

    // If the keydir has a lock, we need to decrement the refcount and
    // potentially release it
    if (keydir->lock)
    {
        bitcask_priv_data* priv = (bitcask_priv_data*)enif_priv_data(env);
        enif_mutex_lock(priv->global_keydirs_lock);
        keydir->refcount--;
        if (keydir->refcount == 0)
        {
            // This is the last reference to the named keydir. As such,
            // remove it from the hashtable so no one else tries to use it
            rb_red_blk_node* node = RBExactQuery(priv->global_keydirs, keydir->name);
            RBDelete(priv->global_keydirs, node);
        }
        else
        {
            // At least one other reference; just throw away our keydir pointer
            // so the check below doesn't release the memory.
            keydir = 0;
        }

        // Unlock ASAP. Wanted to avoid holding this mutex while we clean up the
        // keydir, since it may take a while to walk a large keydir and free each
        // entry.
        enif_mutex_unlock(priv->global_keydirs_lock);
    }

    // If keydir is still defined, it's either privately owner or has a
    // refcount of 0. Either way, we want to release it.
    if (keydir)
    {
        if (keydir->lock)
        {
            enif_rwlock_destroy(keydir->lock);
        }

        free_keydir(env, keydir);
    }
}

static void bitcask_nifs_lock_resource_cleanup(ErlNifEnv* env, void* arg)
{
    bitcask_lock_handle* handle = (bitcask_lock_handle*)arg;
    lock_release(handle);
}

/* !@#$! The !@#$! library assumes exactly -1, 0, or 1. !@#$! */

static int memcmp_101(const char *a, const char *b, int len)
{
    int cmp = memcmp(a, b, len);

    if (cmp < 0)
        return -1;
    else if (cmp == 0)
        return 0;
    else
        return 1;
}

static int keydir_entry_equal(const void* x, const void* y)
{
    const bitcask_keydir_entry_keycheat* lhs = x;
    const bitcask_keydir_entry_keycheat* rhs = y;
    int cmp;

    /* lhs->key[lhs->key_sz] = '\0'; rhs->key[rhs->key_sz] = '\0'; fprintf(stderr, "keydir_entry_equal: %s vs %s\n", lhs->key, rhs->key); */
    if (lhs->key_sz < rhs->key_sz)
    {
        cmp = memcmp_101(lhs->key, rhs->key, lhs->key_sz);
        if (cmp == 0)
            return -1;
        else
            return cmp;
    }
    else if (lhs->key_sz > rhs->key_sz)
    {
        cmp = memcmp_101(lhs->key, rhs->key, rhs->key_sz);
        if (cmp == 0)
            return 1;
        else
            return cmp;
    }
    else
    {
        return memcmp_101(lhs->key, rhs->key, lhs->key_sz);
    }
}

static void keydir_destroy_key(void *x)
{
}

static void keydir_destroy_info(void *x)
{
    if (my_env_copy == NULL) {
        // noop
    } else {
        bitcask_keydir_entry *current_entry = x;
        enif_free_compat(env, current_entry);
    }
}

static void keydir_print_key(const void *x)
{
    const bitcask_keydir_entry_keycheat* e = x;

    printf("kdir k: ");
    fwrite(e->key, e->key_sz, 1, stdout);
    printf("\n");
}

static void keydir_print_info(void *x)
{
    bitcask_keydir_entry* e = x;

    printf("file_id %lu total_sz %lu offset %llu tstamp %lu {key...}\n",
           (unsigned long) e->file_id, (unsigned long) e->total_sz, (unsigned long long) e->offset, (unsigned long) e->tstamp);
}

static int fstats_entry_equal(const void* x, const void* y)
{
    const uint32_t* lhs = x;
    const uint32_t* rhs = y;

    if (*lhs < *rhs)
        return -1;
    else if (*lhs == *rhs)
        return 0;
    else
        return -1;
}

static void fstats_destroy_key(void *x)
{
}

static void fstats_destroy_info(void *x)
{
    if (my_env_copy == NULL) {
        // noop
    } else {
        bitcask_fstats_entry *curr_f = x;
        enif_free_compat(env, curr_f);
    }
}

static void fstats_print_key(const void *x)
{
    const uint32_t* file_id = x;

    printf("fstats k: %ul\n", *file_id);
}

static void fstats_print_info(void *x)
{
    bitcask_fstats_entry* e = x;

    printf("file_id %lu live_keys %lu total_keys %lu live_bytes %llu total_bytes %llu\n",
           (unsigned long) e->file_id, (unsigned long) e->live_keys,
           (unsigned long) e->total_keys, (unsigned long long) e->live_bytes,
           (unsigned long long) e->total_bytes);
}

static int bitcask_keydir_entry_equal(const void* x, const void* y)
{
    const bitcask_keydir* lhs = x;
    const bitcask_keydir* rhs = y;
    int n;

    if ((n = strcmp(lhs->name, rhs->name)) < 0)
        return -1;
    else if (n == 0)
        return 0;
    else
        return 1;
}

static void bitcask_keydir_destroy_key(void *x)
{
}

static void bitcask_keydir_destroy_info(void *x)
{
}

static void bitcask_keydir_print_key(const void *x)
{
    const char* name = x;

    printf("bitcask_keydir k: %s\n", name);
}

static void bitcask_keydir_print_info(void *x)
{
    bitcask_keydir* e = x;

    printf("key_count %ld key_bytes %ld refcount %u is_ready %d\n",
           e->key_count, e->key_bytes, e->refcount, e->is_ready);
}

static rb_red_blk_tree* new_entries_tree(void)
{
    return RBTreeCreate(keydir_entry_equal, keydir_destroy_key,
                        keydir_destroy_info,
                        keydir_print_key, keydir_print_info);
}

static rb_red_blk_tree* new_fstats_tree(void)
{
    return RBTreeCreate(fstats_entry_equal, fstats_destroy_key,
                        fstats_destroy_info,
                        fstats_print_key, fstats_print_info);
}

static rb_red_blk_tree* new_global_tree(void)
{
    return RBTreeCreate(bitcask_keydir_entry_equal,
                        bitcask_keydir_destroy_key,
                        bitcask_keydir_destroy_info,
                        bitcask_keydir_print_key,
                        bitcask_keydir_print_info); 
}

void my_tree_insert(rb_red_blk_tree *tree, void *key, void *info)
{
    /*
    ** All caller code has checked for duplicates already, so there's
    ** no risk here of inserting a duplicate.
    */
    RBTreeInsert(tree, key, info);
}

static int on_load(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    bitcask_keydir_RESOURCE = enif_open_resource_type_compat(env, "bitcask_keydir_resource",
                                                      &bitcask_nifs_keydir_resource_cleanup,
                                                      ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER,
                                                      0);

    bitcask_lock_RESOURCE = enif_open_resource_type_compat(env, "bitcask_lock_resource",
                                                    &bitcask_nifs_lock_resource_cleanup,
                                                    ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER,
                                                    0);
    // Initialize shared keydir hashtable
    bitcask_priv_data* priv = enif_alloc_compat(env, sizeof(bitcask_priv_data));
    priv->global_keydirs = new_global_tree();
    priv->global_keydirs_lock = enif_mutex_create("bitcask_global_handles_lock");
    *priv_data = priv;

    // Initialize atoms that we use throughout the NIF.
    ATOM_ALLOCATION_ERROR = enif_make_atom(env, "allocation_error");
    ATOM_ALREADY_EXISTS = enif_make_atom(env, "already_exists");
    ATOM_BITCASK_ENTRY = enif_make_atom(env, "bitcask_entry");
    ATOM_ERROR = enif_make_atom(env, "error");
    ATOM_FALSE = enif_make_atom(env, "false");
    ATOM_FSTAT_ERROR = enif_make_atom(env, "fstat_error");
    ATOM_FTRUNCATE_ERROR = enif_make_atom(env, "ftruncate_error");
    ATOM_GETFL_ERROR = enif_make_atom(env, "getfl_error");
    ATOM_ILT_CREATE_ERROR = enif_make_atom(env, "ilt_create_error");
    ATOM_ITERATION_IN_PROCESS = enif_make_atom(env, "iteration_in_process");
    ATOM_ITERATION_NOT_PERMITTED = enif_make_atom(env, "iteration_not_permitted");
    ATOM_ITERATION_NOT_STARTED = enif_make_atom(env, "iteration_not_started");
    ATOM_LOCK_NOT_WRITABLE = enif_make_atom(env, "lock_not_writable");
    ATOM_NOT_FOUND = enif_make_atom(env, "not_found");
    ATOM_NOT_READY = enif_make_atom(env, "not_ready");
    ATOM_OK = enif_make_atom(env, "ok");
    ATOM_PREAD_ERROR = enif_make_atom(env, "pread_error");
    ATOM_PWRITE_ERROR = enif_make_atom(env, "pwrite_error");
    ATOM_READY = enif_make_atom(env, "ready");
    ATOM_SETFL_ERROR = enif_make_atom(env, "setfl_error");
    ATOM_TRUE = enif_make_atom(env, "true");

    return 0;
}

ERL_NIF_INIT(bitcask_nifs, nif_funcs, &on_load, NULL, NULL, NULL);

