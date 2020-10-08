#ifndef PROGRAM_POINTS_H
#define PROGRAM_POINTS_H

#include <stdint.h>

/* instructions are halfword-aligned */
#define PROGRAM_POINTS (FLASH_SIZE >> 1)

typedef struct program_point_t {
    uint32_t address;
    uint32_t count;
} program_point_t;

void program_point_init (void);
void program_point_update (uint32_t address);
void program_point_print (void);

#endif /* PROGRAM_POINTS_H */
