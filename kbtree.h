/*-
 * Copyright 1997-1999, 2001, John-Mark Gurney.
 *			 2008-2009, Attractive Chaos <attractor@live.co.uk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *	  notice, this list of conditions and the following disclaimer in the
 *	  documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.	IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef __AC_KBTREE_H
#define __AC_KBTREE_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define KB_MAX_DEPTH 64

#ifndef KB_FREE
#define KB_FREE(p, sz) free(p)
#endif

#ifndef KB_CALLOC
#define KB_CALLOC(n, sz) calloc(n, sz)
#endif

typedef struct {
	int32_t is_internal:1, n:31;
} kbnode_t;

typedef struct {
	kbnode_t *x;
	int i;
} kbpos_t;

typedef struct {
	kbpos_t stack[KB_MAX_DEPTH], *p;
} kbitr_t;

#define	__KB_KEY(type, x)	((type*)((char*)(x) + 4))
#define __KB_PTR(btr, x)	((kbnode_t**)((char*)(x) + (btr)->off_ptr))

#define __KB_TREE_T(name)						\
	typedef struct {							\
		kbnode_t *root;							\
		int	off_ptr, ilen, elen;				\
		int	n, t;								\
		int	n_keys, n_nodes;					\
	} kbtree_##name##_t;

#define __KB_INIT(name, key_t)											\
	int kb_static_init_##name(kbtree_##name##_t* b, int size)			\
	{																	\
		memset(b, 0, sizeof(*b));										\
		b->t = ((size - 4 - sizeof(void*)) / (sizeof(void*) + sizeof(key_t)) + 1) >> 1; \
		if (b->t < 2) return -1;										\
		b->n = 2 * b->t - 1;											\
		b->off_ptr = 4 + b->n * sizeof(key_t);							\
		b->ilen = (4 + sizeof(void*) + b->n * (sizeof(void*) + sizeof(key_t)) + 3) >> 2 << 2; \
		b->elen = (b->off_ptr + 3) >> 2 << 2;							\
        /* Note: the root node doesn't need this much memory (should */ \
		/* be passing ilen here). But if we use ilen, we may pass    */	\
		/* elen when freeing the root, since root->is_internal == 0, */	\
		/* which will confuse a pool-based allocation scheme.        */	\
		b->root = (kbnode_t*)KB_CALLOC(1, b->elen);						\
		++b->n_nodes;													\
		return 0;														\
	}																	\
	kbtree_##name##_t *kb_init_##name(int size)							\
	{																	\
		kbtree_##name##_t *b;											\
		const size_t alloc_sz = sizeof(kbtree_##name##_t);				\
		b = (kbtree_##name##_t*)KB_CALLOC(1, alloc_sz);					\
		if (kb_static_init_##name(b, size) != 0) {						\
			KB_FREE(b, alloc_sz); return 0;								\
		}																\
		return b;														\
	}

#define __kb_static_destroy(name, b) do {								\
		if (b) {														\
			int i, max = 8;												\
			kbnode_t *x, **top, **stack = 0;							\
			top = stack = (kbnode_t**)calloc(max, sizeof(kbnode_t*));	\
			*top++ = (b)->root;											\
			while (top != stack) {										\
				x = *--top;												\
				if (x->is_internal == 0) {								\
					KB_FREE(x, (b)->elen); continue;					\
				}														\
				for (i = 0; i <= x->n; ++i)								\
					if (__KB_PTR(b, x)[i]) {							\
						if (top - stack == max) {						\
							max <<= 1;									\
							stack = (kbnode_t**)realloc(stack, max * sizeof(kbnode_t*)); \
							top = stack + (max>>1);						\
						}												\
						*top++ = __KB_PTR(b, x)[i];						\
					}													\
				KB_FREE(x, (b)->ilen);									\
			}															\
			free(stack);												\
		}																\
	} while (0)

#define __kb_destroy(name, b) do {										\
		__kb_static_destroy(name, b);									\
		KB_FREE(b, sizeof(kbtree_##name##_t));							\
	} while (0)

#define __KB_GET_AUX1(name, key_t, __cmp)								\
	static inline int __kb_getp_aux_##name(const kbnode_t * __restrict x, const key_t * __restrict k, int *r) \
	{																	\
		int tr, *rr, begin = 0, end = x->n;								\
		if (x->n == 0) return -1;										\
		rr = r? r : &tr;												\
		while (begin < end) {											\
			int mid = (begin + end) >> 1;								\
			if (__cmp(__KB_KEY(key_t, x)[mid], *k) < 0) begin = mid + 1; \
			else end = mid;												\
		}																\
		if (begin == x->n) { *rr = 1; return x->n - 1; }				\
		if ((*rr = __cmp(*k, __KB_KEY(key_t, x)[begin])) < 0) --begin;	\
		return begin;													\
	}

#define __KB_GET(name, key_t)											\
	static kbpos_t kb_getpos_##name(kbtree_##name##_t *b, const key_t * __restrict k) \
	{																	\
		kbpos_t pos;													\
		int r = 0;														\
		pos.x = b->root;												\
		while (pos.x) {													\
			pos.i = __kb_getp_aux_##name(pos.x, k, &r);					\
			if (pos.i >= 0 && r == 0) return pos;						\
			if (pos.x->is_internal == 0) { pos.x = 0; return pos; }		\
			pos.x = __KB_PTR(b, pos.x)[pos.i + 1];						\
		}																\
		pos.x = 0;														\
		return pos;														\
	}																	\
	static key_t *kb_getp_##name(kbtree_##name##_t *b, const key_t * __restrict k) \
	{																	\
		const kbpos_t pos = kb_getpos_##name(b, k);						\
		return pos.x? &__KB_KEY(key_t, pos.x)[pos.i] : 0;				\
	}																	\
	static inline key_t *kb_get_##name(kbtree_##name##_t *b, const key_t k) \
	{																	\
		return kb_getp_##name(b, &k);									\
	}

#define __KB_INTERVAL(name, key_t)										\
	static void kb_intervalp_##name(kbtree_##name##_t *b, const key_t * __restrict k, key_t **lower, key_t **upper)	\
	{																	\
		int i, r = 0;													\
		kbnode_t *x = b->root;											\
		*lower = *upper = 0;											\
		while (x) {														\
			i = __kb_getp_aux_##name(x, k, &r);							\
			if (i >= 0 && r == 0) {										\
				*lower = *upper = &__KB_KEY(key_t, x)[i];				\
				return;													\
			}															\
			if (i >= 0) *lower = &__KB_KEY(key_t, x)[i];				\
			if (i < x->n - 1) *upper = &__KB_KEY(key_t, x)[i + 1];		\
			if (x->is_internal == 0) return;							\
			x = __KB_PTR(b, x)[i + 1];									\
		}																\
	}																	\
	static inline void kb_interval_##name(kbtree_##name##_t *b, const key_t k, key_t **lower, key_t **upper) \
	{																	\
		kb_intervalp_##name(b, &k, lower, upper);						\
	}

#define __KB_PUT(name, key_t, __cmp)									\
	/* x must be an internal node */									\
	static void __kb_split_##name(kbtree_##name##_t *b, kbnode_t *x, int i, kbnode_t *y) \
	{																	\
		kbnode_t *z;													\
		z = (kbnode_t*)KB_CALLOC(1, y->is_internal? b->ilen : b->elen);	\
		++b->n_nodes;													\
		z->is_internal = y->is_internal;								\
		z->n = b->t - 1;												\
		memcpy(__KB_KEY(key_t, z), __KB_KEY(key_t, y) + b->t, sizeof(key_t) * (b->t - 1)); \
		if (y->is_internal) memcpy(__KB_PTR(b, z), __KB_PTR(b, y) + b->t, sizeof(void*) * b->t); \
		y->n = b->t - 1;												\
		memmove(__KB_PTR(b, x) + i + 2, __KB_PTR(b, x) + i + 1, sizeof(void*) * (x->n - i)); \
		__KB_PTR(b, x)[i + 1] = z;										\
		memmove(__KB_KEY(key_t, x) + i + 1, __KB_KEY(key_t, x) + i, sizeof(key_t) * (x->n - i)); \
		__KB_KEY(key_t, x)[i] = __KB_KEY(key_t, y)[b->t - 1];			\
		++x->n;															\
	}																	\
	static kbpos_t __kb_putp_aux_##name(kbtree_##name##_t *b, kbnode_t *x, const key_t * __restrict k) \
	{																	\
		kbpos_t pos;													\
		pos.x = x;														\
		if (pos.x->is_internal == 0) {									\
			pos.i = __kb_getp_aux_##name(pos.x, k, 0);					\
			if (pos.i != pos.x->n - 1)									\
				memmove(__KB_KEY(key_t, pos.x) + pos.i + 2, __KB_KEY(key_t, pos.x) + pos.i + 1, (pos.x->n - pos.i - 1) * sizeof(key_t)); \
			++pos.i;													\
			__KB_KEY(key_t, pos.x)[pos.i] = *k;							\
			++pos.x->n;													\
		} else {														\
			pos.i = __kb_getp_aux_##name(pos.x, k, 0) + 1;				\
			if (__KB_PTR(b, pos.x)[pos.i]->n == 2 * b->t - 1) {			\
				__kb_split_##name(b, pos.x, pos.i, __KB_PTR(b, pos.x)[pos.i]);	\
				if (__cmp(*k, __KB_KEY(key_t, pos.x)[pos.i]) > 0) ++pos.i;		\
			}															\
			pos = __kb_putp_aux_##name(b, __KB_PTR(b, pos.x)[pos.i], k);\
		}																\
		return pos;														\
	}																	\
	static kbpos_t kb_putpos_##name(kbtree_##name##_t *b, const key_t * __restrict k) \
	{																	\
		kbnode_t *r, *s;												\
		++b->n_keys;													\
		r = b->root;													\
		if (r->n == 2 * b->t - 1) {										\
			++b->n_nodes;												\
			s = (kbnode_t*)KB_CALLOC(1, b->ilen);						\
			b->root = s; s->is_internal = 1; s->n = 0;					\
			__KB_PTR(b, s)[0] = r;										\
			__kb_split_##name(b, s, 0, r);								\
			r = s;														\
		}																\
		return __kb_putp_aux_##name(b, r, k);							\
	}																	\
	static key_t *kb_putp_##name(kbtree_##name##_t *b, const key_t * __restrict k) \
	{																	\
		const kbpos_t pos = kb_putpos_##name(b, k);						\
		return &__KB_KEY(key_t, pos.x)[pos.i];							\
	}																	\
	static inline void kb_put_##name(kbtree_##name##_t *b, const key_t k) \
	{																	\
		kb_putp_##name(b, &k);											\
	}


#define __KB_DEL(name, key_t)											\
	static key_t __kb_delp_aux_##name(kbtree_##name##_t *b, kbnode_t *x, const key_t * __restrict k, int s) \
	{																	\
		int yn, zn, i, r = 0;											\
		kbnode_t *xp, *y, *z;											\
		key_t kp;														\
		if (x == 0) return *k;											\
		if (s) { /* s can only be 0, 1 or 2 */							\
			r = x->is_internal == 0? 0 : s == 1? 1 : -1;				\
			i = s == 1? x->n - 1 : -1;									\
		} else i = __kb_getp_aux_##name(x, k, &r);						\
		if (x->is_internal == 0) {										\
			if (s == 2) ++i;											\
			kp = __KB_KEY(key_t, x)[i];									\
			memmove(__KB_KEY(key_t, x) + i, __KB_KEY(key_t, x) + i + 1, (x->n - i - 1) * sizeof(key_t)); \
			--x->n;														\
			return kp;													\
		}																\
		if (r == 0) {													\
			if ((yn = __KB_PTR(b, x)[i]->n) >= b->t) {					\
				xp = __KB_PTR(b, x)[i];									\
				kp = __KB_KEY(key_t, x)[i];								\
				__KB_KEY(key_t, x)[i] = __kb_delp_aux_##name(b, xp, 0, 1); \
				return kp;												\
			} else if ((zn = __KB_PTR(b, x)[i + 1]->n) >= b->t) {		\
				xp = __KB_PTR(b, x)[i + 1];								\
				kp = __KB_KEY(key_t, x)[i];								\
				__KB_KEY(key_t, x)[i] = __kb_delp_aux_##name(b, xp, 0, 2); \
				return kp;												\
			} else if (yn == b->t - 1 && zn == b->t - 1) {				\
				y = __KB_PTR(b, x)[i]; z = __KB_PTR(b, x)[i + 1];		\
				__KB_KEY(key_t, y)[y->n++] = *k;						\
				memmove(__KB_KEY(key_t, y) + y->n, __KB_KEY(key_t, z), z->n * sizeof(key_t)); \
				if (y->is_internal) memmove(__KB_PTR(b, y) + y->n, __KB_PTR(b, z), (z->n + 1) * sizeof(void*)); \
				y->n += z->n;											\
				memmove(__KB_KEY(key_t, x) + i, __KB_KEY(key_t, x) + i + 1, (x->n - i - 1) * sizeof(key_t)); \
				memmove(__KB_PTR(b, x) + i + 1, __KB_PTR(b, x) + i + 2, (x->n - i - 1) * sizeof(void*)); \
				--x->n;													\
				KB_FREE(z, z->is_internal? b->ilen : b->elen);			\
				return __kb_delp_aux_##name(b, y, k, s);				\
			}															\
		}																\
		++i;															\
		if ((xp = __KB_PTR(b, x)[i])->n == b->t - 1) {					\
			if (i > 0 && (y = __KB_PTR(b, x)[i - 1])->n >= b->t) {		\
				memmove(__KB_KEY(key_t, xp) + 1, __KB_KEY(key_t, xp), xp->n * sizeof(key_t)); \
				if (xp->is_internal) memmove(__KB_PTR(b, xp) + 1, __KB_PTR(b, xp), (xp->n + 1) * sizeof(void*)); \
				__KB_KEY(key_t, xp)[0] = __KB_KEY(key_t, x)[i - 1];		\
				__KB_KEY(key_t, x)[i - 1] = __KB_KEY(key_t, y)[y->n - 1]; \
				if (xp->is_internal) __KB_PTR(b, xp)[0] = __KB_PTR(b, y)[y->n]; \
				--y->n; ++xp->n;										\
			} else if (i < x->n && (y = __KB_PTR(b, x)[i + 1])->n >= b->t) { \
				__KB_KEY(key_t, xp)[xp->n++] = __KB_KEY(key_t, x)[i];	\
				__KB_KEY(key_t, x)[i] = __KB_KEY(key_t, y)[0];			\
				if (xp->is_internal) __KB_PTR(b, xp)[xp->n] = __KB_PTR(b, y)[0]; \
				--y->n;													\
				memmove(__KB_KEY(key_t, y), __KB_KEY(key_t, y) + 1, y->n * sizeof(key_t)); \
				if (y->is_internal) memmove(__KB_PTR(b, y), __KB_PTR(b, y) + 1, (y->n + 1) * sizeof(void*)); \
			} else if (i > 0 && (y = __KB_PTR(b, x)[i - 1])->n == b->t - 1) { \
				__KB_KEY(key_t, y)[y->n++] = __KB_KEY(key_t, x)[i - 1];	\
				memmove(__KB_KEY(key_t, y) + y->n, __KB_KEY(key_t, xp), xp->n * sizeof(key_t));	\
				if (y->is_internal) memmove(__KB_PTR(b, y) + y->n, __KB_PTR(b, xp), (xp->n + 1) * sizeof(void*)); \
				y->n += xp->n;											\
				memmove(__KB_KEY(key_t, x) + i - 1, __KB_KEY(key_t, x) + i, (x->n - i) * sizeof(key_t)); \
				memmove(__KB_PTR(b, x) + i, __KB_PTR(b, x) + i + 1, (x->n - i) * sizeof(void*)); \
				--x->n;													\
				KB_FREE(xp, xp->is_internal? b->ilen : b->elen);		\
				xp = y;													\
			} else if (i < x->n && (y = __KB_PTR(b, x)[i + 1])->n == b->t - 1) { \
				__KB_KEY(key_t, xp)[xp->n++] = __KB_KEY(key_t, x)[i];	\
				memmove(__KB_KEY(key_t, xp) + xp->n, __KB_KEY(key_t, y), y->n * sizeof(key_t));	\
				if (xp->is_internal) memmove(__KB_PTR(b, xp) + xp->n, __KB_PTR(b, y), (y->n + 1) * sizeof(void*)); \
				xp->n += y->n;											\
				memmove(__KB_KEY(key_t, x) + i, __KB_KEY(key_t, x) + i + 1, (x->n - i - 1) * sizeof(key_t)); \
				memmove(__KB_PTR(b, x) + i + 1, __KB_PTR(b, x) + i + 2, (x->n - i - 1) * sizeof(void*)); \
				--x->n;													\
				KB_FREE(y, y->is_internal? b->ilen : b->elen);			\
			}															\
		}																\
		return __kb_delp_aux_##name(b, xp, k, s);						\
	}																	\
	static key_t kb_delp_##name(kbtree_##name##_t *b, const key_t * __restrict k) \
	{																	\
		kbnode_t *x;													\
		key_t ret;														\
		ret = __kb_delp_aux_##name(b, b->root, k, 0);					\
		--b->n_keys;													\
		if (b->root->n == 0 && b->root->is_internal) {					\
			--b->n_nodes;												\
			x = b->root;												\
			b->root = __KB_PTR(b, x)[0];								\
			KB_FREE(x, x->is_internal? b->ilen : b->elen);				\
		}																\
		return ret;														\
	}																	\
	static inline key_t kb_del_##name(kbtree_##name##_t *b, const key_t k) \
	{																	\
		return kb_delp_##name(b, &k);									\
	}

#define __KB_ITR(name, key_t) \
	static inline void kb_itr_first_##name(kbtree_##name##_t *b, kbitr_t *itr) \
	{ \
		itr->p = 0; \
		if (b->n_keys == 0) return; \
		itr->p = itr->stack; \
		itr->p->x = b->root; itr->p->i = 0; \
		while (itr->p->x->is_internal && __KB_PTR(b, itr->p->x)[0] != 0) { \
			kbnode_t *x = itr->p->x; \
			++itr->p; \
			itr->p->x = __KB_PTR(b, x)[0]; itr->p->i = 0; \
		} \
	} \
	static int kb_itr_get_##name(kbtree_##name##_t *b, const key_t * __restrict k, kbitr_t *itr) \
	{ \
		int i, r = 0; \
		itr->p = itr->stack; \
		itr->p->x = b->root; itr->p->i = 0; \
		while (itr->p->x) { \
			i = __kb_getp_aux_##name(itr->p->x, k, &r); \
			if (i >= 0 && r == 0) return 0; \
			if (itr->p->x->is_internal == 0) return -1; \
			itr->p[1].x = __KB_PTR(b, itr->p->x)[i + 1]; \
			itr->p[1].i = i; \
			++itr->p; \
		} \
		return -1; \
	} \
	static inline int kb_itr_next_##name(kbtree_##name##_t *b, kbitr_t *itr) \
	{ \
		if (itr->p < itr->stack) return 0; \
		for (;;) { \
			++itr->p->i; \
			while (itr->p->x && itr->p->i <= itr->p->x->n) { \
				itr->p[1].i = 0; \
				itr->p[1].x = itr->p->x->is_internal? __KB_PTR(b, itr->p->x)[itr->p->i] : 0; \
				++itr->p; \
			} \
			--itr->p; \
			if (itr->p < itr->stack) return 0; \
			if (itr->p->x && itr->p->i < itr->p->x->n) return 1; \
		} \
	}

#define KBTREE_INIT(name, key_t, __cmp)			\
	__KB_TREE_T(name)							\
	__KB_INIT(name, key_t)						\
	__KB_GET_AUX1(name, key_t, __cmp)			\
	__KB_GET(name, key_t)						\
	__KB_INTERVAL(name, key_t)					\
	__KB_PUT(name, key_t, __cmp)				\
	__KB_DEL(name, key_t) \
	__KB_ITR(name, key_t)

#define KB_DEFAULT_SIZE 512

#define kbtree_t(name) kbtree_##name##_t
#define kb_init(name, s) kb_init_##name(s)
#define kb_static_init(name, b, s) kb_static_init_##name(b, s)
#define kb_destroy(name, b) __kb_destroy(name, b)
#define kb_static_destroy(name, b) __kb_static_destroy(name, b)
#define kb_get(name, b, k) kb_get_##name(b, k)
#define kb_put(name, b, k) kb_put_##name(b, k)
#define kb_del(name, b, k) kb_del_##name(b, k)
#define kb_interval(name, b, k, l, u) kb_interval_##name(b, k, l, u)
#define kb_getp(name, b, k) kb_getp_##name(b, k)
#define kb_getpos(name, b, k) kb_getpos_##name(b, k)
#define kb_putp(name, b, k) kb_putp_##name(b, k)
#define kb_putpos(name, b, k) kb_putpos_##name(b, k)
#define kb_delp(name, b, k) kb_delp_##name(b, k)
#define kb_intervalp(name, b, k, l, u) kb_intervalp_##name(b, k, l, u)

#define kb_itr_first(name, b, i) kb_itr_first_##name(b, i)
#define kb_itr_get(name, b, k, i) kb_itr_get_##name(b, k, i)
#define kb_itr_next(name, b, i) kb_itr_next_##name(b, i)
#define kb_itr_key(type, itr) __KB_KEY(type, (itr)->p->x)[(itr)->p->i]
#define kb_itr_valid(itr) ((itr)->p >= (itr)->stack)

#define kb_size(b) ((b)->n_keys)

#define kb_generic_cmp(a, b) (((b) < (a)) - ((a) < (b)))
#define kb_str_cmp(a, b) strcmp(a, b)

/* The following is *DEPRECATED*!!! Use the iterator interface instead! */

typedef struct {
	kbnode_t *x;
	int i;
} __kbstack_t;

#define __kb_traverse(key_t, b, __func) do {							\
		int __kmax = 8;													\
		__kbstack_t *__kstack, *__kp;									\
		__kp = __kstack = (__kbstack_t*)calloc(__kmax, sizeof(__kbstack_t)); \
		__kp->x = (b)->root; __kp->i = 0;								\
		for (;;) {														\
			while (__kp->x && __kp->i <= __kp->x->n) {					\
				if (__kp - __kstack == __kmax - 1) {					\
					__kmax <<= 1;										\
					__kstack = (__kbstack_t*)realloc(__kstack, __kmax * sizeof(__kbstack_t)); \
					__kp = __kstack + (__kmax>>1) - 1;					\
				}														\
				(__kp+1)->i = 0; (__kp+1)->x = __kp->x->is_internal? __KB_PTR(b, __kp->x)[__kp->i] : 0; \
				++__kp;													\
			}															\
			--__kp;														\
			if (__kp >= __kstack) {										\
				if (__kp->x && __kp->i < __kp->x->n) __func(&__KB_KEY(key_t, __kp->x)[__kp->i]); \
				++__kp->i;												\
			} else break;												\
		}																\
		free(__kstack);													\
	} while (0)

#define __kb_get_first(key_t, b, ret) do {	\
		kbnode_t *__x = (b)->root;			\
		while (__KB_PTR(b, __x)[0] != 0)	\
			__x = __KB_PTR(b, __x)[0];		\
		(ret) = __KB_KEY(key_t, __x)[0];	\
	} while (0)

#endif
