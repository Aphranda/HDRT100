#ifndef TDMA_SECOND_CLASS_PARSER_H
#define TDMA_SECOND_CLASS_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Second-class seat payload types.  The seat is a typed container; adding a
 * payload type registers a parser, not a new bus or command slot. */
#define TDMA_SECOND_CLASS_TYPE_REFMEM_CMD 0x52u
#define TDMA_SECOND_CLASS_TYPE_T2_STATIC 0x53u
#define TDMA_SECOND_CLASS_TYPE_T2_DYNAMIC 0x54u
#define TDMA_SECOND_CLASS_PARSER_MAX 8u

typedef bool (*tdma_second_class_parser_fn)(
    const uint8_t *payload,
    size_t payload_size,
    void *context);

typedef struct {
    uint32_t accepted;
    uint32_t rejected;
} tdma_second_class_parser_count_t;

void tdma_second_class_parser_reset(void);
bool tdma_second_class_parser_register(uint8_t type,
                                       tdma_second_class_parser_fn parser,
                                       void *context);
/* Returns true when a registered parser accepted the payload. */
bool tdma_second_class_parser_dispatch(uint8_t type,
                                       const uint8_t *payload,
                                       size_t payload_size);
bool tdma_second_class_parser_get_count(uint8_t type,
                                        tdma_second_class_parser_count_t *count);
uint32_t tdma_second_class_parser_unknown_count(void);
uint32_t tdma_second_class_parser_registered_count(void);

/* Built-in T2 config parsers.  They validate the typed envelope; execution of
 * the requested configuration remains a later T2 state-machine step. */
bool tdma_t2_static_parser(const uint8_t *payload,
                           size_t payload_size,
                           void *context);
bool tdma_t2_dynamic_parser(const uint8_t *payload,
                            size_t payload_size,
                            void *context);

/* T2 static configuration execution hook.  The parser validates the typed
 * envelope, then hands the decoded fields to the registered executor.  The
 * default executor records the applied descriptor so the path is observable
 * before the T2 state machine consumes it. */
typedef bool (*tdma_t2_static_apply_fn)(uint32_t target_mask,
                                        uint32_t config_id,
                                        uint32_t value,
                                        void *context);
void tdma_t2_static_set_apply(tdma_t2_static_apply_fn apply, void *context);
uint32_t tdma_t2_static_applied_count(void);
bool tdma_t2_static_get_last(uint32_t *target_mask,
                             uint32_t *config_id,
                             uint32_t *value);

#define TDMA_T2_STATIC_MAGIC 0x5453u /* "TS" */
#define TDMA_T2_DYNAMIC_MAGIC 0x5444u /* "TD" */
#define TDMA_T2_STATIC_SIZE 16u
#define TDMA_T2_DYNAMIC_SIZE 24u

#ifdef __cplusplus
}
#endif

#endif /* TDMA_SECOND_CLASS_PARSER_H */
