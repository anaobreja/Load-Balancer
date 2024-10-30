/* Copyright 2023 Obreja Ana-Maria */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "server.h"

// initializarea serverului (implicit a hashtable-ului)
server_memory *init_server_memory()
{
	server_memory *server = malloc(sizeof(server_memory));
	DIE(server == NULL, "Malloc failed");

	server->ht = ht_create(HMAX, hash_function_string,
										compare_function_strings);

	return server;
}

// stocarea in hashtable-ul unui server obiectul (cheie, valoare)
void server_store(server_memory *server, char *key, char *value) {
	ht_put(server->ht, key, strlen(key) + 1, value, strlen(value) + 1);
}

// returneaza valoarea din hashtable asociata cheii date
char *server_retrieve(server_memory *server, char *key) {
	return ht_get(server->ht, key);
}

// stergerea unui element din hashtable
void server_remove(server_memory *server, char *key) {
	ht_remove_entry(server->ht, key);
}

// eliberarea resurselor folosite pentru un server
void free_server_memory(server_memory *server) {
	ht_free(server->ht);
	free(server);
}
