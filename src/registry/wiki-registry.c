//
// wiki-registry.c
//
// Copyright (c) 2014 Stephen Mathieson
// MIT licensed
//

#include "github-registry.h"
#include "gitlab-registry.h"
#include "gumbo-parser/gumbo.h"
#include "list/list.h"
#include "url/url.h"
#include "wiki-registry-internal.h"
#include <stdlib.h>
#include <string.h>

enum wiki_registry_type_t {
  REGISTRY_TYPE_GITHUB,
  REGISTRY_TYPE_GITLAB,
};

struct wiki_registry_t {
  enum wiki_registry_type_t type;
  char *url;
  char *hostname;
  char *secret;
  list_t *packages;
};

/**
 * Create a new wiki package.
 */
wiki_package_ptr_t wiki_package_new() {
  wiki_package_ptr_t pkg = malloc(sizeof(struct wiki_package_t));
  if (pkg) {
    pkg->id = NULL;
    pkg->href = NULL;
    pkg->description = NULL;
    pkg->category = NULL;
  }
  return pkg;
}

/**
 * Free a wiki_package_t.
 */
void wiki_package_free(wiki_package_ptr_t pkg) {
  free(pkg->id);
  free(pkg->href);
  free(pkg->description);
  free(pkg->category);
  free(pkg);
}

wiki_registry_ptr_t wiki_registry_create(const char *url, const char *secret) {
  wiki_registry_ptr_t registry = malloc(sizeof(struct wiki_registry_t));
  registry->url = strdup(url);
  registry->secret = strdup(secret);

  if (strstr(url, "github.com") != NULL) {
    registry->type = REGISTRY_TYPE_GITHUB;
  } else if (strstr(url, "gitlab") != NULL) {
    registry->type = REGISTRY_TYPE_GITLAB;
  } else {
    return NULL;
  }

  url_data_t *parsed = url_parse(url);
  registry->hostname = strdup(parsed->hostname);
  url_free(parsed);

  return registry;
}

void wiki_registry_free(wiki_registry_ptr_t registry) {
  free(registry->url);
  free(registry->hostname);
  if (registry->packages != NULL) {
    list_iterator_t *it = list_iterator_new(registry->packages, LIST_HEAD);
    list_node_t *node;
    while ((node = list_iterator_next(it))) {
      wiki_package_free(node->val);
    }
    list_iterator_destroy(it);
    list_destroy(registry->packages);
  }
  free(registry);
}

const char *wiki_registry_get_url(wiki_registry_ptr_t registry) {
  return registry->url;
}

bool wiki_registry_fetch(wiki_registry_ptr_t registry) {
  switch (registry->type) {
  case REGISTRY_TYPE_GITLAB:
    registry->packages = gitlab_registry_fetch(registry->url, registry->hostname, registry->secret);
    if (registry->packages != NULL) {
      return true;
    }
    break;
  case REGISTRY_TYPE_GITHUB:
    registry->packages = github_registry_fetch(registry->url);
    if (registry->packages != NULL) {
      return true;
    }
    break;
  default:
    return false;
  }

  return false;
}

wiki_registry_iterator_t wiki_registry_iterator_new(wiki_registry_ptr_t registry) {
  return list_iterator_new(registry->packages, LIST_HEAD);
}

wiki_package_ptr_t wiki_registry_iterator_next(wiki_registry_iterator_t iterator) {
  list_node_t *node = list_iterator_next(iterator);
  if (node == NULL) {
    return NULL;
  }

  return (wiki_package_ptr_t) node->val;
}

void wiki_registry_iterator_destroy(wiki_registry_iterator_t iterator) {
  list_iterator_destroy(iterator);
}

wiki_package_ptr_t wiki_registry_find_package(wiki_registry_ptr_t registry, const char *package_id) {
  wiki_registry_iterator_t it = wiki_registry_iterator_new(registry);
  wiki_package_ptr_t pack;
  while ((pack = wiki_registry_iterator_next(it))) {
    if (0 == strcmp(package_id, pack->id)) {
      wiki_registry_iterator_destroy(it);
      return pack;
    }
  }
  wiki_registry_iterator_destroy(it);

  return NULL;
}

char *wiki_package_get_id(wiki_package_ptr_t package) {
  return package->id;
}

char *wiki_package_get_href(wiki_package_ptr_t package) {
  return package->href;
}

char *wiki_package_get_description(wiki_package_ptr_t package) {
  return package->description;
}

char *wiki_package_get_category(wiki_package_ptr_t package) {
  return package->category;
}