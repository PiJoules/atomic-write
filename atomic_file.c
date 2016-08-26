#include "atomic_file.h"

typedef struct ShmSegment ShmSegment;
typedef enum {
    AVAILABLE,  // Nothing happening to shm
    WRITING,  // Something currently being written to shm
} ShmSegmentStatus;

struct ShmSegment {
    key_t key;
    int id;
    ShmSegmentStatus status;
    size_t buffer_size;
    void* buffer_start;  // Address of this pointer is start of shared mem. This must be at the end.
};


// Private API functions
ShmSegment* create_shm_seg(key_t, size_t, int);
void detatch_shm_seg(ShmSegment*);
void free_shm_seg(ShmSegment*);
int shm_seg_is_up(key_t);
void write_to_shm_seg(ShmSegment*, const char*);
void* buffer_start(const ShmSegment*);
key_t str_to_key(const char*);


/**
 * djb2 by Dan Bernstein.
 */
key_t str_to_key(const char* str){
    int hash = 5381;

    for (int i = 0; *(str + i); i++){
        int c = (int)str[i];
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    }

    return (key_t)hash;
}


/**
 * Create and initiualize the shared memory segment.
 */
ShmSegment* create_shm_seg(key_t key, size_t size, int permissions){
    if (!(permissions & WRITE)){
        fprintf(stderr, "The permissions given must include WRITE in order to initiualize default values for the shared memory segment.\n");
        return NULL;
    }

    size_t shm_seg_size = sizeof(ShmSegment);
    if (size < shm_seg_size){
        fprintf(stderr, "The minimum required size for a shared memory segment is %zu bytes.\n", shm_seg_size);
        return NULL;
    }

    // Actual size to allocate will be the the size of the struct and
    // the buffer size after it
    size_t total_size = shm_seg_size + size;

    // Create segment
    int shmid = shmget(key, total_size, permissions);
    if (shmid == -1){
        perror("shmget");
        fprintf(stderr, "Could not create node for key %d.\n", key);
        return NULL;
    }

    // Attach segment to local memory
    void* shm_addr = shmat(shmid, NULL, 0);
    if (!shm_addr){
        perror("shmat");
        fprintf(stderr, "Could not create node for key %d.\n", key);
        return NULL;
    }

    // Create and copy the node into shared mem
    ShmSegment* seg = (ShmSegment*)shm_addr;
    seg->key = key;
    seg->id = shmid;
    seg->buffer_size = size;

    return (ShmSegment*)shm_addr;
}


void detatch_shm_seg(ShmSegment* seg){
    if (shmdt(seg) == -1){
        perror("shmdt");
        exit(EXIT_FAILURE);
    }
}


void free_shm_seg(ShmSegment* seg){
    int id = seg->id;
    detatch_shm_seg(seg);
    if (shmctl(id, IPC_RMID, NULL) == -1){
       perror("shmctl");
       exit(EXIT_FAILURE);
    }
}


int shm_seg_is_up(key_t key){
    int shmid = shmget(key, sizeof(ShmSegment), READ);
    if (shmid == -1){
        return 0;
    }

    // Attach segment to local memory
    void* shm_addr = shmat(shmid, NULL, 0);
    if (!shm_addr){
        return 0;
    }

    // Detatch segment
    if (shmdt(shm_addr) == -1){
        perror("shmdt");
        exit(EXIT_FAILURE);
    }

    return 1;
}


/**
 * Return the address of shared memory where we cna start writing.
 */
void* buffer_start(const ShmSegment* seg){
    return (void*)(&(seg->buffer_start));
}


/**
 * Copy the string to the shared memory segment buffer.
 */
void write_to_shm_seg(ShmSegment* seg, const char* str){
    // Wait until available
    while (seg->status != AVAILABLE){
        sleep(1);
    }

    // Segment will now be written to
    seg->status = WRITING;
    size_t len = strlen(str);
    memcpy(buffer_start(seg), str, len);

    // Now available
    seg->status = AVAILABLE;
}
