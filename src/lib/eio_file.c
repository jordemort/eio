/* EIO - EFL data type library
 * Copyright (C) 2010 Enlightenment Developers:
 *           Cedric Bail <cedric.bail@free.fr>
 *           Vincent "caro" Torri  <vtorri at univ-evry dot fr>
 *           Stephen "okra" Houston <UnixTitan@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library;
 * if not, see <http://www.gnu.org/licenses/>.
 */

#include "eio_private.h"
#include "Eio.h"

#ifdef HAVE_XATTR
# include <sys/xattr.h>
#endif

/*============================================================================*
 *                                  Local                                     *
 *============================================================================*/

/**
 * @cond LOCAL
 */

static void
_eio_file_heavy(void *data, Ecore_Thread *thread)
{
   Eio_File_Char_Ls *async = data;
   Eina_Iterator *ls;
   const char *file;
   Eina_List *pack = NULL;
   double start, current;

   ls = eina_file_ls(async->ls.directory);
   if (!ls)
     {
	eio_file_thread_error(&async->ls.common, thread);
	return ;
     }

   eio_file_container_set(&async->ls.common, eina_iterator_container_get(ls));

   start = ecore_time_get();

   EINA_ITERATOR_FOREACH(ls, file)
     {
	Eina_Bool filter = EINA_TRUE;

	if (async->filter_cb)
	  {
	     filter = async->filter_cb((void*) async->ls.common.data, &async->ls.common, file);
	  }

	if (filter)
          {
             Eio_File_Char *send_fc;

             send_fc = eio_char_malloc();
             if (!send_fc) goto on_error;

             send_fc->filename = file;
	     send_fc->associated = async->ls.common.worker.associated;
	     async->ls.common.worker.associated = NULL;

	     pack = eina_list_append(pack, send_fc);
          }
	else
          {
          on_error:
             eina_stringshare_del(file);

             if (async->ls.common.worker.associated)
               {
                  eina_hash_free(async->ls.common.worker.associated);
                  async->ls.common.worker.associated = NULL;
               }
          }

	current = ecore_time_get();
	if (current - start > EIO_PACKED_TIME)
	  {
             start = current;
             ecore_thread_feedback(thread, pack);
             pack = NULL;
          }

	if (ecore_thread_check(thread))
	  break;
     }

   if (pack) ecore_thread_feedback(thread, pack);

   eio_file_container_set(&async->ls.common, NULL);

   eina_iterator_free(ls);
}

static void
_eio_file_notify(void *data, Ecore_Thread *thread __UNUSED__, void *msg_data)
{
   Eio_File_Char_Ls *async = data;
   Eina_List *pack = msg_data;
   Eio_File_Char *info;

   EINA_LIST_FREE(pack, info)
     {
        async->ls.common.main.associated = info->associated;

        async->main_cb((void*) async->ls.common.data,
                       &async->ls.common,
                       info->filename);

        if (async->ls.common.main.associated)
          {
             eina_hash_free(async->ls.common.main.associated);
             async->ls.common.main.associated = NULL;
          }

        eina_stringshare_del(info->filename);
        eio_char_free(info);
     }
}

static void
_eio_file_eina_ls_heavy(Ecore_Thread *thread, Eio_File_Direct_Ls *async, Eina_Iterator *ls)
{
   const Eina_File_Direct_Info *info;
   Eina_List *pack = NULL;
   double start, current;

   if (!ls)
     {
	eio_file_thread_error(&async->ls.common, thread);
	return ;
     }

   eio_file_container_set(&async->ls.common, eina_iterator_container_get(ls));

   start = ecore_time_get();

   EINA_ITERATOR_FOREACH(ls, info)
     {
	Eina_Bool filter = EINA_TRUE;

	if (async->filter_cb)
	  {
	     filter = async->filter_cb((void*) async->ls.common.data, &async->ls.common, info);
	  }

	if (filter)
	  {
	     Eio_File_Direct_Info *send_di;

	     send_di = eio_direct_info_malloc();
	     if (!send_di) continue;

	     memcpy(&send_di->info, info, sizeof (Eina_File_Direct_Info));
	     send_di->associated = async->ls.common.worker.associated;
	     async->ls.common.worker.associated = NULL;

             pack = eina_list_append(pack, send_di);
	  }
	else if (async->ls.common.worker.associated)
	  {
             eina_hash_free(async->ls.common.worker.associated);
             async->ls.common.worker.associated = NULL;
	  }

        current = ecore_time_get();
        if (current - start > EIO_PACKED_TIME)
          {
             start = current;
             ecore_thread_feedback(thread, pack);
             pack = NULL;
          }

	if (ecore_thread_check(thread))
	  break;
     }

   if (pack) ecore_thread_feedback(thread, pack);

   eio_file_container_set(&async->ls.common, NULL);

   eina_iterator_free(ls);
}

static void
_eio_file_direct_heavy(void *data, Ecore_Thread *thread)
{
   Eio_File_Direct_Ls *async = data;
   Eina_Iterator *ls;

   ls = eina_file_direct_ls(async->ls.directory);

   _eio_file_eina_ls_heavy(thread, async, ls);
}

static void
_eio_file_stat_heavy(void *data, Ecore_Thread *thread)
{
   Eio_File_Direct_Ls *async = data;
   Eina_Iterator *ls;

   ls = eina_file_stat_ls(async->ls.directory);

   _eio_file_eina_ls_heavy(thread, async, ls);
}

static void
_eio_file_direct_notify(void *data, Ecore_Thread *thread __UNUSED__, void *msg_data)
{
   Eio_File_Direct_Ls *async = data;
   Eina_List *pack = msg_data;
   Eio_File_Direct_Info *info;

   EINA_LIST_FREE(pack, info)
     {
        async->ls.common.main.associated = info->associated;

        async->main_cb((void*) async->ls.common.data,
                       &async->ls.common,
                       &info->info);

        if (async->ls.common.main.associated)
          {
             eina_hash_free(async->ls.common.main.associated);
             async->ls.common.main.associated = NULL;
          }

        eio_direct_info_free(info);
     }
}

static void
_eio_eina_file_copy_xattr(Ecore_Thread *thread __UNUSED__,
			  Eio_File_Progress *op __UNUSED__,
			  Eina_File *f, int out)
{
   Eina_Iterator *it;
   Eina_Xattr *attr;

   it = eina_file_xattr_value_get(f);
   EINA_ITERATOR_FOREACH(it, attr)
     {
#ifdef HAVE_XATTR
        fsetxattr(out, attr->name, attr->value, attr->length, 0);
#endif
     }
   eina_iterator_free(it);
}

#ifdef HAVE_XATTR
static void
_eio_file_copy_xattr(Ecore_Thread *thread __UNUSED__,
                     Eio_File_Progress *op __UNUSED__,
                     int in, int out)
{
   char *tmp;
   ssize_t length;
   ssize_t i;

   length = flistxattr(in, NULL, 0);

   if (length <= 0) return ;

   tmp = alloca(length);
   length = flistxattr(in, tmp, length);

   for (i = 0; i < length; i += strlen(tmp) + 1)
     {
        ssize_t attr_length;
        void *value;

        attr_length = fgetxattr(in, tmp, NULL, 0);
        if (!attr_length) continue ;

        value = malloc(attr_length);
        if (!value) continue ;
        attr_length = fgetxattr(in, tmp, value, attr_length);

        if (attr_length > 0)
          fsetxattr(out, tmp, value, attr_length, 0);

        free(value);
     }
}
#endif

static Eina_Bool
_eio_file_write(int fd, void *mem, ssize_t length)
{
   ssize_t count;

   if (length == 0) return EINA_TRUE;

   count = write(fd, mem, length);
   if (count != length) return EINA_FALSE;
   return EINA_TRUE;
}

#ifndef MAP_HUGETLB
# define MAP_HUGETLB 0
#endif

static Eina_Bool
_eio_file_copy_mmap(Ecore_Thread *thread, Eio_File_Progress *op, Eina_File *f, int out)
{
   char *m = MAP_FAILED;
   long long i;
   long long size;

   size = eina_file_size_get(f);

   for (i = 0; i < size; i += EIO_PACKET_SIZE * EIO_PACKET_COUNT)
     {
	int j;
	int k;
	int c;

        m = eina_file_map_new(f, EINA_FILE_SEQUENTIAL,
                              i, EIO_PACKET_SIZE * EIO_PACKET_COUNT);
	if (!m)
	  goto on_error;

	c = size - i;
	if (c - EIO_PACKET_SIZE * EIO_PACKET_COUNT > 0)
	  c = EIO_PACKET_SIZE * EIO_PACKET_COUNT;
	else
	  c = size - i;

	for (j = EIO_PACKET_SIZE, k = 0; j < c; k = j, j += EIO_PACKET_SIZE)
	  {
	     if (!_eio_file_write(out, m + k, EIO_PACKET_SIZE))
	       goto on_error;

	     eio_progress_send(thread, op, i + j, size);
	  }

	if (!_eio_file_write(out, m + k, c - k))
	  goto on_error;

        if (eina_file_map_faulted(f, m))
          goto on_error;

	eio_progress_send(thread, op, i + c, size);

        eina_file_map_free(f, m);
	m = NULL;

	if (ecore_thread_check(thread))
          goto on_error;
     }

   return EINA_TRUE;

 on_error:
   if (m != NULL) eina_file_map_free(f, m);
   return EINA_FALSE;
}

#ifdef HAVE_SPLICE
static int
_eio_file_copy_splice(Ecore_Thread *thread, Eio_File_Progress *op, int in, int out, long long size)
{
   int result = 0;
   long long count;
   long long i;
   int pipefd[2];

   if (pipe(pipefd) < 0)
     return -1;

   for (i = 0; i < size; i += count)
     {
	count = splice(in, 0, pipefd[1], NULL, EIO_PACKET_SIZE * EIO_PACKET_COUNT, SPLICE_F_MORE | SPLICE_F_MOVE);
	if (count < 0) goto on_error;

	count = splice(pipefd[0], NULL, out, 0, count, SPLICE_F_MORE | SPLICE_F_MOVE);
	if (count < 0) goto on_error;

	eio_progress_send(thread, op, i, size);

	if (ecore_thread_check(thread))
          goto on_error;
     }

   result = 1;

 on_error:
   if (result != 1 && (errno == EBADF || errno == EINVAL)) result = -1;
   close(pipefd[0]);
   close(pipefd[1]);

   return result;
}
#endif

static void
_eio_file_copy_heavy(void *data, Ecore_Thread *thread)
{
   Eio_File_Progress *copy = data;

   eio_file_copy_do(thread, copy);
}

static void
_eio_file_copy_notify(void *data, Ecore_Thread *thread __UNUSED__, void *msg_data)
{
   Eio_File_Progress *copy = data;

   eio_progress_cb(msg_data, copy);
}

static void
_eio_file_copy_free(Eio_File_Progress *copy)
{
   eina_stringshare_del(copy->source);
   eina_stringshare_del(copy->dest);
   eio_file_free(&copy->common);
}

static void
_eio_file_copy_end(void *data, Ecore_Thread *thread __UNUSED__)
{
   Eio_File_Progress *copy = data;

   copy->common.done_cb((void*) copy->common.data, &copy->common);

   _eio_file_copy_free(copy);
}

static void
_eio_file_copy_error(void *data, Ecore_Thread *thread __UNUSED__)
{
   Eio_File_Progress *copy = data;

   eio_file_error(&copy->common);

   _eio_file_copy_free(copy);
}

static void
_eio_file_move_free(Eio_File_Move *move)
{
   eina_stringshare_del(move->progress.source);
   eina_stringshare_del(move->progress.dest);
   eio_file_free(&move->progress.common);
}

static void
_eio_file_move_copy_progress(void *data, Eio_File *handler __UNUSED__, const Eio_Progress *info)
{
   Eio_File_Move *move = data;

   move->progress.progress_cb((void*) move->progress.common.data, &move->progress.common, info);
}

static void
_eio_file_move_unlink_done(void *data, Eio_File *handler __UNUSED__)
{
   Eio_File_Move *move = data;

   move->progress.common.done_cb((void*) move->progress.common.data, &move->progress.common);

   _eio_file_move_free(move);
}

static void
_eio_file_move_unlink_error(void *data, Eio_File *handler __UNUSED__, int error)
{
   Eio_File_Move *move = data;

   move->copy = NULL;

   move->progress.common.error = error;
   eio_file_error(&move->progress.common);

   _eio_file_move_free(move);
}

static void
_eio_file_move_copy_done(void *data, Eio_File *handler __UNUSED__)
{
   Eio_File_Move *move = data;
   Eio_File *rm;

   rm = eio_file_unlink(move->progress.source,
			_eio_file_move_unlink_done,
			_eio_file_move_unlink_error,
			move);
   if (rm) move->copy = rm;
}

static void
_eio_file_move_copy_error(void *data, Eio_File *handler __UNUSED__, int error)
{
   Eio_File_Move *move = data;

   move->progress.common.error = error;
   eio_file_error(&move->progress.common);

   _eio_file_move_free(move);
}

static void
_eio_file_move_heavy(void *data, Ecore_Thread *thread)
{
   Eio_File_Move *move = data;

   if (rename(move->progress.source, move->progress.dest) < 0)
     eio_file_thread_error(&move->progress.common, thread);
   else
     eio_progress_send(thread, &move->progress, 1, 1);
}

static void
_eio_file_move_notify(void *data, Ecore_Thread *thread __UNUSED__, void *msg_data)
{
   Eio_File_Move *move = data;

   eio_progress_cb(msg_data, &move->progress);
}

static void
_eio_file_move_end(void *data, Ecore_Thread *thread __UNUSED__)
{
   Eio_File_Move *move = data;

   move->progress.common.done_cb((void*) move->progress.common.data, &move->progress.common);

   _eio_file_move_free(move);
}

static void
_eio_file_move_error(void *data, Ecore_Thread *thread __UNUSED__)
{
   Eio_File_Move *move = data;

   if (move->copy)
     {
	eio_file_cancel(move->copy);
	return ;
     }

   if (move->progress.common.error == EXDEV)
     {
	Eio_File *eio_cp;

	eio_cp = eio_file_copy(move->progress.source, move->progress.dest,
			       move->progress.progress_cb ? _eio_file_move_copy_progress : NULL,
			       _eio_file_move_copy_done,
			       _eio_file_move_copy_error,
			       move);

	if (eio_cp)
          {
             move->copy = eio_cp;

             move->progress.common.thread = ((Eio_File_Progress*)move->copy)->common.thread;
             return ;
          }
     }

   eio_file_error(&move->progress.common);

   _eio_file_move_free(move);
}

/**
 * @endcond
 */


/*============================================================================*
 *                                 Global                                     *
 *============================================================================*/

/**
 * @cond LOCAL
 */

void
eio_progress_cb(Eio_Progress *progress, Eio_File_Progress *op)
{
   op->progress_cb((void *) op->common.data, &op->common, progress);

   eio_progress_free(progress);
}

Eina_Bool
eio_file_copy_do(Ecore_Thread *thread, Eio_File_Progress *copy)
{
   Eina_File *f;
#ifdef HAVE_SPLICE
   struct stat buf;
   int in = -1;
#endif
   mode_t md;
   int result = -1;
   int out = -1;

#ifdef HAVE_SPLICE
   in = open(copy->source, O_RDONLY);
   if (in < 0)
     {
	eio_file_thread_error(&copy->common, thread);
	return EINA_FALSE;
     }

   /*
     As we need file size for progression information and both copy method
     call fstat (better than stat as it avoid race condition).
    */
   if (fstat(in, &buf) < 0)
     goto on_error;

   md = buf.st_mode;
#endif

   /* open write */
   out = open(copy->dest, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
   if (out < 0)
     goto on_error;

#ifdef HAVE_SPLICE
   /* fast file copy code using Linux splice API */
   result = _eio_file_copy_splice(thread, copy, in, out, buf.st_size);
   if (result == 0)
     goto on_error;
#endif

   /* classic copy method using mmap and write */
   if (result == -1)
     {
#ifndef HAVE_SPLICE
        struct stat buf;

        if (stat(copy->source, &buf) < 0)
          goto on_error;

        md = buf.st_mode;
#endif

        f = eina_file_open(copy->source, 0);
        if (!f) goto on_error;

        if (!_eio_file_copy_mmap(thread, copy, f, out))
          {
             eina_file_close(f);
             goto on_error;
          }

        _eio_eina_file_copy_xattr(thread, copy, f, out);

        eina_file_close(f);
     }
   else
     {
#if defined HAVE_XATTR && defined HAVE_SPLICE
       _eio_file_copy_xattr(thread, copy, in, out);
#endif
     }

   /* change access right to match source */
#ifdef HAVE_CHMOD
   if (fchmod(out, md) != 0)
     goto on_error;
#else
   if (chmod(copy->dest, md) != 0)
     goto on_error;
#endif

   close(out);
#ifdef HAVE_SPLICE
   close(in);
#endif

   return EINA_TRUE;

 on_error:
   eio_file_thread_error(&copy->common, thread);

#ifdef HAVE_SPLICE
   if (in >= 0) close(in);
#endif
   if (out >= 0) close(out);
   if (out >= 0)
     unlink(copy->dest);
   return EINA_FALSE;
}

void
eio_async_free(Eio_File_Ls *async)
{
   eina_stringshare_del(async->directory);
   eio_file_free(&async->common);
}

void
eio_async_end(void *data, Ecore_Thread *thread __UNUSED__)
{
   Eio_File_Ls *async = data;

   async->common.done_cb((void*) async->common.data, &async->common);

   eio_async_free(async);
}

void
eio_async_error(void *data, Ecore_Thread *thread __UNUSED__)
{
   Eio_File_Ls *async = data;

   eio_file_error(&async->common);

   eio_async_free(async);
}

/**
 * @endcond
 */


/*============================================================================*
 *                                   API                                      *
 *============================================================================*/

EAPI Eio_File *
eio_file_ls(const char *dir,
	    Eio_Filter_Cb filter_cb,
	    Eio_Main_Cb main_cb,
	    Eio_Done_Cb done_cb,
	    Eio_Error_Cb error_cb,
	    const void *data)
{
   Eio_File_Char_Ls *async;

   EINA_SAFETY_ON_NULL_RETURN_VAL(dir, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(main_cb, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(done_cb, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(error_cb, NULL);

   async = malloc(sizeof (Eio_File_Char_Ls));
   EINA_SAFETY_ON_NULL_RETURN_VAL(async, NULL);

   async->filter_cb = filter_cb;
   async->main_cb = main_cb;
   async->ls.directory = eina_stringshare_add(dir);

   if (!eio_long_file_set(&async->ls.common,
			  done_cb,
			  error_cb,
			  data,
			  _eio_file_heavy,
			  _eio_file_notify,
			  eio_async_end,
			  eio_async_error))
     return NULL;

   return &async->ls.common;
}

EAPI Eio_File *
eio_file_direct_ls(const char *dir,
		   Eio_Filter_Direct_Cb filter_cb,
		   Eio_Main_Direct_Cb main_cb,
		   Eio_Done_Cb done_cb,
		   Eio_Error_Cb error_cb,
		   const void *data)
{
   Eio_File_Direct_Ls *async;

   EINA_SAFETY_ON_NULL_RETURN_VAL(dir, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(main_cb, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(done_cb, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(error_cb, NULL);

   async = malloc(sizeof(Eio_File_Direct_Ls));
   EINA_SAFETY_ON_NULL_RETURN_VAL(async, NULL);

   async->filter_cb = filter_cb;
   async->main_cb = main_cb;
   async->ls.directory = eina_stringshare_add(dir);

   if (!eio_long_file_set(&async->ls.common,
			  done_cb,
			  error_cb,
			  data,
			  _eio_file_direct_heavy,
			  _eio_file_direct_notify,
			  eio_async_end,
			  eio_async_error))
     return NULL;

   return &async->ls.common;
}

EAPI Eio_File *
eio_file_stat_ls(const char *dir,
                 Eio_Filter_Direct_Cb filter_cb,
                 Eio_Main_Direct_Cb main_cb,
                 Eio_Done_Cb done_cb,
                 Eio_Error_Cb error_cb,
                 const void *data)
{
   Eio_File_Direct_Ls *async;

   EINA_SAFETY_ON_NULL_RETURN_VAL(dir, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(main_cb, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(done_cb, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(error_cb, NULL);

   async = malloc(sizeof(Eio_File_Direct_Ls));
   EINA_SAFETY_ON_NULL_RETURN_VAL(async, NULL);

   async->filter_cb = filter_cb;
   async->main_cb = main_cb;
   async->ls.directory = eina_stringshare_add(dir);

   if (!eio_long_file_set(&async->ls.common,
			  done_cb,
			  error_cb,
			  data,
			  _eio_file_stat_heavy,
			  _eio_file_direct_notify,
			  eio_async_end,
			  eio_async_error))
     return NULL;

   return &async->ls.common;
}

EAPI Eina_Bool
eio_file_cancel(Eio_File *ls)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ls, EINA_FALSE);
   return ecore_thread_cancel(ls->thread);
}

EAPI Eina_Bool
eio_file_check(Eio_File *ls)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ls, EINA_TRUE);
   return ecore_thread_check(ls->thread);
}

EAPI void *
eio_file_container_get(Eio_File *ls)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ls, EINA_FALSE);
   return ls->container;
}

EAPI Eina_Bool
eio_file_associate_add(Eio_File *ls,
                       const char *key,
                       const void *data, Eina_Free_Cb free_cb)
{
   /* FIXME: Check if we are in the right worker thred */
   if (!ls->worker.associated)
     ls->worker.associated = eina_hash_string_small_new(eio_associate_free);

   return eina_hash_add(ls->worker.associated,
                        key,
                        eio_associate_malloc(data, free_cb));
}

EAPI Eina_Bool
eio_file_associate_direct_add(Eio_File *ls,
                              const char *key,
                              const void *data, Eina_Free_Cb free_cb)
{
   /* FIXME: Check if we are in the right worker thred */
   if (!ls->worker.associated)
     ls->worker.associated = eina_hash_string_small_new(eio_associate_free);

   return eina_hash_direct_add(ls->worker.associated,
                               key,
                               eio_associate_malloc(data, free_cb));
}

EAPI void *
eio_file_associate_find(Eio_File *ls, const char *key)
{
   Eio_File_Associate *search;

   if (!ls->main.associated)
     return NULL;

   search = eina_hash_find(ls->main.associated, key);
   if (!search) return NULL;
   return search->data;
}

EAPI Eio_File *
eio_file_copy(const char *source,
	      const char *dest,
	      Eio_Progress_Cb progress_cb,
	      Eio_Done_Cb done_cb,
	      Eio_Error_Cb error_cb,
	      const void *data)
{
   Eio_File_Progress *copy;

   EINA_SAFETY_ON_NULL_RETURN_VAL(source, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dest, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(done_cb, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(error_cb, NULL);

   copy = malloc(sizeof(Eio_File_Progress));
   EINA_SAFETY_ON_NULL_RETURN_VAL(copy, NULL);

   copy->op = EIO_FILE_COPY;
   copy->progress_cb = progress_cb;
   copy->source = eina_stringshare_add(source);
   copy->dest = eina_stringshare_add(dest);

   if (!eio_long_file_set(&copy->common,
			  done_cb,
			  error_cb,
			  data,
			  _eio_file_copy_heavy,
			  _eio_file_copy_notify,
			  _eio_file_copy_end,
			  _eio_file_copy_error))
     return NULL;

   return &copy->common;
}

EAPI Eio_File *
eio_file_move(const char *source,
	      const char *dest,
	      Eio_Progress_Cb progress_cb,
	      Eio_Done_Cb done_cb,
	      Eio_Error_Cb error_cb,
	      const void *data)
{
   Eio_File_Move *move;

   EINA_SAFETY_ON_NULL_RETURN_VAL(source, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dest, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(done_cb, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(error_cb, NULL);

   move = malloc(sizeof(Eio_File_Move));
   EINA_SAFETY_ON_NULL_RETURN_VAL(move, NULL);

   move->progress.op = EIO_FILE_MOVE;
   move->progress.progress_cb = progress_cb;
   move->progress.source = eina_stringshare_add(source);
   move->progress.dest = eina_stringshare_add(dest);
   move->copy = NULL;

   if (!eio_long_file_set(&move->progress.common,
			  done_cb,
			  error_cb,
			  data,
			  _eio_file_move_heavy,
			  _eio_file_move_notify,
			  _eio_file_move_end,
			  _eio_file_move_error))
     return NULL;

   return &move->progress.common;
}
