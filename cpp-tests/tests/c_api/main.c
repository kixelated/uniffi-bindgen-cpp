/* The generated C API on the default dispatcher thread. */
#include <c_test_support.h>

#include <capi.h>

#include <string.h>

static int streq(const char *a, const char *b) {
    return a && b && strcmp(a, b) == 0;
}

/* The value a map holds for `key`, or NULL. */
static const char *lookup(const capi_string_string_map *map, const char *key) {
    size_t i;
    for (i = 0; i < map->len; i++) {
        if (streq(map->keys[i], key)) {
            return map->values[i];
        }
    }
    return NULL;
}

static void defaults(void) {
    capi_item item = capi_item_default();
    CHECK(item.name == NULL);
    CHECK(item.count == 7);
    CHECK(streq(item.title, "untitled \"draft\""));
    CHECK(item.scale == 1.5);
    CHECK(item.visible);
    CHECK(item.offset == -3);
    CHECK(item.note == NULL);
    CHECK(!item.limit.has_value);
    CHECK(!item.tint.has_value);
    CHECK(item.target == NULL);
    CHECK(item.extra == NULL);
    CHECK(item.owner == NULL);
    CHECK(item.tags.len == 0 && item.tags.data == NULL);
}

/* Every field kind survives a round trip through Rust. */
static void records(void) {
    static const uint8_t payload[] = {1, 2, 3};
    static const uint8_t extra_data[] = {9};
    const char *tags[] = {"a", "b"};
    const char *label_keys[] = {"k1", "k2"};
    const char *label_values[] = {"v1", "v2"};
    const char *point_keys[] = {"p"};
    const capi_point point_values[] = {{5, 6}};
    const uint64_t values[] = {10, 20, 30};
    capi_optional_u32 maybes[2];
    capi_point path[2];
    capi_point target;
    capi_bytes extra;
    capi_store *owner = capi_store_new("owner");
    capi_item item = capi_item_default();
    capi_item *echo;
    capi_string_string_map labels;
    char *owner_name;

    maybes[0].has_value = true;
    maybes[0].value = 4;
    maybes[1].has_value = false;
    maybes[1].value = 0;
    path[0].x = 1;
    path[0].y = 1;
    path[1].x = 2;
    path[1].y = 2;
    target.x = 7;
    target.y = 8;
    extra.data = extra_data;
    extra.len = 1;

    item.name = "first";
    item.note = "a note";
    item.limit.has_value = true;
    item.limit.value = 99;
    item.tint.has_value = true;
    item.tint.value = CAPI_COLOR_BLUE;
    item.target = &target;
    item.extra = &extra;
    item.owner = owner;
    item.tags.data = tags;
    item.tags.len = 2;
    item.payload.data = payload;
    item.payload.len = 3;
    item.origin.x = -1;
    item.origin.y = -2;
    item.color = CAPI_COLOR_GREEN;
    item.shape.tag = CAPI_SHAPE_PATH;
    item.shape.path.points.data = path;
    item.shape.path.points.len = 2;
    item.labels.keys = label_keys;
    item.labels.values = label_values;
    item.labels.len = 2;
    item.points.keys = point_keys;
    item.points.values = point_values;
    item.points.len = 1;
    item.values.data = values;
    item.values.len = 3;
    item.maybes.data = maybes;
    item.maybes.len = 2;

    echo = capi_echo(&item);
    CHECK(echo != NULL);
    CHECK(streq(echo->name, "first"));
    CHECK(echo->count == 7);
    CHECK(streq(echo->title, "untitled \"draft\""));
    CHECK(echo->scale == 1.5);
    CHECK(echo->visible);
    CHECK(echo->offset == -3);
    CHECK(streq(echo->note, "a note"));
    CHECK(echo->limit.has_value && echo->limit.value == 99);
    CHECK(echo->tint.has_value && echo->tint.value == CAPI_COLOR_BLUE);
    CHECK(echo->target && echo->target->x == 7 && echo->target->y == 8);
    CHECK(echo->extra && echo->extra->len == 1 && echo->extra->data[0] == 9);
    CHECK(echo->tags.len == 2 && streq(echo->tags.data[0], "a") && streq(echo->tags.data[1], "b"));
    CHECK(echo->payload.len == 3 && memcmp(echo->payload.data, payload, 3) == 0);
    CHECK(echo->origin.x == -1 && echo->origin.y == -2);
    CHECK(echo->color == CAPI_COLOR_GREEN);
    CHECK(echo->shape.tag == CAPI_SHAPE_PATH);
    CHECK(echo->shape.path.points.len == 2);
    CHECK(echo->shape.path.points.data[1].x == 2);
    labels = echo->labels;
    CHECK(labels.len == 2);
    CHECK(streq(lookup(&labels, "k1"), "v1"));
    CHECK(streq(lookup(&labels, "k2"), "v2"));
    CHECK(echo->points.len == 1 && streq(echo->points.keys[0], "p"));
    CHECK(echo->points.values[0].x == 5 && echo->points.values[0].y == 6);
    CHECK(echo->values.len == 3 && echo->values.data[2] == 30);
    CHECK(echo->maybes.len == 2);
    CHECK(echo->maybes.data[0].has_value && echo->maybes.data[0].value == 4);
    CHECK(!echo->maybes.data[1].has_value);

    /* The echoed record owns a new handle to the same store. */
    CHECK(echo->owner != NULL && echo->owner != owner);
    owner_name = capi_store_name(echo->owner);
    CHECK(streq(owner_name, "owner"));
    capi_string_free(owner_name);

    capi_item_free(echo);
    capi_store_free(owner);

    /* Empty optionals and collections round-trip as empty. */
    item = capi_item_default();
    item.name = "empty";
    echo = capi_echo(&item);
    CHECK(echo->note == NULL && echo->target == NULL && echo->extra == NULL);
    CHECK(echo->owner == NULL && !echo->limit.has_value);
    CHECK(echo->payload.len == 0 && echo->payload.data == NULL);
    CHECK(echo->shape.tag == CAPI_SHAPE_DOT);
    CHECK(echo->labels.len == 0 && echo->labels.keys == NULL);
    capi_item_free(echo);

    capi_item_free(NULL);
}

static void enums(void) {
    capi_shape *shape = capi_make_shape(0);
    CHECK(shape->tag == CAPI_SHAPE_DOT);
    capi_shape_free(shape);

    shape = capi_make_shape(1);
    CHECK(shape->tag == CAPI_SHAPE_CIRCLE && shape->circle.radius == 2.5);
    capi_shape_free(shape);

    shape = capi_make_shape(2);
    CHECK(shape->tag == CAPI_SHAPE_LABEL && streq(shape->label.v1, "two"));
    capi_shape_free(shape);

    shape = capi_make_shape(3);
    CHECK(shape->tag == CAPI_SHAPE_PATH && shape->path.points.len == 3);
    CHECK(shape->path.points.data[2].x == 2 && shape->path.points.data[2].y == -2);
    capi_shape_free(shape);
}

static void scalars(void) {
    static const uint8_t data[] = {1, 2, 3};
    capi_bytes payload;
    capi_optional_u64 value;
    capi_optional_color color;
    capi_point point;
    char *text;

    payload.data = data;
    payload.len = 3;
    CHECK(capi_checksum(&payload) == 6);
    payload.data = NULL;
    payload.len = 0;
    CHECK(capi_checksum(&payload) == 0);

    value.has_value = false;
    value.value = 0;
    color.has_value = false;
    color.value = CAPI_COLOR_RED;
    text = capi_describe(value, color, NULL, NULL, NULL);
    CHECK(streq(text, "None None None None None"));
    capi_string_free(text);

    value.has_value = true;
    value.value = 3;
    color.has_value = true;
    color.value = CAPI_COLOR_BLUE;
    point.x = 1;
    point.y = 2;
    payload.data = data;
    payload.len = 2;
    text = capi_describe(value, color, "hi", &point, &payload);
    CHECK(streq(text, "Some(3) Some(Blue) Some(\"hi\") Some(CapiPoint { x: 1, y: 2 }) Some([1, 2])"));
    capi_string_free(text);
}

static void errors(void) {
    capi_error *error;
    capi_flat_error *flat;

    CHECK(capi_fail(0) == NULL);

    error = capi_fail(1);
    CHECK(error && error->tag == CAPI_ERROR_NOT_FOUND);
    CHECK(streq(capi_error_message(error), "not found"));
    capi_error_free(error);

    error = capi_fail(2);
    CHECK(error && error->tag == CAPI_ERROR_INVALID);
    CHECK(streq(error->invalid.v1, "two"));
    CHECK(streq(capi_error_message(error), "invalid: two"));
    capi_error_free(error);

    error = capi_fail(9);
    CHECK(error && error->tag == CAPI_ERROR_CODE);
    CHECK(error->code.code == 9);
    CHECK(error->code.at.x == 3 && error->code.at.y == 4);
    CHECK(streq(capi_error_message(error), "code 9 at 3,4"));
    capi_error_free(error);

    flat = capi_flat_fail(false);
    CHECK(flat && flat->tag == CAPI_FLAT_ERROR_BUSY);
    CHECK(streq(capi_flat_error_message(flat), "busy: later"));
    capi_flat_error_free(flat);

    flat = capi_flat_fail(true);
    CHECK(flat && flat->tag == CAPI_FLAT_ERROR_GONE);
    CHECK(streq(capi_flat_error_message(flat), "gone"));
    capi_flat_error_free(flat);

    capi_error_free(NULL);
}

static void objects(void) {
    capi_store *store = NULL;
    capi_store *fork;
    capi_store *renamed;
    capi_store *filled;
    capi_error *error;
    capi_item item;
    capi_item items[2];
    capi_item_list list;
    capi_item *got = NULL;
    capi_string_list *names;
    capi_optional_u64 limit;
    uint32_t index;
    char *text;

    error = capi_store_open("", &store);
    CHECK(error && error->tag == CAPI_ERROR_INVALID && store == NULL);
    capi_error_free(error);

    CHECK(capi_store_open("main", &store) == NULL && store != NULL);
    CHECK(capi_store_len(store) == 0);

    item = capi_item_default();
    item.name = "one";
    item.note = "n1";
    item.limit.has_value = true;
    item.limit.value = 5;
    index = 99;
    CHECK(capi_store_add(store, &item, &index) == NULL && index == 0);

    /* A failed call clears its out-param. */
    item.name = "";
    index = 99;
    error = capi_store_add(store, &item, &index);
    CHECK(error && error->tag == CAPI_ERROR_INVALID && index == 0);
    capi_error_free(error);

    item.name = "two";
    item.note = NULL;
    item.limit.has_value = false;
    CHECK(capi_store_add(store, &item, &index) == NULL && index == 1);
    CHECK(capi_store_len(store) == 2);

    CHECK(capi_store_get(store, 1, &got) == NULL);
    CHECK(streq(got->name, "two") && got->note == NULL);
    capi_item_free(got);

    error = capi_store_get(store, 5, &got);
    CHECK(error && error->tag == CAPI_ERROR_CODE && error->code.code == 5 && got == NULL);
    capi_error_free(error);

    got = capi_store_find(store, "one");
    CHECK(got && got->count == 7);
    capi_item_free(got);
    CHECK(capi_store_find(store, "nope") == NULL);

    text = capi_store_note(store, 0);
    CHECK(streq(text, "n1"));
    capi_string_free(text);
    CHECK(capi_store_note(store, 1) == NULL);

    limit = capi_store_limit(store, 0);
    CHECK(limit.has_value && limit.value == 5);
    limit = capi_store_limit(store, 1);
    CHECK(!limit.has_value);

    names = capi_store_names(store);
    CHECK(names->len == 2 && streq(names->data[0], "one") && streq(names->data[1], "two"));
    capi_string_list_free(names);

    fork = capi_store_fork(store, NULL);
    text = capi_store_name(fork);
    CHECK(streq(text, "main"));
    capi_string_free(text);
    renamed = capi_store_fork(store, "other");
    text = capi_store_name(renamed);
    CHECK(streq(text, "other"));
    capi_string_free(text);

    CHECK(capi_store_total(store, fork, NULL) == 4);
    CHECK(capi_store_total(store, fork, renamed) == 6);

    capi_store_clear(store);
    CHECK(capi_store_len(store) == 0 && capi_store_len(fork) == 2);

    items[0] = capi_item_default();
    items[0].name = "x";
    items[1] = capi_item_default();
    items[1].name = "y";
    list.data = items;
    list.len = 2;
    filled = capi_store_with_items("filled", &list);
    CHECK(capi_store_len(filled) == 2);

    capi_store_free(filled);
    capi_store_free(renamed);
    capi_store_free(fork);
    capi_store_free(store);
    capi_store_free(NULL);
}

typedef struct {
    test_latch *done;
    capi_error *error;
    uint64_t value;
    char *text;
    /* For the blocking-free test. */
    test_latch *entered;
    test_latch *release;
    int finished;
} result;

static void on_sum(void *user_data, uint64_t value) {
    result *r = (result *)user_data;
    r->value = value;
    test_latch_signal(r->done);
}

static void on_greet(void *user_data, capi_error *error, char *value) {
    result *r = (result *)user_data;
    r->error = error;
    r->text = value;
    test_latch_signal(r->done);
}

static void on_next(void *user_data, capi_error *error, uint64_t value) {
    result *r = (result *)user_data;
    r->error = error;
    r->value = value;
    test_latch_signal(r->done);
}

static void on_next_slow(void *user_data, capi_error *error, uint64_t value) {
    result *r = (result *)user_data;
    (void)error;
    r->value = value;
    test_latch_signal(r->entered);
    test_latch_wait(r->release);
    r->finished = 1;
}

static void async_calls(void) {
    const uint64_t values[] = {10, 20, 30};
    capi_u64_list list;
    capi_store *store = capi_store_new("async");
    capi_task *task;
    result r;

    memset(&r, 0, sizeof r);
    r.done = test_latch_new();

    /* Ready on its first poll, yet still delivered on the dispatcher. */
    list.data = values;
    list.len = 3;
    task = capi_sum(&list, on_sum, &r);
    test_latch_wait(r.done);
    CHECK(r.value == 60);
    capi_task_free(task);

    task = capi_greet("bob", on_greet, &r);
    test_latch_wait(r.done);
    CHECK(r.error == NULL && streq(r.text, "hello, bob"));
    capi_string_free(r.text);
    capi_task_free(task);

    task = capi_greet("", on_greet, &r);
    test_latch_wait(r.done);
    CHECK(r.error && r.error->tag == CAPI_ERROR_INVALID && r.text == NULL);
    capi_error_free(r.error);
    capi_task_free(task);

    /* Pending until another call wakes it. */
    task = capi_store_next(store, on_next, &r);
    capi_store_push(store, 42);
    test_latch_wait(r.done);
    CHECK(r.error == NULL && r.value == 42);
    capi_task_free(task);

    /* Freeing a task whose callback is running elsewhere waits for the callback. */
    r.entered = test_latch_new();
    r.release = test_latch_new();
    task = capi_store_next(store, on_next_slow, &r);
    capi_store_push(store, 7);
    test_latch_wait(r.entered);
    test_latch_signal_from_thread(r.release);
    capi_task_free(task);
    CHECK(r.finished);
    test_latch_free(r.entered);
    test_latch_free(r.release);

    capi_store_close(store);
    task = capi_store_next(store, on_next, &r);
    test_latch_wait(r.done);
    CHECK(r.error && r.error->tag == CAPI_ERROR_NOT_FOUND);
    capi_error_free(r.error);
    capi_task_free(task);

    capi_task_free(NULL);
    capi_store_free(store);
    test_latch_free(r.done);
}

int main(void) {
    defaults();
    records();
    enums();
    scalars();
    errors();
    objects();
    async_calls();
    capi_shutdown_dispatcher();
    return 0;
}
