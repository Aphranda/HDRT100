/* One maintenance owner, four concurrent SMs and four finite FIFO DMAs.
 * Status indices are the versioned DATA:LOOP diagnostic wire layout. */
enum {
    DATA_LOOP_UNLOAD = 1, DATA_LOOP_CLAIM, DATA_LOOP_LOAD,
    DATA_LOOP_CONFIG_SM, DATA_LOOP_CONFIG_DMA, DATA_LOOP_ARM, DATA_LOOP_RUN,
    DATA_LOOP_FREEZE, DATA_LOOP_ABORT, DATA_LOOP_QUIESCED, DATA_LOOP_DECODE,
    DATA_LOOP_REMOVE, DATA_LOOP_RELEASE, DATA_LOOP_RESTORE, DATA_LOOP_PUBLISH
};
static const char *const DATA_LOOP_RESOURCE_OWNER = "TDMA_MAINTENANCE_PIO";
_Static_assert(BOARD_TDMA_SPI_MASTER_SM == 0u && BOARD_TDMA_SPI_SLAVE_SM == 1u &&
    BOARD_TDMA_SPI_CAPTURE_SM == 2u && BOARD_TDMA_SPI_RTT_SM == 3u,
    "DATA loop wire-layout SM identities must match the maintenance board contract");
static const uint data_loop_dma_channels[4] = {
    BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL, BOARD_TDMA_RX_COMMAND_LOADER_DMA_CHANNEL,
    BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL, BOARD_TDMA_ORIGIN_EXECUTOR_DMA_CHANNEL
};
static const pio_program_t *const data_loop_programs[4] = {
    &tdma_pio_spi_marker_origin_program, &tdma_pio_spi_data_train_source_program,
    &tdma_pio_spi_data_train_sink_program, &tdma_pio_spi_cal_loopback_capture_program
};
_Static_assert(sizeof(tdma_pio_spi_marker_origin_program_instructions) +
    sizeof(tdma_pio_spi_data_train_source_program_instructions) +
    sizeof(tdma_pio_spi_data_train_sink_program_instructions) +
    sizeof(tdma_pio_spi_cal_loopback_capture_program_instructions) <= 32u * sizeof(uint16_t),
    "Concurrent DATA loop programs must fit maintenance PIO");

static void data_loop_transition(tdma_pio_spi_phys_t *p, uint32_t next)
{
    tdma_pio_spi_phys_p3_write_begin(p);
    p->data_loop.fields[56] = next;
    tdma_pio_spi_phys_p3_write_end(p);
}

static void data_loop_fail(tdma_pio_spi_phys_t *p, uint32_t reason)
{
    tdma_pio_spi_phys_p3_write_begin(p);
    p->data_loop.fields[4] = reason;
    p->p3.reject_reason = reason;
    p->data_loop.fields[56] = DATA_LOOP_FREEZE;
    tdma_pio_spi_phys_p3_write_end(p);
}

static bool tdma_pio_spi_phys_data_loop_start(tdma_pio_spi_phys_t *p,
                                            const tdma_pio_spi_p3_request_t *r)
{
    if (p == NULL || r == NULL || r->marker_words == NULL || r->data_loop_words == NULL ||
        r->marker_generation == 0u || r->epoch == 0u || r->epoch > 255u || r->marker_codebook > 3u ||
        r->marker_sample_count == 0u || r->marker_word_count != (r->marker_sample_count + 31u) / 32u ||
        r->marker_word_count > TDMA_PIO_SPI_DATA_TRAIN_BUFFER_WORDS - 2u ||
        r->rxphase_base_ns == 0u || r->data_loop_gap < 256u || r->data_loop_gap > 1000u ||
        r->data_loop_mark_offset < -10 || r->data_loop_mark_offset > 10 ||
        r->sck_launch_offset_samples < -10 || r->sck_launch_offset_samples > 10 ||
        r->marker_fault > 2u || p->p3_transition != TDMA_PIO_SPI_P3_TRANSITION_IDLE ||
        p->p3.state == TDMA_PIO_SPI_P3_ARMED) return false;
    const uint64_t b = ((uint64_t)r->rxphase_base_ns + 2u) / 4u;
    const int64_t source = 1 + (int64_t)b + r->data_loop_mark_offset;
    const int64_t sink = source + 2 * (int64_t)b + r->sck_launch_offset_samples;
    /* The common buffer includes quiet lead, both paths, response and tail. */
    if (source < 1 || source > 32 || sink < 1 || sink > 32 || b > 31u ||
        256ull + r->data_loop_gap + 4u * b + (uint64_t)source +
            r->marker_sample_count + 128u > 8192u) return false;
    if (!tdma_pio_spi_phys_leave_flight_for_maintenance(p) ||
        (s_tdma_pio_spi_program_persona != TDMA_PIO_SPI_PROGRAM_PERSONA_NORMAL &&
         s_tdma_pio_spi_program_persona != TDMA_PIO_SPI_PROGRAM_PERSONA_NONE)) return false;

    uint32_t *marker = s_tdma_pio_spi_cal_tx_workspace.data_loop.marker;
    uint32_t *data = s_tdma_pio_spi_cal_tx_workspace.data_loop.data;
    const uint32_t marker_words = (320u + r->marker_sample_count + 31u) / 32u;
    const uint32_t data_words = (64u + r->marker_sample_count + 31u) / 32u;
    for (uint32_t i = 0u; i < marker_words; ++i) marker[i] = UINT32_MAX;
    for (uint32_t i = 0u; i < data_words; ++i) data[i] = 0u;
    if (r->marker_fault != 1u) {
        memcpy(marker + 8u, r->marker_words, r->marker_word_count * sizeof(uint32_t));
        if ((r->marker_sample_count & 31u) != 0u)
            marker[8u + r->marker_word_count - 1u] |= UINT32_MAX << (r->marker_sample_count & 31u);
    }
    if (r->marker_fault != 2u) {
        memcpy(data, r->data_loop_words, r->marker_word_count * sizeof(uint32_t));
        if ((r->marker_sample_count & 31u) != 0u)
            data[r->marker_word_count - 1u] &= (1u << (r->marker_sample_count & 31u)) - 1u;
    }
    tdma_pio_spi_phys_p3_write_begin(p);
    memset(&p->p3, 0, sizeof(p->p3));
    memset(&p->data_loop, 0, sizeof(p->data_loop));
    p->p3.role = TDMA_PIO_SPI_P3_ROLE_DATA_LOOPBACK;
    p->p3.state = TDMA_PIO_SPI_P3_ARMED; p->p3.epoch = r->epoch;
    p->p3.sample_period_ns = 4u; p->p3.requested_words = 256u;
    p->p3.flags = TDMA_PIO_SPI_P3_FLAG_DIAGNOSTIC_ONLY;
    p->p3_transition = TDMA_PIO_SPI_P3_TRANSITION_START_UNLOAD;
    p->p3_program_step = p->p3_program_count = 0u;
    p->data_loop_program_count = p->data_loop_dma_configured = p->data_loop_drain_pending = 0u;
    p->data_loop_stop_requested = p->data_loop_abort_issued = false;
    p->data_loop_decode_sample = p->data_loop_previous = 0u;
    p->data_loop_deadline = time_us_64() + TDMA_PIO_SPI_MARKER_LOOPBACK_TIMEOUT_US;
    uint32_t *v = p->data_loop.fields;
    v[0]=1u; v[1]=1u; v[2]=6u; v[3]=1u; v[5]=r->epoch; v[6]=r->marker_generation;
    v[7]=r->marker_codebook; v[8]=4u; v[9]=r->rxphase_base_ns; v[10]=(uint32_t)b; v[11]=(uint32_t)b*4u;
    v[12]=(uint32_t)r->data_loop_mark_offset; v[13]=(uint32_t)r->sck_launch_offset_samples;
    v[14]=r->data_loop_gap; v[15]=128u; v[16]=r->data_loop_gap; v[17]=r->data_loop_gap+2u*(uint32_t)b-128u;
    v[18]=(uint32_t)source; v[19]=(uint32_t)sink; v[22]=v[23]=r->marker_sample_count;
    v[24]=v[26]=marker_words; v[25]=v[27]=data_words; v[30]=v[32]=256u; v[33]=v[35]=2048u;
    for (uint32_t i=0u;i<4u;++i) { v[36u+i]=i; v[40u+i]=data_loop_dma_channels[i]; }
    v[44]=26u; v[45]=27u; v[46]=29u; v[47]=24u; v[53]=r->marker_fault;
    v[56]=DATA_LOOP_UNLOAD; v[59]=128u; v[61]=1u;
    tdma_pio_spi_phys_p3_write_end(p);
    return true;
}

static void tdma_pio_spi_phys_data_loop_stop(tdma_pio_spi_phys_t *p)
{
    if (p->p3_transition == TDMA_PIO_SPI_P3_TRANSITION_IDLE) {
        tdma_pio_spi_phys_p3_write_begin(p);
        p->p3.state=TDMA_PIO_SPI_P3_IDLE;
        p->data_loop.fields[1]=0u;
        tdma_pio_spi_phys_p3_write_end(p);
        return;
    }
    p->data_loop_stop_requested = true;
    const uint32_t state = p->data_loop.fields[56];
    /* Finish any partially unloaded NORMAL catalog before restoring it. */
    if (state != DATA_LOOP_UNLOAD && state < DATA_LOOP_FREEZE) data_loop_transition(p, DATA_LOOP_FREEZE);
}

static bool data_loop_claim(tdma_pio_spi_phys_t *p)
{
    const uint a=data_loop_dma_channels[1], b=data_loop_dma_channels[3];
    if (s_tdma_pio_spi_command_dma_channel >= 0 || s_tdma_pio_spi_executor_dma_channel >= 0 ||
        dma_channel_is_claimed(a) || dma_channel_is_claimed(b)) return false;
    if (!resource_arbiter_acquire_owned(TDMA_STATE_MACHINE_DATA_LOOP_ADDITIONAL_RESOURCE_MASK,
                                       DATA_LOOP_RESOURCE_OWNER)) return false;
    dma_channel_claim(a); dma_channel_claim(b);
    s_tdma_pio_spi_command_dma_channel=(int)a; s_tdma_pio_spi_executor_dma_channel=(int)b;
    tdma_pio_spi_phys_p3_write_begin(p); p->data_loop.fields[55]=1u; tdma_pio_spi_phys_p3_write_end(p);
    return true;
}

static void tdma_pio_spi_phys_data_loop_service(tdma_pio_spi_phys_t *p)
{
    uint32_t *v=p->data_loop.fields;
    const uint32_t state=v[56];
    if (state == 0u) return;
    if (state == DATA_LOOP_UNLOAD) {
        if (!tdma_pio_spi_phys_cal_persona_switch_ready()) return;
        const uint32_t before=p->p3_program_step;
        if (tdma_pio_spi_phys_p3_unload_source_step(p))
            data_loop_transition(p, p->data_loop_stop_requested ? DATA_LOOP_FREEZE : DATA_LOOP_CLAIM);
        else if (before == p->p3_program_step) data_loop_fail(p, TDMA_PIO_SPI_P3_REJECT_RESOURCE);
    } else if (state == DATA_LOOP_CLAIM) {
        if (!tdma_pio_spi_phys_ensure_sms_claimed() || !tdma_pio_spi_phys_ensure_tx_dma() ||
            !tdma_pio_spi_phys_ensure_rx_dma() || !data_loop_claim(p)) data_loop_fail(p, TDMA_PIO_SPI_P3_REJECT_RESOURCE);
        else data_loop_transition(p, DATA_LOOP_LOAD);
    } else if (state == DATA_LOOP_LOAD) {
        const uint32_t i=p->data_loop_program_count;
        if (!pio_can_add_program(BOARD_TDMA_SPI_PIO, data_loop_programs[i])) {
            data_loop_fail(p, TDMA_PIO_SPI_P3_REJECT_RESOURCE); return;
        }
        p->data_loop_program_offsets[i]=pio_add_program(BOARD_TDMA_SPI_PIO, data_loop_programs[i]);
        if (++p->data_loop_program_count == 4u) {
            tdma_pio_spi_phys_cal_mark_persona(p, TDMA_PIO_SPI_PROGRAM_PERSONA_DATA_LOOPBACK);
            p->p3_program_step=0u; data_loop_transition(p, DATA_LOOP_CONFIG_SM);
        }
    } else if (state == DATA_LOOP_CONFIG_SM) {
        const uint sm=p->p3_program_step++;
        const uint off=p->data_loop_program_offsets[sm];
        pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, sm, false);
        pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, sm); pio_sm_restart(BOARD_TDMA_SPI_PIO, sm);
        if (sm == 0u) tdma_pio_spi_marker_origin_program_init(BOARD_TDMA_SPI_PIO,sm,off,26u);
        else if (sm == 1u) {
            pio_sm_set_consecutive_pindirs(BOARD_TDMA_SPI_PIO,sm,27u,1u,false);
            const uint32_t instruction=tdma_pio_spi_data_train_source_program_init(BOARD_TDMA_SPI_PIO,sm,off,27u,29u,v[18]);
            pio_sm_put(BOARD_TDMA_SPI_PIO,sm,v[16]-1u);
            tdma_pio_spi_phys_p3_write_begin(p); v[20]=instruction; tdma_pio_spi_phys_p3_write_end(p);
        } else if (sm == 2u) {
            const uint32_t instruction=tdma_pio_spi_data_train_sink_program_init(BOARD_TDMA_SPI_PIO,sm,off,26u,24u,v[19]);
            pio_sm_put(BOARD_TDMA_SPI_PIO,sm,v[17]-1u);
            tdma_pio_spi_phys_p3_write_begin(p); v[21]=instruction; tdma_pio_spi_phys_p3_write_end(p);
        } else {
            tdma_pio_spi_cal_loopback_capture_program_init(BOARD_TDMA_SPI_PIO,sm,off,250000000u);
            gpio_put(BOARD_UP_BISS_DE_PIN,true); gpio_put(BOARD_DN_BISS_DE_PIN,false); gpio_put(BOARD_TRIG_DE_PIN,true);
            tdma_pio_spi_phys_p3_write_begin(p); v[3]|=2u; v[54]=5u; tdma_pio_spi_phys_p3_write_end(p);
            p->p3_program_step=0u; data_loop_transition(p,DATA_LOOP_CONFIG_DMA);
        }
    } else if (state == DATA_LOOP_CONFIG_DMA) {
        const uint i=p->p3_program_step++, ch=data_loop_dma_channels[i];
        dma_channel_config c=dma_channel_get_default_config(ch);
        channel_config_set_transfer_data_size(&c,DMA_SIZE_32);
        channel_config_set_read_increment(&c,i<2u); channel_config_set_write_increment(&c,i>=2u);
        channel_config_set_chain_to(&c,ch); channel_config_set_ring(&c,false,0u);
        channel_config_set_sniff_enable(&c,false); channel_config_set_irq_quiet(&c,true);
        channel_config_set_dreq(&c,pio_get_dreq(BOARD_TDMA_SPI_PIO,i,i<2u));
        void *target=i<2u ? (void *)&BOARD_TDMA_SPI_PIO->txf[i] : i==2u ?
            (void *)s_tdma_pio_spi_cal_rx_workspace.data_train : (void *)s_tdma_pio_spi_marker_loopback_capture;
        const void *source=i==0u ? (void *)s_tdma_pio_spi_cal_tx_workspace.data_loop.marker : i==1u ?
            (void *)s_tdma_pio_spi_cal_tx_workspace.data_loop.data : (void *)&BOARD_TDMA_SPI_PIO->rxf[i];
        const uint count=i==0u?v[24]:i==1u?v[25]:i==2u?256u:2048u;
        dma_channel_configure(ch,&c,target,source,count,i<2u);
        p->data_loop_dma_configured|=1u<<i;
        if (i==3u) data_loop_transition(p,DATA_LOOP_ARM);
    } else if (state == DATA_LOOP_ARM) {
        if (pio_sm_is_tx_fifo_empty(BOARD_TDMA_SPI_PIO,0u) || pio_sm_is_tx_fifo_empty(BOARD_TDMA_SPI_PIO,1u)) {
            if (time_us_64()>=p->data_loop_deadline) data_loop_fail(p,TDMA_PIO_SPI_P3_REJECT_TIMEOUT);
            return;
        }
        BOARD_TDMA_SPI_PIO->fdebug=(1u<<PIO_FDEBUG_TXSTALL_LSB)|(1u<<(PIO_FDEBUG_TXSTALL_LSB+1u));
        dma_start_channel_mask((1u<<data_loop_dma_channels[2])|(1u<<data_loop_dma_channels[3]));
        pio_enable_sm_mask_in_sync(BOARD_TDMA_SPI_PIO,15u); p->armed=true;
        data_loop_transition(p,DATA_LOOP_RUN);
    } else if (state == DATA_LOOP_RUN) {
        uint32_t remaining[4];
        for(uint i=0;i<4;++i) remaining[i]=dma_hw->ch[data_loop_dma_channels[i]].transfer_count;
        tdma_pio_spi_phys_p3_write_begin(p);
        v[26]=remaining[0];v[27]=remaining[1];v[32]=remaining[2];v[35]=remaining[3];
        for(uint i=0;i<2;++i) {
            if(remaining[i]==0u && pio_sm_is_tx_fifo_empty(BOARD_TDMA_SPI_PIO,i)) {
                const uint32_t bit=1u<<(PIO_FDEBUG_TXSTALL_LSB+i);
                if((p->data_loop_drain_pending&(1u<<i))==0u) {
                    BOARD_TDMA_SPI_PIO->fdebug=bit;p->data_loop_drain_pending|=1u<<i;
                } else if(BOARD_TDMA_SPI_PIO->fdebug&bit) v[28u+i]=1u;
            }
        }
        if(v[28]&&v[29])v[3]|=8u;
        if(!remaining[2]&&!remaining[3])v[3]|=4u;
        tdma_pio_spi_phys_p3_write_end(p);
        if((v[3]&12u)==12u)data_loop_transition(p,DATA_LOOP_FREEZE);
        else if(time_us_64()>=p->data_loop_deadline)data_loop_fail(p,TDMA_PIO_SPI_P3_REJECT_TIMEOUT);
    } else if (state == DATA_LOOP_FREEZE) {
        for(uint sm=0;sm<4;++sm)pio_sm_set_enabled(BOARD_TDMA_SPI_PIO,sm,false);
        tdma_pio_spi_phys_set_line_drivers(false);p->armed=false;
        uint32_t mask=0u;
        tdma_pio_spi_phys_p3_write_begin(p);
        v[54]=0u;
        for(uint i=0;i<4;++i)if(p->data_loop_dma_configured&(1u<<i)) {
            const uint32_t remaining=dma_hw->ch[data_loop_dma_channels[i]].transfer_count;
            mask|=1u<<data_loop_dma_channels[i];
            if(i<2u)v[26u+i]=remaining;
            else { const uint idx=i==2u?30u:33u;v[idx+2u]=remaining;v[idx+1u]=remaining<=v[idx]?v[idx]-remaining:0u; }
        }
        v[57]=mask;v[58]=mask;
        p->data_loop_abort_issued=false;
        tdma_pio_spi_phys_p3_write_end(p);
        data_loop_transition(p,DATA_LOOP_ABORT);
    } else if (state == DATA_LOOP_ABORT) {
        const uint32_t mask=v[57];
        if(!p->data_loop_abort_issued) {
            tdma_pio_spi_phys_disable_dma_mask(mask);dma_hw->abort=mask;
            p->data_loop_abort_issued=true;return;
        }
        uint32_t pending=dma_hw->abort&mask;
        for(uint i=0;i<4;++i)if((mask&(1u<<data_loop_dma_channels[i]))&&dma_channel_is_busy(data_loop_dma_channels[i]))pending|=1u<<data_loop_dma_channels[i];
        tdma_pio_spi_phys_p3_write_begin(p);v[58]=pending;tdma_pio_spi_phys_p3_write_end(p);
        if(pending!=0u)return; /* No buffer/FIFO/program reuse until hardware quiesces. */
        __dmb();data_loop_transition(p,DATA_LOOP_QUIESCED);
    } else if (state == DATA_LOOP_QUIESCED) {
        tdma_pio_spi_phys_cal_cleanup(p);
        tdma_pio_spi_phys_p3_write_begin(p);v[57]=v[58]=0u;tdma_pio_spi_phys_p3_write_end(p);
        data_loop_transition(p,DATA_LOOP_DECODE);
    } else if (state == DATA_LOOP_DECODE) {
        uint32_t edge_mask=v[48], times[4]={v[49],v[50],v[51],v[52]};
        const uint bits[4]={2u,3u,5u,0u};
        const uint32_t limit=v[34]*4u;
        uint32_t end=p->data_loop_decode_sample+128u;if(end>limit)end=limit;
        for(;p->data_loop_decode_sample<end;++p->data_loop_decode_sample) {
            const uint i=p->data_loop_decode_sample;
            const uint32_t cur=(s_tdma_pio_spi_marker_loopback_capture[i/4u]>>((i%4u)*8u))&255u;
            if(i!=0u)for(uint n=0;n<4;++n)if(!(edge_mask&(1u<<n))) {
                const uint32_t bit=1u<<bits[n];
                const bool edge=n<2u ? ((p->data_loop_previous&bit)&&!(cur&bit)) : (!(p->data_loop_previous&bit)&&(cur&bit));
                if(edge){edge_mask|=1u<<n;times[n]=i;}
            }
            p->data_loop_previous=cur;
        }
        tdma_pio_spi_phys_p3_write_begin(p);v[48]=edge_mask;for(uint i=0;i<4;++i)v[49u+i]=times[i];tdma_pio_spi_phys_p3_write_end(p);
        if(end==limit) {
            if(v[4]==0u && !p->data_loop_stop_requested) {
                if(edge_mask!=15u) {tdma_pio_spi_phys_p3_write_begin(p);v[4]=TDMA_PIO_SPI_P3_REJECT_EDGE_MISSING;tdma_pio_spi_phys_p3_write_end(p);}
                else if(!(times[0]<times[1]&&times[1]<times[2]&&times[2]<times[3])) {
                    tdma_pio_spi_phys_p3_write_begin(p);v[4]=TDMA_PIO_SPI_P3_REJECT_EDGE_ORDER;tdma_pio_spi_phys_p3_write_end(p);
                }
            }
            data_loop_transition(p,DATA_LOOP_REMOVE);
        }
    } else if (state == DATA_LOOP_REMOVE) {
        if(p->data_loop_program_count!=0u) {
            const uint i=--p->data_loop_program_count;
            pio_remove_program(BOARD_TDMA_SPI_PIO,data_loop_programs[i],p->data_loop_program_offsets[i]);return;
        }
        tdma_pio_spi_phys_cal_mark_persona(p,TDMA_PIO_SPI_PROGRAM_PERSONA_NONE);
        data_loop_transition(p,DATA_LOOP_RELEASE);
    } else if (state == DATA_LOOP_RELEASE) {
        if(v[55]) {
            dma_channel_unclaim(data_loop_dma_channels[1]);dma_channel_unclaim(data_loop_dma_channels[3]);
            s_tdma_pio_spi_command_dma_channel=s_tdma_pio_spi_executor_dma_channel=-1;
            resource_arbiter_release_owned(TDMA_STATE_MACHINE_DATA_LOOP_ADDITIONAL_RESOURCE_MASK,DATA_LOOP_RESOURCE_OWNER);
            tdma_pio_spi_phys_p3_write_begin(p);v[55]=0u;tdma_pio_spi_phys_p3_write_end(p);
        }
        p->p3_program_count=0u;data_loop_transition(p,DATA_LOOP_RESTORE);
    } else if (state == DATA_LOOP_RESTORE) {
        if(tdma_pio_spi_phys_p3_load_normal_step(p))data_loop_transition(p,DATA_LOOP_PUBLISH);
    } else if (state == DATA_LOOP_PUBLISH) {
        tdma_pio_spi_phys_p3_write_begin(p);
        v[1]=p->data_loop_stop_requested?0u:v[4]?3u:2u;v[56]=0u;
        p->p3.state=v[1];p->p3.reject_reason=v[4];p->p3.produced_words=v[31];
        p->p3_transition=TDMA_PIO_SPI_P3_TRANSITION_IDLE;
        tdma_pio_spi_phys_p3_write_end(p);
    }
}

bool tdma_pio_spi_phys_get_data_loop_snapshot(const tdma_pio_spi_phys_t *p,
                                            tdma_pio_spi_data_loop_snapshot_t *snapshot)
{
    if(p==NULL||snapshot==NULL)return false;
    for(uint i=0;i<64u;++i){
        const uint32_t guard=__atomic_load_n(&p->p3_guard,__ATOMIC_ACQUIRE);if(guard&1u)continue;
        const uint32_t role=p->p3.role,state=p->p3.state;*snapshot=p->data_loop;
        if(state==0u&&role!=TDMA_PIO_SPI_P3_ROLE_DATA_LOOPBACK){memset(snapshot,0,sizeof(*snapshot));snapshot->fields[0]=1u;snapshot->fields[2]=6u;}
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if(guard!=__atomic_load_n(&p->p3_guard,__ATOMIC_ACQUIRE))continue;
        return state==0u||role==TDMA_PIO_SPI_P3_ROLE_DATA_LOOPBACK;
    }return false;
}

bool tdma_pio_spi_phys_copy_data_loop_capture(const tdma_pio_spi_phys_t *p,
    uint32_t epoch,uint32_t kind,uint32_t offset,uint32_t count,uint32_t generation,uint32_t *words,uint32_t *total)
{
    if(p==NULL||words==NULL||total==NULL||kind>1u||count==0u||count>32u||generation==0u)return false;
    const uint32_t guard=__atomic_load_n(&p->p3_guard,__ATOMIC_ACQUIRE);if(guard&1u)return false;
    const uint32_t *v=p->data_loop.fields;const uint32_t produced=v[kind==0u?31u:34u];
    if(p->p3.role!=TDMA_PIO_SPI_P3_ROLE_DATA_LOOPBACK||(p->p3.state!=2u&&p->p3.state!=3u)||v[56]!=0u||
        v[5]!=epoch||v[6]!=generation||offset>produced||count>produced-offset)return false;
    const uint32_t *source=kind==0u?s_tdma_pio_spi_cal_rx_workspace.data_train:s_tdma_pio_spi_marker_loopback_capture;
    memcpy(words,source+offset,count*sizeof(*words));*total=produced;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);return guard==__atomic_load_n(&p->p3_guard,__ATOMIC_ACQUIRE);
}
