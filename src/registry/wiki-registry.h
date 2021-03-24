//
// wiki-registry.h
//
// Copyright (c) 2013 Stephen Mathieson
// MIT licensed
//


#ifndef WIKI_REGISTRY_H
#define WIKI_REGISTRY_H 1

#include <stdbool.h>

typedef struct wiki_package_t* wiki_package_ptr_t;
typedef struct wiki_registry_t* wiki_registry_ptr_t;
typedef list_iterator_t* wiki_registry_iterator_t;

/**
 * Create a new wiki_registry for the given url.
 * @param url the url of the registry.
 * @param secret the secret to authenticate with this registry of NULL if no secret is required.
 * @return a handle to the registry
 */
wiki_registry_ptr_t wiki_registry_create(const char* url, const char* secret);

/**
 * Free the memory held by the registry.
 * @param registry
 */
void wiki_registry_free(wiki_registry_ptr_t registry);

/**
 * Fetch the list of packages from the registry.
 * @param registry
 */
bool wiki_registry_fetch(wiki_registry_ptr_t registry);

/**
 * Get the url for the registry
 * @param registry
 * @return
 */
const char* wiki_registry_get_url(wiki_registry_ptr_t registry);

/**
 * An iterator through the packages in the registry.
 */
wiki_registry_iterator_t wiki_registry_iterator_new(wiki_registry_ptr_t registry);
wiki_package_ptr_t wiki_registry_iterator_next(wiki_registry_iterator_t iterator);
void wiki_registry_iterator_destroy(wiki_registry_iterator_t iterator);

/**
 * Search the registry for a package
 * @param registry a registry handle
 * @param package_id the identifier of the package "<namespace>/<package_name>"
 * @return a pointer to the package if it could be found or NULL
 */
wiki_package_ptr_t wiki_registry_find_package(wiki_registry_ptr_t registry, const char* package_id);

char*wiki_package_get_id(wiki_package_ptr_t package);
char* wiki_package_get_href(wiki_package_ptr_t package);
char* wiki_package_get_description(wiki_package_ptr_t package);
char* wiki_package_get_category(wiki_package_ptr_t package);

#endif
