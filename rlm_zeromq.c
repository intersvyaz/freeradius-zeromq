#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>
#include <zmq.h>
#include <bson.h>

#define RLM_ZEROMQ_FORMAT_RAW_STR "raw"
#define RLM_ZEROMQ_FORMAT_BSON_STR "bson"

#define min(x, y) ((x) < (y) ? (x) : (y))

enum rlm_zeromq_format {
  RLM_ZEROMQ_FORMAT_RAW,
  RLM_ZEROMQ_FORMAT_BSON
};

typedef struct rlm_zeromq_t {
  struct {
    const char *server;
    const char *format;
    vp_tmpl_t *data;
  } cfg;

  int format;
  const char *name;
  void *zmq_ctx;
  fr_connection_pool_t *pool;
} rlm_zeromq_t;

typedef struct rlm_zeromq_conn {
  void *sock;
} rlm_zeromq_conn_t;

static const CONF_PARSER module_config[] = {
    {"server", FR_CONF_OFFSET(PW_TYPE_STRING | PW_TYPE_NOT_EMPTY, rlm_zeromq_t, cfg.server), NULL},
    {"format", FR_CONF_OFFSET(PW_TYPE_STRING | PW_TYPE_REQUIRED, rlm_zeromq_t, cfg.format), NULL},
    {"data", FR_CONF_OFFSET(PW_TYPE_STRING | PW_TYPE_TMPL | PW_TYPE_REQUIRED, rlm_zeromq_t, cfg.data), NULL},
    CONF_PARSER_TERMINATOR
};

/**
 * Detach module.
 * @param[in] instance Module instance.
 * @return Zero on success.
 */
static int mod_detach(void *instance) {
  rlm_zeromq_t *inst = instance;

  fr_connection_pool_free(inst->pool);

  if (inst->zmq_ctx) {
    if (0 != zmq_ctx_destroy(inst->zmq_ctx)) {
      ERROR("rlm_zeromq (%s): Ungraceful zeromq context close: %s", inst->name, zmq_strerror(errno));
    }
  }

  return 0;
}

/**
 * Module connection destructor.
 * @param[in] conn Connection handle.
 * @return Zero on success.
 */
static int mod_conn_free(rlm_zeromq_conn_t *conn) {
  if (conn->sock) {
    if (0 != zmq_close(conn->sock)) {
      ERROR("rlm_zeromq: Ungraceful zeromq socket close: %s", zmq_strerror(errno));
    }
  }

  DEBUG("rlm_zeromq: closed connection");
  return 0;
}

/**
 * Module connection constructor.
 * @param[in] ctx Talloc context.
 * @param[in] instance Module instance.
 * @return NULL on error, else a connection handle.
 */
static void *mod_conn_create(TALLOC_CTX *ctx, void *instance) {
  rlm_zeromq_t *inst = instance;

  void *sock = NULL;

  sock = zmq_socket(inst->zmq_ctx, ZMQ_PUSH);
  if (!sock) {
    ERROR("rlm_zeromq (%s): Failed to create socket: %s", inst->name, zmq_strerror(errno));
    goto err;
  }

  if (zmq_connect(sock, inst->cfg.server) != 0) {
    ERROR("rlm_zeromq (%s): Failed to connect to '%s': %s", inst->name, inst->cfg.server, zmq_strerror(errno));
    goto err;
  }

  rlm_zeromq_conn_t *conn = talloc_zero(ctx, rlm_zeromq_conn_t);
  conn->sock = sock;
  talloc_set_destructor(conn, mod_conn_free);

  return conn;

  err:
  if (sock) zmq_close(sock);
  return NULL;
}

/**
 * Instantiate module.
 * @param[in] conf Module config.
 * @param[in] instance Module instance.
 * @return Zero on success.
 */
static int mod_instantiate(CONF_SECTION *conf, void *instance) {
  rlm_zeromq_t *inst = instance;
  (void) conf;

  inst->name = cf_section_name2(conf);
  if (!inst->name) {
    inst->name = cf_section_name1(conf);
  }

  if (!strcasecmp(inst->cfg.format, RLM_ZEROMQ_FORMAT_RAW_STR)) {
    inst->format = RLM_ZEROMQ_FORMAT_RAW;
  } else if (!strcasecmp(inst->cfg.format, RLM_ZEROMQ_FORMAT_BSON_STR)) {
    inst->format = RLM_ZEROMQ_FORMAT_BSON;
  } else {
    cf_log_err_cs(conf, "Invalid 'format' option, use 'raw' or 'bson'");
    goto err;
  }

  if (!cf_pair_find(conf, "pool")) {
    if (!inst->cfg.server) {
      cf_log_err_cs(conf, "Invalid or missing 'server' option");
      goto err;
    }

    inst->zmq_ctx = zmq_ctx_new();
    if (!inst->zmq_ctx) {
      ERROR("rlm_zeromq (%s): Failed to create zeromq context: %s", inst->name, zmq_strerror(errno));
      goto err;
    }
  } else {
    if (inst->cfg.server) {
      cf_log_err_cs(conf, "Can't use server option when foreign connection pool specified");
      goto err;
    }
  }

  inst->pool = fr_connection_pool_module_init(conf, inst, mod_conn_create, NULL, inst->name);
  if (!inst->pool) {
    goto err;
  }

  return 0;

  err:
  mod_detach(inst);
  return -1;
}

/**
 * Main module procedure.
 * @param[in] instance Module instance.
 * @param[in] request Radius request.
 * @return One of #rlm_rcode_t.
 */
static rlm_rcode_t mod_proc(void *instance, REQUEST *request) {
  rlm_zeromq_t *inst = instance;
  rlm_zeromq_conn_t *conn = NULL;
  rlm_rcode_t code = RLM_MODULE_FAIL;

  char *data = NULL;
  bson_t *bson_data = NULL;
  const void *msg = NULL;
  size_t msg_len = 0;

  conn = fr_connection_get(inst->pool);
  if (!conn) {
    goto end;
  }

  ssize_t data_len = tmpl_aexpand(request, &data, request, inst->cfg.data, NULL, NULL);
  if (data_len < 0) {
    RERROR("Failed to substitute attributes for data '%s'", inst->cfg.data->name);
    goto end;
  }

  if (inst->format == RLM_ZEROMQ_FORMAT_BSON) {
    bson_error_t error;

    bson_data = bson_new_from_json((uint8_t *) data, data_len, &error);
    if (!bson_data) {
      RERROR("JSON->BSON conversion failed for '%s': %d.%d %s", data, error.domain, error.code, error.message);
      goto end;
    }

    msg = bson_get_data(bson_data);
    msg_len = bson_data->len;
  } else {
    msg = data;
    msg_len = (size_t) data_len;
  }

  if (TEMP_FAILURE_RETRY(zmq_send(conn->sock, msg, msg_len, 0)) == -1) {
    RERROR("Failed to send message: %s", zmq_strerror(errno));
  } else {
    code = RLM_MODULE_OK;
  }

  end:
  if (conn) fr_connection_release(inst->pool, conn);
  if (bson_data) bson_destroy(bson_data);
  return code;
}

/* globally exported name */
extern module_t rlm_zeromq;
module_t rlm_zeromq = {
    .magic = RLM_MODULE_INIT,
    .name = "zeromq",
    .type = RLM_TYPE_THREAD_SAFE | RLM_TYPE_HUP_SAFE,
    .inst_size = sizeof(rlm_zeromq_t),
    .config = module_config,
    .bootstrap = NULL,
    .instantiate = mod_instantiate,
    .detach = mod_detach,
    .methods = {
        [MOD_AUTHENTICATE] = mod_proc,
        [MOD_AUTHORIZE] = mod_proc,
        [MOD_PREACCT] = mod_proc,
        [MOD_ACCOUNTING] = mod_proc,
        [MOD_SESSION] = NULL,
        [MOD_PRE_PROXY] = mod_proc,
        [MOD_POST_PROXY] = mod_proc,
        [MOD_POST_AUTH] = mod_proc,
#ifdef WITH_COA
        [MOD_RECV_COA] = mod_proc,
        [MOD_SEND_COA] = mod_proc,
#endif
    },
};
