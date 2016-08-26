#ifndef ATOMIC_FILE_H
#define ATOMIC_FILE_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// Permissions
#define READ 0444
#define WRITE 0222

typedef struct AtomicFileQueue AtomicFileQueue;
typedef struct AtomicFile AtomicFile;

// Public API functions
AtomicFile* atomic_file(const char* filename);
void atomic_file_write(AtomicFile, const char*);
void atomic_file_free(AtomicFile* file);


#endif
