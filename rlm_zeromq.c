#ifdef STANDALONE_BUILD

#include <freeradius/radiusd.h>
#include <freeradius/modules.h>

#else
#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>
#endif

#include <zmq.h>
#include <bson.h>

#define RLM_ZEROMQ_FORMAT_RAW_STR "raw"
#define RLM_ZEROMQ_FORMAT_BSON_STR "bson"

enum rlm_zeromq_format
{
    RLM_ZEROMQ_FORMAT_RAW,
    RLM_ZEROMQ_FORMAT_BSON
};

typedef struct rlm_zeromq_t
{
    struct
    {
        char *server;
        char *format;
        char *data;
        bool lazy_connect;
    } cfg;

    int format;
    void *zmq_ctx;
    void *zmq_sock;
    bool connected;
} rlm_zeromq_t;

static CONF_PARSER module_config[] = {
        {"server",       PW_TYPE_STRING_PTR, offsetof(rlm_zeromq_t, cfg.server),       NULL, ""},
        {"format",       PW_TYPE_STRING_PTR, offsetof(rlm_zeromq_t, cfg.format),       NULL, ""},
        {"data",         PW_TYPE_STRING_PTR, offsetof(rlm_zeromq_t, cfg.data),         NULL, ""},
        {"lazy_connect", PW_TYPE_BOOLEAN,    offsetof(rlm_zeromq_t, cfg.lazy_connect), NULL, "yes"},
        {NULL, -1, 0,                                                                  NULL, NULL}
};

static int zeromq_detach(void *instance)
{
    rlm_zeromq_t *inst = instance;

    if (inst->zmq_sock) {
        if (0 != zmq_close(inst->zmq_sock)) {
            radlog(L_ERR, "rlm_zeromq: Ungraceful zeromq socket close: %s", zmq_strerror(errno));
        }
    }
    if (inst->zmq_ctx) {
        if (0 != zmq_ctx_destroy(inst->zmq_ctx)) {
            radlog(L_ERR, "rlm_zeromq: Ungraceful zeromq context close: %s", zmq_strerror(errno));
        }
    }

    free(inst);

    return 0;
}

static int zeromq_connect(rlm_zeromq_t *inst)
{
    if (0 != zmq_connect(inst->zmq_sock, inst->cfg.server)) {
        radlog(L_ERR, "rlm_zeromq: Failed to connect to '%s': %s", inst->cfg.server, zmq_strerror(errno));
        return -1;
    } else {
        radlog(L_INFO, "rlm_zeromq: Connected to %s", inst->cfg.server);
    }

    return 0;
}

static int zeromq_instantiate(CONF_SECTION *conf, void **instance)
{
    rlm_zeromq_t *inst;

    inst = rad_malloc(sizeof(rlm_zeromq_t));
    if (!inst) {
        return -1;
    }
    memset(inst, 0, sizeof(*inst));

    if (cf_section_parse(conf, inst, module_config) < 0) {
        goto err;
    }

    if (0 == strcasecmp(inst->cfg.format, RLM_ZEROMQ_FORMAT_RAW_STR)) {
        inst->format = RLM_ZEROMQ_FORMAT_RAW;
    } else if (0 == strcasecmp(inst->cfg.format, RLM_ZEROMQ_FORMAT_BSON_STR)) {
        inst->format = RLM_ZEROMQ_FORMAT_BSON;
    } else {
        radlog(L_ERR, "rlm_zeromq: Invalid output format data type '%s', only 'raw' or 'bson' is acceptable",
               inst->cfg.format);
        goto err;
    }

    inst->zmq_ctx = zmq_ctx_new();
    if (!inst->zmq_ctx) {
        radlog(L_ERR, "rlm_zeromq: Failed to create zeromq context: %s", zmq_strerror(errno));
        goto err;
    }

    inst->zmq_sock = zmq_socket(inst->zmq_ctx, ZMQ_PUSH);
    if (!inst->zmq_sock) {
        radlog(L_ERR, "rlm_zeromq: Failed to create zeromq socket: %s", zmq_strerror(errno));
        return -1;
    }

    if (!inst->cfg.lazy_connect) {
        if (0 != zeromq_connect(inst)) {
            goto err;
        }
        inst->connected = true;
    }

    *instance = inst;
    return 0;

    err:
    zeromq_detach(inst);
    return -1;
}

static int zeromq_proc(void *instance, REQUEST *request)
{
    rlm_zeromq_t *inst = instance;

    if (!inst->connected) {
        if (0 != zeromq_connect(inst)) {
            return RLM_MODULE_NOOP;
        }
        inst->connected = true;
    }

    const void *data = NULL;
    size_t data_len = 0;

    bson_t *doc = NULL;

    char pre_data[8192];
    int pre_data_len = radius_xlat(pre_data, sizeof(pre_data), inst->cfg.data, request, NULL);

    if (RLM_ZEROMQ_FORMAT_BSON == inst->format) {
        bson_error_t error;

        doc = bson_new_from_json((uint8_t *) pre_data, pre_data_len, &error);
        if (!doc) {
            radlog(L_ERR, "rlm_zeromq: JSON->BSON conversion failed for '%s': %d.%d %s",
                   pre_data, error.domain, error.code, error.message);
            return RLM_MODULE_NOOP;
        }

        data = bson_get_data(doc);
        data_len = doc->len;
    } else {
        data = pre_data;
        data_len = (size_t) pre_data_len;
    }

    if (-1 == zmq_send(inst->zmq_sock, data, data_len, 0)) {
        radlog(L_ERR, "rlm_zeromq: Failed to send message: %s", zmq_strerror(errno));
    }

    if (doc) {
        bson_destroy(doc);
    }

    return RLM_MODULE_NOOP;
}

/* globally exported name */
module_t rlm_zeromq = {
        RLM_MODULE_INIT,
        "zeromq",
        RLM_TYPE_THREAD_UNSAFE,
        zeromq_instantiate,   /* instantiation */
        zeromq_detach,        /* detach */
        {
                zeromq_proc,  /* authentication */
                zeromq_proc,  /* authorization */
                zeromq_proc,  /* preaccounting */
                zeromq_proc,  /* accounting */
                NULL,         /* checksimul */
                zeromq_proc,  /* pre-proxy */
                zeromq_proc,  /* post-proxy */
                zeromq_proc   /* post-auth */
        },
};
