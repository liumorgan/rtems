/**
 * @file
 *
 * @brief LibIO Internal Interface
 * 
 * This file is the libio internal interface.
 */

/*
 *  COPYRIGHT (c) 1989-2011.
 *  On-Line Applications Research Corporation (OAR).
 *
 *  Modifications to support reference counting in the file system are
 *  Copyright (c) 2012 embedded brains GmbH.
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.rtems.org/license/LICENSE.
 */

#ifndef _RTEMS_RTEMS_LIBIO__H
#define _RTEMS_RTEMS_LIBIO__H

#include <sys/uio.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>

#include <rtems.h>
#include <rtems/libio.h>
#include <rtems/seterr.h>
#include <rtems/score/assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup LibIOInternal IO Internal Library
 *
 * @brief Internal IO library API and implementation.
 *
 */
/**@{**/

#define RTEMS_FILESYSTEM_SYMLOOP_MAX 32

/*
 * Not defined in newlib so provide here. Users should use dup2 and
 * not this non-portable fcntl command. Provided here to allow the
 * RTEMS implementation to work.
 */
#define F_DUP2FD 20

/*
 *  Semaphore to protect the io table
 */

#define RTEMS_LIBIO_SEM         rtems_build_name('L', 'B', 'I', 'O')
#define RTEMS_LIBIO_IOP_SEM(n)  rtems_build_name('L', 'B', 'I', n)

extern rtems_id                          rtems_libio_semaphore;

/*
 *  File descriptor Table Information
 */

extern const uint32_t rtems_libio_number_iops;
extern rtems_libio_t rtems_libio_iops[];
extern void *rtems_libio_iop_free_head;
extern void **rtems_libio_iop_free_tail;

extern const rtems_filesystem_file_handlers_r rtems_filesystem_null_handlers;

extern rtems_filesystem_mount_table_entry_t rtems_filesystem_null_mt_entry;

/**
 * @brief The global null location.
 *
 * Every operation and the open and fstat handlers of this location returns an
 * error status.  The errno is not touched by these operations and handlers.
 * The purpose of this location is to deliver the error return status for a
 * previous error condition which must set the errno accordingly.
 *
 * The usage of this null location instead of the NULL pointer eliminates 
 * a lot of branches.
 *
 * The user environment root and current directory are statically initialized
 * with the null location.  Due to that all file system services are in a
 * defined state even if no root file system was mounted.
 */
extern rtems_filesystem_global_location_t rtems_filesystem_global_location_null;

/**
 * @brief Sets the iop flags to the specified flags together with
 * LIBIO_FLAGS_OPEN.
 *
 * Use this once a file descriptor allocated via rtems_libio_allocate() is
 * fully initialized.
 *
 * @param[in] iop The iop.
 * @param[in] flags The flags.
 */
static inline void rtems_libio_iop_flags_initialize(
  rtems_libio_t *iop,
  uint32_t       flags
)
{
  _Atomic_Store_uint(
    &iop->flags,
    LIBIO_FLAGS_OPEN | flags,
    ATOMIC_ORDER_RELEASE
  );
}

/**
 * @brief Sets the specified flags in the iop.
 *
 * @param[in] iop The iop.
 * @param[in] set The flags to set.
 *
 * @return The previous flags.
 */
static inline unsigned int rtems_libio_iop_flags_set(
  rtems_libio_t *iop,
  unsigned int   set
)
{
  return _Atomic_Fetch_or_uint( &iop->flags, set, ATOMIC_ORDER_RELAXED );
}

/**
 * @brief Clears the specified flags in the iop.
 *
 * @param[in] iop The iop.
 * @param[in] clear The flags to clear.
 *
 * @return The previous flags.
 */
static inline unsigned int rtems_libio_iop_flags_clear(
  rtems_libio_t *iop,
  unsigned int   clear
)
{
  return _Atomic_Fetch_and_uint( &iop->flags, ~clear, ATOMIC_ORDER_RELAXED );
}

/**
 * @brief Maps a file descriptor to the iop.
 *
 * The file descriptor must be a valid index into the iop table.
 *
 * @param[in] fd The file descriptor.
 *
 * @return The iop corresponding to the specified file descriptor.
 */
static inline rtems_libio_t *rtems_libio_iop( int fd )
{
  return &rtems_libio_iops[ fd ];
}

/**
 * @brief Holds a refernece to the iop.
 *
 * @param[in] iop The iop.
 *
 * @return The flags corresponding to the specified iop.
 */
static inline unsigned int rtems_libio_iop_hold( rtems_libio_t *iop )
{
  return _Atomic_Fetch_add_uint(
    &iop->flags,
    LIBIO_FLAGS_REFERENCE_INC,
    ATOMIC_ORDER_ACQUIRE
  );
}

/**
 * @brief Drops a refernece to the iop.
 *
 * @param[in] iop The iop.
 */
static inline void rtems_libio_iop_drop( rtems_libio_t *iop )
{
#if defined(RTEMS_DEBUG)
  unsigned int flags;
  bool         success;

  flags = _Atomic_Load_uint( &iop->flags, ATOMIC_ORDER_RELAXED );

  do {
    unsigned int desired;

    _Assert( flags >= LIBIO_FLAGS_REFERENCE_INC );

    desired = flags - LIBIO_FLAGS_REFERENCE_INC;
    success = _Atomic_Compare_exchange_uint(
      &iop->flags,
      &flags,
      desired,
      ATOMIC_ORDER_RELEASE,
      ATOMIC_ORDER_RELAXED
    );
  } while ( !success );
#else
  _Atomic_Fetch_sub_uint(
    &iop->flags,
    LIBIO_FLAGS_REFERENCE_INC,
    ATOMIC_ORDER_RELEASE
  );
#endif
}

/*
 *  rtems_libio_iop_to_descriptor
 *
 *  Macro to convert an internal file descriptor pointer (iop) into
 *  the integer file descriptor used by the "section 2" system calls.
 */

#define rtems_libio_iop_to_descriptor(_iop) \
  ((_iop) - &rtems_libio_iops[0])

/*
 *  rtems_libio_check_is_open
 *
 *  Macro to check if a file descriptor is actually open.
 */

#define rtems_libio_check_is_open(_iop) \
  do {                                               \
      if (((_iop)->flags & LIBIO_FLAGS_OPEN) == 0) { \
          errno = EBADF;                             \
          return -1;                                 \
      }                                              \
  } while (0)

/**
 * @brief Macro to get the iop for the specified file descriptor.
 *
 * Checks that the file descriptor is in the valid range and open.
 */
#define LIBIO_GET_IOP( _fd, _iop ) \
  do { \
    unsigned int _flags; \
    if ( (uint32_t) ( _fd ) >= rtems_libio_number_iops ) { \
      rtems_set_errno_and_return_minus_one( EBADF ); \
    } \
    _iop = rtems_libio_iop( _fd ); \
    _flags = rtems_libio_iop_hold( _iop ); \
    if ( ( _flags & LIBIO_FLAGS_OPEN ) == 0 ) { \
      rtems_libio_iop_drop( _iop ); \
      rtems_set_errno_and_return_minus_one( EBADF ); \
    } \
  } while ( 0 )

/**
 * @brief Macro to get the iop for the specified file descriptor with access
 * flags and error.
 *
 * Checks that the file descriptor is in the valid range and open.
 */
#define LIBIO_GET_IOP_WITH_ACCESS( _fd, _iop, _access_flags, _access_error ) \
  do { \
    unsigned int _flags; \
    unsigned int _mandatory; \
    if ( (uint32_t) ( _fd ) >= rtems_libio_number_iops ) { \
      rtems_set_errno_and_return_minus_one( EBADF ); \
    } \
    _iop = rtems_libio_iop( _fd ); \
    _flags = rtems_libio_iop_hold( _iop ); \
    _mandatory = LIBIO_FLAGS_OPEN | ( _access_flags ); \
    if ( ( _flags & _mandatory ) != _mandatory ) { \
      int _error; \
      rtems_libio_iop_drop( _iop ); \
      if ( ( _flags & LIBIO_FLAGS_OPEN ) == 0 ) { \
        _error = EBADF; \
      } else { \
        _error = _access_error; \
      } \
      rtems_set_errno_and_return_minus_one( _error ); \
    } \
  } while ( 0 )

/*
 *  rtems_libio_check_buffer
 *
 *  Macro to check if a buffer pointer is valid.
 */

#define rtems_libio_check_buffer(_buffer) \
  do {                                    \
      if ((_buffer) == 0) {               \
          errno = EINVAL;                 \
          return -1;                      \
      }                                   \
  } while (0)

/*
 *  rtems_libio_check_count
 *
 *  Macro to check if a count or length is valid.
 */

#define rtems_libio_check_count(_count) \
  do {                                  \
      if ((_count) == 0) {              \
          return 0;                     \
      }                                 \
  } while (0)

/**
 * @brief Clones a node.
 *
 * The caller must hold the file system instance lock.
 *
 * @param[out] clone The cloned location.
 * @param[in] master The master location.
 *
 * @see rtems_filesystem_instance_lock().
 */
void rtems_filesystem_location_clone(
  rtems_filesystem_location_info_t *clone,
  const rtems_filesystem_location_info_t *master
);

/**
 * @brief Releases all resources of a location.
 *
 * This function may block on a mutex and may complete an unmount process.
 *
 * @param[in] loc The location to free.
 *
 * @note The file system root location is released by the file system
 * instance destruction handler (see @ref rtems_filesystem_fsunmount_me_t).
 *
 * @see rtems_filesystem_freenode_t.
 */
void rtems_filesystem_location_free( rtems_filesystem_location_info_t *loc );

/*
 *  External structures
 */
#include <rtems/userenv.h>

void rtems_libio_free_user_env( void *env );

extern pthread_key_t rtems_current_user_env_key;

static inline void rtems_libio_lock( void )
{
  rtems_semaphore_obtain( rtems_libio_semaphore, RTEMS_WAIT, RTEMS_NO_TIMEOUT );
}

static inline void rtems_libio_unlock( void )
{
  rtems_semaphore_release( rtems_libio_semaphore );
}

static inline void rtems_filesystem_mt_lock( void )
{
  rtems_libio_lock();
}

static inline void rtems_filesystem_mt_unlock( void )
{
  rtems_libio_unlock();
}

extern rtems_interrupt_lock rtems_filesystem_mt_entry_lock_control;

#define rtems_filesystem_mt_entry_declare_lock_context( ctx ) \
  rtems_interrupt_lock_context ctx

#define rtems_filesystem_mt_entry_lock( ctx ) \
  rtems_interrupt_lock_acquire( &rtems_filesystem_mt_entry_lock_control, &ctx )

#define rtems_filesystem_mt_entry_unlock( ctx ) \
  rtems_interrupt_lock_release( &rtems_filesystem_mt_entry_lock_control, &ctx )

static inline void rtems_filesystem_instance_lock(
  const rtems_filesystem_location_info_t *loc
)
{
  const rtems_filesystem_mount_table_entry_t *mt_entry = loc->mt_entry;

  (*mt_entry->ops->lock_h)( mt_entry );
}

static inline void rtems_filesystem_instance_unlock(
  const rtems_filesystem_location_info_t *loc
)
{
  const rtems_filesystem_mount_table_entry_t *mt_entry = loc->mt_entry;

  (*mt_entry->ops->unlock_h)( mt_entry );
}

/*
 *  File Descriptor Routine Prototypes
 */

/**
 * This routine searches the IOP Table for an unused entry.  If it
 * finds one, it returns it.  Otherwise, it returns NULL.
 */
rtems_libio_t *rtems_libio_allocate(void);

/**
 * Convert UNIX fnctl(2) flags to ones that RTEMS drivers understand
 */
unsigned int rtems_libio_fcntl_flags( int fcntl_flags );

/**
 * Convert RTEMS internal flags to UNIX fnctl(2) flags
 */
int rtems_libio_to_fcntl_flags( unsigned int flags );

/**
 * This routine frees the resources associated with an IOP (file descriptor)
 * and clears the slot in the IOP Table.
 */
void rtems_libio_free(
  rtems_libio_t *iop
);

/*
 *  File System Routine Prototypes
 */

rtems_filesystem_location_info_t *
rtems_filesystem_eval_path_start(
  rtems_filesystem_eval_path_context_t *ctx,
  const char *path,
  int eval_flags
);

rtems_filesystem_location_info_t *
rtems_filesystem_eval_path_start_with_parent(
  rtems_filesystem_eval_path_context_t *ctx,
  const char *path,
  int eval_flags,
  rtems_filesystem_location_info_t *parentloc,
  int parent_eval_flags
);

rtems_filesystem_location_info_t *
rtems_filesystem_eval_path_start_with_root_and_current(
  rtems_filesystem_eval_path_context_t *ctx,
  const char *path,
  size_t pathlen,
  int eval_flags,
  rtems_filesystem_global_location_t *const *global_root_ptr,
  rtems_filesystem_global_location_t *const *global_current_ptr
);

void rtems_filesystem_eval_path_continue(
  rtems_filesystem_eval_path_context_t *ctx
);

void rtems_filesystem_eval_path_cleanup(
  rtems_filesystem_eval_path_context_t *ctx
);

void rtems_filesystem_eval_path_recursive(
  rtems_filesystem_eval_path_context_t *ctx,
  const char *path,
  size_t pathlen
);

void rtems_filesystem_eval_path_cleanup_with_parent(
  rtems_filesystem_eval_path_context_t *ctx,
  rtems_filesystem_location_info_t *parentloc
);

/**
 * @brief Requests a path evaluation restart.
 *
 * Sets the start and current location to the new start location.  The caller
 * must terminate its current evaluation process.  The path evaluation
 * continues in the next loop iteration within
 * rtems_filesystem_eval_path_continue().  This avoids recursive invocations.
 * The function obtains the new start location and clones it to set the new
 * current location.  The previous start and current locations are released.
 *
 * @param[in, out] ctx The path evaluation context.
 * @param[in, out] newstartloc_ptr Pointer to the new start location.
 */
void rtems_filesystem_eval_path_restart(
  rtems_filesystem_eval_path_context_t *ctx,
  rtems_filesystem_global_location_t **newstartloc_ptr
);

typedef enum {
  RTEMS_FILESYSTEM_EVAL_PATH_GENERIC_CONTINUE,
  RTEMS_FILESYSTEM_EVAL_PATH_GENERIC_DONE,
  RTEMS_FILESYSTEM_EVAL_PATH_GENERIC_NO_ENTRY
} rtems_filesystem_eval_path_generic_status;

/**
 * @brief Tests if the current location is a directory.
 *
 * @param[in, out] ctx The path evaluation context.
 * @param[in, out] arg The handler argument.
 *
 * @retval true The current location is a directory.
 * @retval false Otherwise.
 *
 * @see rtems_filesystem_eval_path_generic().
 */
typedef bool (*rtems_filesystem_eval_path_is_directory)(
  rtems_filesystem_eval_path_context_t *ctx,
  void *arg
);

/**
 * @brief Evaluates a token.
 *
 * @param[in, out] ctx The path evaluation context.
 * @param[in, out] arg The handler argument.
 * @param[in] token The token contents.
 * @param[in] tokenlen The token length in characters.
 *
 * @retval status The generic path evaluation status.
 *
 * @see rtems_filesystem_eval_path_generic().
 */
typedef rtems_filesystem_eval_path_generic_status
(*rtems_filesystem_eval_path_eval_token)(
  rtems_filesystem_eval_path_context_t *ctx,
  void *arg,
  const char *token,
  size_t tokenlen
);

typedef struct {
  rtems_filesystem_eval_path_is_directory is_directory;
  rtems_filesystem_eval_path_eval_token eval_token;
} rtems_filesystem_eval_path_generic_config;

void rtems_filesystem_eval_path_generic(
  rtems_filesystem_eval_path_context_t *ctx,
  void *arg,
  const rtems_filesystem_eval_path_generic_config *config
);

void rtems_filesystem_initialize(void);

/**
 * @brief Copies a location.
 *
 * A bitwise copy is performed.  The destination location will be added to the
 * corresponding mount entry.
 *
 * @param[out] dst The destination location.
 * @param[in] src The  source location.
 *
 * @retval dst The destination location.
 *
 * @see rtems_filesystem_location_clone().
 */
rtems_filesystem_location_info_t *rtems_filesystem_location_copy(
  rtems_filesystem_location_info_t *dst,
  const rtems_filesystem_location_info_t *src
);

static inline rtems_filesystem_location_info_t *
rtems_filesystem_location_initialize_to_null(
  rtems_filesystem_location_info_t *loc
)
{
  return rtems_filesystem_location_copy(
    loc,
    &rtems_filesystem_global_location_null.location
  );
}

rtems_filesystem_global_location_t *
rtems_filesystem_location_transform_to_global(
  rtems_filesystem_location_info_t *loc
);

/**
 * @brief Assigns a global file system location.
 *
 * @param[in, out] lhs_global_loc_ptr Pointer to the global left hand side file
 * system location.  The current left hand side location will be released.
 * @param[in] rhs_global_loc The global right hand side file system location.
 */
void rtems_filesystem_global_location_assign(
  rtems_filesystem_global_location_t **lhs_global_loc_ptr,
  rtems_filesystem_global_location_t *rhs_global_loc
);

/**
 * @brief Obtains a global file system location.
 *
 * Deferred releases will be processed in this function.
 *
 * This function must be called from normal thread context and may block on a
 * mutex.  Thread dispatching is disabled to protect some critical sections.
 *
 * @param[in] global_loc_ptr Pointer to the global file system location.
 *
 * @return A global file system location.  It returns always a valid object.
 * In case of an error, the global null location will be returned.  Each
 * operation or handler of the null location returns an error status.  The
 * errno indicates the error.  The NULL pointer is never returned.
 *
 * @see rtems_filesystem_location_transform_to_global(),
 * rtems_filesystem_global_location_obtain_null(), and
 * rtems_filesystem_global_location_release().
 */
rtems_filesystem_global_location_t *rtems_filesystem_global_location_obtain(
  rtems_filesystem_global_location_t *const *global_loc_ptr
);

/**
 * @brief Releases a global file system location.
 *
 * In case the reference count reaches zero, all associated resources will be
 * released.  This may include the complete unmount of the corresponding file
 * system instance.
 *
 * This function may block on a mutex.  It may be called within critical
 * sections of the operating system.  In this case the release will be
 * deferred.  The next obtain call will do the actual release.
 *
 * @param[in] global_loc The global file system location.  It must not be NULL.
 * @param[in] deferred If true, then do a deferred release, otherwise release
 *   it immediately.
 *
 * @see rtems_filesystem_global_location_obtain().
 */
void rtems_filesystem_global_location_release(
  rtems_filesystem_global_location_t *global_loc,
  bool deferred
);

void rtems_filesystem_location_detach(
  rtems_filesystem_location_info_t *detach
);

void rtems_filesystem_location_copy_and_detach(
  rtems_filesystem_location_info_t *copy,
  rtems_filesystem_location_info_t *detach
);

static inline rtems_filesystem_global_location_t *
rtems_filesystem_global_location_obtain_null(void)
{
  rtems_filesystem_global_location_t *global_loc = NULL;

  return rtems_filesystem_global_location_obtain( &global_loc );
}

static inline bool rtems_filesystem_location_is_null(
  const rtems_filesystem_location_info_t *loc
)
{
  return loc->handlers == &rtems_filesystem_null_handlers;
}

static inline bool rtems_filesystem_global_location_is_null(
  const rtems_filesystem_global_location_t *global_loc
)
{
  return rtems_filesystem_location_is_null( &global_loc->location );
}

static inline void rtems_filesystem_location_error(
  const rtems_filesystem_location_info_t *loc,
  int eno
)
{
  if ( !rtems_filesystem_location_is_null( loc ) ) {
    errno = eno;
  }
}

int rtems_filesystem_mknod(
  const rtems_filesystem_location_info_t *parentloc,
  const char *name,
  size_t namelen,
  mode_t mode,
  dev_t dev
);

int rtems_filesystem_chdir( rtems_filesystem_location_info_t *loc );

int rtems_filesystem_chmod(
  const rtems_filesystem_location_info_t *loc,
  mode_t mode
);

int rtems_filesystem_chown(
  const rtems_filesystem_location_info_t *loc,
  uid_t owner,
  gid_t group
);

static inline bool rtems_filesystem_is_ready_for_unmount(
  rtems_filesystem_mount_table_entry_t *mt_entry
)
{
  bool ready = !mt_entry->mounted
    && rtems_chain_has_only_one_node( &mt_entry->location_chain )
    && mt_entry->mt_fs_root->reference_count == 1;

  if ( ready ) {
    rtems_chain_initialize_empty( &mt_entry->location_chain );
  }

  return ready;
}

static inline void rtems_filesystem_location_add_to_mt_entry(
  rtems_filesystem_location_info_t *loc
)
{
  rtems_filesystem_mt_entry_declare_lock_context( lock_context );

  rtems_filesystem_mt_entry_lock( lock_context );
  rtems_chain_append_unprotected(
    &loc->mt_entry->location_chain,
    &loc->mt_entry_node
  );
  rtems_filesystem_mt_entry_unlock( lock_context );
}

void rtems_filesystem_location_remove_from_mt_entry(
  rtems_filesystem_location_info_t *loc
);

void rtems_filesystem_do_unmount(
  rtems_filesystem_mount_table_entry_t *mt_entry
);

static inline bool rtems_filesystem_location_is_instance_root(
  const rtems_filesystem_location_info_t *loc
)
{
  const rtems_filesystem_mount_table_entry_t *mt_entry = loc->mt_entry;

  return (*mt_entry->ops->are_nodes_equal_h)(
    loc,
    &mt_entry->mt_fs_root->location
  );
}

static inline const char *rtems_filesystem_eval_path_get_path(
  rtems_filesystem_eval_path_context_t *ctx
)
{
  return ctx->path;
}

static inline size_t rtems_filesystem_eval_path_get_pathlen(
  rtems_filesystem_eval_path_context_t *ctx
)
{
  return ctx->pathlen;
}

static inline void rtems_filesystem_eval_path_set_path(
  rtems_filesystem_eval_path_context_t *ctx,
  const char *path,
  size_t pathlen
)
{
  ctx->path = path;
  ctx->pathlen = pathlen;
}

static inline void rtems_filesystem_eval_path_clear_path(
  rtems_filesystem_eval_path_context_t *ctx
)
{
  ctx->pathlen = 0;
}

static inline const char *rtems_filesystem_eval_path_get_token(
  rtems_filesystem_eval_path_context_t *ctx
)
{
  return ctx->token;
}

static inline size_t rtems_filesystem_eval_path_get_tokenlen(
  rtems_filesystem_eval_path_context_t *ctx
)
{
  return ctx->tokenlen;
}

static inline void rtems_filesystem_eval_path_set_token(
  rtems_filesystem_eval_path_context_t *ctx,
  const char *token,
  size_t tokenlen
)
{
  ctx->token = token;
  ctx->tokenlen = tokenlen;
}

static inline void rtems_filesystem_eval_path_clear_token(
  rtems_filesystem_eval_path_context_t *ctx
)
{
  ctx->tokenlen = 0;
}

static inline void rtems_filesystem_eval_path_put_back_token(
  rtems_filesystem_eval_path_context_t *ctx
)
{
  size_t tokenlen = ctx->tokenlen;

  ctx->path -= tokenlen;
  ctx->pathlen += tokenlen;
  ctx->tokenlen = 0;
}

void rtems_filesystem_eval_path_eat_delimiter(
  rtems_filesystem_eval_path_context_t *ctx
);

void rtems_filesystem_eval_path_next_token(
  rtems_filesystem_eval_path_context_t *ctx
);

static inline void rtems_filesystem_eval_path_get_next_token(
  rtems_filesystem_eval_path_context_t *ctx,
  const char **token,
  size_t *tokenlen
)
{
  rtems_filesystem_eval_path_next_token(ctx);
  *token = ctx->token;
  *tokenlen = ctx->tokenlen;
}

static inline rtems_filesystem_location_info_t *
rtems_filesystem_eval_path_get_currentloc(
  rtems_filesystem_eval_path_context_t *ctx
)
{
  return &ctx->currentloc;
}

static inline bool rtems_filesystem_eval_path_has_path(
  const rtems_filesystem_eval_path_context_t *ctx
)
{
  return ctx->pathlen > 0;
}

static inline bool rtems_filesystem_eval_path_has_token(
  const rtems_filesystem_eval_path_context_t *ctx
)
{
  return ctx->tokenlen > 0;
}

static inline int rtems_filesystem_eval_path_get_flags(
  const rtems_filesystem_eval_path_context_t *ctx
)
{
  return ctx->flags;
}

static inline void rtems_filesystem_eval_path_set_flags(
  rtems_filesystem_eval_path_context_t *ctx,
  int flags
)
{
  ctx->flags = flags;
}

static inline void rtems_filesystem_eval_path_clear_and_set_flags(
  rtems_filesystem_eval_path_context_t *ctx,
  int clear,
  int set
)
{
  int flags = ctx->flags;

  flags &= ~clear;
  flags |= set;

  ctx->flags = flags;
}

static inline void rtems_filesystem_eval_path_extract_currentloc(
  rtems_filesystem_eval_path_context_t *ctx,
  rtems_filesystem_location_info_t *get
)
{
  rtems_filesystem_location_copy_and_detach(
    get,
    &ctx->currentloc
  );
}

void rtems_filesystem_eval_path_error(
  rtems_filesystem_eval_path_context_t *ctx,
  int eno
);

/**
 * @brief Checks that the locations exist in the same file system instance.
 *
 * @retval 0 The locations exist and are in the same file system instance.
 * @retval -1 An error occurred.  The @c errno indicates the error.
 */
int rtems_filesystem_location_exists_in_same_instance_as(
  const rtems_filesystem_location_info_t *a,
  const rtems_filesystem_location_info_t *b
);

/**
 * @brief Checks if access to an object is allowed for the current user.
 *
 * If the effective UID is zero or equals the UID of the object, then the user
 * permission flags of the object will be used.  Otherwise if the effective GID
 * is zero or equals the GID of the object or one of the supplementary group
 * IDs is equal to the GID of the object, then the group permission flags of
 * the object will be used.  Otherwise the other permission flags of the object
 * will be used.
 *
 * @param[in] flags The flags determining the access type.  It can be
 *   RTEMS_FS_PERMS_READ, RTEMS_FS_PERMS_WRITE or RTEMS_FS_PERMS_EXEC.
 * @param[in] object_mode The mode of the object specifying the permission flags.
 * @param[in] object_uid The UID of the object.
 * @param[in] object_gid The GID of the object.
 *
 * @retval true Access is allowed.
 * @retval false Otherwise.
 */
bool rtems_filesystem_check_access(
  int flags,
  mode_t object_mode,
  uid_t object_uid,
  gid_t object_gid
);

bool rtems_filesystem_eval_path_check_access(
  rtems_filesystem_eval_path_context_t *ctx,
  int eval_flags,
  mode_t node_mode,
  uid_t node_uid,
  gid_t node_gid
);

static inline bool rtems_filesystem_is_delimiter(char c)
{
  return c == '/' || c == '\\';
}

static inline bool rtems_filesystem_is_current_directory(
  const char *token,
  size_t tokenlen
)
{
  return tokenlen == 1 && token [0] == '.';
}

static inline bool rtems_filesystem_is_parent_directory(
  const char *token,
  size_t tokenlen
)
{
  return tokenlen == 2 && token [0] == '.' && token [1] == '.';
}

typedef ssize_t ( *rtems_libio_iovec_adapter )(
  rtems_libio_t      *iop,
  const struct iovec *iov,
  int                 iovcnt,
  ssize_t             total
);

static inline ssize_t rtems_libio_iovec_eval(
  int                        fd,
  const struct iovec        *iov,
  int                        iovcnt,
  unsigned int               flags,
  rtems_libio_iovec_adapter  adapter
)
{
  ssize_t        total;
  int            v;
  rtems_libio_t *iop;

  /*
   *  Argument validation on IO vector
   */
  if ( iov == NULL )
    rtems_set_errno_and_return_minus_one( EINVAL );

  if ( iovcnt <= 0 )
    rtems_set_errno_and_return_minus_one( EINVAL );

  if ( iovcnt > IOV_MAX )
    rtems_set_errno_and_return_minus_one( EINVAL );

  /*
   *  OpenGroup says that you are supposed to return EINVAL if the
   *  sum of the iov_len values in the iov array would overflow a
   *  ssize_t.
   */
  total = 0;
  for ( v = 0 ; v < iovcnt ; ++v ) {
    size_t len = iov[ v ].iov_len;

    if ( len > ( size_t ) ( SSIZE_MAX - total ) ) {
      rtems_set_errno_and_return_minus_one( EINVAL );
    }

    total += ( ssize_t ) len;

    if ( iov[ v ].iov_base == NULL && len != 0 ) {
      rtems_set_errno_and_return_minus_one( EINVAL );
    }
  }

  LIBIO_GET_IOP_WITH_ACCESS( fd, iop, flags, EBADF );

  if ( total > 0 ) {
    total = ( *adapter )( iop, iov, iovcnt, total );
  }

  rtems_libio_iop_drop( iop );
  return total;
}

/**
 * @brief Returns the file type of the file referenced by the filesystem
 * location.
 *
 * @brief[in] loc The filesystem location.
 *
 * @return The type of the file or an invalid file type in case of an error.
 */
static inline mode_t rtems_filesystem_location_type(
  const rtems_filesystem_location_info_t *loc
)
{
  struct stat st;

  st.st_mode = 0;
  (void) ( *loc->handlers->fstat_h )( loc, &st );

  return st.st_mode;
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif
/* end of include file */
