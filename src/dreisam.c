/*
 * The dreisam front controller. Processes FastCGI requests
 * and dispatches them to the action and view operators.
 *
 * Copyright (C) 2015 Rene Hartmann.
 * See the file COPYING for redistribution information.
 */

#include <fcgiapp.h>

#include <dli/iinterp.h>
#include <rel/rdb.h>

#include "getaction.h"
#include "viewop.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* Maximum POST request size is 1MB */
#define POST_SIZE_MAX ((size_t) (1024 * 1024))

/* Return codes */
enum {
    DR_ERR_INIT_BUILTINS = 25,
    DR_ERR_INIT_INTERP = 26,
    DR_ERR_INIT_OP = 27,
    DR_ERR_DECL = 28,
    DR_ERR_CONFIG = 29,
    DR_ERR_CONNECT = 30,
    DR_ERR_REQUEST = 31,
    DR_ERR_GET_OP = 32
};

static FCGX_Stream *fcgi_out;
static FCGX_Stream *fcgi_err;
static RDB_operator *send_headers_op;

static const char *
sc_reason(int code) {
    switch (code) {
    case 100:
        return "Continue";
    case 101:
        return "Switching Protocols";
    case 102:
        return "Processing";
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 202:
        return "Accepted";
    case 203:
        return "Non-Authoritative Information";
    case 204:
        return "No Content";
    case 205:
        return "Reset Content";
    case 206:
        return "Partial Content";
    case 207:
        return "Multi-Status";
    case 208:
        return "Already Reported";
    case 226:
        return "IM Used";
    case 300:
        return "Multiple Choices";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 303:
        return "See Other";
    case 304:
        return "Not Modified";
    case 305:
        return "Use Proxy";
    case 307:
        return "Temporary Redirect";
    case 308:
        return "Permanent Redirect";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 402:
        return "Payment Required";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 406:
        return "Not Acceptable";
    case 407:
        return "Proxy Authentication Required";
    case 408:
        return "Request Time-out";
    case 409:
        return "Conflict";
    case 410:
        return "Gone";
    case 411:
        return "Length Required";
    case 412:
        return "Precondition Failed";
    case 413:
        return "Request Entity Too Large";
    case 414:
        return "Request-URL Too Long";
    case 415:
        return "Unsupported Media Type";
    case 416:
        return "Requested range not satisfiable";
    case 417:
        return "Expectation Failed  ";
    case 420:
        return "Policy Not Fulfilled";
    case 421:
        return "There are too many connections from your internet address";
    case 422:
        return "Unprocessable Entity";
    case 423:
        return "Locked";
    case 424:
        return "Failed Dependency";
    case 425:
        return "Unordered Collection";
    case 426:
        return "Upgrade Required";
    case 428:
        return "Precondition Required";
    case 429:
        return "Too Many Requests";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    case 504:
        return "Gateway Time-out";
    case 505:
        return "HTTP Version not supported";
    case 506:
        return "Variant Also Negotiates";
    case 507:
        return "Insufficient Storage";
    case 508:
        return "Loop Detected";
    case 509:
        return "Bandwidth Limit Exceeded";
    case 510:
        return "Not Extended";
    }
    return "";
}

static void
log_err(Duro_interp *interpp, RDB_exec_context *ecp, FCGX_Stream *err)
{
    RDB_object msgobj;
    RDB_object *errp = RDB_get_err(ecp);

    RDB_init_obj(&msgobj);

    FCGX_PutS(RDB_type_name(RDB_obj_type(errp)), err);

    if (RDB_obj_property(errp, "msg", &msgobj, NULL, ecp, NULL) == RDB_OK) {
        FCGX_FPrintF(err, ": %s", RDB_obj_string(&msgobj));
    }
    if (interpp->err_opname != NULL) {
        FCGX_FPrintF(err, " in %s", interpp->err_opname);
    }
    if (interpp->err_line != -1) {
        FCGX_FPrintF(err, " at line %d", interpp->err_line);
    }
    FCGX_PutS("\n", err);

    RDB_destroy_obj(&msgobj, ecp);
}

static int
send_headers(RDB_exec_context *ecp)
{
    if (RDB_call_update_op(send_headers_op, 0, NULL, ecp, NULL)
            != RDB_OK) {
        FCGX_PutS("Sending headers failed\n", fcgi_err);
        return RDB_ERROR;
    }
    return RDB_OK;
}

static int
op_net_put_line(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    send_headers(ecp);
    if (FCGX_PutS(RDB_obj_string(argv[0]), fcgi_out) == -1)
        goto error;
    if (FCGX_PutS("\n", fcgi_out) == -1)
        goto error;

    return RDB_OK;

error:
    RDB_errcode_to_error(errno, ecp);
    return RDB_ERROR;
}

static int
op_net_put(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    send_headers(ecp);
    if (FCGX_PutS(RDB_obj_string(argv[0]), fcgi_out) == -1) {
        RDB_errcode_to_error(errno, ecp);
        return RDB_ERROR;
    }

    return RDB_OK;
}

static int
op_net_put_err_line(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    send_headers(ecp);
    if (FCGX_PutS(RDB_obj_string(argv[0]), fcgi_err) == -1)
        goto error;
    if (FCGX_PutS("\n", fcgi_err) == -1)
        goto error;

    return RDB_OK;

error:
    RDB_errcode_to_error(errno, ecp);
    return RDB_ERROR;
}

static int
op_net_put_err(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp)
{
    send_headers(ecp);
    if (FCGX_PutS(RDB_obj_string(argv[0]), fcgi_err) == -1) {
        RDB_errcode_to_error(errno, ecp);
        return RDB_ERROR;
    }

    return RDB_OK;
}

static int
op_net_status_reason(int argc, RDB_object *argv[], RDB_operator *op,
        RDB_exec_context *ecp, RDB_transaction *txp, RDB_object *retvalp)
{
    return RDB_string_to_obj(retvalp, sc_reason(RDB_obj_int(argv[0])), ecp);
}

/*
 * Send error response if it has not already been sent.
 * *ecp is only used to look up the interpreter and is not modified even if RDB_ERROR is returned.
 */
static int
send_error_response(int status, const char *msg, FCGX_Stream *out, RDB_exec_context *ecp)
{
    int cnt;
    RDB_exec_context ec;
    RDB_object *headers_sent;
    Duro_interp *interpp = RDB_ec_property(ecp, "INTERP");

    if (interpp == NULL)
        return RDB_ERROR;

    RDB_init_exec_context(&ec);
    headers_sent = Duro_lookup_var("resp_headers_sent", interpp, &ec);
    RDB_destroy_exec_context(&ec);

    if (RDB_obj_bool(headers_sent))
        return 0;
    cnt = FCGX_FPrintF(out,
              "Status: %d %s\n"
              "Content-type: text/html; charset=utf-8\n"
              "\n"
              "<html>\n"
              "<head>\n"
              "<title>%s</title>\n"
              "<p>%s\n"
              "<html>\n",
              status, sc_reason(status), msg, msg);
    if (cnt > 0)
        RDB_bool_to_obj(headers_sent, RDB_TRUE);
    return cnt;
}

/*
 * Reads POST request data and converts it to model data.
 *
 * If RDB_ERROR has been returned and *client_error is TRUE,
 * a response has been sent and no error value has been stored in *ecp.
 */
static int
post_req_to_model(RDB_object *modelp, RDB_exec_context *ecp,
        FCGX_ParamArray envp, FCGX_Stream *in, FCGX_Stream *out,
        FCGX_Stream *err, RDB_bool *client_error)
{
    int ret;
    size_t len;
    char *content;
    char *content_length = FCGX_GetParam("CONTENT_LENGTH", envp);
    if (content_length == NULL) {
        send_error_response(411, "Content length required", out, ecp);
        *client_error = RDB_TRUE;
        return RDB_ERROR;
    }
    len = (size_t) atol(content_length);

    /* Check size limit */
    if (len > POST_SIZE_MAX) {
        send_error_response(413, "Content length limit exceeded", out, ecp);
        *client_error = RDB_TRUE;
        return RDB_ERROR;
    }

    /* Read content */
    content = RDB_alloc(len + 1, ecp);
    if (content == NULL) {
        send_error_response(507, "Insufficient memory", out, ecp);
        *client_error = RDB_FALSE;
        return RDB_ERROR;
    }
    if (FCGX_GetStr(content, len, in) != len) {
        send_error_response(400, "Request length does not match content length",
                out, ecp);
        RDB_free(content);
        *client_error = RDB_TRUE;
        return RDB_ERROR;
    }
    content[len] = '\0';

    /* Convert content to tuple */
    ret = RDB_net_form_to_tuple(modelp, content, ecp);
    RDB_free(content);
    *client_error = RDB_FALSE;
    return ret;
}

static int
process_request(Duro_interp *interpp, RDB_exec_context *ecp,
        FCGX_ParamArray envp, FCGX_Stream *in, FCGX_Stream *out, FCGX_Stream *err)
{
    int ret;
    RDB_object viewopname;
    RDB_object model;
    RDB_object *argv[2];
    RDB_operator *controller_op;
    const char *path_info;
    const char *request_method;
    RDB_operator *view_op;

    RDB_init_obj(&viewopname);
    RDB_init_obj(&model);

    if (Duro_dt_execute_str("resp_status := ''; "
            "resp_headers := array ('Content-type: text/html; charset=utf-8'); "
            "resp_headers_sent := FALSE;",
            interpp, ecp) != RDB_OK) {
        goto error;
    }

    path_info = FCGX_GetParam("PATH_INFO", envp);
    if (path_info == NULL)
        path_info = "";

    /*
     * The initial view name is taken from path_info, with the leading
     * slash removed, and preceded by 't.' so it will match the view operator
     * generated from a template.
     */
    RDB_string_to_obj(&viewopname, "t.", ecp);
    if (path_info[0] == '/') {
        if (RDB_append_string(&viewopname, path_info + 1, ecp) != RDB_OK)
            goto error;
    } else {
        if (RDB_append_string(&viewopname, path_info, ecp) != RDB_OK)
            goto error;
    }

    controller_op = Dr_get_action_op(path_info, interpp, ecp);
    if (controller_op == NULL) {
        if (RDB_obj_type(RDB_get_err(ecp)) == &RDB_NOT_FOUND_ERROR) {
            FCGX_FPrintF(out,
                    "Status: 404 Not found\n"
                    "Content-type: text/html; charset=utf-8\n"
                    "\n"
                    "<html>\n"
                    "<head>\n"
                    "<title>No action found</title>\n"
                    "<body>\n"
                    "<p>No action found for path %s\n"
                    "</html>\n",
                    path_info);
            return RDB_OK;
        }
        if (RDB_obj_type(RDB_get_err(ecp)) == &RDB_OPERATOR_NOT_FOUND_ERROR) {
            log_err(interpp, ecp, err);
            FCGX_FPrintF(out,
                    "Status: 404 Not found\n"
                    "Content-type: text/html; charset=utf-8\n"
                    "\n"
                    "<html>\n"
                    "<head>\n"
                    "<title>No action operator found</title>\n"
                    "<body>\n"
                    "<p>No action operator found for path %s\n"
                    "<html>\n",
                    path_info);
            return RDB_OK;
        }
        goto error;
    }

    if (RDB_set_init_value(&model, RDB_get_parameter(controller_op, 0)->typ,
            interpp->envp, ecp) != RDB_OK) {
        FCGX_PutS("Initializing the model failed\n", err);
        goto error;
    }

    request_method = FCGX_GetParam("REQUEST_METHOD", envp);
    if (request_method == NULL)
        request_method = "";

    if (strcmp(request_method, "GET") == 0) {
        char *query_string = FCGX_GetParam("QUERY_STRING", envp);
        if (RDB_net_form_to_tuple(&model, query_string, ecp) != RDB_OK)
            goto error;
    } else if (strcmp(request_method, "POST") == 0) {
        RDB_bool client_error;
        if (post_req_to_model(&model, ecp, envp, in, out, err, &client_error)
                != RDB_OK) {
            if (client_error)
                return RDB_OK;
            goto error;
        }
    } else {
        FCGX_FPrintF(out,
                  "Status: 405 Method not allowed\n"
                  "Content-type: text/html; charset=utf-8\n"
                  "Allow: GET, POST\n"
                  "\n"
                  "<html>\n"
                  "<head>\n"
                  "<title>Request method not supported</title>\n"
                  "<body>\n"
                  "<p>Request method not supported\n"
                  "<html>\n",
                  path_info);
        return RDB_OK;
    }

    fcgi_out = out;
    fcgi_err = err;

    argv[0] = &model;
    argv[1] = &viewopname;
    if (RDB_call_update_op(controller_op, 2, argv, ecp, NULL)
            != RDB_OK) {
        FCGX_PutS("Invoking controller operator failed\n", err);
        goto error;
    }

    if (*RDB_obj_string(&viewopname) != '\0') {
        view_op = Dr_provide_view_op(&viewopname, interpp, ecp, err);
        if (view_op == NULL)
            goto error;

        argv[0] = &model;
        if (RDB_call_update_op(view_op, 1, argv, ecp, NULL) != RDB_OK) {
            FCGX_FPrintF(err, "Invoking view operator %s failed\n",
                    RDB_obj_string(&viewopname));
            log_err(interpp, ecp, err);
            send_error_response(500, "processing the request failed", out, ecp);

            return RDB_OK;
        }
    } else {
        /* No view - send headers if they haven't already been sent */
        send_headers(ecp);
    }

    RDB_destroy_obj(&viewopname, ecp);
    RDB_destroy_obj(&model, ecp);
    return RDB_OK;

error:
    ret = RDB_ERROR;
    /*
     * If there is a syntax error in the template,
     * or the template was not found
     * or an operator was not found
     * return RDB_OK so the process won't exit
     */
    if (RDB_obj_type(RDB_get_err(ecp)) == &RDB_SYNTAX_ERROR
            || RDB_obj_type(RDB_get_err(ecp)) == &RDB_OPERATOR_NOT_FOUND_ERROR
            || RDB_obj_type(RDB_get_err(ecp)) == &RDB_RESOURCE_NOT_FOUND_ERROR)
        ret = RDB_OK;

    log_err(interpp, ecp, err);

    send_error_response(500, "An error occurred while processing the request",
            out, ecp);

    RDB_destroy_obj(&viewopname, ecp);
    RDB_destroy_obj(&model, ecp);
    return ret;
}

static int
create_fcgi_ops(Duro_interp *interpp, RDB_exec_context *ecp)
{
    int ret;
    RDB_parameter param;

    param.typ = &RDB_STRING;
    param.update = RDB_FALSE;
    if (RDB_put_upd_op(&interpp->sys_upd_op_map, "net.put_line", 1, &param,
            &op_net_put_line, ecp) != RDB_OK) {
        ret = DR_ERR_INIT_OP;
        goto error;
    }
    if (RDB_put_upd_op(&interpp->sys_upd_op_map, "net.put_err_line", 1, &param,
            &op_net_put_err_line, ecp) != RDB_OK) {
        ret = DR_ERR_INIT_OP;
        goto error;
    }

    if (RDB_put_upd_op(&interpp->sys_upd_op_map, "net.put", 1, &param,
            &op_net_put, ecp) != RDB_OK) {
        ret = DR_ERR_INIT_OP;
        goto error;
    }
    if (RDB_put_upd_op(&interpp->sys_upd_op_map, "net.put_err", 1, &param,
            &op_net_put_err, ecp) != RDB_OK) {
        ret = DR_ERR_INIT_OP;
        goto error;
    }

    param.typ = &RDB_INTEGER;
    if (RDB_put_global_ro_op("net.status_reason", 1, &param.typ,
            &RDB_STRING, op_net_status_reason, ecp) != RDB_OK) {
        goto error;
    }
    return RDB_OK;

error:
    return ret;
}

int
main(void)
{
    int ret;
    FCGX_Stream *in, *out, *err;
    FCGX_ParamArray envp;
    RDB_exec_context ec;
    Duro_interp interp;

    RDB_init_exec_context(&ec);

    if (RDB_init_builtin(&ec) != RDB_OK) {
        ret = DR_ERR_INIT_BUILTINS;
        goto error;
    }

    if (Duro_init_interp(&interp, &ec, NULL, "") != RDB_OK) {
        ret = DR_ERR_INIT_INTERP;
        goto error;
    }

    ret = create_fcgi_ops(&interp, &ec);
    if (ret != RDB_OK)
        goto error;

    if (Duro_dt_execute_str("var path_info string; "
            "var dbenv string; "
            "var resp_status string; "
            "var resp_headers array string; "
            "var resp_headers_sent boolean;",
            &interp, &ec) != RDB_OK) {
        ret = DR_ERR_DECL;
        goto error;
    }

    if (Duro_dt_execute_path("dreisam/config.td", &interp, &ec) != RDB_OK) {
        ret = DR_ERR_CONFIG;
        goto error;
    }

    if (Duro_dt_execute_str("connect(dbenv);", &interp, &ec) != RDB_OK) {
        ret = DR_ERR_CONNECT;
        goto error;
    }

    if (Duro_dt_execute_str("begin tx;", &interp, &ec) != RDB_OK) {
        ret = DR_ERR_GET_OP;
        goto error;
    }

    send_headers_op = RDB_get_update_op("net.send_headers", 0, NULL, NULL, &ec,
            Duro_dt_tx(&interp));
    if (send_headers_op == NULL) {
        ret = DR_ERR_GET_OP;
        goto error;
    }

    if (Duro_dt_execute_str("commit;", &interp, &ec) != RDB_OK) {
        ret = DR_ERR_GET_OP;
        goto error;
    }

    while (FCGX_Accept(&in, &out, &err, &envp) >= 0) {
        if (process_request(&interp, &ec, envp, in, out, err) != RDB_OK) {
            FCGX_Finish();
            ret = DR_ERR_REQUEST;
            goto error;
        }
    }

    Duro_destroy_interp(&interp);

    RDB_destroy_exec_context(&ec);

	return EXIT_SUCCESS;

error:
    Duro_destroy_interp(&interp);

    RDB_destroy_exec_context(&ec);

    return ret;
}
