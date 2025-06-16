#include <bio/bio.h>
#include <assert.h>

//! [CLS]
typedef struct {
	int foo;
} my_cls_t;

static void
init_my_cls(void* ptr) {
	my_cls_t* cls = ptr;
	*cls = (my_cls_t){ .foo = 42 };
}

static const bio_cls_t MY_CLS = {
	.size = sizeof(my_cls_t),
	.init = &init_my_cls,
};

static void entrypoint(void* userdata) {
	my_cls_t* cls = bio_get_cls(&MY_CLS);
	assert(cls->foo == 42);  // Initialized

	my_cls_t* cls2 = bio_get_cls(&MY_CLS);
	assert(cls == cls2);  // The same object
}
//! [CLS]
