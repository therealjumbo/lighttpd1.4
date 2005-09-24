#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


#include "base.h"
#include "log.h"
#include "buffer.h"

#include "plugin.h"

#include "stat_cache.h"

/* plugin config for all request/connections */

typedef struct {
	array *indexfiles;
} plugin_config;

typedef struct {
	PLUGIN_DATA;
	
	buffer *tmp_buf;
	
	plugin_config **config_storage;
	
	plugin_config conf; 
} plugin_data;

/* init the plugin data */
INIT_FUNC(mod_indexfile_init) {
	plugin_data *p;
	
	p = calloc(1, sizeof(*p));
	
	p->tmp_buf = buffer_init();
	
	return p;
}

/* detroy the plugin data */
FREE_FUNC(mod_indexfile_free) {
	plugin_data *p = p_d;
	
	UNUSED(srv);

	if (!p) return HANDLER_GO_ON;
	
	if (p->config_storage) {
		size_t i;
		for (i = 0; i < srv->config_context->used; i++) {
			plugin_config *s = p->config_storage[i];

			if (!s) continue;
			
			array_free(s->indexfiles);
			
			free(s);
		}
		free(p->config_storage);
	}
	
	buffer_free(p->tmp_buf);
	
	free(p);
	
	return HANDLER_GO_ON;
}

/* handle plugin config and check values */

SETDEFAULTS_FUNC(mod_indexfile_set_defaults) {
	plugin_data *p = p_d;
	size_t i = 0;
	
	config_values_t cv[] = { 
		{ "index-file.names",           NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_CONNECTION },       /* 0 */
		{ "server.indexfiles",          NULL, T_CONFIG_ARRAY, T_CONFIG_SCOPE_CONNECTION },       /* 1 */
		{ NULL,                         NULL, T_CONFIG_UNSET, T_CONFIG_SCOPE_UNSET }
	};
	
	if (!p) return HANDLER_ERROR;
	
	p->config_storage = calloc(1, srv->config_context->used * sizeof(specific_config *));
	
	for (i = 0; i < srv->config_context->used; i++) {
		plugin_config *s;
		
		s = calloc(1, sizeof(plugin_config));
		s->indexfiles    = array_init();
		
		cv[0].destination = s->indexfiles;
		cv[1].destination = s->indexfiles; /* old name for [0] */
		
		p->config_storage[i] = s;
	
		if (0 != config_insert_values_global(srv, ((data_config *)srv->config_context->data[i])->value, cv)) {
			return HANDLER_ERROR;
		}
	}
	
	return HANDLER_GO_ON;
}

#define PATCH(x) \
	p->conf.x = s->x;
static int mod_indexfile_patch_connection(server *srv, connection *con, plugin_data *p) {
	size_t i, j;
	plugin_config *s = p->config_storage[0];
	
	PATCH(indexfiles);
	
	/* skip the first, the global context */
	for (i = 1; i < srv->config_context->used; i++) {
		data_config *dc = (data_config *)srv->config_context->data[i];
		s = p->config_storage[i];
		
		/* condition didn't match */
		if (!config_check_cond(srv, con, dc)) continue;
		
		/* merge config */
		for (j = 0; j < dc->value->used; j++) {
			data_unset *du = dc->value->data[j];
			
			if (buffer_is_equal_string(du->key, CONST_STR_LEN("server.indexfiles"))) {
				PATCH(indexfiles);
			} else if (buffer_is_equal_string(du->key, CONST_STR_LEN("index-file.names"))) {
				PATCH(indexfiles);
			}
		}
	}
	
	return 0;
}
#undef PATCH

URIHANDLER_FUNC(mod_indexfile_subrequest) {
	plugin_data *p = p_d;
	size_t k;
	stat_cache_entry *sce = NULL;
	
	if (con->uri.path->used == 0) return HANDLER_GO_ON;
	if (con->uri.path->ptr[con->uri.path->used - 2] != '/') return HANDLER_GO_ON;
	
	mod_indexfile_patch_connection(srv, con, p);
	
	if (con->conf.log_request_handling) {
		log_error_write(srv, __FILE__, __LINE__,  "s",  "-- handling the request as Indexfile");
		log_error_write(srv, __FILE__, __LINE__,  "sb", "URI          :", con->uri.path);
	}
	
	/* indexfile */
	for (k = 0; k < p->conf.indexfiles->used; k++) {
		data_string *ds = (data_string *)p->conf.indexfiles->data[k];
		
		buffer_copy_string_buffer(p->tmp_buf, con->physical.path);
		buffer_append_string_buffer(p->tmp_buf, ds->value);
		
		if (HANDLER_ERROR == stat_cache_get_entry(srv, con, p->tmp_buf, &sce)) {
			if (errno == EACCES) {
				con->http_status = 403;
				buffer_reset(con->physical.path);
				
				return HANDLER_FINISHED;
			}
			
			if (errno != ENOENT &&
			    errno != ENOTDIR) {
				/* we have no idea what happend. let's tell the user so. */
				
				con->http_status = 500;
				
				log_error_write(srv, __FILE__, __LINE__, "ssbsb",
						"file not found ... or so: ", strerror(errno),
						con->uri.path,
						"->", con->physical.path);
				
				buffer_reset(con->physical.path);
				
				return HANDLER_FINISHED;
			}
			continue;
		}
			
		/* rewrite uri.path to the real path (/ -> /index.php) */
		buffer_append_string_buffer(con->uri.path, ds->value);
		buffer_copy_string_buffer(con->physical.path, p->tmp_buf);
		
		/* fce is already set up a few lines above */
		
		return HANDLER_GO_ON;
	}
	
	/* not found */
	return HANDLER_GO_ON;
}

/* this function is called at dlopen() time and inits the callbacks */

int mod_indexfile_plugin_init(plugin *p) {
	p->version     = LIGHTTPD_VERSION_ID;
	p->name        = buffer_init_string("indexfile");
	
	p->init        = mod_indexfile_init;
	p->handle_subrequest_start = mod_indexfile_subrequest;
	p->set_defaults  = mod_indexfile_set_defaults;
	p->cleanup     = mod_indexfile_free;
	
	p->data        = NULL;
	
	return 0;
}
