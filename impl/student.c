#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>

#include "../io300.h"



/*
    student.c
    Fill in the following stencils
*/

/*
    When starting, you might want to change this for testing on small files.
*/
#ifndef CACHE_SIZE
#define CACHE_SIZE 4096
// #define CACHE_SIZE 8
#endif

#if(CACHE_SIZE < 4)
#error "internal cache size should not be below 4."
#error "if you changed this during testing, that is fine."
#error "when handing in, make sure it is reset to the provided value"
#error "if this is not done, the autograder will not run"
#endif

/*
   This macro enables/disables the dbg() function. Use it to silence your
   debugging info.
   Use the dbg() function instead of printf debugging if you don't want to
   hunt down 30 printfs when you want to hand in
*/
#define DEBUG_PRINT 0
#define DEBUG_STATISTICS 0

struct io300_file {
    /* read,write,seek all take a file descriptor as a parameter */
    int fd;
    /* this will serve as our cache */
    char *cache;

    // TODO: Your properties go here
    int rw; // this is the pointer in file that reads and writes
    int start; // the start of the valid cache
    int end; // the end of the valid cache
    int cur; // the end of the actual read
    int mod; // whether it's been modified
    int offset; // the relative offset in cache (start offset) in regards to the rw in file


    /* Used for debugging, keep track of which io300_file is which */
    char *description;
    /* To tell if we are getting the performance we are expecting */
    struct io300_statistics {
        int read_calls;
        int write_calls;
        int seeks;
    } stats;
};

/*
    Assert the properties that you would like your file to have at all times.
    Call this function frequently (like at the beginning of each function) to
    catch logical errors early on in development.
*/
static void check_invariants(struct io300_file *f) {
    assert(f != NULL);
    assert(f->cache != NULL);
    assert(f->fd >= 0);

    // gdb random_block_cat
    // b io300_read
    // condition 1 f->cur + f->offset != f->rw

    // TODO: Add more invariants
    assert(f->rw >= 0);
    assert(f->start >= 0);
    assert(f->end >= f->start);
    assert(f->cur >= f->start && f->cur <= f->end);
    assert(f->cur + f->offset == f->rw);
}

/*
    Wrapper around printf that provides information about the
    given file. You can silence this function with the DEBUG_PRINT macro.
*/
static void dbg(struct io300_file *f, char *fmt, ...) {
    (void)f; (void)fmt;
#if(DEBUG_PRINT == 1)
    static char buff[300];
    size_t const size = sizeof(buff);
    int n = snprintf(
        buff,
        size,
        // TODO: Add the fields you want to print when debugging
        "{desc:%s, } -- f->cur is: %d\n ",
        f->description, f->cur
    );
    int const bytes_left = size - n;
    va_list args;
    va_start(args, fmt);
    vsnprintf(&buff[n], bytes_left, fmt, args);
    va_end(args);
    printf("%s", buff);
#endif
}



struct io300_file *io300_open(const char *const path, char *description) {
    if (path == NULL) {
        fprintf(stderr, "error: null file path\n");
        return NULL;
    }

    int const fd = open(path, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    if (fd == -1) {
        fprintf(stderr, "error: could not open file: `%s`: %s\n", path, strerror(errno));
        return NULL;
    }

    struct io300_file *const ret = malloc(sizeof(*ret));
    if (ret == NULL) {
        fprintf(stderr, "error: could not allocate io300_file\n");
        return NULL;
    }

    ret->fd = fd;
    ret->cache = malloc(CACHE_SIZE);
    if (ret->cache == NULL) {
        fprintf(stderr, "error: could not allocate file cache\n");
        close(ret->fd);
        free(ret);
        return NULL;
    }
    ret->description = description;

    // TODO: Initialize your file
    ret->rw = 0; // this is the pointer in file that reads and writes
    ret->start = 0; // the start of the valid cache
    ret->end = 0;// the end of the valid cache
    ret->cur = 0;// the end of the actual read
    ret->mod = 0; // whether it's been modified 0 -> unmod; 1 -> mod
    ret->offset = 0;// the relative offset in cache (start offset) in regards to the rw in file

    check_invariants(ret);
    dbg(ret, "Just finished initializing file from path: %s\n", path);
    return ret;
}

int io300_seek(struct io300_file *const f, off_t const pos) {
    check_invariants(f);
    f->stats.seeks++;
    
    // TODO: Implement this
    // if within cache
    if(pos - f->offset >= f->start && pos - f->offset < f->end){
        f->cur += pos - f->rw;
        f->rw = pos;
        return f->rw;
    }

    // if not contain new pos
    if(lseek(f->fd, pos, SEEK_SET) == -1){
        return -1;
    }

    io300_flush(f); // flush the cache we have now
    f->rw = pos;
    f->offset = pos;
    f->start = 0;
    f->cur = 0;
    // lseek to the position in index
    // read into the cache
    int bytes_read = read(f->fd, f->cache, CACHE_SIZE);
    f->end = f->start + bytes_read;

    return f->rw;
}

int io300_close(struct io300_file *const f) {
    check_invariants(f);

#if(DEBUG_STATISTICS == 1)
    printf("stats: {desc: %s, read_calls: %d, write_calls: %d, seeks: %d}\n",
            f->description, f->stats.read_calls, f->stats.write_calls, f->stats.seeks);
#endif
    // TODO: Implement this
    // update cache result into file
    io300_flush(f);
    free(f->cache);
    free(f);
    return 0;
}

off_t io300_filesize(struct io300_file *const f) {
    check_invariants(f);
    struct stat s;
    int const r = fstat(f->fd, &s);
    if (r >= 0 && S_ISREG(s.st_mode)) {
        return s.st_size;
    } else {
        return -1;
    }
}


int io300_readc(struct io300_file *const f) {
    check_invariants(f);
    // TODO: Implement this
    unsigned char c;
    // if empty or exceed cur cache, flush and reload
    if(f->cur >= f->end){
        io300_flush(f); // flush our cache and start new

        f->offset += f->end - f->start; // increment the offset by the cache size
        f->start = 0;
        f->cur = 0;
        
        // syscall read into the cache
        int bytes_read = (int) read(f->fd, f->cache, CACHE_SIZE);
        // if there are no more bytes to read
        if(bytes_read == 0){
            return 0;
        }
        f->end = f->start + bytes_read;  
    }

    c = f->cache[f->cur];
    f->rw++;
    f->cur++;
    return c;
}

int io300_writec(struct io300_file *f, int ch) {
    check_invariants(f);
    // TODO: Implement this
    char const c = (unsigned char)ch;

    if(f->cur > CACHE_SIZE - 1){ // outside of cache: fill in cache and then change char
        io300_flush(f); // flush our cache and start new
        f->offset += f->end - f->start;
        f->start = 0;
        f->cur = 0;

        // f->end = CACHE_SIZE - 1 < file_end - f->rw ? CACHE_SIZE - 1 : file_end - f->rw;
        // syscall read into the cache
        int bytes_read = (int) read(f->fd, f->cache, CACHE_SIZE);
        f->end = f->start + bytes_read;
    }
    f->cache[f->cur++] = c; // alter data
    if(f->cur > f->end){
        f->end = f->cur; // now f->cur pos become valid as well
    }
    f->rw++;
    f->mod = 1; // change to modified
    return 1;
}

ssize_t io300_read_within(struct io300_file *const f, char *const buff, size_t const sz){
    check_invariants(f);
    size_t bytes_read = sz;
    int leftover = 0;
    if(f->cur + (int) sz > f->end){
        io300_flush(f); // flush our cache and start new

        // read in the remaining characters
        leftover = f->end - f->cur;
        memcpy(buff, f->cache + f->cur, leftover);
        // reset metadata
        f->start = 0;
        f->cur = 0;
        // syscall read into the cache
        bytes_read = read(f->fd, f->cache, CACHE_SIZE);
        f->offset += f->end - f->start; // increment the offset by the cache size
        f->end = f->start + (int) bytes_read;

        // update the rw header
        f->rw += leftover;
        // not enough to read
        if(bytes_read + leftover < sz){
            // read all bytes_read
            if(bytes_read == 1){
                *(buff + leftover) = *(f->cache + f->cur);
            } else{
                memcpy(buff + leftover, f->cache + f->cur, bytes_read);
            }
            f->cur += bytes_read;
            f->rw += bytes_read;
            return bytes_read + leftover;
        } else{ // enough bytes to read
            if(sz-leftover == 1){
                *(buff + leftover) = *(f->cache + f->cur);
            } else{
                memcpy(buff + leftover, f->cache + f->cur, sz - leftover);
            }
            f->cur += sz - leftover;
            f->rw += sz - leftover;
            return sz;
        }
    }

    // enough to read w/o flush
    f->rw += sz;
    memcpy(buff, f->cache + f->cur, sz);
    f->cur += sz;
    return sz;
}

ssize_t io300_read(struct io300_file *const f, char *const buff, size_t const sz) {
    check_invariants(f);
    // TODO: Implement this
    // return read(f->fd, buff, sz);
    size_t needed = sz;
    int total_read = 0;

    if(needed > CACHE_SIZE){
        // if there are leftover in remaining cache
        int rem = f->end - f->cur;

        if(rem != 0){
            memcpy(buff, f->cache + f->cur, rem);

            f->offset += f->cur + rem;
            f->cur = 0;
            f->end = 0;
            f->rw += rem; // update the rw header
        }
        
        needed = needed % CACHE_SIZE;
        total_read = sz - needed - rem;

        int bytes = read(f->fd, buff + rem, total_read);

        f->cur = 0;
        f->end = 0;
        f->rw += bytes;
        f->offset += bytes;
        
        total_read = bytes + rem; // actual read within conditional statement

        // don't have enough bytes to read
        if(total_read < (int) (sz - needed)){
            return total_read;
        }
    }

    return total_read + io300_read_within(f, buff + total_read, needed);
}

ssize_t io300_write(struct io300_file *const f, const char *buff, size_t const sz) {
    check_invariants(f);
    // TODO: Implement this
    size_t needed = sz;
    // enough space in cache
    if(needed  < CACHE_SIZE - (size_t) f->end){
        memcpy(f->cache + f->end, buff, needed);
        f->rw += needed;
        f->cur += needed;
        f->end += needed;
        f->mod = 1;
        return needed;
    }
    // read more than what's available in the cache
    if(needed > CACHE_SIZE){
        // if there are leftover in remaining cache
        int rem = f->mod == 1 ? f->end - f->start : f->end - f->cur;
        char* new_buff = malloc(needed + rem);

        if(f->mod == 1){
            memcpy(new_buff, f->cache, rem);
        } else{
            memcpy(new_buff, f->cache + f->cur, rem);
        }
        
        memcpy(new_buff + rem, buff, sz);
        write(f->fd, new_buff, rem + sz);
        // reset pointer
        f->rw += rem + sz;
        f->offset += f->cur + rem + sz;
        f->cur = 0;
        f->end = 0;
        f->mod = 0; // already written to disk
        free(new_buff);
    } else{ // for bytes within cache size
        for(int i = 0; i < (int) sz ; i++){
            io300_writec(f, buff[i]);
        }
        f->mod = 1;
    }
    
    return sz;
}

int io300_flush(struct io300_file *const f) {
    check_invariants(f);
    // TODO: Implement this
    // check if there is modification
    if(f->mod == 0){
        return 0;
    }

    // find the start of cache in regards to the file start
    lseek(f->fd, f->start + f->offset, SEEK_SET);
    write(f->fd, f->cache, f->end - f->start);
    f->mod = 0; // reset mod to 0
    return 0;
}