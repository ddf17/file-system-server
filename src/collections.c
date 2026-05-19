/**
 * Minimal collection implementations used by the file server.
 */
#include "fss/dictionary.h"
#include "fss/vector.h"

#include <stdlib.h>
#include <string.h>

struct vector {
    void **data;
    size_t size;
    size_t capacity;
    destructor_type destructor;
};

vector *vector_create(copy_constructor_type copy_constructor,
                      destructor_type destructor,
                      default_constructor_type default_constructor) {
    (void)copy_constructor;
    (void)default_constructor;

    vector *this = calloc(1, sizeof(vector));
    if (!this) {
        return NULL;
    }

    this->capacity = INITIAL_CAPACITY;
    this->data = calloc(this->capacity, sizeof(void *));
    if (!this->data) {
        free(this);
        return NULL;
    }

    this->destructor = destructor;
    return this;
}

void vector_destroy(vector *this) {
    if (!this) {
        return;
    }

    if (this->destructor) {
        for (size_t i = 0; i < this->size; i++) {
            this->destructor(this->data[i]);
        }
    }

    free(this->data);
    free(this);
}

size_t vector_size(vector *this) {
    return this ? this->size : 0;
}

void *vector_get(vector *this, size_t n) {
    if (!this || n >= this->size) {
        return NULL;
    }
    return this->data[n];
}

void vector_push_back(vector *this, void *element) {
    if (!this) {
        return;
    }

    if (this->size == this->capacity) {
        size_t next_capacity = this->capacity * GROWTH_FACTOR;
        void **next_data = realloc(this->data, next_capacity * sizeof(void *));
        if (!next_data) {
            return;
        }
        this->data = next_data;
        this->capacity = next_capacity;
    }

    this->data[this->size++] = element;
}

void vector_erase(vector *this, size_t position) {
    if (!this || position >= this->size) {
        return;
    }

    if (this->destructor) {
        this->destructor(this->data[position]);
    }

    if (position + 1 < this->size) {
        memmove(&this->data[position],
                &this->data[position + 1],
                (this->size - position - 1) * sizeof(void *));
    }
    this->size--;
}

struct dictionary_entry {
    int key;
    void *value;
};

struct dictionary {
    struct dictionary_entry *entries;
    size_t size;
    size_t capacity;
};

dictionary *dictionary_create(hash_function_type hash_function, compare comp,
                              copy_constructor_type key_copy_constructor,
                              destructor_type key_destructor,
                              copy_constructor_type value_copy_constructor,
                              destructor_type value_destructor) {
    (void)hash_function;
    (void)comp;
    (void)key_copy_constructor;
    (void)key_destructor;
    (void)value_copy_constructor;
    (void)value_destructor;

    dictionary *this = calloc(1, sizeof(dictionary));
    if (!this) {
        return NULL;
    }

    this->capacity = INITIAL_CAPACITY;
    this->entries = calloc(this->capacity, sizeof(struct dictionary_entry));
    if (!this->entries) {
        free(this);
        return NULL;
    }

    return this;
}

dictionary *int_to_shallow_dictionary_create(void) {
    return dictionary_create(NULL, NULL, NULL, NULL, NULL, NULL);
}

void dictionary_destroy(dictionary *this) {
    if (!this) {
        return;
    }
    free(this->entries);
    free(this);
}

void dictionary_set(dictionary *this, void *key, void *value) {
    if (!this || !key) {
        return;
    }

    int int_key = *(int *)key;
    for (size_t i = 0; i < this->size; i++) {
        if (this->entries[i].key == int_key) {
            this->entries[i].value = value;
            return;
        }
    }

    if (this->size == this->capacity) {
        size_t next_capacity = this->capacity * GROWTH_FACTOR;
        struct dictionary_entry *next_entries =
            realloc(this->entries, next_capacity * sizeof(struct dictionary_entry));
        if (!next_entries) {
            return;
        }
        this->entries = next_entries;
        this->capacity = next_capacity;
    }

    this->entries[this->size].key = int_key;
    this->entries[this->size].value = value;
    this->size++;
}

void *dictionary_get(dictionary *this, void *key) {
    if (!this || !key) {
        return NULL;
    }

    int int_key = *(int *)key;
    for (size_t i = 0; i < this->size; i++) {
        if (this->entries[i].key == int_key) {
            return this->entries[i].value;
        }
    }

    return NULL;
}

void dictionary_remove(dictionary *this, void *key) {
    if (!this || !key) {
        return;
    }

    int int_key = *(int *)key;
    for (size_t i = 0; i < this->size; i++) {
        if (this->entries[i].key == int_key) {
            if (i + 1 < this->size) {
                memmove(&this->entries[i],
                        &this->entries[i + 1],
                        (this->size - i - 1) * sizeof(struct dictionary_entry));
            }
            this->size--;
            return;
        }
    }
}
