#include "atomic_file.h"
#include <assert.h>
#include <pthread.h>

static const char filename[] = "test.log";
#define lines_to_print 10
#define times_to_write 10
#define files_to_create 10
static pthread_t threads[files_to_create + files_to_create * times_to_write];


void* write_to_file(void* args){
    AtomicFile* file = (AtomicFile*)args;
    for (int i = 0; i < lines_to_print; i++){
        atomic_file_write(file, "%s\n", "This is a very long string that should take some time to copy and we can prove atomic_file_write is not atomic if any of the lines in the resulting file do not match this string. This is definitely not the best way to test it, but it's not a bad way, I'd like to think.");
    }
    return NULL;
}


void* multiple_write_to_file(void* args){
    size_t start = *((size_t*)args);
    // Creation
    AtomicFile* file = atomic_file(filename, "a");
    assert(access(filename, F_OK) != -1);

    // Multiple writing
    // Start multiple threads, write to the same file.
    for (int i = 0; i < times_to_write; i++){
        pthread_create(&threads[start + i], NULL, write_to_file, file);
    }
    for (int i = 0; i < times_to_write; i++){
        pthread_join(threads[start + i], NULL);
    }

    // Closing
    atomic_file_close(file);
    assert(access(filename, F_OK) != -1);

    return NULL;
}


void empty_file(const char* filename){
    AtomicFile* file = atomic_file(filename, "w");
    atomic_file_close(file);
}


int main(int argc, char** argv){
    empty_file(filename);

    // Multiple writing
    // Start multiple threads, write to the same file.
    size_t idxs[files_to_create];
    for (int i = 0; i < files_to_create; i++){
        idxs[i] = files_to_create + times_to_write*i;
        pthread_create(&threads[i], NULL, multiple_write_to_file, &idxs[i]);
    }
    for (int i = 0; i < files_to_create; i++){
        pthread_join(threads[i], NULL);
    }

    // Closing
    assert(access(filename, F_OK) != -1);

    return 0;
}
