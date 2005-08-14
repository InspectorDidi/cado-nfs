#ifndef MANU_H_
#define MANU_H_

#ifdef	__cplusplus
#include <cassert>
extern "C" {
#endif

/* This stuff requires assert.h and stdio.h to be included first 
 * (or cassert and cstdio) */

/* Note: If we use the following, the full iostream stuff must be
 * included, which is a pain. We prefer to get around with cstdio alone
 */
#if defined(FORCE_CROAK_ON_CERR) && defined(__cplusplus)
#define manu_croaks(x,y)					\
	std::cerr	<< (x) << " in " << __func__ 			\
		<< " at " << __FILE__ << ':' << __LINE__	\
		<< " -- " << (y) << endl;

#else
#define manu_croaks(x,y)					\
	fprintf(stderr,"%s in %s at %s:%d -- %s\n",		\
			(x),__func__,__FILE__,__LINE__,(y))
#endif

#ifndef BEGIN_BLOCK
#define BEGIN_BLOCK	do {
#define END_BLOCK	} while (0)
#endif

/**********************************************************************/
/* A couple of helper macros that I like to define */
#ifdef ASSERT
#undef ASSERT
#endif
#ifdef ASSERT_ALWAYS
#undef ASSERT_ALWAYS
#endif
#ifdef BUG
#undef BUG
#endif
#ifdef BUG_ON
#undef BUG_ON
#endif
#ifdef BUG_IF
#undef BUG_IF
#endif
#ifdef MISSING
#undef MISSING
#endif
#ifdef WARNING
#undef WARNING
#endif
#ifdef BASH_USER
#undef BASH_USER
#endif

#define ASSERT(x)	assert(x)
#define BUG()	BEGIN_BLOCK				\
		manu_croaks("code BUG()", "Abort");	\
		abort();				\
	END_BLOCK
#define	BUG_ON(x)	BEGIN_BLOCK			\
	if (x) {					\
		manu_croaks("code BUG() : "		\
			"condition " #x " reached",	\
			"Abort");			\
		abort();				\
	}						\
	END_BLOCK
#define	BUG_IF(x)	BEGIN_BLOCK			\
	if (x) {					\
		manu_croaks("code BUG() : "		\
			"condition " #x " reached",	\
			"Abort");			\
		abort();				\
	}						\
	END_BLOCK
#define	ASSERT_ALWAYS(x)	BEGIN_BLOCK		\
	if (!(x)) {					\
		manu_croaks("code BUG() : "		\
			"condition " #x " failed",	\
			"Abort");			\
		abort();				\
	}						\
	END_BLOCK
#define MISSING()					\
	BEGIN_BLOCK					\
		manu_croaks("missing code", "Abort");	\
		BUG();					\
	END_BLOCK
#define WARNING(x)	manu_croaks("Warning",(x))
#define BAD_CODE_WARNING				\
	BEGIN_BLOCK					\
		static int i;				\
		if (!i++) {				\
			WARNING("This is bad code");	\
		}					\
	END_BLOCK
#define	BASH_USER(x)					\
	BEGIN_BLOCK					\
		manu_croaks("impolite user", (x));	\
		BUG();					\
	END_BLOCK

/**********************************************************************/
#ifndef TRUE
#define TRUE (0==0)
#endif

#ifndef FALSE
#define FALSE (0==1)
#endif

#ifndef ABS
#define ABS(x) ((x) >= 0 ? (x) : -(x))
#endif
	
#ifndef MIN
#define MIN(l,o) ((l) < (o) ? (l) : (o))
#endif

#ifndef MAX
#define MAX(h,i) ((h) > (i) ? (h) : (i))
#endif

/**********************************************************************/
#if defined(__GNUC__) && !defined(__cplusplus)
#define try_inline __inline__
#define UNUSED_VARIABLE __attribute__ ((unused))
#define EXPECT(x,val)	__builtin_expect(x,val)
#else
#define try_inline
#define UNUSED_VARIABLE
#define	EXPECT(x,val)	(x)
#endif

#define EXPECT_TRUE(x)	EXPECT(x,1)
#define EXPECT_FALSE(x)	EXPECT(x,0)

/**********************************************************************/
#ifndef DISABLE_ALLOCA
#include <alloca.h>
#define FAST_ALLOC(x)   alloca(x)
#define FAST_FREE(x)    
#else
#define FAST_ALLOC(x)   my_malloc(x)
#define FAST_FREE(x)    free(x)
#endif

/**********************************************************************/
/* That's dirty, but I really don't want to link libm in */
#define iceildiv(x,y)	(((x)+(y)-1)/(y))


#ifdef	__cplusplus
}
#endif

#endif	/* MANU_H_ */
