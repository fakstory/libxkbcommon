/*
 * Copyright © 2012 Ran Benita <ran234@gmail.com>
 * Copyright © 2019 Red Hat, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "config.h"
#include "test-config.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "xkbcommon/xkbcommon.h"
#include "test.h"
#include "utils.h"
#include "darray.h"
#include "xkbcomp/rules.h"

ATTR_PRINTF(3, 0) static void
log_fn_capture(struct xkb_context *ctx, enum xkb_log_level level,
               const char *fmt, va_list args)
{
    (void) level;
    darray_char *ls = xkb_context_get_user_data(ctx);
    if (!ls)
        return;
    char *s = NULL;
    int size = vasprintf(&s, fmt, args);
    if (size < 0 || !s)
        return;
    darray_append_string(*ls, s);
    free(s);
}

struct test_data {
    /* Rules file */
    const char *rules;

    /* Input */
    const char *model;
    const char *layout;
    const char *variant;
    const char *options;

    /* Expected output */
    const char *keycodes;
    const char *types;
    const char *compat;
    const char *symbols;
    const char *geometry;

    /* Or set this if xkb_components_from_rules() should fail. */
    bool should_fail;
};

static bool
test_rules(struct xkb_context *ctx, struct test_data *data)
{
    fprintf(stderr, "\n\nChecking : %s\t%s\t%s\t%s\t%s\n", data->rules,
            data->model, data->layout, data->variant, data->options);

    if (data->should_fail)
        fprintf(stderr, "Expecting: FAILURE\n");
    else
        fprintf(stderr, "Expecting: %s\t%s\t%s\t%s\n",
                data->keycodes, data->types, data->compat, data->symbols);

    const struct xkb_rule_names rmlvo = {
        data->rules, data->model, data->layout, data->variant, data->options
    };
    struct xkb_component_names kccgst;
    if (xkb_components_names_from_rules(ctx, &rmlvo, NULL, &kccgst)) {
        fprintf(stderr, "Received : %s\t%s\t%s\t%s\n",
                kccgst.keycodes, kccgst.types, kccgst.compatibility,
                kccgst.symbols);
    } else {
        fprintf(stderr, "Received : FAILURE\n");
        return data->should_fail;
    }

    const bool passed = streq_not_null(kccgst.keycodes, data->keycodes) &&
                        streq_not_null(kccgst.types, data->types) &&
                        streq_not_null(kccgst.compatibility, data->compat) &&
                        streq_not_null(kccgst.symbols, data->symbols) &&
                        streq_null(kccgst.geometry, data->geometry);

    free(kccgst.keycodes);
    free(kccgst.types);
    free(kccgst.compatibility);
    free(kccgst.symbols);
    free(kccgst.geometry);

    return passed;
}

int
main(int argc, char *argv[])
{
    struct xkb_context *ctx;

    test_init();

    setenv("XKB_CONFIG_ROOT", TEST_XKB_CONFIG_ROOT, 1);

    ctx = test_get_context(0);
    assert(ctx);

    struct test_data test1 = {
        .rules = "inc-src-simple",

        .model = "my_model", .layout = "my_layout", .variant = "", .options = "",

        .keycodes = "my_keycodes", .types = "default_types",
        .compat = "default_compat", .symbols = "my_symbols",
    };
    assert(test_rules(ctx, &test1));

    struct test_data test2 = {
        .rules = "inc-src-nested",

        .model = "my_model", .layout = "my_layout", .variant = "", .options = "",

        .keycodes = "my_keycodes", .types = "default_types",
        .compat = "default_compat", .symbols = "my_symbols",
    };
    assert(test_rules(ctx, &test2));

    struct test_data test3 = {
        .rules = "inc-src-looped",

        .model = "my_model", .layout = "my_layout", .variant = "", .options = "",

        .should_fail = true,
    };
    assert(test_rules(ctx, &test3));

    struct test_data test4 = {
        .rules = "inc-src-before-after",

        .model = "before_model", .layout = "my_layout", .variant = "", .options = "",

        .keycodes = "my_keycodes", .types = "default_types",
        .compat = "default_compat", .symbols = "default_symbols",
    };
    assert(test_rules(ctx, &test4));

    struct test_data test5 = {
        .rules = "inc-src-options",

        .model = "my_model", .layout = "my_layout", .variant = "my_variant",
        .options = "option11,my_option,colon:opt,option111",

        .keycodes = "my_keycodes", .types = "default_types",
        .compat = "default_compat+substring+group(bla)|some:compat",
        .symbols = "my_symbols+extra_variant+altwin(menu)",
    };
    assert(test_rules(ctx, &test5));

    struct test_data test6 = {
        .rules = "inc-src-loop-twice",

        .model = "my_model", .layout = "my_layout", .variant = "", .options = "",

        .keycodes = "my_keycodes", .types = "default_types",
        .compat = "default_compat", .symbols = "my_symbols",
    };
    assert(test_rules(ctx, &test6));

    struct test_data test7 = {
      .rules = "inc-no-newline",
      .should_fail = true,
    };
    assert(test_rules(ctx, &test7));

    struct test_data test8 = {
        .rules = "inc-src-relative-path",

        .model = "my_model", .layout = "my_layout", .variant = "", .options = "",

        .keycodes = "my_keycodes", .types = "default_types",
        .compat = "default_compat", .symbols = "my_symbols",
    };
    assert(test_rules(ctx, &test8));

    /*
     * Regression: when xkb_resolve_rules processes a rules file that has no
     * matching <rules>.pre file, the search-loop probe for ".pre" used to
     * leave the last attempted (non-existent) <rules>.pre filename in the
     * shared path[PATH_MAX] buffer. read_rules_file then reported parse
     * errors under the <rules>.pre filename instead of the real filename.
     *
     * "inc-no-bang" contains an unprefixed `include` (missing the leading
     * `!`), which the rules grammar rejects. The assertion checks that the
     * captured error names "inc-no-bang" — never "inc-no-bang.pre".
     */
    {
        struct xkb_context *log_ctx = test_get_context(0);
        assert(log_ctx);
        darray_char captured;
        darray_init(captured);
        xkb_context_set_user_data(log_ctx, &captured);
        xkb_context_set_log_fn(log_ctx, log_fn_capture);
        xkb_context_set_log_level(log_ctx, XKB_LOG_LEVEL_WARNING);

        struct test_data test_no_bang = {
            .rules = "inc-no-bang",
            .model = "my_model", .layout = "my_layout",
            .variant = "", .options = "",
            .should_fail = true,
        };
        assert(test_rules(log_ctx, &test_no_bang));

        const char *log = darray_items(captured);
        fprintf(stderr, "Captured log:\n%s", log ? log : "(empty)");
        assert(log);
        assert(strstr(log, "inc-no-bang") != NULL);
        assert(strstr(log, "inc-no-bang.pre") == NULL);

        darray_free(captured);
        xkb_context_unref(log_ctx);
    }

    xkb_context_unref(ctx);
    return 0;
}
