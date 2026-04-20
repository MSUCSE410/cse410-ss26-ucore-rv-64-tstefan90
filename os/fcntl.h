#ifndef FCNTL_H
#define FCNTL_H

#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR 0x002
#define O_CREATE 0x200
#define O_TRUNC 0x400

#define DIR  0x040000   // directory
#define FILE 0x100000   // regular file

struct Stat {
    uint64 dev;
    uint64 ino;
    uint32 mode;
    uint32 nlink;
    uint64 pad[7];
};

#endif // FCNIL_H