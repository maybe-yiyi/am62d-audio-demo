#include <stdio.h>
#include <stdlib.h>

#include "cJSON.h"

#include "config.h"

static struct cJSON *conf_fread(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	rewind(f);

	char *buf = malloc(len + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}

	fread(buf, 1, len, f);
	fclose(f);
	buf[len] = '\0';

	struct cJSON *config = cJSON_Parse(buf);
	free(buf);

	return config;
}

struct pipeline_config *config_load(const char *path)
{
	struct pipeline_config *conf = calloc(1, sizeof(struct pipeline_config));
	if (!conf) {
		fprintf(stderr, "config: out of memory\n");
		goto exit;
	}

	conf->json = conf_fread(path);
	if (!conf->json) {
		fprintf(stderr, "config: failed to read config\n");
		goto free_config;
	}

	struct cJSON *name = cJSON_GetObjectItemCaseSensitive(conf->json, "name");
	if (cJSON_IsString(name)) {
		int ret = snprintf(conf->name, sizeof(conf->name), "%s", name->valuestring);
		if (ret >= (int)sizeof(conf->name)) {
			fprintf(stderr, "config: 'name' truncated \n");
		}
	} else {
		fprintf(stderr, "config: 'name' missing or malformed\n");
	}

	struct cJSON *nodes = cJSON_GetObjectItemCaseSensitive(conf->json, "nodes");
	struct cJSON *node;
	cJSON_ArrayForEach(node, nodes) {
		struct cJSON *id = cJSON_GetObjectItemCaseSensitive(node, "id");
		if (cJSON_IsString(id)) {
			int ret = snprintf(conf->nodes[conf->n_nodes].id,
					sizeof(conf->nodes[0].id),
					"%s", id->valuestring);
			if (ret >= (int)sizeof(conf->nodes[0].id)) {
				fprintf(stderr, "config: node 'id' field truncated \n");
			}
		} else {
			fprintf(stderr, "config: node 'id' field missing or malformed\n");
			goto cleanup_json;
		}

		struct cJSON *plugin = cJSON_GetObjectItemCaseSensitive(node, "plugin");
		if (cJSON_IsString(plugin)) {
			int ret = snprintf(conf->nodes[conf->n_nodes].plugin,
					sizeof(conf->nodes[0].plugin),
					"%s", plugin->valuestring);
			if (ret >= (int)sizeof(conf->nodes[0].plugin)) {
				fprintf(stderr, "config: node 'plugin' field truncated \n");
			}
		} else {
			fprintf(stderr, "config: node 'plugin' field missing or malformed\n");
			goto cleanup_json;
		}

		conf->n_nodes++;
	}

	struct cJSON *links = cJSON_GetObjectItemCaseSensitive(conf->json, "links");
	struct cJSON *link;
	cJSON_ArrayForEach(link, links) {
		struct cJSON *from = cJSON_GetObjectItemCaseSensitive(link, "from");
		if (cJSON_IsString(from)) {
			int ret = snprintf(conf->links[conf->n_links].from,
					sizeof(conf->links[0].from),
					"%s", from->valuestring);
			if (ret >= (int)sizeof(conf->links[0].from)) {
				fprintf(stderr, "config: link 'from' field truncated \n");
			}
		} else {
			fprintf(stderr, "config: link 'from' field missing or malformed\n");
			goto cleanup_json;
		}

		struct cJSON *to = cJSON_GetObjectItemCaseSensitive(link, "to");
		if (cJSON_IsString(to)) {
			int ret = snprintf(conf->links[conf->n_links].to,
					sizeof(conf->links[0].to),
					"%s", to->valuestring);
			if (ret >= (int)sizeof(conf->links[0].to)) {
				fprintf(stderr, "config: link 'to' field truncated \n");
			}
		} else {
			fprintf(stderr, "config: link 'to' field missing or malformed\n");
			goto cleanup_json;
		}

		conf->n_links++;
	}

	struct cJSON *ctrl_links = cJSON_GetObjectItemCaseSensitive(conf->json, "control_links");
	struct cJSON *ctrl_link;
	cJSON_ArrayForEach(ctrl_link, ctrl_links) {
		struct cJSON *from = cJSON_GetObjectItemCaseSensitive(ctrl_link, "from");
		if (cJSON_IsString(from)) {
			int ret = snprintf(conf->ctrl_links[conf->n_ctrl_links].from,
					sizeof(conf->ctrl_links[0].from),
					"%s", from->valuestring);
			if (ret >= (int)sizeof(conf->ctrl_links[0].from))
				fprintf(stderr, "config: control_link 'from' truncated\n");
		} else {
			fprintf(stderr, "config: control_link 'from' missing or malformed\n");
			goto cleanup_json;
		}

		struct cJSON *to = cJSON_GetObjectItemCaseSensitive(ctrl_link, "to");
		if (cJSON_IsString(to)) {
			int ret = snprintf(conf->ctrl_links[conf->n_ctrl_links].to,
					sizeof(conf->ctrl_links[0].to),
					"%s", to->valuestring);
			if (ret >= (int)sizeof(conf->ctrl_links[0].to))
				fprintf(stderr, "config: control_link 'to' truncated\n");
		} else {
			fprintf(stderr, "config: control_link 'to' missing or malformed\n");
			goto cleanup_json;
		}

		struct cJSON *param = cJSON_GetObjectItemCaseSensitive(ctrl_link, "param");
		if (cJSON_IsString(param)) {
			int ret = snprintf(conf->ctrl_links[conf->n_ctrl_links].param,
					sizeof(conf->ctrl_links[0].param),
					"%s", param->valuestring);
			if (ret >= (int)sizeof(conf->ctrl_links[0].param))
				fprintf(stderr, "config: control_link 'param' truncated\n");
		} else {
			fprintf(stderr, "config: control_link 'param' missing or malformed\n");
			goto cleanup_json;
		}

		conf->n_ctrl_links++;
	}

	return conf;

cleanup_json:
	cJSON_Delete(conf->json);
free_config:
	free(conf);
exit:
	return NULL;
}

void config_free(struct pipeline_config *conf)
{
	if (!conf)
		return;

	cJSON_Delete(conf->json);
	free(conf);
}
