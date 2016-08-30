#include "atomic_file.h"

static const size_t default_buff_size = 1024;
static int close_file = 0;

typedef struct ShmSegment ShmSegment;
typedef struct AtomicFileQueueNode AtomicFileQueueNode;
typedef struct Message Message;


struct AtomicFileQueueNode {
    ShmSegment* data;
    AtomicFileQueueNode* next;
};

struct AtomicFileQueue {
    AtomicFileQueueNode* head;
    AtomicFileQueueNode* last;
    size_t size;
    pthread_t* thread;
};


typedef enum {
    AVAILABLE,  // Nothing happening to shm
    WRITING,  // Something currently being written to shm
} ShmSegmentStatus;

struct ShmSegment {
    key_t key;
    int id;
    ShmSegmentStatus status;
    size_t buffer_size;
    int permissions;
    void* buffer_start;  // Address of this pointer is start of shared mem. This must be at the end.
};


// Private API functions
ShmSegment* shmalloc(key_t, size_t, int);
ShmSegment* shmrealloc(ShmSegment*, size_t);
void detatch_shm_seg(ShmSegment*);
void free_shm_seg(ShmSegment*);
int shm_seg_is_up(key_t);
void write_to_shm_seg(ShmSegment*, const void*, size_t);
void* buffer_start(const ShmSegment*);
key_t str_to_key(const char*);
void* process_queue(void*);
void free_atomic_file_queue_node(AtomicFileQueueNode* node);
key_t next_key(key_t key);


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
ShmSegment* shmalloc(key_t key, size_t size, int permissions){
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
    seg->permissions = permissions;

    return (ShmSegment*)shm_addr;
}


/**
 * Resize the shared memory segment.
 * Just delete the old one and return a new one.
 */
ShmSegment* shmrealloc(ShmSegment* seg, size_t size){
    ShmSegment* new_seg = shmalloc(seg->key, size, seg->permissions);
    free_shm_seg(seg);
    return new_seg;
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
 * Return the address of shared memory where we can start writing.
 */
void* buffer_start(const ShmSegment* seg){
    return (void*)(&(seg->buffer_start));
}


/**
 * Copy the string to the shared memory segment buffer.
 */
void write_to_shm_seg(ShmSegment* seg, const void* bytes, size_t size){
    // Wait until available
    while (seg->status != AVAILABLE){
        sleep(1);
    }

    // Segment will now be written to
    seg->status = WRITING;
    memcpy(buffer_start(seg), bytes, size);

    // Now available
    seg->status = AVAILABLE;
}


void free_atomic_file_queue_node(AtomicFileQueueNode* node){
    free_shm_seg(node->data);
    free(node);
}


void* process_queue(void* args){
    AtomicFile* file = (AtomicFile*)args;
    AtomicFileQueue* queue = file->queue;
    AtomicFileQueueNode* head = queue->head;
    FILE* f = fopen(file->filename, "a");

    while (!close_file){
        while (head){
            // Process and print text to file
            Message* msg = (Message*)(buffer_start(head->data));
            const char* str = msg->fmt;
            va_list args;
            va_copy(args, msg->args);

            vfprintf(f, str, args);

            va_end(args);

            // Free node
            AtomicFileQueueNode* next = head->next;
            free_atomic_file_queue_node(head);
            head = next;
        }
        sleep(1);  // Do not bombard cpu with check operations
    }
    fclose(f);

    return NULL;
}


/**
 * The atomic file will be a pointer to a queue.
 * If it has not yet started, que
 */
AtomicFile* atomic_file(const char* filename){
    // Create the file and queue
    AtomicFile* file = (AtomicFile*)malloc(sizeof(AtomicFile));
    if (!file){
        perror("malloc failed");
        return NULL;
    }

    AtomicFileQueue* queue = (AtomicFileQueue*)malloc(sizeof(AtomicFileQueue));
    if (!queue){
        perror("malloc failed");
        return NULL;
    }
    file->queue = queue;
    queue->head = NULL;
    queue->head = NULL;
    queue->size = 0;


    // Start a new thread
    pthread_t* pt = (pthread_t*)malloc(sizeof(pthread_t));
    if (!pt){
        perror("malloc failed");
        free(queue);
        free(file);
        return NULL;
    }
    queue->thread = pt;
    pthread_create(pt, NULL, process_queue, file);

    return file;
}


key_t next_key(key_t key){
    key_t next = key + 1;
    while (!shm_seg_is_up(next)){
        next++;
    }
    return next;
}


/**
 * Add a new node to the queue.
 */
void atomic_file_write(AtomicFile* file, const char* str, ...){
    AtomicFileQueue* queue = file->queue;
    AtomicFileQueueNode* head = queue->head;
    ShmSegment* seg = shmalloc(next_key(head->data->key), sizeof(ShmSegment), IPC_CREAT | READ | WRITE);
    Message msg;
    size_t len = sizeof(str);
    msg.fmt = (char*)malloc(sizeof(char) * len);
    strncpy(msg.fmt, str, len);
    va_copy(msg.args, )
}


void atomic_file_free(AtomicFile* file){
    AtomicFileQueue* queue = file->queue;
    AtomicFileQueueNode* current = queue->head;
    while (current){
        AtomicFileQueueNode* next = current->next;

        // Free this node and the shm segment
        free_shm_seg(current->data);
        free(current);

        current = next;
    }

    pthread_join(*(queue->thread), NULL);
    free(queue->thread);
    free(queue);
    free(file);
}
