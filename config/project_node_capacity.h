#ifndef PROJECT_NODE_CAPACITY_H
#define PROJECT_NODE_CAPACITY_H

/* Local storage/admission capacity. Wire slots, RefMem regions and persisted
 * Calibration records have independent fixed layouts. CMake reads this default
 * so standalone domain tests and the application select the same capacity. */
#define PROJECT_NODE_CAPACITY_DEFAULT 6
#ifndef PROJECT_NODE_CAPACITY
#define PROJECT_NODE_CAPACITY PROJECT_NODE_CAPACITY_DEFAULT
#endif

#if PROJECT_NODE_CAPACITY < 2 || PROJECT_NODE_CAPACITY > 8
#error "PROJECT_NODE_CAPACITY must be an integer from 2 through 8"
#endif

#endif
