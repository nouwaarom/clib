//
// Created by eput on 11-03-21.
//

#ifndef CLIB_SRC_REPOSITORY_GITHUB_REPOSITORY_H
#define CLIB_SRC_REPOSITORY_GITHUB_REPOSITORY_H

char* github_repository_get_url_for_file(const char* hostname, const char* slug, const char* version, const char *file, const char* secret);

#endif//CLIB_SRC_REPOSITORY_GITHUB_REPOSITORY_H
