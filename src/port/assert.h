#ifndef SLIPSTREAM5000_ASSERT_H
#define SLIPSTREAM5000_ASSERT_H

#ifndef SLIP_ASSERT_TO_STDERR
#error The project assert replacement requires SLIP_ASSERT_TO_STDERR
#endif

#ifdef __cplusplus
extern "C" {
#endif

void SlipAssertFail(const char *message, const char *file, unsigned line);

#ifdef __cplusplus
}
#endif

#undef assert

#ifdef NDEBUG
#define assert(expression) ((void)0)
#else
#define assert(expression) ((void)((!!(expression)) || (SlipAssertFail(#expression, __FILE__, (unsigned)__LINE__), 0)))
#endif

#endif
