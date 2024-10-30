/* Copyright 2023 Obreja Ana-Maria */
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "load_balancer.h"


unsigned int hash_function_servers(void *a) {
    unsigned int uint_a = *((unsigned int *)a);

    uint_a = ((uint_a >> 16u) ^ uint_a) * 0x45d9f3b;
    uint_a = ((uint_a >> 16u) ^ uint_a) * 0x45d9f3b;
    uint_a = (uint_a >> 16u) ^ uint_a;
    return uint_a;
}

unsigned int hash_function_key(void *a) {
    unsigned char *puchar_a = (unsigned char *)a;
    unsigned int hash = 5381;
    int c;

    while ((c = *puchar_a++))
        hash = ((hash << 5u) + hash) + c;

    return hash;
}

// cautare binara facuta in functie de hash-urile elementelor
// din hashring
int binary_search(int *array, int l, int r, unsigned int x)
{
    if (r >= l) {
    int mid = l + (r - l) / 2;

    if (hash_function_servers(array + mid) == hash_function_servers(&x))
        return mid;

    if (hash_function_servers(array + mid) > hash_function_servers(&x))
        return binary_search(array, l, mid - 1, x);

    return binary_search(array, mid + 1, r, x);
    }

    return -1;
}

// functie de initializare a load_balancerului alaturi de
// server si hashring
load_balancer *init_load_balancer() {
    load_balancer *load = malloc(sizeof(load_balancer));
    DIE(load == NULL, "Malloc failed");

    load->hashring = malloc(sizeof(int) * MAX_REPLIQUE);
    DIE(load->hashring == NULL, "Malloc failed");

    load->server = malloc(sizeof(server_memory));
    DIE(load->server == NULL, "Malloc failed");

    load->server_size = 0;
    load->hashring_size = 0;

    return load;
}

void add_hashring_element(int *hashring, int size, int label)
{
    int i, j;

    // daca hashring-ul este gol, adaugam fara verificare
    if (!size) {
        hashring[0] = label;
        return;
    }

    // verificam ce element din hashring are hash-ul mai mare
    // decat cel al replicii si daca elementul exista, adaugam
    // replica inaintea lui
    for (i = 0; i < size; i++) {
        if (hash_function_servers(&label) <
            hash_function_servers(hashring + i)) {
            for (j = size; j > i; j--)
                hashring[j] = hashring[j - 1];

            hashring[i] = label;

            return;
        }
    }

    // daca nu a fost gasit niciun element, vom adauga replica
    // la finalul vectorului
    hashring[size] = label;
}

void rebalance_hashring(load_balancer *main, int label)
{
    int i, j;
    int size = main->hashring_size;

    int pos_current, pos_next, pos_prev;

    // gasirea pozitiei replicii adaugate anterior
    pos_current =
    binary_search(main->hashring, 0, main->hashring_size - 1, label);

    // gasirea pozitiilor de dinainte si de dupa element din hashring
    if (pos_current == size - 1) {
        pos_next = 0;
        pos_prev = pos_current - 1;
    } else if (pos_current == 0) {
        pos_prev = size - 1;
        pos_next = pos_current + 1;
    } else {
        pos_next = pos_current + 1;
        pos_prev = pos_current - 1;
    }

    int server_id_current = main->hashring[pos_current] % LABEL_NR;
    int server_id_next = main->hashring[pos_next] % LABEL_NR;

    // rebalansarea nu este necesara
    if (server_id_current == server_id_next)
        return;

    int pos_next_server, pos_current_server;

    // gasirea pozitiilor in server
    for (i = 0; i < main->server_size; i++) {
        if (main->server[i]->id == server_id_next)
            pos_next_server = i;
        if (main->server[i]->id == server_id_current)
            pos_current_server = i;
    }

    // preluam hashtable-ul serverului de pe care trebuie
    // sa redistribuim obiectele
    hashtable_t * hashtable = main->server[pos_next_server]->ht;

    // salvam in variabile hash-urile replicilor cu care vom face
    // comparatia
    unsigned int hash_current =
    hash_function_servers(main->hashring + pos_current);

    unsigned int hash_prev =
    hash_function_servers(main->hashring + pos_prev);

    // pentru fiecare element verificam daca trebuie redistribuit,
    // parcurgand hashtable-ul
    for (i = 0; i < (int)hashtable->hmax; i++) {
        ll_node_t *current_node = hashtable->buckets[i]->head;
        int list_size = hashtable->buckets[i]->size;


        for (j = 0; j < list_size; j++) {
            unsigned int hash_key =
            hash_function_key(((info *)current_node->data)->key);

            // se stocheaza elementele
            if (hash_current > hash_key || hash_prev < hash_key) {
                server_store(main->server[pos_current_server],
                ((info *)current_node->data)->key,
                ((info *)current_node->data)->value);
            }

            current_node = current_node->next;
        }
    }
}

void loader_add_aux(load_balancer *main, int label)
{
    // adaugarea propriu-zisa a serverului
    add_hashring_element(main->hashring, main->hashring_size, label);

    main->hashring_size++;

    // caz in care rebalansarea nu este necesara
    if (main->hashring_size <= 3)
        return;

    // redistribuirea obiectelor
    rebalance_hashring(main, label);
}

void loader_add_server(load_balancer *main, int server_id) {
    int i;

    // realocam hashring-ul si serverul pentru a putea adauga elemente
    main->hashring =
    realloc(main->hashring, sizeof(int) * (main->hashring_size + MAX_REPLIQUE));
    DIE(main->hashring == NULL, "Realloc failed");

    main->server =
    realloc(main->server, sizeof(server_memory) * (main->server_size + 1));
    DIE(main->server == NULL, "Realloc failed");

    // initializam serverul
    main->server[main->server_size] = init_server_memory();
    main->server[main->server_size]->id = server_id;
    main->server_size++;

    // adaugam in hashring si rebalansam
    for (i = 0; i < 3; i++) {
        int label = pow(10, 5) * i + server_id;
        loader_add_aux(main, label);
    }
}

void loader_store(load_balancer *main, char *key, char *value, int *server_id) {
    int ok = 0;
    int i;

    // cautam replica in hashring care are hash-ul mai mare decat hash-ul
    // cheii si preluam id-ul serverului
    for (i = 0; i < main->hashring_size; i++) {
        if (hash_function_key(key) <
        hash_function_servers(&main->hashring[i])) {
            *server_id = main->hashring[i] % LABEL_NR;

            ok = 1;
            break;
        }
    }

    // hash-ul cheii este mai mare decat toate hash-urile din hashring,
    // vom stoca obiectul in primul server (in functie de replica)
    if (!ok)
        *server_id = main->hashring[0] % LABEL_NR;

    int k;

    for (i = 0; i < main->server_size; i++)
                if (main->server[i]->id == *server_id) {
                    k = i;
                }
    server_store(main->server[k], key, value);
}

void loader_remove_aux(load_balancer *main, int server_id)
{
    int pos, i, j;
    // cautam in vectorul de servere pozitia pe care se afla
    // serverul
    for (i = 0; i < main->server_size; i++) {
        if (main->server[i]->id == server_id) {
            pos = i;
            break;
        }
    }

    // folosim un hashtable pentru a prelua elementele pe
    // care trebuie sa le redistribuim dupa ce stergem serverul
    hashtable_t *hashtable = main->server[pos]->ht;

    // stergerea efectiva a serverului din vector
    free(main->server[pos]);

    for (i = pos; i < main->server_size - 1; i++)
        main->server[i] = main->server[i + 1];

    main->server_size--;

    // stergerea din hashring a serverului si a replicilor
    for (i = 0; i < MAX_REPLIQUE; i++) {
        int pos_current =
        binary_search(main->hashring, 0,
        main->hashring_size - 1, i * LABEL_NR + server_id);

        for (j = pos_current; j < main->hashring_size - 1; j++)
                main->hashring[j] = main->hashring[j + 1];
         main->hashring_size--;
    }

    // redistribuirea obiectelor din hashtable
    for (i = 0; i < (int)hashtable->hmax; i++) {
        ll_node_t *current_node = hashtable->buckets[i]->head;

        int list_size = hashtable->buckets[i]->size;

        for (j = 0; j < list_size; j++) {
            // adaugam obiectul in serverul corespunzator
            int index_server = 0;
            loader_store(main, ((info *)current_node->data)->key,
                        ((info *)current_node->data)->value, &index_server);

            current_node = current_node->next;
        }
    }

    ht_free(hashtable);
}

void loader_remove_server(load_balancer *main, int server_id) {
    // daca in hashring exista doar un server alaturi de replici,
    // "golim" hashring-ul
    if (main->hashring_size == MAX_REPLIQUE) {
        main->hashring_size -= MAX_REPLIQUE;
        main->hashring =
        realloc(main->hashring, main->hashring_size * sizeof(int));
        return;
    }

    // stergere din hashring si din server
    loader_remove_aux(main, server_id);

    // realocam vectorul de servere si hashring-ul dupa ce am sters
    // elementele
    main->hashring =
    realloc(main->hashring, sizeof(int) * (main->hashring_size));
    DIE(main->hashring == NULL, "Realloc failed");

    main->server =
    realloc(main->server, sizeof(server_memory) * (main->server_size));
    DIE(main->server == NULL, "Realloc failed");
}

char *loader_retrieve(load_balancer *main, char *key, int *server_id) {
    unsigned int hash = hash_function_key(key);
    int *hashring = main->hashring;

    int ok = 0;
    int i;

    // cautam in hashring prima replica a serverului care
    // are hash-ul mai mare decat hash-ul cheii
    for (i = 0; i < main->hashring_size; i++) {
        if (hash < hash_function_servers(hashring + i)) {
            *server_id = main->hashring[i] % LABEL_NR;
            ok = 1;
            break;
        }
    }

    // hash-ul cheii este mai mare decat hash-urile tuturor
    // replicilor
    if (!ok)
        *server_id = main->hashring[0] % LABEL_NR;

    int pos;
    for (i = 0; i < main->server_size; i++)
                if (main->server[i]->id == *server_id)
                    pos = i;

    // returnam valoarea din hashtable-ul serverului
    return server_retrieve(main->server[pos], key);
}

// functie folosita pentru a elibera memoria folosita pentru
// load_balancer
void free_load_balancer(load_balancer *main) {
    int i;
    for (i = 0; i < main->server_size; i++)
        free_server_memory(main->server[i]);

    free(main->server);
    free(main->hashring);
    free(main);
}
