#ifndef _LINUX_JUMP_LABEL_H
#define _LINUX_JUMP_LABEL_H
/* Static keys as plain flags. */
struct static_key { int enabled; };
struct static_key_true { struct static_key key; };
struct static_key_false { struct static_key key; };
#define STATIC_KEY_INIT_TRUE	{ .enabled = 1 }
#define STATIC_KEY_INIT_FALSE	{ .enabled = 0 }
#define STATIC_KEY_TRUE_INIT	(struct static_key_true) { .key = STATIC_KEY_INIT_TRUE }
#define STATIC_KEY_FALSE_INIT	(struct static_key_false) { .key = STATIC_KEY_INIT_FALSE }
#define DEFINE_STATIC_KEY_TRUE(name)	struct static_key_true name = STATIC_KEY_TRUE_INIT
#define DEFINE_STATIC_KEY_FALSE(name)	struct static_key_false name = STATIC_KEY_FALSE_INIT
#define DECLARE_STATIC_KEY_TRUE(name)	extern struct static_key_true name
#define DECLARE_STATIC_KEY_FALSE(name)	extern struct static_key_false name
#define DEFINE_STATIC_KEY_TRUE_RO(name)	DEFINE_STATIC_KEY_TRUE(name)
#define DEFINE_STATIC_KEY_FALSE_RO(name)	DEFINE_STATIC_KEY_FALSE(name)
#define static_branch_likely(x)		(__builtin_expect(!!(x)->key.enabled, 1))
#define static_branch_unlikely(x)	(__builtin_expect(!!(x)->key.enabled, 0))
#define static_branch_enable(x)		((x)->key.enabled = 1)
#define static_branch_disable(x)	((x)->key.enabled = 0)
#define static_branch_inc(x)		((x)->key.enabled++)
#define static_branch_dec(x)		((x)->key.enabled--)
#define static_key_enabled(x)		((x)->key.enabled > 0)
#define static_key_false(x)			((x)->enabled > 0)
#define static_key_true(x)			((x)->enabled > 0)
#endif
