#ifndef SCPI_SEQUENCE_NODE_COMMANDS_H
#define SCPI_SEQUENCE_NODE_COMMANDS_H

#include "scpi/scpi.h"

/* Dynamic RefMem node-slot configuration for sequence roles. */
scpi_result_t scpi_sequence_node_load(scpi_t *context);
scpi_result_t scpi_sequence_node_activate(scpi_t *context);
scpi_result_t scpi_sequence_node_load_q(scpi_t *context);
scpi_result_t scpi_sequence_node_role(scpi_t *context);
scpi_result_t scpi_sequence_node_role_q(scpi_t *context);
scpi_result_t scpi_sequence_link_config(scpi_t *context);
scpi_result_t scpi_sequence_link_q(scpi_t *context);
scpi_result_t scpi_sequence_link_transport_q(scpi_t *context);
scpi_result_t scpi_sequence_counter_q(scpi_t *context);
scpi_result_t scpi_sequence_counter_history_q(scpi_t *context);
scpi_result_t scpi_sequence_history_q(scpi_t *context);

#define SCPI_SEQUENCE_NODE_COMMANDS \
    {.pattern = "CONFigure:SEQuence:NODE:LOAD", .callback = scpi_sequence_node_load}, \
    {.pattern = "CONFigure:SEQuence:NODE:ACTivate", .callback = scpi_sequence_node_activate}, \
    {.pattern = "READ:SEQuence:NODE:LOAD?", .callback = scpi_sequence_node_load_q}, \
    {.pattern = "CONFigure:SEQuence:NODE:ROLE", .callback = scpi_sequence_node_role}, \
    {.pattern = "READ:SEQuence:NODE:ROLE?", .callback = scpi_sequence_node_role_q}, \
    {.pattern = "CONFigure:SEQuence:LINK", .callback = scpi_sequence_link_config}, \
    {.pattern = "READ:SEQuence:LINK?", .callback = scpi_sequence_link_q}, \
    {.pattern = "READ:SEQuence:LINK:TRANsport?", .callback = scpi_sequence_link_transport_q}, \
    {.pattern = "READ:SEQuence:COUNter?", .callback = scpi_sequence_counter_q}, \
    {.pattern = "READ:SEQuence:COUNter:HISTory?", .callback = scpi_sequence_counter_history_q}, \
    {.pattern = "READ:SEQuence:HISTory?", .callback = scpi_sequence_history_q}

#endif
