#ifndef SCPI_REALTIME_COMPONENT_COMMANDS_H
#define SCPI_REALTIME_COMPONENT_COMMANDS_H

#include "scpi/scpi.h"
#include "scpi_realtime_encoder_commands.h"
#include "scpi_realtime_io_commands.h"
#include "scpi_realtime_pcnt_commands.h"
#include "scpi_realtime_sequence_commands.h"
#include "scpi_realtime_status_commands.h"

#define SCPI_REALTIME_COMPONENT_COMMANDS \
    SCPI_REALTIME_IO_COMMANDS, \
    SCPI_REALTIME_SEQUENCE_COMMANDS, \
    SCPI_REALTIME_ENCODER_COMMANDS, \
    SCPI_REALTIME_PCNT_COMMANDS, \
    SCPI_REALTIME_STATUS_COMMANDS

#endif
