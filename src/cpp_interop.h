#ifndef __CPP_H__
#define __CPP_H__

#ifdef __cplusplus
#define Q_BEGIN_DECLS	extern "C" {
#define Q_END_DECLS		}
#define QAPI			extern "C"
#else
#define Q_BEGIN_DECLS
#define Q_END_DECLS
#define QAPI
#endif

#endif // __CPP_H__

// vim: set noexpandtab tabstop=4 shiftwidth=4 :
